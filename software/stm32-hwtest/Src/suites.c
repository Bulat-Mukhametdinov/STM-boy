/* P0 suites: selftest, spin, prompt. */

#include "suites.h"
#include "bench.h"
#include "clock.h"
#include "cmd.h"
#include "crc.h"
#include "gfx.h"
#include "hwcal.h"
#include "log.h"
#include "power.h"
#include "pwrtest.h"
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

/* Order = recommended order on the device menu. [U] = SUITE_F_USER. */
const suite_t g_suites[] = {
    {"selftest", suite_selftest, "flash CRC, W25Q JEDEC, clock, HWCAL load", 0},
    {"pins", suite_pins, "GPIO pull-up/down classification vs netlist + neighbour bridges", 0},
    {"display.info", suite_display_info, "LCD config + SPI1/PA5-7 register check, fill time", 0},
    {"backlight", suite_backlight, "TIM3_CH1/PA6 AF2 registers + measured PWM freq/duty", 0},
    {"display.cal", suite_display_cal, "[U] offsets: [land|port] [madctl=] [x=] [y=] [t=idle_s]", SUITE_F_USER},
    {"display.orient", suite_display_orient, "[U] pick MY/MX/MV for landscape + portrait [land|port]", SUITE_F_USER},
    {"display.rgb", suite_display_rgb, "[U] full-screen R/G/B, RGB vs BGR", SUITE_F_USER},
    {"display.invert", suite_display_invert, "[U] colour bars with INVOFF / INVON", SUITE_F_USER},
    {"display.gamma", suite_display_gamma, "[U] 32-step ramps, gamma default/retroport/sitronix", SUITE_F_USER},
    {"display.bars", suite_display_bars, "[U] colour bars", SUITE_F_USER},
    {"display.gradient", suite_display_gradient, "[U] RGB565 gradients", SUITE_F_USER},
    {"display.spi", suite_display_spi, "[U] SPI1 A/B flicker 12/24/48 MHz x 8/16 bit x GPIO speed", SUITE_F_USER},
    {"display.tear", suite_display_tear, "[U] tearing finder: write order x fps x panel Hz [auto=1]", SUITE_F_USER},
    {"display.native", suite_display_native, "native-scan translation self-check: corner markers + mapping + cost", 0},
    {"display.vscroll", suite_display_vscroll, "[U] hardware scroll (VSCRDEF/VSCRSADD) vs full redraw [auto=1] [t=] [sps=]", SUITE_F_USER},
    {"demo", suite_demo, "[U] 3D cubes / raycaster / raytracer, 8bpp+palette DMA present [auto=1] [t=] [scene=] [fps=]", SUITE_F_USER},
    {"oc.sweep", suite_oc_sweep, "AUTONOMOUS clock sweep [list=48,64,..] [max=260] [step=8] [t=10]; IWDG + W25Q journal; USB drops; ends with a reset; read with `oc log`", 0},
    {"oc.ws", suite_oc_ws, "min flash wait states per freq [list=96,120,144] (caches off, journal, ends with a reset)", 0},
    {"backlight.user", suite_backlight_user, "[U] lowest usable level + flicker/whine per PWM freq", SUITE_F_USER},
    {"input.live", suite_input_live, "[U] press every button once (POWER excluded)", SUITE_F_USER},
    {"input.chord", suite_input_chord, "[U] button combinations", SUITE_F_USER},
    {"input.bounce", suite_input_bounce, "[U] EXTI bounce analyser [btn=A] [n=30] -> debounce", SUITE_F_USER},
    {"power.info", suite_power_info, "VDDA, Vpin, Vbat, die temp, CHG, VBUS, presence", 0},
    {"power.adccal", suite_power_adccal, "[U] enter multimeter VBAT + 3V3 -> gain/offset (adccal point|fit)", SUITE_F_USER},
    {"power.handshake", suite_power_handshake, "[U] OFF_REQ/ACK latency+cut: ack=now|delay|never | report", SUITE_F_USER},
    {"power.current", suite_power_current, "[U] current per load state with a meter: [s=N] [n=20]", SUITE_F_USER},
    {"power.rint", suite_power_rint, "[U] R_int + state currents from charger step (no meter) | report", SUITE_F_USER},
    {"battlog", suite_battlog, "pulsed S15/S2 battery logger [dry=1] [resume=1] [new=1] (P2b)", 0},
    {"w25q.info", suite_w25q_info, "JEDEC, SR1-3 block protect, UID, SFDP", 0},
    {"w25q.readcrc", suite_w25q_readcrc, "whole-chip CRC at 6/12/24 MHz (must match), MB/s", 0},
    {"w25q.scratch", suite_w25q_scratch, "DESTRUCTIVE (scratch only): confirm=yes; timed erase/program + patterns", 0},
    {"w25q.sector", suite_w25q_sector, "backup-erase-pattern-restore of 3 scratch sectors", 0},
    {"w25q.pd", suite_w25q_pd, "power-down 0xB9/0xAB timing", 0},
    {"ram.march", suite_ram_march, "transparent March C- on SRAM (IRQs off per 1 KB block)", 0},
    {"flash.check", suite_flash_check, "image CRC, flash size, UID, option bytes (read-only)", 0},
    {"bench.cpu", suite_bench_cpu, "int/float/double/math/game kernels (cycles)", 0},
    {"bench.mem", suite_bench_mem, "memcpy/memset: newlib/word/LDM/DMA2 + crossover", 0},
    {"bench.flash", suite_bench_flash, "ART on/off, wait states, RAM execution", 0},
    {"bench.irq", suite_bench_irq, "NVIC latency, tail-chain, FPU lazy stacking, EXTI", 0},
    {"bench.lcd", suite_bench_lcd, "SPI1 12/24/48 MHz frame/fill/rects, polling vs DMA", 0},
    {"bench.gfx", suite_bench_gfx, "8bpp+palette pipeline, sprites, tilemap, glyphs, RAM left", 0},
    {"bench.w25q", suite_bench_w25q, "W25Q read speeds (read-only), concurrent with LCD DMA", 0},
    {"bench.audio", suite_bench_audio, "software mixer cost per sample/CPU %", 0},
    {"audio.tone", suite_audio_tone, "[U] sine [f=1000] [db=-12] [s=5] [cfg=0-3] [eng=a|b] [fs=] (amp ON)", SUITE_F_USER},
    {"audio.sweep", suite_audio_sweep, "[U] 1/3-octave steps 50 Hz-12.5 kHz, 3 s each, ~100 s", SUITE_F_USER},
    {"audio.chirp", suite_audio_chirp, "[U] 20 Hz-15 kHz exp chirp 10 s x3", SUITE_F_USER},
    {"audio.noise", suite_audio_noise, "[U] [type=pink|white] [s=30] [db=-15]", SUITE_F_USER},
    {"audio.hiss", suite_audio_hiss, "[U] idle hiss: amp off / PA8 low,high,hi-Z / PWM 50% x4 carriers, 15 s each", SUITE_F_USER},
    {"audio.matrix", suite_audio_matrix, "[U] carrier x Fs x dither (48 cfg) idle+1 kHz [from= to= idle= tone=]", SUITE_F_USER},
    {"audio.clip", suite_audio_clip, "[U] 1 kHz / 200 Hz -20..0 dBFS in 1 dB steps", SUITE_F_USER},
    {"audio.pop", suite_audio_pop, "[U] amp on/off naive vs 20/50/100 ms ramp x3", SUITE_F_USER},
    {"audio.cpu", suite_audio_cpu, "mixer cost: engine A ISR / engine B block, 22.05 kHz 4 ch (amp OFF)", 0},
    {"bench.sys", suite_bench_sys, "CDC, WFI, ADC, CRC unit, entropy [stop=1]", 0},
    {"bench.all", suite_bench_all, "all auto benchmarks + frame budget", 0},
    {"spin", suite_spin, "spin <ms>: busy loop with IRQs enabled (DFU-touch test)", 0},
    {"prompt", suite_prompt, "[U] prompt [timeout_s]: @P/@A round trip via device buttons", SUITE_F_USER},
};
const int g_suite_count = (int)(sizeof(g_suites) / sizeof(g_suites[0]));

/* ---------------------------------------------------------------- args */

const char *suite_arg_str(int argc, char **argv, const char *key, const char *def)
{
    size_t kl = strlen(key);
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], key, kl) == 0 && argv[i][kl] == '=') {
            return &argv[i][kl + 1];
        }
    }
    return def;
}

uint32_t suite_arg_u32(int argc, char **argv, const char *key, uint32_t def)
{
    const char *v = suite_arg_str(argc, argv, key, NULL);
    if (!v || !*v) {
        return def;
    }
    char *end;
    unsigned long r = strtoul(v, &end, 0);
    return (end == v) ? def : (uint32_t)r;
}

bool suite_user_start(const char *suite, const char *text, int argc, char **argv)
{
    static const char *const ans[] = {"A=start", "B=skip", NULL};
    char id[40];
    snprintf(id, sizeof(id), "%s_start", suite);
    for (char *p = id; *p; p++) {
        if (*p == '.') *p = '_';
    }
    uint32_t t_s = suite_arg_u32(argc, argv, "t", 60u);
    ui_answer_t a = ui_prompt(id, text, ans, t_s * 1000u);
    if (a == UI_ANS_OK) {
        return true;
    }
    res_printf("{\"t\":\"skip\",\"suite\":\"%s\",\"reason\":\"%s\"}", suite,
               a == UI_ANS_FAIL ? "skipped by user" : ui_answer_str(a));
    return false;
}

bool suite_has_word(int argc, char **argv, const char *word)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], word) == 0) {
            return true;
        }
    }
    return false;
}

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
