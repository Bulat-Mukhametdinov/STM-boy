#ifndef HWCAL_H
#define HWCAL_H

/* Hardware calibration record (HWCAL) in W25Q sector 0xFFF000.
   Two 2 KB halves: A = 0xFFF000, B = 0xFFF800. Each half holds one record:
   header {magic 'HWC1', version, len, seq, crc32} + hwcal_t payload.
   Load = the valid record with the highest seq. See hwcal.c for the write
   scheme and its power-loss behaviour. */

#include "log.h"
#include <stdbool.h>
#include <stdint.h>

#define HWCAL_MAGIC     0x31435748u  /* "HWC1" little-endian */
#define HWCAL_VERSION   1u
#define HWCAL_SECTOR    0xFFF000u
#define HWCAL_HALF_SIZE 2048u

typedef struct {
    /* display */
    uint8_t madctl_land;
    uint8_t xoff_land;
    uint8_t yoff_land;
    uint8_t madctl_port;
    uint8_t xoff_port;
    uint8_t yoff_port;
    uint8_t bgr;
    uint8_t invert;
    uint8_t gamma_id;
    uint8_t spi1_div_ok;     /* SPI1 prescaler proven reliable */
    uint8_t spi1_div_max;    /* fastest prescaler that worked at all */
    /* audio / input */
    uint8_t audio_cfg_id;
    uint8_t debounce_ms;
    /* gauge state */
    uint8_t gauge_level;
    uint8_t gauge_mode;
    uint8_t reserved0;
    uint16_t bl_pwm_hz;
    uint16_t bl_min_level;
    uint16_t amp_ramp_ms;
    uint16_t rint_mohm;
    int16_t vbat_offset_mv;
    uint16_t i_chg_ma;
    uint32_t vbat_gain_q16;  /* 65536 = 1.0 */
    uint32_t gauge_seq;
    uint16_t i_state_ma[18]; /* current per state S0..S17 */
    uint16_t ocv_mv[21];     /* OCV at 0,5,...,100 % */
    uint16_t reserved1;
} hwcal_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t len;
    uint32_t seq;
    uint32_t crc32;          /* zlib CRC over version..seq (8 bytes) + payload */
} hwcal_hdr_t;

_Static_assert(sizeof(hwcal_hdr_t) + sizeof(hwcal_t) <= HWCAL_HALF_SIZE, "HWCAL must fit a half");

typedef enum {
    HWCAL_VALID = 0,   /* a valid record was loaded */
    HWCAL_EMPTY,       /* sector erased (all 0xFF): defaults in use */
    HWCAL_FOREIGN,     /* non-0xFF data that is not HWCAL: defaults in use, writes refused w/o force */
    HWCAL_NO_FLASH,    /* W25Q not detected */
} hwcal_state_t;

typedef enum {
    HWCAL_OK = 0,
    HWCAL_ERR_FOREIGN,  /* refusing to overwrite foreign data (needs force) */
    HWCAL_ERR_EXISTS,   /* `init` on an already valid record (needs force) */
    HWCAL_ERR_FLASH,    /* erase/program/readback failure */
    HWCAL_ERR_NO_FLASH,
} hwcal_err_t;

extern hwcal_t g_hwcal;   /* current values (record or defaults) */

void hwcal_defaults(hwcal_t *c);
hwcal_state_t hwcal_load(void);
hwcal_state_t hwcal_state(void);
uint32_t hwcal_seq(void);
char hwcal_slot(void);     /* 'A', 'B' or '-' */
hwcal_err_t hwcal_save(const hwcal_t *c, bool force);
hwcal_err_t hwcal_init(bool force);  /* write defaults */
const char *hwcal_state_str(hwcal_state_t s);
const char *hwcal_err_str(hwcal_err_t e);
void hwcal_to_json(jb_t *j);         /* adds all fields + status */

#endif
