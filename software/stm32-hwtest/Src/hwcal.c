/*
 * HWCAL storage.
 *
 * Layout: one 4 KB W25Q sector at 0xFFF000, halves A (0xFFF000) and
 * B (0xFFF800). A record = hwcal_hdr_t + hwcal_t; the rest of the half stays
 * 0xFF.
 *
 * Write scheme ("ping-pong, erase only when needed"):
 *   1. The new record (seq = current + 1) goes into the half that does NOT
 *      hold the current valid record.
 *   2. If that half is blank (all 0xFF) it is programmed directly: the current
 *      record stays intact the whole time. The header magic is programmed
 *      LAST, so an interrupted program leaves a half without magic (ignored).
 *      => fully power-loss safe; this is the case for every other save.
 *   3. If that half is not blank, the whole sector must be erased (a 4 KB
 *      sector is the smallest erase unit, so both halves die). The current
 *      record is already in RAM (g_hwcal); we erase, then program the NEW
 *      record into half A (magic last). Half B stays blank, so the next save
 *      is again a direct program (step 2).
 *      Power-loss window: from the erase until the new record's magic is
 *      written (~50 ms typ.). A loss in that window leaves an empty sector and
 *      the firmware falls back to defaults ("hwcal":false in info). Accepted
 *      for P0; a dedicated backup sector could close this window later.
 *   After programming, the half is read back and validated.
 *
 * First write ever: if no valid record exists and the sector contains
 * anything but 0xFF (e.g. ROM-pack data of an older stm32-retroport layout),
 * writes are refused unless `force` is set (host: `hwcal init force`; the UI
 * will ask for an A+B confirmation later).
 */

#include "hwcal.h"
#include "crc.h"
#include "w25q.h"

#include <stddef.h>
#include <string.h>

hwcal_t g_hwcal;

static hwcal_state_t s_state = HWCAL_NO_FLASH;
static uint32_t s_seq;
static int s_slot = -1; /* 0 = A, 1 = B */

static uint32_t half_addr(int slot)
{
    return HWCAL_SECTOR + (uint32_t)slot * HWCAL_HALF_SIZE;
}

void hwcal_defaults(hwcal_t *c)
{
    static const uint16_t ocv_default[21] = {
        /* generic 1S Li-ion curve, replaced by the measured one in P2b/P6 */
        3300, 3500, 3600, 3650, 3690, 3710, 3730, 3750, 3770, 3790, 3810,
        3840, 3870, 3910, 3950, 3980, 4020, 4060, 4100, 4150, 4200,
    };
    memset(c, 0, sizeof(*c));
    c->madctl_land = 0xA0;   /* MY|MV, RGB (stm32-bringup) */
    c->xoff_land = 3;
    c->yoff_land = 1;
    c->madctl_port = 0x00;   /* portrait defaults are unverified until P1 */
    c->xoff_port = 2;
    c->yoff_port = 1;
    c->bgr = 0;
    c->invert = 0;
    c->gamma_id = 0;
    c->spi1_div_ok = 8;
    c->spi1_div_max = 8;
    c->audio_cfg_id = 0;
    c->debounce_ms = 20;
    c->bl_pwm_hz = 1000;
    c->bl_min_level = 0;
    c->amp_ramp_ms = 20;
    c->rint_mohm = 150;
    c->vbat_gain_q16 = 65536u;
    c->vbat_offset_mv = 0;
    c->i_chg_ma = 0;
    memcpy(c->ocv_mv, ocv_default, sizeof(c->ocv_mv));
    c->gauge_level = 0;
    c->gauge_mode = 0;
    c->gauge_seq = 0;
}

static uint32_t record_crc(const hwcal_hdr_t *h, const void *payload, size_t len)
{
    uint32_t crc = crc32_zlib(0, &h->version, offsetof(hwcal_hdr_t, crc32) - offsetof(hwcal_hdr_t, version));
    return crc32_zlib(crc, payload, len);
}

/* Reads and validates one half. Returns true and fills out/seq if valid. */
static bool read_half(int slot, hwcal_t *out, uint32_t *seq)
{
    hwcal_hdr_t h;
    hwcal_t tmp;

    if (w25q_read(half_addr(slot), &h, sizeof(h)) != W25Q_OK) {
        return false;
    }
    if (h.magic != HWCAL_MAGIC || h.version != HWCAL_VERSION || h.len == 0u ||
        h.len > sizeof(hwcal_t) || h.seq == 0xFFFFFFFFu) {
        return false;
    }
    /* Older/shorter payloads: missing tail fields keep their defaults. */
    hwcal_defaults(&tmp);
    if (w25q_read(half_addr(slot) + sizeof(h), &tmp, h.len) != W25Q_OK) {
        return false;
    }
    if (record_crc(&h, &tmp, h.len) != h.crc32) {
        return false;
    }
    *out = tmp;
    *seq = h.seq;
    return true;
}

static bool range_blank(uint32_t addr, uint32_t len)
{
    uint8_t buf[128];
    while (len) {
        uint32_t n = len < sizeof(buf) ? len : sizeof(buf);
        if (w25q_read(addr, buf, n) != W25Q_OK) {
            return false;
        }
        for (uint32_t i = 0; i < n; i++) {
            if (buf[i] != 0xFFu) {
                return false;
            }
        }
        addr += n;
        len -= n;
    }
    return true;
}

hwcal_state_t hwcal_load(void)
{
    hwcal_t a, b;
    uint32_t sa = 0, sb = 0;
    bool va, vb;

    hwcal_defaults(&g_hwcal);
    s_seq = 0;
    s_slot = -1;

    if (!w25q_present()) {
        s_state = HWCAL_NO_FLASH;
        return s_state;
    }
    va = read_half(0, &a, &sa);
    vb = read_half(1, &b, &sb);
    if (va && (!vb || sa >= sb)) {
        g_hwcal = a;
        s_seq = sa;
        s_slot = 0;
        s_state = HWCAL_VALID;
    } else if (vb) {
        g_hwcal = b;
        s_seq = sb;
        s_slot = 1;
        s_state = HWCAL_VALID;
    } else {
        s_state = range_blank(HWCAL_SECTOR, 2u * HWCAL_HALF_SIZE) ? HWCAL_EMPTY : HWCAL_FOREIGN;
    }
    return s_state;
}

hwcal_state_t hwcal_state(void) { return s_state; }
uint32_t hwcal_seq(void) { return s_seq; }
char hwcal_slot(void) { return (s_slot < 0) ? '-' : (char)('A' + s_slot); }

static hwcal_err_t program_record(int slot, const hwcal_t *c, uint32_t seq)
{
    hwcal_hdr_t h;
    h.magic = 0xFFFFFFFFu; /* programmed last */
    h.version = HWCAL_VERSION;
    h.len = (uint16_t)sizeof(hwcal_t);
    h.seq = seq;
    h.crc32 = record_crc(&h, c, sizeof(hwcal_t));

    uint32_t base = half_addr(slot);
    if (w25q_program(base + 4u, (const uint8_t *)&h + 4u, sizeof(h) - 4u) != W25Q_OK ||
        w25q_program(base + sizeof(h), c, sizeof(hwcal_t)) != W25Q_OK) {
        return HWCAL_ERR_FLASH;
    }
    uint32_t magic = HWCAL_MAGIC;
    if (w25q_program(base, &magic, sizeof(magic)) != W25Q_OK) {
        return HWCAL_ERR_FLASH;
    }
    hwcal_t back;
    uint32_t back_seq;
    if (!read_half(slot, &back, &back_seq) || back_seq != seq ||
        memcmp(&back, c, sizeof(hwcal_t)) != 0) {
        return HWCAL_ERR_FLASH;
    }
    return HWCAL_OK;
}

hwcal_err_t hwcal_save(const hwcal_t *c, bool force)
{
    if (!w25q_present()) {
        return HWCAL_ERR_NO_FLASH;
    }
    hwcal_t copy = *c; /* c may alias g_hwcal */
    int target;

    if (s_slot < 0) {
        /* No valid record: first write ever (or after a lost erase window). */
        bool blank = range_blank(HWCAL_SECTOR, 2u * HWCAL_HALF_SIZE);
        if (!blank) {
            if (!force) {
                return HWCAL_ERR_FOREIGN;
            }
            if (w25q_erase_sector(HWCAL_SECTOR) != W25Q_OK) {
                return HWCAL_ERR_FLASH;
            }
        }
        target = 0;
    } else {
        target = 1 - s_slot;
        if (!range_blank(half_addr(target), HWCAL_HALF_SIZE)) {
            /* Both halves used: erase (window documented above), new -> A. */
            if (w25q_erase_sector(HWCAL_SECTOR) != W25Q_OK) {
                return HWCAL_ERR_FLASH;
            }
            target = 0;
        }
    }

    uint32_t seq = s_seq + 1u;
    hwcal_err_t e = program_record(target, &copy, seq);
    if (e != HWCAL_OK) {
        hwcal_load(); /* resync with whatever is in flash now */
        return e;
    }
    g_hwcal = copy;
    s_seq = seq;
    s_slot = target;
    s_state = HWCAL_VALID;
    return HWCAL_OK;
}

hwcal_err_t hwcal_init(bool force)
{
    hwcal_t d;
    if (s_state == HWCAL_VALID && !force) {
        return HWCAL_ERR_EXISTS;
    }
    hwcal_defaults(&d);
    return hwcal_save(&d, force);
}

const char *hwcal_state_str(hwcal_state_t s)
{
    switch (s) {
    case HWCAL_VALID: return "valid";
    case HWCAL_EMPTY: return "empty";
    case HWCAL_FOREIGN: return "foreign";
    case HWCAL_NO_FLASH: return "no_flash";
    default: return "?";
    }
}

const char *hwcal_err_str(hwcal_err_t e)
{
    switch (e) {
    case HWCAL_OK: return "ok";
    case HWCAL_ERR_FOREIGN: return "foreign data in HWCAL sector; use 'hwcal init force'";
    case HWCAL_ERR_EXISTS: return "valid record exists; use 'hwcal init force' to reset to defaults";
    case HWCAL_ERR_FLASH: return "flash write/verify failed";
    case HWCAL_ERR_NO_FLASH: return "W25Q not detected";
    default: return "?";
    }
}

void hwcal_to_json(jb_t *j)
{
    const hwcal_t *c = &g_hwcal;
    char slot[2] = {hwcal_slot(), '\0'};

    jb_str(j, "status", hwcal_state_str(s_state));
    jb_u32(j, "seq", s_seq);
    jb_str(j, "slot", slot);
    jb_u32(j, "version", HWCAL_VERSION);
    jb_u32(j, "madctl_land", c->madctl_land);
    jb_u32(j, "xoff_land", c->xoff_land);
    jb_u32(j, "yoff_land", c->yoff_land);
    jb_u32(j, "madctl_port", c->madctl_port);
    jb_u32(j, "xoff_port", c->xoff_port);
    jb_u32(j, "yoff_port", c->yoff_port);
    jb_u32(j, "bgr", c->bgr);
    jb_u32(j, "invert", c->invert);
    jb_u32(j, "gamma_id", c->gamma_id);
    jb_u32(j, "spi1_div_ok", c->spi1_div_ok);
    jb_u32(j, "spi1_div_max", c->spi1_div_max);
    jb_u32(j, "bl_pwm_hz", c->bl_pwm_hz);
    jb_u32(j, "bl_min_level", c->bl_min_level);
    jb_u32(j, "audio_cfg_id", c->audio_cfg_id);
    jb_u32(j, "amp_ramp_ms", c->amp_ramp_ms);
    jb_u32(j, "debounce_ms", c->debounce_ms);
    jb_u32(j, "vbat_gain_q16", c->vbat_gain_q16);
    jb_i32(j, "vbat_offset_mv", c->vbat_offset_mv);
    jb_u32(j, "rint_mohm", c->rint_mohm);
    jb_u16_array(j, "i_state_ma", c->i_state_ma, 18);
    jb_u32(j, "i_chg_ma", c->i_chg_ma);
    jb_u16_array(j, "ocv_mv", c->ocv_mv, 21);
    jb_u32(j, "gauge_level", c->gauge_level);
    jb_u32(j, "gauge_mode", c->gauge_mode);
    jb_u32(j, "gauge_seq", c->gauge_seq);
}
