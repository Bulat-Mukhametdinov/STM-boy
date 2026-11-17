/* Host command dispatcher — implements PROTOCOL.md (P0 command set).
   Every command ends with "@OK <cmd>" or "@ERR <cmd> <msg>", except `run`,
   which ends with "@DONE <suite> pass|fail|abort". */

#include "cmd.h"
#include "bench.h"
#include "board.h"
#include "clock.h"
#include "crc.h"
#include "dfu.h"
#include "fw_id.h"
#include "hwcal.h"
#include "pwrtest.h"
#include "lcd.h"
#include "log.h"
#include "noinit.h"
#include "power.h"
#include "suites.h"
#include "ui.h"
#include "usb.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define MAX_ARGS 8

static const char *s_busy;          /* NULL = idle */
static volatile bool s_abort;
static bool s_hello_sent;
static const char *s_reset_cause = "unknown";
static uint32_t s_reset_csr;

/* ---------------------------------------------------------------- busy / abort */

void busy_begin(const char *suite)
{
    s_busy = suite;
    s_abort = false;
    g_usb_abort_req = false;
}

void busy_end(void)
{
    s_busy = NULL;
    s_abort = false;
    g_usb_abort_req = false;
}

const char *busy_name(void) { return s_busy ? s_busy : "idle"; }
bool abort_requested(void) { return s_abort || g_usb_abort_req; }
void abort_request(void) { s_abort = true; }

/* ---------------------------------------------------------------- reset cause */

void cmd_capture_reset_cause(void)
{
    uint32_t csr = RCC->CSR;
    s_reset_csr = csr;
    if (csr & RCC_CSR_PORRSTF) {
        s_reset_cause = "por";
    } else if (csr & RCC_CSR_BORRSTF) {
        s_reset_cause = "bor";
    } else if (csr & RCC_CSR_IWDGRSTF) {
        s_reset_cause = "iwdg";
    } else if (csr & RCC_CSR_WWDGRSTF) {
        s_reset_cause = "wwdg";
    } else if (csr & RCC_CSR_LPWRRSTF) {
        s_reset_cause = "lpwr";
    } else if (csr & RCC_CSR_SFTRSTF) {
        s_reset_cause = "sft";
    } else if (csr & RCC_CSR_PINRSTF) {
        s_reset_cause = "pin";
    } else {
        s_reset_cause = "unknown"; /* e.g. bootloader exit without our clean reset */
    }
    RCC->CSR |= RCC_CSR_RMVF; /* clear for the next boot */
}

const char *cmd_reset_cause(void) { return s_reset_cause; }

/* ---------------------------------------------------------------- info */

static const char *boot_path(void)
{
    uint32_t f = g_noinit.boot_flags;
    if (f & NOINIT_BOOT_CLEAN_RESET) return "bootloader_exit";
    if (f & NOINIT_BOOT_COLD) return "cold";
    return "warm";
}

void cmd_emit_info(const char *prefix)
{
    static jb_t j; /* 1 KB: keep it off the stack */
    char uid[25], serial[13];

    board_uid_hex(uid);
    board_usb_serial(serial);

    jb_begin(&j, "info");
    jb_str(&j, "uid", uid);
    jb_str(&j, "serial", serial);
    jb_str(&j, "fw", g_fw_hash);
    jb_u32(&j, "build", g_fw_build);
    jb_u32(&j, "clk", SystemCoreClock);
    jb_str(&j, "opt", g_fw_opt);
    jb_str(&j, "busy", busy_name());
    jb_bool(&j, "hwcal", hwcal_state() == HWCAL_VALID);
    jb_str(&j, "reset", s_reset_cause);
    jb_u32(&j, "flash_kb", board_flash_kb());
    jb_u32(&j, "drops", log_drops());
    jb_u32(&j, "rx_drops", usb_rx_drops());
    jb_str(&j, "boot", boot_path());
    jb_u32(&j, "boot_count", g_noinit.boot_count);
    jb_hex32(&j, "csr", s_reset_csr);
    jb_str(&j, "clock", clock_status_str(clock_last_status()));
    jb_str(&j, "hwcal_state", hwcal_state_str(hwcal_state()));
    jb_bool(&j, "w25q", w25q_present());
    jb_u32(&j, "uptime_ms", HAL_GetTick());
    jb_bool(&j, "vbus", power_vbus());
    jb_bool(&j, "chg", power_charging());
    jb_u32(&j, "faults", g_noinit.fault_count);
    if (g_noinit.fault_count) {
        char f[96];
        snprintf(f, sizeof(f),
                 "{\"pc\":\"%08" PRIX32 "\",\"lr\":\"%08" PRIX32 "\",\"cfsr\":\"%08" PRIX32
                 "\",\"hfsr\":\"%08" PRIX32 "\"}",
                 g_noinit.fault_pc, g_noinit.fault_lr, g_noinit.fault_cfsr, g_noinit.fault_hfsr);
        jb_raw(&j, "fault", f);
    }
    jb_end(&j);
    jb_emit(&j, prefix);
}

/* ---------------------------------------------------------------- commands */

static void cmd_help(void)
{
    log_puts("stm32-hwtest commands:\r\n"
             "  ping | help | info | suites | crc | abort\r\n"
             "  dfu            enter the ST system bootloader (USB DFU)\r\n"
             "  reset          software reset\r\n"
             "  hwcal get      show the calibration record\r\n"
             "  hwcal init [force]  write defaults (force: overwrite foreign data / existing record)\r\n"
             "  hwcal save [force]  write the RAM calibration to the W25Q (refuses foreign data w/o force)\r\n"
             "  usbbench rx|tx <bytes>  CDC throughput (host: hwtest.py usbbench)\r\n"
             "  w25q id        JEDEC + unique id of the external flash\r\n"
             "  w25q read <addr_hex> <len>   binary dump (@B), len <= 65536\r\n"
             "  w25q crc <addr_hex> <len>    zlib CRC-32 of a range (any length)\r\n"
             "  w25q scan <addr_hex> <len> [step]  non-0xFF ranges at step granularity\r\n"
             "  run <suite> [args]  e.g. run selftest, run spin 5000, run display.cal\r\n"
             "  answer <id> ok|fail  answer the open device prompt from the host\r\n");
    proto_ok("help");
}

static void cmd_crc(void)
{
    fw_crc_result_t r;
    fw_crc_check(&r);
    res_printf("{\"t\":\"crc\",\"calc\":\"%08" PRIX32 "\",\"stored\":\"%08" PRIX32
               "\",\"len\":%" PRIu32 ",\"us\":%" PRIu32 ",\"pass\":%s}",
               r.calc, r.stored, r.len, r.us, r.pass ? "true" : "false");
    proto_ok("crc");
}

static void cmd_w25q(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "id") == 0) {
        uint8_t uid[8];
        char uh[17];
        uint32_t id = w25q_read_jedec();
        w25q_read_uid(uid);
        for (int i = 0; i < 8; i++) {
            snprintf(&uh[2 * i], 3, "%02X", uid[i]);
        }
        res_printf("{\"t\":\"w25q_id\",\"jedec\":\"%06" PRIX32 "\",\"uid\":\"%s\",\"sr1\":\"%02X\","
                   "\"spi_hz\":%" PRIu32 "}",
                   id & 0xFFFFFFu, uh, (unsigned)w25q_read_sr1(), w25q_spi_hz());
        proto_ok("w25q id");
        return;
    }
    if (w25q_cmd_ext(argc, argv)) {
        return;
    }
    proto_err("w25q", "usage: w25q id | read <addr_hex> <len> | crc <addr_hex> <len> | "
                      "scan <addr_hex> <len> [step]");
}

/* hwcal set key=value ...  : edit the RAM calibration (not persisted until `hwcal save`) */
static bool hwcal_set_field(const char *key, uint32_t v)
{
    static const struct { const char *k; size_t off; uint8_t sz; } f[] = {
        {"madctl_land", offsetof(hwcal_t, madctl_land), 1}, {"xoff_land", offsetof(hwcal_t, xoff_land), 1},
        {"yoff_land", offsetof(hwcal_t, yoff_land), 1},     {"madctl_port", offsetof(hwcal_t, madctl_port), 1},
        {"xoff_port", offsetof(hwcal_t, xoff_port), 1},     {"yoff_port", offsetof(hwcal_t, yoff_port), 1},
        {"bgr", offsetof(hwcal_t, bgr), 1},                 {"invert", offsetof(hwcal_t, invert), 1},
        {"gamma_id", offsetof(hwcal_t, gamma_id), 1},       {"spi1_div_ok", offsetof(hwcal_t, spi1_div_ok), 1},
        {"spi1_div_max", offsetof(hwcal_t, spi1_div_max), 1}, {"debounce_ms", offsetof(hwcal_t, debounce_ms), 1},
        {"bl_pwm_hz", offsetof(hwcal_t, bl_pwm_hz), 2},     {"bl_min_level", offsetof(hwcal_t, bl_min_level), 2},
    };
    for (unsigned i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
        if (strcmp(f[i].k, key) == 0) {
            uint8_t *p = (uint8_t *)&g_hwcal + f[i].off;
            if (f[i].sz == 1) {
                *p = (uint8_t)v;
            } else {
                *(uint16_t *)(void *)p = (uint16_t)v;
            }
            return true;
        }
    }
    return false;
}

static void hwcal_report_err(hwcal_err_t e)
{
    char d[96], m[200];
    if (e == HWCAL_ERR_FLASH) {
        snprintf(m, sizeof(m), "%s: %s", hwcal_err_str(e), hwcal_fail_str(d, sizeof(d)));
        proto_err("hwcal", m);
    } else {
        proto_err("hwcal", hwcal_err_str(e));
    }
}

static void cmd_hwcal(int argc, char **argv)
{
    static jb_t j;
    if (argc >= 3 && strcmp(argv[1], "set") == 0) {
        for (int i = 2; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (!eq) {
                proto_err("hwcal", "usage: hwcal set key=value ...");
                return;
            }
            *eq = '\0';
            if (!hwcal_set_field(argv[i], (uint32_t)strtoul(eq + 1, NULL, 0))) {
                proto_err("hwcal", "unknown field");
                return;
            }
        }
        proto_ok("hwcal set");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "get") == 0) {
        jb_begin(&j, "hwcal");
        hwcal_to_json(&j);
        jb_end(&j);
        jb_emit(&j, "@R ");
        proto_ok("hwcal get");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "init") == 0) {
        bool force = (argc >= 3 && strcmp(argv[2], "force") == 0);
        hwcal_err_t e = hwcal_init(force);
        if (e != HWCAL_OK) {
            hwcal_report_err(e);
            return;
        }
        res_printf("{\"t\":\"hwcal_init\",\"seq\":%" PRIu32 ",\"slot\":\"%c\"}", hwcal_seq(),
                   hwcal_slot());
        proto_ok(force ? "hwcal init force" : "hwcal init");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "save") == 0) { /* write the RAM calibration (g_hwcal) to the W25Q */
        bool force = (argc >= 3 && strcmp(argv[2], "force") == 0);
        hwcal_err_t e = hwcal_save(&g_hwcal, force);
        if (e != HWCAL_OK) {
            hwcal_report_err(e);
            return;
        }
        res_printf("{\"t\":\"hwcal_save\",\"seq\":%" PRIu32 ",\"slot\":\"%c\"}", hwcal_seq(), hwcal_slot());
        proto_ok(force ? "hwcal save force" : "hwcal save");
        return;
    }
    proto_err("hwcal", "usage: hwcal get | hwcal init [force] | hwcal save [force]");
}

/* @R {"t":"suites","list":[...],"user":[...]}  ("user" = [U] suites that need
   a human at the device; their prompts time out). */
static void cmd_suites(void)
{
    static char buf[2048];
    size_t n = 0;
    n += (size_t)snprintf(&buf[n], sizeof(buf) - n, "@R {\"t\":\"suites\",\"list\":[");
    for (int i = 0; i < g_suite_count && n < sizeof(buf) - 64u; i++) {
        n += (size_t)snprintf(&buf[n], sizeof(buf) - n, "%s\"%s\"", i ? "," : "", g_suites[i].name);
    }
    n += (size_t)snprintf(&buf[n], sizeof(buf) - n, "],\"user\":[");
    bool first = true;
    for (int i = 0; i < g_suite_count && n < sizeof(buf) - 16u; i++) {
        if (g_suites[i].flags & SUITE_F_USER) {
            n += (size_t)snprintf(&buf[n], sizeof(buf) - n, "%s\"%s\"", first ? "" : ",", g_suites[i].name);
            first = false;
        }
    }
    if (n < sizeof(buf) - 6u) {
        n += (size_t)snprintf(&buf[n], sizeof(buf) - n, "]}\r\n");
        log_write(buf, n);
    }
    proto_ok("suites");
}

/* answer <prompt id> ok|fail : answer the open device prompt from the host. */
static void cmd_answer(int argc, char **argv)
{
    if (argc < 3 || (strcmp(argv[2], "ok") != 0 && strcmp(argv[2], "fail") != 0)) {
        proto_err("answer", "usage: answer <id> ok|fail");
        return;
    }
    ui_host_answer_set(argv[1], strcmp(argv[2], "ok") == 0 ? UI_ANS_OK : UI_ANS_FAIL);
    proto_ok("answer");
}

static void wait_flush_ms(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - t0) < ms) {
        log_poll();
    }
}

static void cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        proto_err("run", "usage: run <suite> [args]");
        return;
    }
    const suite_t *s = suite_find(argv[1]);
    if (!s) {
        proto_err("run", "unknown suite");
        return;
    }
    suite_run(s, argc - 1, &argv[1]);
}

static int tokenize(char *line, char **argv)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (!*p) {
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    return argc;
}

/* restricted = called from sys_service() while something else is running */
static void dispatch(char *line, bool restricted)
{
    char *argv[MAX_ARGS];
    int argc;

    if (strcmp(line, USB_LINE_TOOLONG) == 0) {
        proto_err("line", "too long (max 120 chars)");
        return;
    }
    argc = tokenize(line, argv);
    if (argc == 0) {
        return;
    }
    const char *c = argv[0];

    if (!s_hello_sent) {
        s_hello_sent = true; /* first command counts as "port opened" */
        cmd_emit_info("@HELLO ");
    }

    if (strcmp(c, "ping") == 0) {
        proto_ok("ping");
    } else if (strcmp(c, "info") == 0) {
        cmd_emit_info("@R ");
        proto_ok("info");
    } else if (strcmp(c, "help") == 0) {
        cmd_help();
    } else if (strcmp(c, "suites") == 0) {
        cmd_suites();
    } else if (strcmp(c, "abort") == 0) {
        if (s_busy) {
            s_abort = true;
        }
        proto_ok("abort"); /* a running suite then ends with @DONE <suite> abort */
    } else if (strcmp(c, "dfu") == 0) {
        proto_ok("dfu");
        wait_flush_ms(50);
        dfu_request();
    } else if (strcmp(c, "reset") == 0) {
        proto_ok("reset");
        wait_flush_ms(50);
        NVIC_SystemReset();
    } else if (strcmp(c, "answer") == 0) {
        cmd_answer(argc, argv);
    } else if (strcmp(c, "adccal") == 0 || strcmp(c, "cur") == 0) {
        pwr_cmd(argc, argv);
    } else if (strcmp(c, "lcd") == 0) {             /* lcd div <2|4|8|16>: SPI1 prescaler on the fly (scope work) */
        unsigned long d = (argc >= 3 && strcmp(argv[1], "div") == 0) ? strtoul(argv[2], NULL, 0) : 0;
        if (argc >= 3 && strcmp(argv[1], "speed") == 0) {   /* lcd speed <0..3>: OSPEEDR for SCK/MOSI */
            unsigned long sp = strtoul(argv[2], NULL, 0);
            if (sp <= 3u) {
                lcd_set_gpio_speed((uint32_t)sp);
                res_printf("{\"t\":\"lcd\",\"gpio_speed\":%lu}", sp);
                proto_ok("lcd speed");
            } else {
                proto_err("lcd", "usage: lcd speed <0..3>");
            }
        } else if (d == 2 || d == 4 || d == 8 || d == 16 || d == 32) {
            lcd_set_spi_div((uint16_t)d);
            res_printf("{\"t\":\"lcd\",\"spi_div\":%lu,\"spi_hz\":%lu}", d, 96000000ul / d);
            proto_ok("lcd div");
        } else {
            proto_err("lcd", "usage: lcd div <2|4|8|16|32>");
        }
    } else if (strcmp(c, "battlog") == 0) {
        battlog_cmd(argc, argv, restricted);
    } else if (restricted) {
        proto_err(c, "busy");
    } else if (strcmp(c, "crc") == 0) {
        cmd_crc();
    } else if (strcmp(c, "w25q") == 0) {
        cmd_w25q(argc, argv);
    } else if (strcmp(c, "usbbench") == 0) {
        usbbench_cmd(argc, argv);
    } else if (strcmp(c, "hwcal") == 0) {
        cmd_hwcal(argc, argv);
    } else if (strcmp(c, "run") == 0) {
        cmd_run(argc, argv);
    } else {
        proto_err(c, "unknown command");
    }
}

void cmd_init(void)
{
    s_busy = NULL;
    s_abort = false;
    s_hello_sent = false;
}

static void hello_check(void)
{
    if (g_usb_dtr_rise) {
        g_usb_dtr_rise = false;
        if (!s_hello_sent) {
            s_hello_sent = true;
            cmd_emit_info("@HELLO ");
        }
    }
}

void cmd_poll(void)
{
    char line[USB_RX_LINE_MAX];
    hello_check();
    while (usb_rx_pop_line(line, sizeof(line))) {
        dispatch(line, false);
    }
}

void sys_service(void)
{
    char line[USB_RX_LINE_MAX];
    log_poll();
    power_poll();
    hello_check();
    while (usb_rx_pop_line(line, sizeof(line))) {
        dispatch(line, true);
    }
}
