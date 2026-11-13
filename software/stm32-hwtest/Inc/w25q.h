#ifndef W25Q_H
#define W25Q_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* W25Q128JV on SPI3 (PB3 SCK, PB4 MISO, PB5 MOSI: AF6; CS PA15 GPIO),
   register-level, polled, mode 0, default PCLK1/4 = 12 MHz. Port of
   stm32-retroport Core/Src/w25q128.c without HAL_SPI. No DMA yet. */

#define W25Q_SIZE        (16u * 1024u * 1024u)
#define W25Q_SECTOR_SIZE 4096u
#define W25Q_PAGE_SIZE   256u

/* External flash map (see README / docs/hardware-reference). The last 1 MB is
   reserved for stm32-hwtest; stm32-retroport's ROM pack ends at 0xF00000. */
#define W25Q_HWTEST_BASE      0xF00000u
#define W25Q_SCRATCH_BASE     0xF00000u  /* destructive tests, battery log: 0xF00000-0xFDFFFF */
#define W25Q_SCRATCH_END      0xFE0000u
#define W25Q_OCLOG_BASE       0xFE0000u  /* overclock / bus-limit journal: 0xFE0000-0xFEFFFF */
#define W25Q_OCLOG_END        0xFF0000u
#define W25Q_HWCAL_ADDR       0xFFF000u  /* one 4 KB sector, A/B halves */

typedef enum {
    W25Q_OK = 0,
    W25Q_ERR_PARAM,
    W25Q_ERR_TIMEOUT,
    W25Q_ERR_NOT_PRESENT,
} w25q_status_t;

bool w25q_init(void);                 /* GPIO + SPI3 + JEDEC check */
bool w25q_present(void);
void w25q_set_div(uint16_t div);      /* SPI3 prescaler of PCLK1 (2..256) */
uint32_t w25q_spi_hz(void);
uint32_t w25q_read_jedec(void);       /* 0xEF4018 for W25Q128JV */
w25q_status_t w25q_read_uid(uint8_t uid[8]);
uint8_t w25q_read_sr1(void);
w25q_status_t w25q_read(uint32_t addr, void *buf, size_t len);
w25q_status_t w25q_program(uint32_t addr, const void *buf, size_t len); /* any length, splits pages */
w25q_status_t w25q_erase_sector(uint32_t addr);                          /* 4 KB aligned */
w25q_status_t w25q_power_down(void);
w25q_status_t w25q_release_power_down(void);
const char *w25q_status_str(w25q_status_t s);

#endif
