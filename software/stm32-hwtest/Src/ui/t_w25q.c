/* P3 W25Q suites: w25q.info, w25q.readcrc, w25q.scratch, w25q.sector, w25q.pd.
   Writes happen ONLY inside the scratch area 0xF00000-0xFDFFFF (and the driver itself refuses any
   program/erase below 0xF00000). */

#include "bench.h"
#include "cmd.h"
#include "log.h"
#include "timebase.h"
#include "ui.h"
#include "w25q.h"
#include "gfx.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define SUITE_I "w25q.info"

static void line(bool pass, const char *name, const char *detail)
{
    ui_con_line(pass ? COLOR_GREEN : COLOR_RED, "%-4s %s %s", pass ? "PASS" : "FAIL", name, detail);
}

/* ================================================================ w25q.info */

suite_verdict_t suite_w25q_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bool all = true;
    uint32_t id = w25q_read_jedec() & 0xFFFFFFu;
    uint8_t uid[8];
    w25q_read_uid(uid);
    uint8_t sr1 = w25q_read_reg(0x05), sr2 = w25q_read_reg(0x35), sr3 = w25q_read_reg(0x15);
    bool idok = (id == 0xEF4018u);
    res_printf("{\"t\":\"test\",\"suite\":\"w25q.info\",\"name\":\"jedec\",\"pass\":%s,\"jedec\":\"%06" PRIX32
               "\",\"mfr\":\"Winbond\",\"uid\":\"%02X%02X%02X%02X%02X%02X%02X%02X\",\"spi_hz\":%" PRIu32 "}",
               idok ? "true" : "false", id, uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7],
               w25q_spi_hz());
    line(idok, "jedec", "EF4018");
    all &= idok;

    unsigned bp = (sr1 >> 2) & 7u, tb = (sr1 >> 5) & 1u, sec = (sr1 >> 6) & 1u, srp0 = (sr1 >> 7) & 1u;
    unsigned cmp = (sr2 >> 6) & 1u, srl = sr2 & 1u, qe = (sr2 >> 1) & 1u, lb = (sr2 >> 3) & 7u, sus = (sr2 >> 7) & 1u;
    unsigned wps = (sr3 >> 2) & 1u, drv = (sr3 >> 5) & 3u;
    bool unprot = (bp == 0u) && !srp0 && !cmp && !srl && !(sr1 & 1u);
    res_printf("{\"t\":\"test\",\"suite\":\"w25q.info\",\"name\":\"status_regs\",\"pass\":%s,\"sr1\":\"%02X\",\"sr2\":\"%02X\","
               "\"sr3\":\"%02X\",\"bp\":%u,\"tb\":%u,\"sec\":%u,\"srp0\":%u,\"cmp\":%u,\"srl\":%u,\"qe\":%u,\"lb\":%u,"
               "\"sus\":%u,\"wps\":%u,\"drv\":%u,\"busy\":%u,\"note\":\"block protect %s\"}",
               unprot ? "true" : "false", sr1, sr2, sr3, bp, tb, sec, srp0, cmp, srl, qe, lb, sus, wps, drv, sr1 & 1u,
               unprot ? "clear: whole array writable" : "SET: part of the array is write-protected");
    char d[40];
    snprintf(d, sizeof(d), "SR %02X %02X %02X bp=%u", sr1, sr2, sr3, bp);
    line(unprot, "block-protect", d);
    all &= unprot;

    uint8_t *sf = g_arena;
    w25q_read_sfdp(sf, 256);
    bool sig = (memcmp(sf, "SFDP", 4) == 0);
    uint32_t ptp = (uint32_t)sf[12] | ((uint32_t)sf[13] << 8) | ((uint32_t)sf[14] << 16);
    uint32_t density_bits = 0;
    if (sig && ptp + 12u <= 256u) {
        uint32_t dw2 = (uint32_t)sf[ptp + 4] | ((uint32_t)sf[ptp + 5] << 8) | ((uint32_t)sf[ptp + 6] << 16) |
                       ((uint32_t)sf[ptp + 7] << 24);
        density_bits = (dw2 & 0x80000000u) ? 0u : dw2 + 1u;
    }
    res_printf("{\"t\":\"test\",\"suite\":\"w25q.info\",\"name\":\"sfdp\",\"pass\":%s,\"signature\":\"%c%c%c%c\","
               "\"rev\":\"%u.%u\",\"nph\":%u,\"bfpt_offset\":%" PRIu32 ",\"density_bits\":%" PRIu32 ",\"density_MB\":%" PRIu32 "}",
               (sig && density_bits == 128u * 1024u * 1024u) ? "true" : "false", sf[0], sf[1], sf[2], sf[3], sf[5], sf[4],
               sf[6] + 1u, ptp, density_bits, density_bits / 8u / 1048576u);
    snprintf(d, sizeof(d), "%u Mbit", (unsigned)(density_bits >> 20));
    line(sig && density_bits == 128u * 1024u * 1024u, "sfdp", d);
    all &= sig && density_bits == 128u * 1024u * 1024u;
    return all ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ w25q.readcrc */

suite_verdict_t suite_w25q_readcrc(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static const uint16_t divs[3] = {8, 4, 2};
    uint32_t crcs[3] = {0, 0, 0};
    const uint32_t chunk = 16384u;
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    (void)RCC->AHB1ENR;
    for (int i = 0; i < 3; i++) {
        w25q_set_div(divs[i]);
        uint32_t hz = w25q_spi_hz();
        uint32_t t0 = HAL_GetTick();
        CRC->CR = CRC_CR_RESET;
        for (uint32_t a = 0; a < W25Q_SIZE; a += chunk) {
            if (w25q_read(a, g_arena, chunk) != W25Q_OK) {
                w25q_set_div(4);
                return SUITE_FAIL;
            }
            const uint32_t *w = (const uint32_t *)(void *)g_arena;
            for (uint32_t k = 0; k < chunk / 4u; k++) {
                CRC->DR = w[k];
            }
            if ((a & 0xFFFFFu) == 0u) {
                sys_service();
                if (abort_requested()) {
                    w25q_set_div(4);
                    return SUITE_ABORT;
                }
            }
        }
        crcs[i] = CRC->DR;
        uint32_t ms = HAL_GetTick() - t0;
        uint32_t kBps = ms ? (uint32_t)(((uint64_t)W25Q_SIZE * 1000u / 1024u) / ms) : 0u;
        res_printf("{\"t\":\"test\",\"suite\":\"w25q.readcrc\",\"name\":\"crc_%" PRIu32 "mhz\",\"pass\":true,"
                   "\"crc\":\"%08" PRIX32 "\",\"ms\":%" PRIu32 ",\"kBps\":%" PRIu32 ",\"mbps\":\"%" PRIu32 ".%02" PRIu32
                   "\",\"spi_hz\":%" PRIu32 ",\"bytes\":%" PRIu32 "}",
                   hz / 1000000u, crcs[i], ms, kBps, kBps * 1024u / 1000000u, (kBps * 1024u / 10000u) % 100u, hz,
                   (uint32_t)W25Q_SIZE);
        ui_con_line(COLOR_WHITE, "%2" PRIu32 " MHz crc %08" PRIX32 " %" PRIu32 " ms", hz / 1000000u, crcs[i], ms);
    }
    w25q_set_div(4);
    bool same = (crcs[0] == crcs[1]) && (crcs[1] == crcs[2]);
    res_printf("{\"t\":\"test\",\"suite\":\"w25q.readcrc\",\"name\":\"match\",\"pass\":%s,\"crc\":\"%08" PRIX32 "\"}",
               same ? "true" : "false", crcs[1]);
    line(same, "crc match", "6/12/24 MHz");
    return same ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ scratch helpers */

#define SCR_LO ((uint32_t)W25Q_SCRATCH_BASE)
#define SCR_HI ((uint32_t)W25Q_SCRATCH_END)

static bool scr_ok(uint32_t a, uint32_t len) { return a >= SCR_LO && len <= SCR_HI - SCR_LO && a <= SCR_HI - len; }

static void gen_page(int pat, uint32_t addr, uint8_t *p)
{
    switch (pat) {
    case 0: memset(p, 0x55, 256); break;
    case 1: memset(p, 0xAA, 256); break;
    case 2:
        for (uint32_t i = 0; i < 256; i += 4) {
            uint32_t v = addr + i;
            memcpy(&p[i], &v, 4);
        }
        break;
    case 3: {
        uint32_t x = (addr | 1u) * 2654435761u;
        for (uint32_t i = 0; i < 256; i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            p[i] = (uint8_t)(x >> 8);
        }
        break;
    }
    default: memset(p, 0x00, 256); break;
    }
}

typedef struct { uint32_t min, max, sum, n; } tstat_t;
static void tstat_add(tstat_t *t, uint32_t v)
{
    if (t->n == 0u || v < t->min) t->min = v;
    if (v > t->max) t->max = v;
    t->sum += v;
    t->n++;
}

/* Programs a whole block with pattern `pat` (page by page, timed). */
static bool prog_block(int pat, uint32_t addr, uint32_t len, tstat_t *tp)
{
    uint8_t *pg = g_arena;
    if (!scr_ok(addr, len)) return false;
    for (uint32_t a = addr; a < addr + len; a += 256u) {
        gen_page(pat, a, pg);
        uint32_t us = 0;
        if (w25q_program_page_timed(a, pg, 256, &us) != W25Q_OK) return false;
        if (tp) tstat_add(tp, us);
        if ((a & 0x3FFFu) == 0u) {
            sys_service();
            if (abort_requested()) return false;
        }
    }
    return true;
}

/* Verifies a block against the pattern; pat < 0 means blank (0xFF). Returns mismatching bytes. */
static uint32_t verify_block(int pat, uint32_t addr, uint32_t len)
{
    uint8_t *rd = g_arena + 4096;
    uint8_t *ex = g_arena + 8192;
    uint32_t bad = 0;
    for (uint32_t a = addr; a < addr + len; a += 4096u) {
        w25q_read(a, rd, 4096);
        for (uint32_t o = 0; o < 4096u; o += 256u) {
            if (pat < 0) {
                memset(ex, 0xFF, 256);
            } else {
                gen_page(pat, a + o, ex);
            }
            for (uint32_t i = 0; i < 256u; i++) {
                if (rd[o + i] != ex[i]) bad++;
            }
        }
        sys_service();
    }
    return bad;
}

/* ================================================================ w25q.scratch */

suite_verdict_t suite_w25q_scratch(int argc, char **argv)
{
    const char *sname = "w25q.scratch";
    if (!suite_has_word(argc, argv, "confirm=yes")) {
        res_printf("{\"t\":\"test\",\"suite\":\"%s\",\"name\":\"confirm\",\"pass\":false,\"why\":\"destructive: run with "
                   "confirm=yes (scratch 0xF00000-0xFDFFFF only)\"}",
                   sname);
        ui_con_line(COLOR_RED, "needs confirm=yes");
        return SUITE_FAIL;
    }
    bool all = true;
    char d[48];

    /* 1. blank scan of the whole scratch area */
    uint32_t nonff = 0, first = 0xFFFFFFFFu;
    for (uint32_t a = SCR_LO; a < SCR_HI; a += 16384u) {
        w25q_read(a, g_arena, 16384);
        for (uint32_t i = 0; i < 16384u; i++) {
            if (g_arena[i] != 0xFF) {
                if (first == 0xFFFFFFFFu) first = a + i;
                nonff++;
            }
        }
        sys_service();
        if (abort_requested()) return SUITE_ABORT;
    }
    res_printf("{\"t\":\"test\",\"suite\":\"%s\",\"name\":\"blank_scan\",\"pass\":true,\"bytes\":%" PRIu32
               ",\"non_ff\":%" PRIu32 ",\"first_non_ff\":\"%06" PRIX32 "\"}",
               sname, SCR_HI - SCR_LO, nonff, first == 0xFFFFFFFFu ? 0u : first);
    ui_con_line(COLOR_WHITE, "scratch non-FF bytes: %" PRIu32, nonff);

    /* 2. timed erase vs datasheet (programmed with 0x00 first so the erase has work to do) */
    static const struct {
        const char *name; uint8_t cmd; uint32_t size; uint32_t base; int count; uint32_t typ_ms, max_ms;
    } er[] = {
        {"erase_4k", 0x20, 4096, 0xF00000, 3, 45, 400},
        {"erase_32k", 0x52, 32768, 0xF08000, 2, 120, 1600},
        {"erase_64k", 0xD8, 65536, 0xF20000, 2, 150, 2000},
    };
    tstat_t tprog = {0, 0, 0, 0};
    for (unsigned e = 0; e < 3; e++) {
        tstat_t te = {0, 0, 0, 0};
        bool ok = true;
        for (int k = 0; k < er[e].count && ok; k++) {
            uint32_t a = er[e].base + (uint32_t)k * er[e].size;
            if (!prog_block(4, a, er[e].size, &tprog)) { ok = false; break; }
            uint32_t us = 0;
            if (w25q_erase_cmd(er[e].cmd, a, &us) != W25Q_OK) { ok = false; break; }
            tstat_add(&te, us);
            if (verify_block(-1, a, er[e].size) != 0u) ok = false;
            sys_service();
            if (abort_requested()) return SUITE_ABORT;
        }
        bool inmax = ok && (te.max <= er[e].max_ms * 1000u);
        res_printf("{\"t\":\"test\",\"suite\":\"%s\",\"name\":\"%s\",\"pass\":%s,\"n\":%" PRIu32 ",\"min_ms\":%" PRIu32
                   ",\"max_ms\":%" PRIu32 ",\"avg_ms\":%" PRIu32 ",\"typ_ms\":%" PRIu32 ",\"datasheet_max_ms\":%" PRIu32
                   ",\"min_us\":%" PRIu32 ",\"max_us\":%" PRIu32 "}",
                   sname, er[e].name, inmax ? "true" : "false", te.n, te.min / 1000u, te.max / 1000u,
                   te.n ? te.sum / te.n / 1000u : 0u, er[e].typ_ms, er[e].max_ms, te.min, te.max);
        snprintf(d, sizeof(d), "%" PRIu32 "-%" PRIu32 " ms (typ %" PRIu32 ")", te.min / 1000u, te.max / 1000u, er[e].typ_ms);
        line(inmax, er[e].name, d);
        all &= inmax;
    }
    bool pp = tprog.n && tprog.max <= 3000u;
    res_printf("{\"t\":\"test\",\"suite\":\"%s\",\"name\":\"page_program\",\"pass\":%s,\"n\":%" PRIu32 ",\"min_us\":%" PRIu32
               ",\"max_us\":%" PRIu32 ",\"avg_us\":%" PRIu32 ",\"typ_us\":400,\"datasheet_max_us\":3000}",
               sname, pp ? "true" : "false", tprog.n, tprog.min, tprog.max, tprog.n ? tprog.sum / tprog.n : 0u);
    snprintf(d, sizeof(d), "%" PRIu32 "-%" PRIu32 " us", tprog.min, tprog.max);
    line(pp, "page_program", d);
    all &= pp;

    /* 3. data patterns on one 64 KB block */
    static const char *const pn[4] = {"0x55", "0xAA", "addr", "lfsr"};
    const uint32_t pb = 0xF40000u;
    for (int p = 0; p < 4; p++) {
        uint32_t us = 0;
        bool ok = (w25q_erase_cmd(0xD8, pb, &us) == W25Q_OK) && verify_block(-1, pb, 65536) == 0u;
        ok = ok && prog_block(p, pb, 65536, NULL);
        uint32_t bad = ok ? verify_block(p, pb, 65536) : 0xFFFFFFFFu;
        bool pass = ok && bad == 0u;
        res_printf("{\"t\":\"test\",\"suite\":\"%s\",\"name\":\"pattern_%s\",\"pass\":%s,\"bytes\":65536,\"mismatch\":%" PRIu32 "}",
                   sname, pn[p], pass ? "true" : "false", bad);
        line(pass, pn[p], "64K program+verify");
        all &= pass;
        if (abort_requested()) return SUITE_ABORT;
    }

    /* 4. final erase of everything touched, verify blank */
    bool fin = true;
    for (uint32_t a = 0xF00000u; a <= 0xF40000u; a += 0x10000u) {
        uint32_t us;
        fin &= (w25q_erase_cmd(0xD8, a, &us) == W25Q_OK) && verify_block(-1, a, 65536) == 0u;
        sys_service();
    }
    res_printf("{\"t\":\"test\",\"suite\":\"%s\",\"name\":\"final_erase\",\"pass\":%s}", sname, fin ? "true" : "false");
    line(fin, "final erase", "0xF00000-0xF4FFFF blank");
    all &= fin;
    return all ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ w25q.sector */

suite_verdict_t suite_w25q_sector(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static const uint32_t secs[3] = {0xF00000u, 0xF80000u, 0xFDF000u};
    uint8_t *bk = g_arena + 16384, *rd = g_arena + 20480, *pg = g_arena;
    bool all = true;
    for (int i = 0; i < 3; i++) {
        uint32_t a = secs[i];
        bool ok = scr_ok(a, 4096);
        uint32_t us = 0, bad = 0;
        if (ok) w25q_read(a, bk, 4096);
        ok = ok && (w25q_erase_cmd(0x20, a, &us) == W25Q_OK);
        bool blank = ok && verify_block(-1, a, 4096) == 0u;
        bool prog = blank;
        for (uint32_t o = 0; prog && o < 4096u; o += 256u) {
            gen_page(2, a + o, pg);
            prog = (w25q_program_page_timed(a + o, pg, 256, NULL) == W25Q_OK);
        }
        bool pat = prog && verify_block(2, a, 4096) == 0u;
        bool er2 = pat && (w25q_erase_cmd(0x20, a, NULL) == W25Q_OK);
        bool restored = false;
        if (er2) {
            /* restore the original content (skip blank pages) */
            restored = true;
            for (uint32_t o = 0; restored && o < 4096u; o += 256u) {
                bool blankpg = true;
                for (uint32_t k = 0; k < 256u; k++) if (bk[o + k] != 0xFF) { blankpg = false; break; }
                if (!blankpg) restored = (w25q_program_page_timed(a + o, &bk[o], 256, NULL) == W25Q_OK);
            }
            w25q_read(a, rd, 4096);
            for (uint32_t k = 0; k < 4096u; k++) if (rd[k] != bk[k]) bad++;
            restored = restored && bad == 0u;
        }
        bool pass = ok && blank && prog && pat && er2 && restored;
        res_printf("{\"t\":\"test\",\"suite\":\"w25q.sector\",\"name\":\"sector_%06" PRIX32 "\",\"pass\":%s,\"erase_ms\":%" PRIu32
                   ",\"blank\":%s,\"pattern\":%s,\"restored\":%s,\"restore_mismatch\":%" PRIu32 "}",
                   a, pass ? "true" : "false", us / 1000u, blank ? "true" : "false", pat ? "true" : "false",
                   restored ? "true" : "false", bad);
        char d[24];
        snprintf(d, sizeof(d), "%06" PRIX32, a);
        line(pass, "sector", d);
        all &= pass;
        sys_service();
        if (abort_requested()) return SUITE_ABORT;
    }
    return all ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ w25q.pd */

suite_verdict_t suite_w25q_pd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bool all = true;
    uint32_t down_ok_count = 0, rel_min = 0xFFFFFFFFu, rel_max = 0, first_try = 0, imm_valid = 0;
    uint32_t down_jedec = 0, down_sr = 0, ab_id = 0;
    const uint32_t cpu_us = SystemCoreClock / 1000000u;
    bool awake_end = true;
    for (int i = 0; i < 10; i++) {
        w25q_power_down();
        if (i & 1) delay_us(5); /* odd rounds: > tDP (3 us); even rounds: no gap */
        uint32_t j = w25q_read_jedec() & 0xFFFFFFu;
        if (i == 1) {
            down_jedec = j;
            down_sr = w25q_read_reg(0x05);
        }
        if (j == 0xEF4018u) {
            if (i & 1) {
                all = false; /* must NOT answer after tDP */
            } else {
                imm_valid++; /* readable inside tDP: command still being processed */
            }
        } else {
            down_ok_count++;
        }
        delay_us(10);
        uint32_t t0 = DWT->CYCCNT;
        w25q_release_pd_nodelay();
        uint32_t tries = 0, us = 0;
        for (;;) {
            tries++;
            uint32_t jj = w25q_read_jedec() & 0xFFFFFFu;
            if (jj == 0xEF4018u) {
                us = (DWT->CYCCNT - t0) / cpu_us;
                break;
            }
            if (tries > 200u) {
                awake_end = false;
                us = 0xFFFFu;
                break;
            }
        }
        if (tries == 1u) first_try++;
        if (us < rel_min) rel_min = us;
        if (us > rel_max) rel_max = us;
        sys_service();
    }
    /* 0xAB with ID read-out */
    w25q_power_down();
    delay_us(10);
    ab_id = w25q_release_pd_with_id();
    delay_us(10);
    bool awake = (w25q_read_jedec() & 0xFFFFFFu) == 0xEF4018u && awake_end;
    bool idp = (ab_id == 0x17u);
    all &= awake && idp;
    res_printf("{\"t\":\"test\",\"suite\":\"w25q.pd\",\"name\":\"power_down\",\"pass\":%s,\"jedec_while_down\":\"%06" PRIX32
               "\",\"sr1_while_down\":\"%02" PRIX32 "\",\"readable_inside_tdp\":%" PRIu32 ",\"ignored_after_tdp\":%" PRIu32
               ",\"release_first_jedec_us_min\":%" PRIu32 ",\"release_first_jedec_us_max\":%" PRIu32
               ",\"first_try_ok\":%" PRIu32 ",\"ab_device_id\":\"%02" PRIX32 "\",\"awake_at_end\":%s}",
               all ? "true" : "false", down_jedec, down_sr, imm_valid, down_ok_count - 0u, rel_min, rel_max, first_try,
               ab_id, awake ? "true" : "false");
    ui_con_line(all ? COLOR_GREEN : COLOR_RED, "release->JEDEC %" PRIu32 "-%" PRIu32 " us, ABid %02" PRIX32, rel_min,
                rel_max, ab_id);
    w25q_release_pd_nodelay(); /* make sure it is awake whatever happened */
    return all ? SUITE_PASS : SUITE_FAIL;
}
