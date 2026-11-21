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
#define W25Q_MEDIA_BASE       0x200000u  /* media container region 0x200000-0xEFFFFF, writable only via the media API */
#define W25Q_MEDIA_END        0xF00000u
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
w25q_status_t w25q_media_erase_block(uint32_t addr);               /* 64 KB aligned, inside the media region */
w25q_status_t w25q_media_program(uint32_t addr, const void *buf, size_t len);
const char *w25q_status_str(w25q_status_t s);

/* Write-path health counters (R1/F4): erase retries = erase opcodes that were not executed the first time;
   prog_ignored = page programs that left WEL set (opcode ignored) and were repeated. */
typedef struct {
    uint32_t erases, erase_retries, erase_fails, programs, prog_ignored;
} w25q_stats_t;
extern w25q_stats_t g_w25q_stats;
void w25q_set_cs_hold(bool on);       /* default on: >= 200 ns CS high after every command (tSHSL) */
bool w25q_cs_hold(void);

/* P3/P5 extensions. Writes (program/erase) are refused below W25Q_HWTEST_BASE. */
uint8_t w25q_read_reg(uint8_t cmd);                 /* 0x05 SR1, 0x35 SR2, 0x15 SR3 */
uint8_t w25q_read_lock(uint32_t addr);               /* 0x3D individual lock bit */
void w25q_global_unlock(void);                       /* 0x98: volatile individual lock bits -> unlocked */
w25q_status_t w25q_read_sfdp(uint8_t *buf, size_t n);
w25q_status_t w25q_erase_cmd(uint8_t cmd, uint32_t addr, uint32_t *us); /* 0x20/0x52/0xD8 */
w25q_status_t w25q_erase_cmd_nohold(uint8_t cmd, uint32_t addr, uint32_t *us); /* A/B test: pre-fix CS timing */
w25q_status_t w25q_program_page_timed(uint32_t addr, const void *buf, size_t len, uint32_t *us);
w25q_status_t w25q_read_ex(uint32_t addr, void *buf, size_t len, bool fast); /* 0x03 / 0x0B polled */
void w25q_release_pd_nodelay(void);
uint8_t w25q_release_pd_with_id(void);
/* DMA reads (SPI3 RX DMA1 S0, TX dummy DMA1 S5). start: n <= 65535, CS stays low until finish. */
bool w25q_dma_start(uint32_t addr, void *buf, uint32_t n, bool fast);
bool w25q_dma_done(void);
bool w25q_dma_finish(void);
w25q_status_t w25q_read_dma(uint32_t addr, void *buf, size_t len, bool fast); /* any length */

#endif
