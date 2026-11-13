/* P3 memory suites: ram.march (transparent March C- on SRAM) and flash.check. */

#include "bench.h"
#include "board.h"
#include "cmd.h"
#include "crc.h"
#include "fw_id.h"
#include "gfx.h"
#include "log.h"
#include "noinit.h"
#include "ui.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define RAM_BASE   ((uint32_t)0x20000000u)
#define RAM_END    ((uint32_t)0x20020000u)
#define BLK        ((uint32_t)1024u)
#define NBLK       ((RAM_END - RAM_BASE) / BLK)

/* two backup blocks: a block is tested with the other one as its backup */
static uint32_t s_bkP[BLK / 4] __attribute__((aligned(1024)));
static uint32_t s_bkQ[BLK / 4] __attribute__((aligned(1024)));

typedef struct { uint32_t addr, expect, got; } merr_t;
static merr_t s_err[8];
static uint32_t s_nerr;

/* March C-: up(w0) up(r0,w1) up(r1,w0) down(r0,w1) down(r1,w0) down(r0). Errors are kept in
   registers and the result is returned: the block under test may contain any static variable. */
static __attribute__((noinline)) uint32_t march_words(volatile uint32_t *p, uint32_t n, uint32_t a,
                                                       uint32_t *first_addr, uint32_t *first_exp, uint32_t *first_got)
{
    const uint32_t b = ~a;
    uint32_t errs = 0, fa = 0, fe = 0, fg = 0;
#define CHK(i, e)                                  \
    do {                                           \
        uint32_t v_ = p[i];                        \
        if (v_ != (e)) {                           \
            if (!errs) { fa = (uint32_t)(uintptr_t)&p[i]; fe = (e); fg = v_; } \
            errs++;                                \
        }                                          \
    } while (0)
    for (uint32_t i = 0; i < n; i++) p[i] = a;
    for (uint32_t i = 0; i < n; i++) { CHK(i, a); p[i] = b; }
    for (uint32_t i = 0; i < n; i++) { CHK(i, b); p[i] = a; }
    for (uint32_t i = n; i-- > 0;) { CHK(i, a); p[i] = b; }
    for (uint32_t i = n; i-- > 0;) { CHK(i, b); p[i] = a; }
    for (uint32_t i = n; i-- > 0;) { CHK(i, a); }
#undef CHK
    *first_addr = fa;
    *first_exp = fe;
    *first_got = fg;
    return errs;
}

static void test_block(uint32_t addr, uint32_t *bk, uint32_t *errs, uint32_t *tested)
{
    volatile uint32_t *blk = (volatile uint32_t *)(uintptr_t)addr;
    static const uint32_t bgs[2] = {0x00000000u, 0x55555555u};
    uint32_t e_total = 0, fa[2], fe[2], fg[2];
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    for (uint32_t i = 0; i < BLK / 4u; i++) bk[i] = blk[i];
    for (int k = 0; k < 2; k++) {
        uint32_t e = march_words(blk, BLK / 4u, bgs[k], &fa[k], &fe[k], &fg[k]);
        e_total += e;
    }
    for (uint32_t i = 0; i < BLK / 4u; i++) blk[i] = bk[i];
    if (!pm) __enable_irq();
    if (e_total) {
        for (int k = 0; k < 2; k++) {
            if (fa[k] && s_nerr < 8u) {
                s_err[s_nerr].addr = fa[k];
                s_err[s_nerr].expect = fe[k];
                s_err[s_nerr].got = fg[k];
                s_nerr++;
            }
        }
    }
    *errs += e_total;
    *tested += BLK;
}

suite_verdict_t suite_ram_march(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t sp = __get_MSP();
    uint32_t stack_lo = (sp & ~(BLK - 1u)) - BLK; /* keep one extra block below the live stack frame */
    uint32_t pblk = ((uint32_t)(uintptr_t)s_bkP - RAM_BASE) / BLK;
    uint32_t qblk = ((uint32_t)(uintptr_t)s_bkQ - RAM_BASE) / BLK;
    uint32_t errs = 0, tested = 0;
    uint32_t t0 = HAL_GetTick();
    char ranges[256];
    size_t rn = 0;
    ranges[0] = '\0';
    bool skip_prev = false;
    uint32_t skip_start = 0, skipped = 0;
    s_nerr = 0;

    for (uint32_t bi = 0; bi < NBLK; bi++) {
        uint32_t addr = RAM_BASE + bi * BLK;
        bool skip = (addr >= stack_lo) || (bi == pblk) || (bi == qblk) /* P and Q are done in pass 2 */;
        bool pq = (bi == pblk) || (bi == qblk);
        if (pq && addr < stack_lo) {
            skip = true; /* handled below */
        }
        if (!skip) {
            test_block(addr, s_bkP, &errs, &tested);
        }
        bool untested = (addr >= stack_lo);
        if (untested && !skip_prev) skip_start = addr;
        if (!untested && skip_prev && rn < sizeof(ranges) - 40u) {
            rn += (size_t)snprintf(&ranges[rn], sizeof(ranges) - rn, "%s[\"%08" PRIX32 "\",\"%08" PRIX32 "\"]",
                                   rn ? "," : "", skip_start, addr);
        }
        if (untested) skipped += BLK;
        skip_prev = untested;
        if ((bi & 15u) == 0u) {
            sys_service();
            if (abort_requested()) return SUITE_ABORT;
        }
    }
    if (skip_prev && rn < sizeof(ranges) - 40u) {
        rn += (size_t)snprintf(&ranges[rn], sizeof(ranges) - rn, "%s[\"%08" PRIX32 "\",\"%08" PRIX32 "\"]", rn ? "," : "",
                               skip_start, RAM_END);
    }
    /* pass 2: the blocks holding the backup buffers, using the other buffer */
    if ((RAM_BASE + pblk * BLK) < stack_lo) test_block(RAM_BASE + pblk * BLK, s_bkQ, &errs, &tested);
    if (qblk != pblk && (RAM_BASE + qblk * BLK) < stack_lo) test_block(RAM_BASE + qblk * BLK, s_bkP, &errs, &tested);

    uint32_t ms = HAL_GetTick() - t0;
    bool pass = (errs == 0u);
    char errj[200];
    size_t en = 0;
    errj[0] = '\0';
    for (uint32_t i = 0; i < s_nerr && en < sizeof(errj) - 60u; i++) {
        en += (size_t)snprintf(&errj[en], sizeof(errj) - en, "%s{\"addr\":\"%08" PRIX32 "\",\"exp\":\"%08" PRIX32
                               "\",\"got\":\"%08" PRIX32 "\"}", i ? "," : "", s_err[i].addr, s_err[i].expect, s_err[i].got);
    }
    res_printf("{\"t\":\"test\",\"suite\":\"ram.march\",\"name\":\"march_c_minus\",\"pass\":%s,\"tested_bytes\":%" PRIu32
               ",\"untested_bytes\":%" PRIu32 ",\"errors\":%" PRIu32 ",\"ms\":%" PRIu32 ",\"backgrounds\":[\"00000000\","
               "\"55555555\"],\"block\":%" PRIu32 ",\"msp\":\"%08" PRIX32 "\",\"untested\":[%s],\"first_errors\":[%s]}",
               pass ? "true" : "false", tested, (RAM_END - RAM_BASE) - tested, errs, ms, BLK, sp, ranges, errj);
    ui_con_line(pass ? COLOR_GREEN : COLOR_RED, "%" PRIu32 " KB tested, %" PRIu32 " errors", tested / 1024u, errs);
    ui_con_line(COLOR_WHITE, "untested: live stack + .noinit (%" PRIu32 " B)", (RAM_END - RAM_BASE) - tested);
    (void)skipped;
    return pass ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ flash.check */

suite_verdict_t suite_flash_check(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bool all = true;
    fw_crc_result_t r;
    fw_crc_check(&r);
    res_printf("{\"t\":\"test\",\"suite\":\"flash.check\",\"name\":\"image_crc\",\"pass\":%s,\"calc\":\"%08" PRIX32
               "\",\"stored\":\"%08" PRIX32 "\",\"len\":%" PRIu32 ",\"us\":%" PRIu32 ",\"image_pct_of_512k\":%" PRIu32 "}",
               r.pass ? "true" : "false", r.calc, r.stored, r.len, r.us, r.len * 100u / (512u * 1024u));
    ui_con_line(r.pass ? COLOR_GREEN : COLOR_RED, "image CRC %08" PRIX32, r.calc);
    all &= r.pass;

    uint32_t kb = board_flash_kb();
    char uid[25];
    board_uid_hex(uid);
    bool kbok = (kb == 512u);
    res_printf("{\"t\":\"test\",\"suite\":\"flash.check\",\"name\":\"flash_size\",\"pass\":%s,\"flash_kb\":%" PRIu32
               ",\"expect_kb\":512,\"uid\":\"%s\",\"dev_id\":\"%03" PRIX32 "\",\"rev_id\":\"%04" PRIX32 "\"}",
               kbok ? "true" : "false", kb, uid, DBGMCU->IDCODE & 0xFFFu, DBGMCU->IDCODE >> 16);
    ui_con_line(kbok ? COLOR_GREEN : COLOR_RED, "flash %" PRIu32 " KB", kb);
    all &= kbok;

    /* option bytes (read-only) */
    uint32_t o = FLASH->OPTCR;
    uint32_t rdp = (o >> 8) & 0xFFu, bor = (o >> 2) & 3u, nwrp = (o >> 16) & 0xFFu;
    bool rdp0 = (rdp == 0xAAu), nowrp = (nwrp == 0xFFu);
    static const char *const bor_s[4] = {"3 (2.70-3.60 V)", "2 (2.40-2.70 V)", "1 (2.10-2.40 V)", "0 / off (1.80-2.10 V)"};
    /* BOR_LEV 00 = level 3, 01 = level 2, 10 = level 1, 11 = off */
    res_printf("{\"t\":\"test\",\"suite\":\"flash.check\",\"name\":\"option_bytes\",\"pass\":%s,\"optcr\":\"%08" PRIX32
               "\",\"rdp\":\"%02" PRIX32 "\",\"rdp_level\":\"%s\",\"bor_lev\":%" PRIu32 ",\"bor\":\"%s\",\"wdg_sw\":%" PRIu32
               ",\"nrst_stop\":%" PRIu32 ",\"nrst_stdby\":%" PRIu32 ",\"nwrp\":\"%02" PRIX32 "\",\"write_protected\":%s,"
               "\"notes\":[\"%s\",\"%s\"]}",
               (rdp0 && nowrp) ? "true" : "false", o, rdp, rdp == 0xAAu ? "0" : (rdp == 0xCCu ? "2" : "1"), bor,
               bor_s[bor], (o >> 5) & 1u, (o >> 6) & 1u, (o >> 7) & 1u, nwrp, nowrp ? "false" : "true",
               bor == 3u ? "BOR is OFF: no brown-out reset, relevant on a sagging battery (SWD/option bytes change needed to enable)"
                         : "BOR enabled",
               rdp0 ? "RDP level 0: flash readable over SWD/DFU" : "readout protection active");
    ui_con_line(rdp0 ? COLOR_GREEN : COLOR_RED, "RDP %02" PRIX32 " BOR %s", rdp, bor_s[bor]);
    all &= rdp0 && nowrp;
    return all ? SUITE_PASS : SUITE_FAIL;
}
