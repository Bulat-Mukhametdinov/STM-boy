/* Block B: overclock / underclock sweep with journal (W25Q OC log), IWDG supervision and
   boot-time recovery (plan section E). Suites: oc.sweep, oc.ws. Command: oc log|stable|status. */

#include "oc.h"
#include "adc.h"
#include "audio.h"
#include "bench.h"
#include "clock.h"
#include "cmd.h"
#include "crc.h"
#include "gfx.h"
#include "lcd.h"
#include "log.h"
#include "noinit.h"
#include "suites.h"
#include "timebase.h"
#include "ui.h"
#include "usb.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ journal */

#define OC_MAGIC 0x4F43u
#define OC_REC   32u
#define OC_SLOTS ((W25Q_OCLOG_END - W25Q_OCLOG_BASE) / OC_REC)

enum { OC_T_ATTEMPT = 1, OC_T_RESULT = 2, OC_T_NOTE = 3 };
enum {
    R_NONE = 0, R_IWDG, R_SFT, R_BOR, R_PIN, R_POR, R_UNKNOWN, R_FAULT,
    R_PLL, R_SWITCH, R_TEMP, R_CPU, R_FLASH, R_RAM, R_W25Q, R_LCD, R_AUDIO, R_ADC, R_TIMEBASE, R_ABORT
};
static const char *const k_reason[] = {"none", "iwdg", "sft", "bor", "pin", "por", "unknown", "fault",
                                       "pll_lock", "switch", "temp", "cpu", "flash_crc", "ram", "w25q",
                                       "lcd", "audio", "adc", "timebase", "abort"};
#define CHK_CPU 0x01u
#define CHK_FLASH 0x02u
#define CHK_RAM 0x04u
#define CHK_W25Q 0x08u
#define CHK_LCD 0x10u
#define CHK_AUDIO 0x20u
#define CHK_ADC 0x40u
#define CHK_USB 0x80u     /* informational: USB enumerated at this frequency */
#define CHK_REQUIRED 0x7Fu

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t type, verdict;
    uint32_t seq;
    uint16_t mhz;
    uint8_t ws, apb1, apb2, step, reason, checks;
    uint16_t cpu_mops;
    uint16_t lcd_fps10;
    int16_t aud_ppm;
    int16_t temp_dC;
    uint32_t x;            /* PASS: measured audio Fs (Hz); FAIL: fault PC */
    uint32_t crc;
} oc_rec_t;
_Static_assert(sizeof(oc_rec_t) == OC_REC, "oc record size");

static uint32_t s_next = 0xFFFFFFFFu;   /* next free slot, lazily scanned */
static uint32_t s_seq;
static uint16_t s_boot_fail_mhz;
static char s_boot_note[64];

static uint32_t rec_crc(const oc_rec_t *r) { return crc32_zlib(0, r, OC_REC - 4u); }

static bool rec_valid(const oc_rec_t *r)
{
    return r->magic == OC_MAGIC && r->crc == rec_crc(r);
}

static bool slot_blank(const uint8_t *b)
{
    for (uint32_t i = 0; i < OC_REC; i++) {
        if (b[i] != 0xFFu) return false;
    }
    return true;
}

static void rec_read(uint32_t slot, oc_rec_t *r)
{
    (void)w25q_read(W25Q_OCLOG_BASE + slot * OC_REC, r, OC_REC);
}

static void scan(void)
{
    oc_rec_t r;
    s_seq = 0;
    uint32_t n = 0;
    while (n < OC_SLOTS) {
        rec_read(n, &r);
        if (slot_blank((const uint8_t *)&r)) break;
        if (rec_valid(&r) && r.seq > s_seq) s_seq = r.seq;
        n++;
    }
    s_next = n;
}

static void feed_hw(void);

static bool log_clear(void)
{
    { uint32_t us = 0; w25q_status_t es = w25q_erase_cmd(0xD8u, W25Q_OCLOG_BASE, &us); res_printf("{\"t\":\"oc\",\"ev\":\"block_erase\",\"st\":%d,\"us\":%" PRIu32 ",\"sr1\":\"%02X\"}", (int)es, us, w25q_read_sr1()); }
    for (uint32_t a = W25Q_OCLOG_BASE; a < W25Q_OCLOG_END; a += W25Q_SECTOR_SIZE) {
        bool blank = false;
        for (int tries = 0; tries < 3 && !blank; tries++) {
            if (w25q_erase_sector(a) != W25Q_OK) return false;
            blank = true;
            uint8_t b[64];
            for (uint32_t o = 0; o < W25Q_SECTOR_SIZE && blank; o += sizeof(b)) {
                (void)w25q_read(a + o, b, sizeof(b));
                for (uint32_t k = 0; k < sizeof(b); k++) {
                    if (b[k] != 0xFFu) { blank = false; break; }
                }
            }
            if (!blank) res_printf("{\"t\":\"oc\",\"ev\":\"erase_retry\",\"addr\":\"%06" PRIX32 "\",\"try\":%d,\"sr1\":\"%02X\",\"lock\":%u}", a, tries,
                                   w25q_read_sr1(), (unsigned)w25q_read_lock(a));
        }
        if (!blank) return false;
        feed_hw();
        sys_service();
    }
    s_next = 0;
    return true;
}

static bool log_append(oc_rec_t *r)
{
    if (!w25q_present()) return false;
    if (s_next == 0xFFFFFFFFu) scan();
    if (s_next >= OC_SLOTS) {
        if (!log_clear()) return false;
        s_seq = 0;
    }
    r->magic = OC_MAGIC;
    r->seq = ++s_seq;
    r->crc = rec_crc(r);
    uint32_t addr = W25Q_OCLOG_BASE + s_next * OC_REC;
    oc_rec_t rb;
    for (int tries = 0; tries < 2; tries++) {
        if (w25q_program(addr, r, OC_REC) == W25Q_OK && w25q_read(addr, &rb, OC_REC) == W25Q_OK &&
            memcmp(&rb, r, OC_REC) == 0) {
            s_next++;
            g_noinit.oc_ctr = r->seq;
            return true;
        }
        s_next++;                 /* a bad slot is skipped, never rewritten */
        addr += OC_REC;
        if (s_next >= OC_SLOTS) return false;
    }
    return false;
}

/* ------------------------------------------------------------------ watchdog */

static bool s_wdg_armed;
static bool s_feed_ok;

static void feed_hw(void)
{
    if (s_wdg_armed) IWDG->KR = 0xAAAAu;
}

static void wdg_arm(void)
{
    IWDG->KR = 0x5555u;
    IWDG->PR = 3u;                 /* LSI/32: ~35 kHz -> ~1.1 kHz */
    IWDG->RLR = 2200u;             /* ~2 s */
    for (uint32_t i = 0; i < 1000000u && IWDG->SR; i++) {
    }
    IWDG->KR = 0xAAAAu;
    IWDG->KR = 0xCCCCu;
    s_wdg_armed = true;
    s_feed_ok = true;
}

/* Called from sys_service(): only code that is still alive reaches this. */
void oc_wdg_service(void)
{
    if (s_wdg_armed && s_feed_ok) IWDG->KR = 0xAAAAu;
}

/* ------------------------------------------------------------------ attempt / result bookkeeping */

typedef struct {
    uint16_t mhz;
    uint8_t ws, apb1, apb2, step;
} cfg_t;

static void rec_fill(oc_rec_t *r, uint8_t type, const cfg_t *c)
{
    memset(r, 0, sizeof(*r));
    r->type = type;
    r->mhz = c->mhz;
    r->ws = c->ws;
    r->apb1 = c->apb1;
    r->apb2 = c->apb2;
    r->step = c->step;
}

static bool write_attempt(const cfg_t *c)
{
    oc_rec_t r;
    rec_fill(&r, OC_T_ATTEMPT, c);
    bool ok = log_append(&r);
    g_noinit.oc_mhz = c->mhz;
    g_noinit.oc_cfg = c->ws | ((uint32_t)c->apb1 << 8) | ((uint32_t)c->apb2 << 16) | ((uint32_t)c->step << 24);
    g_noinit.oc_faults = g_noinit.fault_count;
    g_noinit.oc_seq = r.seq;
    g_noinit.oc_state = 1;
    return ok;
}

typedef struct {
    uint8_t reason, checks;
    uint16_t cpu_mops, lcd_fps10;
    int16_t aud_ppm, temp;
    uint32_t x;
} res_t;

static bool write_result(const cfg_t *c, const res_t *s)
{
    oc_rec_t r;
    rec_fill(&r, OC_T_RESULT, c);
    r.verdict = (s->reason == R_NONE) ? 1u : 0u;
    r.reason = s->reason;
    r.checks = s->checks;
    r.cpu_mops = s->cpu_mops;
    r.lcd_fps10 = s->lcd_fps10;
    r.aud_ppm = s->aud_ppm;
    r.temp_dC = s->temp;
    r.x = s->x;
    bool ok = log_append(&r);
    g_noinit.oc_state = 0;
    return ok;
}

static void write_note(uint32_t code, uint32_t x)
{
    oc_rec_t r;
    memset(&r, 0, sizeof(r));
    r.type = OC_T_NOTE;
    r.reason = (uint8_t)code;
    r.x = x;
    (void)log_append(&r);
}

/* ------------------------------------------------------------------ boot recovery */

void oc_boot_check(void)
{
    if (!w25q_present()) return;
    scan();
    oc_rec_t last, att;
    bool pending = false;
    memset(&att, 0, sizeof(att));
    if (s_next > 0) {
        rec_read(s_next - 1u, &last);
        if (rec_valid(&last) && last.type == OC_T_ATTEMPT) {
            pending = true;
            att = last;
        }
    }
    if (!pending && (g_noinit.oc_state & 0xFFu) == 1u) {      /* ATTEMPT record lost but .noinit has the copy */
        pending = true;
        att.mhz = (uint16_t)g_noinit.oc_mhz;
        att.ws = (uint8_t)g_noinit.oc_cfg;
        att.apb1 = (uint8_t)(g_noinit.oc_cfg >> 8);
        att.apb2 = (uint8_t)(g_noinit.oc_cfg >> 16);
        att.step = (uint8_t)(g_noinit.oc_cfg >> 24);
    }
    if (!pending) return;

    const char *rc = cmd_reset_cause();
    res_t s = {0};
    s.reason = !strcmp(rc, "iwdg") ? R_IWDG : !strcmp(rc, "sft") ? R_SFT : !strcmp(rc, "bor") ? R_BOR
             : !strcmp(rc, "pin") ? R_PIN : !strcmp(rc, "por") ? R_POR : R_UNKNOWN;
    s.cpu_mops = (uint16_t)(g_noinit.oc_state >> 8);   /* stage = check bit in progress */
    if ((g_noinit.oc_state & 0xFFu) == 1u && g_noinit.fault_count != g_noinit.oc_faults) {
        s.reason = R_FAULT;
        s.x = g_noinit.fault_pc;
    }
    cfg_t c = {att.mhz, att.ws, att.apb1, att.apb2, att.step};
    (void)write_result(&c, &s);
    s_boot_fail_mhz = att.mhz;
    snprintf(s_boot_note, sizeof(s_boot_note), "oc attempt %u MHz ws%u FAILED: %s", att.mhz, att.ws, k_reason[s.reason]);
}

/* ------------------------------------------------------------------ log dump / stable list */

static void emit_rec(uint32_t i, const oc_rec_t *r)
{
    res_printf("{\"t\":\"oc\",\"i\":%" PRIu32 ",\"seq\":%" PRIu32 ",\"ev\":\"%s\",\"mhz\":%u,\"ws\":%u,\"apb1\":%u,"
               "\"apb2\":%u,\"step\":%u,\"pass\":%s,\"reason\":\"%s\",\"checks\":\"%02X\",\"cpu_mops\":%u,"
               "\"lcd_fps10\":%u,\"aud_ppm\":%d,\"temp_dC\":%d,\"x\":%" PRIu32 "}",
               i, r->seq, r->type == OC_T_ATTEMPT ? "attempt" : r->type == OC_T_RESULT ? "result" : "note", r->mhz,
               r->ws, r->apb1, r->apb2, r->step, r->verdict ? "true" : "false", r->type == OC_T_NOTE ? "sweep_end" :
               r->reason < sizeof(k_reason) / sizeof(k_reason[0]) ? k_reason[r->reason] : "?", r->checks,
               r->cpu_mops, r->lcd_fps10, r->aud_ppm, r->temp_dC, r->x);
}

uint32_t oc_stable_list(uint16_t *out, uint32_t max)
{
    if (!w25q_present()) return 0;
    if (s_next == 0xFFFFFFFFu) scan();
    uint16_t f[64];
    uint8_t ok[64];
    uint32_t n = 0;
    oc_rec_t r;
    for (uint32_t i = 0; i < s_next; i++) {
        rec_read(i, &r);
        if (!rec_valid(&r) || r.type != OC_T_RESULT || (r.step & 0x80u)) continue;
        uint32_t k = 0;
        while (k < n && f[k] != r.mhz) k++;
        if (k == n) {
            if (n >= 64u) continue;
            f[n++] = r.mhz;
        }
        ok[k] = r.verdict ? 1u : 0u;
    }
    uint32_t m = 0;
    for (uint32_t k = 0; k < n; k++) {
        if (ok[k] && m < max) out[m++] = f[k];
    }
    for (uint32_t a = 1; a < m; a++) {      /* insertion sort */
        uint16_t v = out[a];
        uint32_t b = a;
        while (b > 0 && out[b - 1u] > v) { out[b] = out[b - 1u]; b--; }
        out[b] = v;
    }
    return m;
}

void oc_cmd(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "log") == 0) {
        if (argc >= 3 && strcmp(argv[2], "clear") == 0) {
            bool ok = w25q_present() && log_clear();
            res_printf("{\"t\":\"oc\",\"ev\":\"clear\",\"ok\":%s}", ok ? "true" : "false");
            ok ? proto_ok("oc log clear") : proto_err("oc", "clear failed");
            return;
        }
        if (s_next == 0xFFFFFFFFu) scan();
        oc_rec_t r;
        uint32_t cnt = 0;
        for (uint32_t i = 0; i < s_next; i++) {
            rec_read(i, &r);
            if (log_pending() > 5000u) log_flush(200);
            if (!rec_valid(&r)) {
                const uint8_t *b = (const uint8_t *)&r;
                res_printf("{\"t\":\"oc\",\"i\":%" PRIu32 ",\"ev\":\"bad\",\"hex\":\"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\","
                           "\"crc_calc\":\"%08" PRIX32 "\",\"crc_rec\":\"%08" PRIX32 "\"}",
                           i, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], rec_crc(&r), r.crc);
                continue;
            }
            emit_rec(i, &r);
            cnt++;
        }
        res_printf("{\"t\":\"oc\",\"ev\":\"log_end\",\"records\":%" PRIu32 ",\"slots\":%" PRIu32 ",\"boot_fail\":\"%s\"}",
                   cnt, (uint32_t)OC_SLOTS, s_boot_note);
        proto_ok("oc log");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "stable") == 0) {
        uint16_t l[32];
        uint32_t n = oc_stable_list(l, 32);
        char b[200];
        int o = 0;
        for (uint32_t i = 0; i < n; i++) o += snprintf(b + o, sizeof(b) - (size_t)o, "%s%u", i ? "," : "", l[i]);
        res_printf("{\"t\":\"oc\",\"ev\":\"stable\",\"mhz\":[%s]}", o ? b : "");
        proto_ok("oc stable");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "status") == 0) {
        res_printf("{\"t\":\"oc\",\"ev\":\"status\",\"slots_used\":%" PRIu32 ",\"slots\":%" PRIu32 ",\"seq\":%" PRIu32
                   ",\"pending\":%" PRIu32 ",\"boot_fail\":\"%s\"}",
                   s_next == 0xFFFFFFFFu ? 0u : s_next, (uint32_t)OC_SLOTS, s_seq, g_noinit.oc_state, s_boot_note);
        proto_ok("oc status");
        return;
    }
    proto_err("oc", "usage: oc log [clear] | oc stable | oc status");
}

/* ------------------------------------------------------------------ checks */

#define OPS_INT 8u
#define OPS_FLT 8u

static __attribute__((noinline)) uint32_t k_int(uint32_t n)
{
    static volatile uint32_t seed = 0x12345678u;   /* volatile: the call must not be hoisted or elided */
    uint32_t x = seed, y = 1u;
    for (uint32_t i = 0; i < n; i++) {
        x = x * 1664525u + 1013904223u;
        y ^= x >> 7;
        y = y * 3u + (x & 0xFFu);
    }
    return y ^ x;
}

static __attribute__((noinline)) uint32_t k_flt(uint32_t n)
{
    volatile float seed = 0.5f;
    float a = 1.0f, b = seed, c = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        a = a * 0.9999f + 0.0001f * b;
        b = b * 1.0001f - 0.00005f * a;
        c += a * b;
    }
    union { float f; uint32_t u; } cv = {c};
    return cv.u;
}

#define CPY_BYTES 8192u
static __attribute__((noinline)) uint32_t k_cpy(uint32_t n)
{
    uint8_t *src = g_arena + 16384u, *dst = g_arena + 24576u;
    uint32_t x = 0xC0FFEEu;
    for (uint32_t i = 0; i < CPY_BYTES; i += 4u) {
        x = x * 1664525u + 1013904223u;
        memcpy(src + i, &x, 4);
    }
    for (uint32_t i = 0; i < n; i++) {
        memset(dst, (int)i, CPY_BYTES);
        memcpy(dst, src, CPY_BYTES);
    }
    return memcmp(dst, src, CPY_BYTES) == 0 ? crc32_zlib(0, dst, CPY_BYTES) : 0xBAD0BAD0u;
}

typedef struct {
    uint32_t chk[3];
    uint32_t cyc[3];
    uint32_t w25q_crc;
    bool have;
} ref_t;
static ref_t s_ref;
static bool s_ratio_chk = true;   /* cycles must stay within 150 % of the 96 MHz reference (off for cache-off runs) */

typedef struct { uint32_t chk, cyc; } km_t;

static km_t meas(uint32_t (*fn)(uint32_t), uint32_t n)
{
    km_t m = {0, 0xFFFFFFFFu};
    for (int i = 0; i < 5; i++) {
        uint32_t t0 = DWT->CYCCNT;
        uint32_t v = fn(n);
        uint32_t c = DWT->CYCCNT - t0;
        if (i && v != m.chk) m.chk = 0xDEADDEADu;   /* inconsistent between repetitions */
        else m.chk = v;
        if (c < m.cyc) m.cyc = c;
        sys_service();
    }
    return m;
}

static bool chk_cpu(uint32_t soak_ms, uint16_t *mops, uint32_t *ratio_pct)
{
    km_t k[3] = {meas(k_int, 20000u), meas(k_flt, 20000u), meas(k_cpy, 8u)};
    uint32_t mhz = SystemCoreClock / 1000000u;
    *mops = (uint16_t)((uint64_t)20000u * OPS_INT * mhz / k[0].cyc);
    (void)OPS_FLT;
    if (!s_ref.have) {
        for (int i = 0; i < 3; i++) { s_ref.chk[i] = k[i].chk; s_ref.cyc[i] = k[i].cyc; }
        s_ref.have = true;
    }
    bool ok = true;
    uint32_t worst = 0;
    for (int i = 0; i < 3; i++) {
        if (k[i].chk != s_ref.chk[i]) ok = false;
        uint32_t pct = (uint32_t)((uint64_t)k[i].cyc * 100u / s_ref.cyc[i]);
        if (pct > worst) worst = pct;
    }
    *ratio_pct = worst;
    uint32_t t0 = HAL_GetTick();
    while (ok && (HAL_GetTick() - t0) < soak_ms) {   /* soak: same kernels, answers must stay identical */
        if (k_int(20000u) != s_ref.chk[0] || k_flt(20000u) != s_ref.chk[1] || k_cpy(2u) != s_ref.chk[2]) ok = false;
        sys_service();
        if (abort_requested()) break;
    }
    if (s_ratio_chk && worst > 150u) ok = false;
    return ok;
}

static bool chk_flash(void)
{
    for (int i = 0; i < 20; i++) {
        fw_crc_result_t r;
        fw_crc_check(&r);
        if (!r.pass) return false;
        if ((i & 7) == 7) sys_service();
    }
    return true;
}

static __attribute__((noinline)) uint32_t march_blk(volatile uint32_t *p, uint32_t n, uint32_t a)
{
    const uint32_t b = ~a;
    uint32_t e = 0;
    for (uint32_t i = 0; i < n; i++) p[i] = a;
    for (uint32_t i = 0; i < n; i++) { if (p[i] != a) e++; p[i] = b; }
    for (uint32_t i = 0; i < n; i++) { if (p[i] != b) e++; p[i] = a; }
    for (uint32_t i = n; i-- > 0;) { if (p[i] != a) e++; p[i] = b; }
    for (uint32_t i = n; i-- > 0;) { if (p[i] != b) e++; p[i] = a; }
    for (uint32_t i = n; i-- > 0;) { if (p[i] != a) e++; }
    return e;
}

static bool chk_ram(void)
{
    volatile uint32_t *p = (volatile uint32_t *)(void *)(g_arena + 40960u);
    uint32_t e = march_blk(p, 4096u, 0x00000000u) + march_blk(p, 4096u, 0x55555555u) +
                 march_blk(p, 4096u, 0xFFFFFFFFu);
    return e == 0u;
}

static bool w25q_crc1m(uint32_t *crc)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    (void)RCC->AHB1ENR;
    CRC->CR = CRC_CR_RESET;
    for (uint32_t a = 0; a < 1024u * 1024u; a += 16384u) {
        if (w25q_read(a, g_arena, 16384u) != W25Q_OK) return false;
        const uint32_t *w = (const uint32_t *)(void *)g_arena;
        for (uint32_t k = 0; k < 4096u; k++) CRC->DR = w[k];
        feed_hw();
        if ((a & 0x3FFFFu) == 0u) sys_service();
    }
    *crc = CRC->DR;
    return true;
}

static bool chk_w25q(void)
{
    uint32_t c;
    if (!w25q_present()) return true;     /* nothing to check (journal cannot work either) */
    if (!w25q_crc1m(&c)) return false;
    if (!s_ref.w25q_crc) { s_ref.w25q_crc = c ? c : 1u; return true; }
    return c == s_ref.w25q_crc;
}

static bool chk_lcd(uint32_t ms, uint16_t *fps10)
{
    char a0[] = "demo", a1[] = "auto=1", a2[16], a3[] = "scene=1";
    snprintf(a2, sizeof(a2), "t=%u", (unsigned)((ms + 999u) / 1000u));
    char *av[4] = {a0, a1, a2, a3};
    g_demo_frames = 0;
    g_demo_fps100 = 0;
    suite_verdict_t v = suite_demo(4, av);
    *fps10 = (uint16_t)(g_demo_fps100 / 10u);
    return v == SUITE_PASS && g_demo_frames > 3u;
}

static bool chk_audio(int16_t *ppm, uint32_t *fs_hz, uint32_t *drift_ppm_nominal)
{
    bool ok = false;
    audio_init();
    audio_pwm_start(3, true);                     /* AMP_SD stays low: the amplifier is never enabled */
    audio_voice_tone(0, AUD_GEN_SINE, 1000.0f, -12.0f);
    if (audio_engine_start(AUD_ENG_B, 23437u, AUD_DITHER_OFF) && !audio_amp_is_on()) {
        /* window edges aligned to block interrupts: +-1 block would be +-2 % of a 1 s window */
        uint32_t b0 = g_aud_stats.dma_ht + g_aud_stats.dma_tc, b1 = b0, t0 = time_us(), tw = t0;
        while (b1 == b0 && (uint32_t)(time_us() - tw) < 200000u) {
            sys_service();
            b1 = g_aud_stats.dma_ht + g_aud_stats.dma_tc;
        }
        t0 = g_aud_stats.last_us;
        b0 = b1;
        tw = time_us();
        while ((uint32_t)(time_us() - tw) < 1000000u) {
            sys_service();
        }
        uint32_t bx = g_aud_stats.dma_ht + g_aud_stats.dma_tc;
        uint32_t bz = bx;
        uint32_t tz = g_aud_stats.last_us;
        while (bz == bx && (uint32_t)(time_us() - tw) < 1300000u) {
            sys_service();
            bz = g_aud_stats.dma_ht + g_aud_stats.dma_tc;
            tz = g_aud_stats.last_us;
        }
        uint32_t el = tz - t0;
        uint32_t blocks = bz - b0;
        uint64_t fs_m = (uint64_t)blocks * AUD_HALF * 1000000ull / el;          /* Hz */
        uint32_t exp_mhz = audio_fs_mhz();                                       /* computed from the timers */
        int32_t p = (int32_t)(((int64_t)fs_m * 1000 - (int64_t)exp_mhz) * 1000000 / (int64_t)exp_mhz);
        *ppm = (int16_t)(p > 32000 ? 32000 : p < -32000 ? -32000 : p);
        *fs_hz = (uint32_t)fs_m;
        *drift_ppm_nominal = (uint32_t)abs((int)(((int64_t)exp_mhz - 23437500) * 1000000 / 23437500));
        ok = (abs(p) < 5000) && g_aud_stats.dma_err == 0u && blocks > 20u;
    }
    audio_engine_stop();
    audio_shutdown();
    return ok;
}

static bool chk_adc(int16_t *temp)
{
    if (!adc_measure(300u)) return false;
    const adc_result_t *r = adc_result();
    *temp = r->temp_dC;
    return r->vdda_mv >= 3000u && r->vdda_mv <= 3600u;
}

static bool chk_timebase(void)
{
    uint32_t t0 = time_us(), c0 = DWT->CYCCNT;
    while ((uint32_t)(time_us() - t0) < 50000u) {
    }
    uint32_t c = DWT->CYCCNT - c0, us = time_us() - t0;
    uint64_t exp = (uint64_t)us * (SystemCoreClock / 1000000u);
    int64_t d = (int64_t)c - (int64_t)exp;
    return d < (int64_t)(exp / 500u) && -d < (int64_t)(exp / 500u);
}

/* ------------------------------------------------------------------ one full frequency step */

typedef struct {
    bool lcd, audio;
    uint32_t soak_ms, lcd_ms;
} step_opts_t;

static uint16_t cur_apb1(void) { return (uint16_t)(HAL_RCC_GetHCLKFreq() / HAL_RCC_GetPCLK1Freq()); }
static uint16_t cur_apb2(void) { return (uint16_t)(HAL_RCC_GetHCLKFreq() / HAL_RCC_GetPCLK2Freq()); }

static bool temp_ok(int16_t *t)
{
    int16_t tt = 0;
    if (adc_measure(300u)) tt = adc_result()->temp_dC;
    *t = tt;
    return tt <= 850;
}

static uint8_t run_checks(const step_opts_t *so, res_t *s, uint32_t *ratio, uint32_t *drift)
{
    uint32_t fs = 0;
    s->checks = 0;
    s->reason = R_NONE;
#define STEP(bit, cond, rsn)                                                         \
    do {                                                                              \
        g_noinit.oc_state = 1u | ((uint32_t)(bit) << 8);                              \
        if (cond) s->checks |= (bit); else if (s->reason == R_NONE) s->reason = (rsn); \
        sys_service();                                                                \
        if (abort_requested()) { s->reason = R_ABORT; return s->reason; }            \
        if (s->reason != R_NONE) return s->reason;                                   \
    } while (0)
    STEP(CHK_CPU, chk_timebase() && chk_cpu(so->soak_ms, &s->cpu_mops, ratio), R_CPU);
    STEP(CHK_FLASH, chk_flash(), R_FLASH);
    STEP(CHK_RAM, chk_ram(), R_RAM);
    STEP(CHK_W25Q, chk_w25q(), R_W25Q);
    if (so->lcd) {
        STEP(CHK_LCD, chk_lcd(so->lcd_ms, &s->lcd_fps10), R_LCD);
    } else {
        s->checks |= CHK_LCD;
    }
    if (so->audio) {
        STEP(CHK_AUDIO, chk_audio(&s->aud_ppm, &fs, drift), R_AUDIO);
        s->x = fs;
    } else {
        s->checks |= CHK_AUDIO;
    }
    STEP(CHK_ADC, chk_adc(&s->temp), R_ADC);
#undef STEP
    if (usb_configured()) s->checks |= CHK_USB;
    if (s->temp > 850) s->reason = R_TEMP;
    return s->reason;
}

static void show(const cfg_t *c, const res_t *s, uint32_t ratio, uint32_t drift)
{
    bool pass = s->reason == R_NONE;
    res_printf("{\"t\":\"oc\",\"ev\":\"result\",\"mhz\":%u,\"ws\":%u,\"apb1\":%u,\"apb2\":%u,\"step\":%u,\"pass\":%s,"
               "\"reason\":\"%s\",\"checks\":\"%02X\",\"cpu_mops\":%u,\"cpu_cyc_pct\":%" PRIu32 ",\"lcd_fps10\":%u,"
               "\"aud_fs\":%" PRIu32 ",\"aud_ppm\":%d,\"aud_nominal_ppm\":%" PRIu32 ",\"temp_dC\":%d,\"usb\":%s,"
               "\"spi1_hz\":%" PRIu32 ",\"spi3_hz\":%" PRIu32 "}",
               c->mhz, c->ws, c->apb1, c->apb2, c->step, pass ? "true" : "false", k_reason[s->reason], s->checks,
               s->cpu_mops, ratio, s->lcd_fps10, s->x, s->aud_ppm, drift, s->temp, (s->checks & CHK_USB) ? "true" : "false",
               (uint32_t)lcd_spi_hz(), (uint32_t)w25q_spi_hz());
    ui_con_line(pass ? COLOR_GREEN : COLOR_RED, "%3uMHz ws%u %s %uMop %ufps", c->mhz, c->ws, pass ? "OK" : k_reason[s->reason],
                s->cpu_mops, s->lcd_fps10 / 10u);
}

static void back_to_96(void)
{
    clock_status_t st = clock_set_mhz(96u, NULL);
    if (st != CLOCK_OK) {
        log_printf("oc: return to 96 MHz failed (%s)\r\n", clock_status_str(st));
    }
}

static void finish(const char *name, suite_verdict_t v)
{
    back_to_96();
    write_note(0xFEu, 0u);    /* sweep end */
    log_printf("%s done (%s); resetting to drop the watchdog\r\n", name, suite_verdict_str(v));
    ui_con_line(COLOR_YELLOW, "resetting...");
    proto_done(name, suite_verdict_str(v));
    log_flush(300);
    HAL_Delay(500);
    NVIC_SystemReset();
}

/* ------------------------------------------------------------------ oc.sweep */

static uint32_t parse_list(const char *s, uint32_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (s && *s && n < max) {
        char *e;
        unsigned long v = strtoul(s, &e, 10);
        if (e == s) break;
        out[n++] = (uint32_t)v;
        s = (*e == ',') ? e + 1 : e;
    }
    return n;
}

static bool prepare(const char *name, suite_verdict_t *v)
{
    if (!w25q_present()) {
        ui_con_line(COLOR_RED, "W25Q missing: no journal");
        proto_err(name, "W25Q required for the OC journal");
        *v = SUITE_ABORT;
        return false;
    }
    back_to_96();
    int16_t t;
    if (!temp_ok(&t)) {
        ui_con_line(COLOR_RED, "too hot: %d dC", t);
        *v = SUITE_ABORT;
        return false;
    }
    return true;
}

suite_verdict_t suite_oc_sweep(int argc, char **argv)
{
    uint32_t list[40];
    uint32_t n = parse_list(suite_arg_str(argc, argv, "list", "48,64,84,96,100,120,144,168,192,216"), list, 40);
    uint32_t maxf = suite_arg_u32(argc, argv, "max", 260u);
    uint32_t step = suite_arg_u32(argc, argv, "step", 8u);
    uint32_t t = suite_arg_u32(argc, argv, "t", 10u);
    suite_verdict_t v = SUITE_PASS;
    res_printf("{\"t\":\"oc\",\"ev\":\"start\",\"suite\":\"oc.sweep\",\"n\":%" PRIu32 ",\"first\":%" PRIu32 ",\"max\":%" PRIu32
               ",\"step\":%" PRIu32 ",\"t\":%" PRIu32 ",\"argc\":%d}",
               n, n ? list[0] : 0u, maxf, step, t, argc);
    if (!prepare("oc.sweep", &v) || n == 0u || step == 0u) {
        return v == SUITE_PASS ? SUITE_ABORT : v;
    }
    step_opts_t so = {.lcd = true, .audio = true, .soak_ms = t * 150u, .lcd_ms = 2000u};
    s_ref.have = false;
    s_ref.w25q_crc = 0;

    /* reference run at 96 MHz (also warms the journal) */
    res_t rs = {0};
    uint32_t ratio = 0, drift = 0;
    wdg_arm();
    cfg_t c0 = {96, (uint8_t)clock_ws_now(), cur_apb1(), cur_apb2(), 0xFE};
    if (run_checks(&so, &rs, &ratio, &drift) != R_NONE) {
        show(&c0, &rs, ratio, drift);
        write_result(&c0, &rs);
        finish("oc.sweep", SUITE_FAIL);
    }
    show(&c0, &rs, ratio, drift);
    ui_con_line(COLOR_CYAN, "ref 96 MHz ok");

    uint32_t idx = 0, f = list[0], last = 0;
    bool first_fail = false;
    uint16_t passed = 0;
    for (;;) {
        if (idx < n) {
            f = list[idx];
        } else {
            f = (last ? last : f) + step;
            if (f > maxf) break;
        }
        idx++;
        clock_plan_t pl;
        if (!clock_plan(f, NULL, &pl)) {
            res_printf("{\"t\":\"oc\",\"ev\":\"skip\",\"mhz\":%" PRIu32 ",\"why\":\"unreachable\"}", f);
            last = f;
            continue;
        }
        int16_t tmp;
        if (!temp_ok(&tmp)) {
            res_t rt = {.reason = R_TEMP, .temp = tmp};
            cfg_t ct = {(uint16_t)f, pl.ws, pl.apb1_div, pl.apb2_div, (uint8_t)idx};
            write_result(&ct, &rt);
            show(&ct, &rt, 0, 0);
            first_fail = true;
            break;
        }
        cfg_t c = {(uint16_t)f, pl.ws, pl.apb1_div, pl.apb2_div, (uint8_t)idx};
        write_attempt(&c);
        s_feed_ok = true;
        clock_status_t st = clock_set_mhz(f, NULL);
        res_t r = {0};
        ratio = drift = 0;
        if (st != CLOCK_OK) {
            r.reason = (st == CLOCK_ERR_PLL) ? R_PLL : R_SWITCH;
            c.mhz = (uint16_t)f;
        } else {
            c.ws = (uint8_t)clock_ws_now();
            c.apb1 = cur_apb1();
            c.apb2 = cur_apb2();
            run_checks(&so, &r, &ratio, &drift);
        }
        if (r.reason == R_NONE) {
            show(&c, &r, ratio, drift);          /* USB may or may not be alive here */
            if (!write_result(&c, &r)) {
                r.reason = R_W25Q;
            }
        }
        if (r.reason != R_NONE) {
            s_feed_ok = false;                   /* nothing is fed while we recover */
            back_to_96();
            s_feed_ok = true;
            show(&c, &r, ratio, drift);
            write_result(&c, &r);
            first_fail = true;
            v = (r.reason == R_ABORT) ? SUITE_ABORT : SUITE_FAIL;
            break;
        }
        passed++;
        last = f;
        if (f > maxf) break;
    }
    (void)first_fail;
    uint16_t l[32];
    uint32_t ns = oc_stable_list(l, 32);
    res_printf("{\"t\":\"oc\",\"ev\":\"summary\",\"passed\":%u,\"max_stable\":%u}", passed, ns ? l[ns - 1u] : 0u);
    finish("oc.sweep", v);
    return v;   /* not reached */
}

/* ------------------------------------------------------------------ oc.ws */

static bool ws_known_fail(uint32_t mhz, uint32_t ws)
{
    if (s_next == 0xFFFFFFFFu) scan();
    oc_rec_t r;
    for (uint32_t i = 0; i < s_next; i++) {
        rec_read(i, &r);
        if (rec_valid(&r) && r.type == OC_T_RESULT && r.mhz == mhz && r.step == (0x80u | ws)) {
            if (!r.verdict) return true;
        }
    }
    return false;
}

suite_verdict_t suite_oc_ws(int argc, char **argv)
{
    uint32_t list[16];
    uint32_t n = parse_list(suite_arg_str(argc, argv, "list", "96,120,144"), list, 16);
    suite_verdict_t v = SUITE_PASS;
    if (!prepare("oc.ws", &v) || n == 0u) {
        return v == SUITE_PASS ? SUITE_ABORT : v;
    }
    step_opts_t so = {.lcd = false, .audio = false, .soak_ms = 300u, .lcd_ms = 0u};
    s_ref.have = false;
    s_ref.w25q_crc = 0;
    res_t rs = {0};
    uint32_t ratio = 0, drift = 0;
    wdg_arm();
    if (run_checks(&so, &rs, &ratio, &drift) != R_NONE) {      /* reference at 96 with the caches on */
        finish("oc.ws", SUITE_FAIL);
    }
    for (uint32_t i = 0; i < n && v == SUITE_PASS; i++) {
        clock_plan_t pl;
        if (!clock_plan(list[i], NULL, &pl)) continue;
        int min_ok = -1;
        for (int ws = pl.ws; ws >= 0; ws--) {
            if (ws_known_fail(list[i], (uint32_t)ws)) break;
            cfg_t c = {(uint16_t)list[i], (uint8_t)ws, pl.apb1_div, pl.apb2_div, (uint8_t)(0x80u | (uint32_t)ws)};
            write_attempt(&c);
            clock_opts_t o = {.ws = ws};
            s_feed_ok = true;
            res_t r = {0};
            clock_status_t st = clock_set_mhz(list[i], &o);
            if (st != CLOCK_OK) {
                r.reason = R_PLL;
            } else {
                /* caches off: every instruction and literal comes straight from the flash array */
                uint32_t acr = FLASH->ACR;
                FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN);
                s_ratio_chk = false;
                run_checks(&so, &r, &ratio, &drift);
                FLASH->ACR = acr;
                s_ratio_chk = true;
            }
            s_feed_ok = false;
            back_to_96();
            s_feed_ok = true;
            write_result(&c, &r);
            show(&c, &r, ratio, drift);
            if (r.reason != R_NONE) break;
            min_ok = ws;
            if (abort_requested()) { v = SUITE_ABORT; break; }
        }
        res_printf("{\"t\":\"oc\",\"ev\":\"ws\",\"mhz\":%" PRIu32 ",\"ws_default\":%u,\"ws_min_ok\":%d}", list[i], pl.ws, min_ok);
        ui_con_line(COLOR_CYAN, "%3uMHz ws min %d (def %u)", (unsigned)list[i], min_ok, pl.ws);
        if (min_ok < 0) v = SUITE_FAIL;
    }
    finish("oc.ws", v);
    return v;
}
