/* P0 suites: selftest, spin, prompt. */

#include "suites.h"
#include "clock.h"
#include "cmd.h"
#include "crc.h"
#include "gfx.h"
#include "hwcal.h"
#include "log.h"
#include "power.h"
#include "timebase.h"
#include "ui.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static suite_verdict_t suite_selftest(int argc, char **argv);
static suite_verdict_t suite_spin(int argc, char **argv);
static suite_verdict_t suite_prompt(int argc, char **argv);

const suite_t g_suites[] = {
    {"selftest", suite_selftest, "flash CRC, W25Q JEDEC, clock, HWCAL load"},
    {"spin", suite_spin, "spin <ms>: busy loop with IRQs enabled (DFU-touch test)"},
    {"prompt", suite_prompt, "prompt [timeout_s]: @P/@A round trip via device buttons"},
};
const int g_suite_count = (int)(sizeof(g_suites) / sizeof(g_suites[0]));

const suite_t *suite_find(const char *name)
{
    for (int i = 0; i < g_suite_count; i++) {
        if (strcmp(g_suites[i].name, name) == 0) {
            return &g_suites[i];
        }
    }
    return NULL;
}

const char *suite_verdict_str(suite_verdict_t v)
{
    switch (v) {
    case SUITE_PASS: return "pass";
    case SUITE_FAIL: return "fail";
    case SUITE_ABORT: return "abort";
    default: return "fail";
    }
}

suite_verdict_t suite_run(const suite_t *s, int argc, char **argv)
{
    busy_begin(s->name);
    ui_con_begin(s->name);
    suite_verdict_t v = s->fn(argc, argv);
    if (v != SUITE_ABORT && abort_requested()) {
        v = SUITE_ABORT;
    }
    ui_con_line(v == SUITE_PASS ? COLOR_GREEN : COLOR_RED, "== %s %s ==", s->name,
                suite_verdict_str(v));
    proto_done(s->name, suite_verdict_str(v));
    busy_end();
    log_flush(20);
    ui_invalidate();
    return v;
}

/* ---------------------------------------------------------------- selftest */

static void test_line(const char *name, bool pass, const char *detail)
{
    ui_con_line(pass ? COLOR_GREEN : COLOR_RED, "%-6s %s %s", pass ? "PASS" : "FAIL", name, detail);
}

static suite_verdict_t suite_selftest(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bool all = true;
    char d[40];

    /* 1. internal flash CRC (HW CRC unit) vs trailer */
    fw_crc_result_t r;
    fw_crc_check(&r);
    res_printf("{\"t\":\"test\",\"suite\":\"selftest\",\"name\":\"crc\",\"pass\":%s,"
               "\"calc\":\"%08" PRIX32 "\",\"stored\":\"%08" PRIX32 "\",\"len\":%" PRIu32
               ",\"us\":%" PRIu32 "}",
               r.pass ? "true" : "false", r.calc, r.stored, r.len, r.us);
    snprintf(d, sizeof(d), "%08" PRIX32, r.calc);
    test_line("crc", r.pass, d);
    all &= r.pass;
    sys_service();
    if (abort_requested()) return SUITE_ABORT;

    /* 2. W25Q JEDEC id */
    uint32_t id = w25q_read_jedec() & 0xFFFFFFu;
    uint8_t uid[8] = {0};
    w25q_read_uid(uid);
    bool jp = (id == 0xEF4018u);
    res_printf("{\"t\":\"test\",\"suite\":\"selftest\",\"name\":\"w25q_jedec\",\"pass\":%s,"
               "\"jedec\":\"%06" PRIX32 "\",\"expect\":\"EF4018\","
               "\"uid\":\"%02X%02X%02X%02X%02X%02X%02X%02X\"}",
               jp ? "true" : "false", id, uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6],
               uid[7]);
    snprintf(d, sizeof(d), "%06" PRIX32, id);
    test_line("w25q", jp, d);
    all &= jp;
    sys_service();
    if (abort_requested()) return SUITE_ABORT;

    /* 3. clock tree */
    bool hse = clock_hse_ready();
    bool pll = clock_sysclk_is_pll();
    bool src = clock_pll_src_is_hse();
    bool f96 = (SystemCoreClock == 96000000u);
    bool cp = hse && pll && src && f96 && (clock_last_status() == CLOCK_OK);
    res_printf("{\"t\":\"test\",\"suite\":\"selftest\",\"name\":\"clock\",\"pass\":%s,"
               "\"sysclk\":%" PRIu32 ",\"hse_ready\":%s,\"sysclk_pll\":%s,\"pll_src\":\"%s\","
               "\"pclk1\":%" PRIu32 ",\"pclk2\":%" PRIu32 ",\"flash_ws\":%" PRIu32 ",\"status\":\"%s\"}",
               cp ? "true" : "false", SystemCoreClock, hse ? "true" : "false",
               pll ? "true" : "false", src ? "hse" : "hsi", clock_pclk1_hz(), clock_pclk2_hz(),
               (uint32_t)(FLASH->ACR & FLASH_ACR_LATENCY), clock_status_str(clock_last_status()));
    snprintf(d, sizeof(d), "%" PRIu32 " MHz", SystemCoreClock / 1000000u);
    test_line("clock", cp, d);
    all &= cp;
    sys_service();
    if (abort_requested()) return SUITE_ABORT;

    /* 4. HWCAL load (re-read from flash) */
    hwcal_state_t hs = hwcal_load();
    bool hp = (hs != HWCAL_NO_FLASH);
    res_printf("{\"t\":\"test\",\"suite\":\"selftest\",\"name\":\"hwcal\",\"pass\":%s,"
               "\"status\":\"%s\",\"seq\":%" PRIu32 ",\"slot\":\"%c\"}",
               hp ? "true" : "false", hwcal_state_str(hs), hwcal_seq(), hwcal_slot());
    test_line("hwcal", hp, hwcal_state_str(hs));
    all &= hp;

    return all ? SUITE_PASS : SUITE_FAIL;
}

/* ---------------------------------------------------------------- spin */

/* Busy computation with interrupts ENABLED. Deliberately does NOT parse host
   commands (simulates a long benchmark); it only pushes output and watches
   the ISR-level abort flag. The 1200-baud touch must still work. */
static suite_verdict_t suite_spin(int argc, char **argv)
{
    uint32_t ms = 5000;
    if (argc >= 2) {
        ms = (uint32_t)strtoul(argv[1], NULL, 0);
    }
    if (ms == 0u) ms = 1u;
    if (ms > 3600000u) ms = 3600000u;

    uint32_t x = 0x12345678u, acc = 0, iter = 0;
    uint32_t t0 = HAL_GetTick();
    uint32_t next_report = 1000u;
    uint32_t c0 = time_cycles();
    ui_con_line(COLOR_WHITE, "spin %" PRIu32 " ms", ms);

    for (;;) {
        for (int k = 0; k < 4096; k++) { /* ~0.2 ms of work per chunk */
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            acc += x * 2654435761u;
        }
        iter++;
        uint32_t el = HAL_GetTick() - t0;
        log_poll();
        power_poll();
        if (abort_requested()) {
            return SUITE_ABORT;
        }
        if (el >= next_report && el < ms) {
            res_printf("{\"t\":\"progress\",\"suite\":\"spin\",\"name\":\"spin\",\"elapsed_ms\":%" PRIu32
                       ",\"iter\":%" PRIu32 "}",
                       el, iter);
            next_report += 1000u;
        }
        if (el >= ms) {
            uint32_t cyc = time_cycles() - c0;
            res_printf("{\"t\":\"spin\",\"suite\":\"spin\",\"name\":\"spin\",\"pass\":true,\"ms\":%" PRIu32
                       ",\"iter\":%" PRIu32 ",\"acc\":\"%08" PRIX32 "\",\"cycles\":%" PRIu32 "}",
                       el, iter, acc, cyc);
            ui_con_line(COLOR_WHITE, "%" PRIu32 " iterations", iter);
            return SUITE_PASS;
        }
    }
}

/* ---------------------------------------------------------------- prompt */

static suite_verdict_t suite_prompt(int argc, char **argv)
{
    uint32_t timeout_s = 60;
    if (argc >= 2) {
        timeout_s = (uint32_t)strtoul(argv[1], NULL, 0);
    }
    ui_answer_t a = ui_prompt("p0_prompt", "Prompt test: press A (ok) or B (fail)", NULL,
                              timeout_s * 1000u);
    res_printf("{\"t\":\"test\",\"suite\":\"prompt\",\"name\":\"prompt\",\"pass\":%s,\"answer\":\"%s\"}",
               a == UI_ANS_OK ? "true" : "false", ui_answer_str(a));
    if (a == UI_ANS_ABORT) return SUITE_ABORT;
    return (a == UI_ANS_OK) ? SUITE_PASS : SUITE_FAIL;
}
