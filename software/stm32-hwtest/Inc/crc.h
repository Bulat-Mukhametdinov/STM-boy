#ifndef CRC_H
#define CRC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* STM32 hardware CRC unit (poly 0x04C11DB7, init 0xFFFFFFFF, 32-bit words,
   no reflection, no final XOR) — same algorithm as tools/append_crc.py. */
uint32_t crc_hw_words(const uint32_t *words, size_t nwords);

/* Firmware image check: CRC over [0x08000000, _flash_crc_end) vs the trailer. */
typedef struct {
    uint32_t calc;
    uint32_t stored;
    uint32_t len;     /* bytes covered */
    uint32_t us;      /* time taken */
    bool pass;
} fw_crc_result_t;

void fw_crc_check(fw_crc_result_t *r);

/* CRC-32/ISO-HDLC (zlib), software, for HWCAL headers and @B dumps. */
uint32_t crc32_zlib(uint32_t crc, const void *data, size_t len);

#endif
