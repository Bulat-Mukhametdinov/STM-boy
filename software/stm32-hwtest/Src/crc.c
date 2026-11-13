#include "crc.h"
#include "timebase.h"
#include "stm32f4xx.h"

extern const uint32_t _flash_crc_end; /* linker: address of the trailer word */

uint32_t crc_hw_words(const uint32_t *words, size_t nwords)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    (void)RCC->AHB1ENR;
    CRC->CR = CRC_CR_RESET;
    for (size_t i = 0; i < nwords; i++) {
        CRC->DR = words[i];
    }
    return CRC->DR;
}

void fw_crc_check(fw_crc_result_t *r)
{
    const uint32_t *start = (const uint32_t *)FLASH_BASE;
    const volatile uint32_t *trailer = (const volatile uint32_t *)(uintptr_t)&_flash_crc_end;
    size_t nwords = ((uintptr_t)trailer - (uintptr_t)FLASH_BASE) / 4u;
    uint32_t t0 = time_us();

    r->calc = crc_hw_words(start, nwords);
    r->us = time_us() - t0;
    r->stored = *trailer;
    r->len = (uint32_t)(nwords * 4u);
    r->pass = (r->calc == r->stored);
}

uint32_t crc32_zlib(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
