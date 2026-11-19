#include "w25q.h"
#include "board.h"
#include "stm32f4xx_hal.h"

#define CMD_WRITE_ENABLE   0x06u
#define CMD_READ_SR1       0x05u
#define CMD_PAGE_PROGRAM   0x02u
#define CMD_READ_DATA      0x03u
#define CMD_SECTOR_ERASE   0x20u
#define CMD_JEDEC_ID       0x9Fu
#define CMD_UNIQUE_ID      0x4Bu
#define CMD_POWER_DOWN     0xB9u
#define CMD_RELEASE_PD     0xABu
#define SR1_BUSY           0x01u
#define SR1_WEL            0x02u

#define TIMEOUT_PAGE_MS    10u    /* tPP max 3 ms */
#define TIMEOUT_ERASE_MS   1000u  /* tSE max 400 ms */
#define SPI_SPIN_LIMIT     100000u

static bool s_present;
static uint16_t s_div = 4;

#define CS_LOW()  gpio_clr(FLASH_CS_PORT, FLASH_CS_PIN)
#define CS_HIGH() gpio_set(FLASH_CS_PORT, FLASH_CS_PIN)

/* Full-duplex byte exchange: every write is paired with a DR read so OVR
   never latches. Bounded spins so a dead bus cannot hang the firmware. */
static uint8_t xfer(uint8_t b)
{
    uint32_t n = 0;
    while (!(SPI3->SR & SPI_SR_TXE) && ++n < SPI_SPIN_LIMIT) {
    }
    *(volatile uint8_t *)&SPI3->DR = b;
    n = 0;
    while (!(SPI3->SR & SPI_SR_RXNE) && ++n < SPI_SPIN_LIMIT) {
    }
    return *(volatile uint8_t *)&SPI3->DR;
}

static void end(void)
{
    uint32_t n = 0;
    while ((SPI3->SR & SPI_SR_BSY) && ++n < SPI_SPIN_LIMIT) {
    }
    CS_HIGH();
}

static void spi3_setup(uint16_t div)
{
    uint32_t br = 0, d = 2;
    while (d < div && br < 7u) {
        d <<= 1;
        br++;
    }
    s_div = (uint16_t)d;
    RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
    (void)RCC->APB1ENR;
    SPI3->CR1 = 0;
    SPI3->CR2 = 0;
    SPI3->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (br << SPI_CR1_BR_Pos); /* mode 0 */
    SPI3->CR1 |= SPI_CR1_SPE;
    (void)SPI3->DR;
    (void)SPI3->SR;
}

void w25q_set_div(uint16_t div) { spi3_setup(div); }
uint32_t w25q_spi_hz(void) { return HAL_RCC_GetPCLK1Freq() / s_div; }

bool w25q_init(void)
{
    gpio_set(FLASH_CS_PORT, FLASH_CS_PIN);
    gpio_config(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 0);
    /* PB3/PB4 default to JTAG (JTDO/NJTRST): explicitly switch to AF6. */
    gpio_config(FLASH_SCK_PORT, FLASH_SCK_PIN, GPIO_MODE_AF_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 6);
    gpio_config(FLASH_MISO_PORT, FLASH_MISO_PIN, GPIO_MODE_AF_, GPIO_PULL_UP_, GPIO_SPEED_VHIGH_, 6);
    gpio_config(FLASH_MOSI_PORT, FLASH_MOSI_PIN, GPIO_MODE_AF_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 6);
    spi3_setup(4); /* PCLK1 48 MHz / 4 = 12 MHz */

    (void)w25q_release_power_down();
    uint32_t id = w25q_read_jedec();
    s_present = (id != 0u) && (id != 0xFFFFFFu) && ((id & 0xFFu) == 0x18u);
    return s_present;
}

bool w25q_present(void) { return s_present; }

uint32_t w25q_read_jedec(void)
{
    CS_LOW();
    xfer(CMD_JEDEC_ID);
    uint32_t id = (uint32_t)xfer(0xFF) << 16;
    id |= (uint32_t)xfer(0xFF) << 8;
    id |= xfer(0xFF);
    end();
    return id;
}

w25q_status_t w25q_read_uid(uint8_t uid[8])
{
    CS_LOW();
    xfer(CMD_UNIQUE_ID);
    for (int i = 0; i < 4; i++) {
        xfer(0xFF); /* 4 dummy bytes */
    }
    for (int i = 0; i < 8; i++) {
        uid[i] = xfer(0xFF);
    }
    end();
    return W25Q_OK;
}

uint8_t w25q_read_sr1(void)
{
    CS_LOW();
    xfer(CMD_READ_SR1);
    uint8_t sr = xfer(0xFF);
    end();
    return sr;
}

static w25q_status_t wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        if ((w25q_read_sr1() & SR1_BUSY) == 0u) {
            return W25Q_OK;
        }
        if ((uint32_t)(HAL_GetTick() - t0) > timeout_ms) {
            return W25Q_ERR_TIMEOUT;
        }
    }
}

static void write_enable(void)
{
    CS_LOW();
    xfer(CMD_WRITE_ENABLE);
    end();
}

static void send_cmd_addr(uint8_t cmd, uint32_t addr)
{
    xfer(cmd);
    xfer((uint8_t)(addr >> 16));
    xfer((uint8_t)(addr >> 8));
    xfer((uint8_t)addr);
}

static bool range_ok(uint32_t addr, size_t len)
{
    return addr < W25Q_SIZE && len <= (W25Q_SIZE - addr);
}

w25q_status_t w25q_read(uint32_t addr, void *buf, size_t len)
{
    uint8_t *p = buf;
    if (!range_ok(addr, len) || (buf == NULL && len)) {
        return W25Q_ERR_PARAM;
    }
    CS_LOW();
    send_cmd_addr(CMD_READ_DATA, addr);
    for (size_t i = 0; i < len; i++) {
        p[i] = xfer(0xFF);
    }
    end();
    return W25Q_OK;
}

/* Driver-level write guard: nothing below the hwtest area (RetroPort ROM pack,
   settings, ...) may ever be programmed or erased by this firmware. */
static bool s_media_open;   /* set only inside the media API below */

static bool write_ok(uint32_t addr, size_t len)
{
    if (s_media_open) {
        return addr >= W25Q_MEDIA_BASE && addr < W25Q_MEDIA_END && len <= (W25Q_MEDIA_END - addr);
    }
    return addr >= W25Q_HWTEST_BASE && range_ok(addr, len);
}

/* Media region 0x200000-0xEFFFFF (container for media.play, see PROTOCOL.md): writable only through these two calls.
   Everything else keeps the generic guard; the RetroPort ROM pack (0x100000-0x133670) is never reachable. */
w25q_status_t w25q_media_erase_block(uint32_t addr)   /* 64 KB aligned, BUSY-polled */
{
    s_media_open = true;
    w25q_status_t st = W25Q_ERR_TIMEOUT;
    for (int attempt = 0; attempt < 3; attempt++) {   /* F4: an erase that "finishes" in < 1 ms was not executed */
        uint32_t us = 0;
        st = w25q_erase_cmd(0xD8u, addr, &us);
        if (st != W25Q_OK || us >= 1000u) break;
        st = W25Q_ERR_TIMEOUT;
    }
    s_media_open = false;
    return st;
}

w25q_status_t w25q_media_program(uint32_t addr, const void *buf, size_t len)
{
    s_media_open = true;
    w25q_status_t st = w25q_program(addr, buf, len);
    s_media_open = false;
    return st;
}

w25q_status_t w25q_program(uint32_t addr, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    if (!write_ok(addr, len) || (buf == NULL && len)) {
        return W25Q_ERR_PARAM;
    }
    while (len) {
        uint32_t room = W25Q_PAGE_SIZE - (addr % W25Q_PAGE_SIZE);
        uint32_t chunk = (len < room) ? (uint32_t)len : room;
        w25q_status_t st = wait_ready(TIMEOUT_PAGE_MS);
        if (st != W25Q_OK) {
            return st;
        }
        write_enable();
        CS_LOW();
        send_cmd_addr(CMD_PAGE_PROGRAM, addr);
        for (uint32_t i = 0; i < chunk; i++) {
            xfer(p[i]);
        }
        end();
        st = wait_ready(TIMEOUT_PAGE_MS);
        if (st != W25Q_OK) {
            return st;
        }
        addr += chunk;
        p += chunk;
        len -= chunk;
    }
    return W25Q_OK;
}

w25q_status_t w25q_erase_sector(uint32_t addr)
{
    if ((addr % W25Q_SECTOR_SIZE) != 0u || !write_ok(addr, W25Q_SECTOR_SIZE)) {
        return W25Q_ERR_PARAM;
    }
    /* F4: the 0x20 erase was sometimes silently not executed (returned in ~11 us,
       WEL left set, data unchanged). A real 4K erase takes >= ~40 ms (typ 45, seen
       53-89 ms), so an erase that "finishes" in < 1 ms is treated as rejected and retried. */
    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t us = 0;
        w25q_status_t st = w25q_erase_cmd(CMD_SECTOR_ERASE, addr, &us);
        if (st != W25Q_OK) {
            return st;
        }
        if (us >= 1000u) {
            return W25Q_OK;
        }
    }
    return W25Q_ERR_TIMEOUT;
}

w25q_status_t w25q_power_down(void)
{
    CS_LOW();
    xfer(CMD_POWER_DOWN);
    end();
    return W25Q_OK;
}

w25q_status_t w25q_release_power_down(void)
{
    CS_LOW();
    xfer(CMD_RELEASE_PD);
    end();
    /* tRES1 = 3 us */
    for (volatile uint32_t i = 0; i < 500u; i++) {
    }
    return W25Q_OK;
}

const char *w25q_status_str(w25q_status_t s)
{
    switch (s) {
    case W25Q_OK: return "ok";
    case W25Q_ERR_PARAM: return "param";
    case W25Q_ERR_TIMEOUT: return "timeout";
    case W25Q_ERR_NOT_PRESENT: return "not_present";
    default: return "?";
    }
}

/* ================================================================ P3/P5 extensions */

uint8_t w25q_read_reg(uint8_t cmd)
{
    CS_LOW();
    xfer(cmd);
    uint8_t v = xfer(0xFF);
    end();
    return v;
}

w25q_status_t w25q_read_sfdp(uint8_t *buf, size_t n)
{
    CS_LOW();
    send_cmd_addr(0x5Au, 0);
    xfer(0xFF); /* 8 dummy clocks */
    for (size_t i = 0; i < n; i++) {
        buf[i] = xfer(0xFF);
    }
    end();
    return W25Q_OK;
}

/* Erase with 0x20 (4K) / 0x52 (32K) / 0xD8 (64K). *us = BUSY time (SR1 polled). */
w25q_status_t w25q_erase_cmd(uint8_t cmd, uint32_t addr, uint32_t *us)
{
    uint32_t size = (cmd == 0x20u) ? 4096u : (cmd == 0x52u) ? 32768u : (cmd == 0xD8u) ? 65536u : 0u;
    if (size == 0u || (addr % size) != 0u || !write_ok(addr, size)) {
        return W25Q_ERR_PARAM;
    }
    w25q_status_t st = wait_ready(TIMEOUT_ERASE_MS);
    if (st != W25Q_OK) {
        return st;
    }
    write_enable();
    CS_LOW();
    send_cmd_addr(cmd, addr);
    end();
    uint32_t t0 = DWT->CYCCNT;
    st = wait_ready(4000u);
    if (us) {
        *us = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000u);
    }
    return st;
}

/* 0x3D read block/sector lock: bit0 = locked (only honoured when SR3.WPS=1). */
uint8_t w25q_read_lock(uint32_t addr)
{
    CS_LOW();
    send_cmd_addr(0x3Du, addr);
    uint8_t v = xfer(0xFF);
    end();
    return v;
}

/* One page program (len <= 256, must not cross a page). *us = BUSY time. */
w25q_status_t w25q_program_page_timed(uint32_t addr, const void *buf, size_t len, uint32_t *us)
{
    const uint8_t *p = buf;
    if (len == 0u || len > W25Q_PAGE_SIZE || (addr % W25Q_PAGE_SIZE) + len > W25Q_PAGE_SIZE ||
        !write_ok(addr, len)) {
        return W25Q_ERR_PARAM;
    }
    w25q_status_t st = wait_ready(TIMEOUT_PAGE_MS);
    if (st != W25Q_OK) {
        return st;
    }
    write_enable();
    CS_LOW();
    send_cmd_addr(CMD_PAGE_PROGRAM, addr);
    for (size_t i = 0; i < len; i++) {
        xfer(p[i]);
    }
    end();
    uint32_t t0 = DWT->CYCCNT;
    st = wait_ready(50u);
    if (us) {
        *us = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000u);
    }
    return st;
}

w25q_status_t w25q_read_ex(uint32_t addr, void *buf, size_t len, bool fast)
{
    uint8_t *p = buf;
    if (!range_ok(addr, len) || (buf == NULL && len)) {
        return W25Q_ERR_PARAM;
    }
    CS_LOW();
    send_cmd_addr(fast ? 0x0Bu : CMD_READ_DATA, addr);
    if (fast) {
        xfer(0xFF);
    }
    for (size_t i = 0; i < len; i++) {
        p[i] = xfer(0xFF);
    }
    end();
    return W25Q_OK;
}

/* Power-down helpers without the blocking delay of w25q_release_power_down(). */
void w25q_release_pd_nodelay(void)
{
    CS_LOW();
    xfer(CMD_RELEASE_PD);
    end();
}

uint8_t w25q_release_pd_with_id(void) /* 0xAB + 3 dummy + ID byte (0x17 for Q128) */
{
    CS_LOW();
    xfer(CMD_RELEASE_PD);
    xfer(0xFF);
    xfer(0xFF);
    xfer(0xFF);
    uint8_t id = xfer(0xFF);
    end();
    return id;
}

/* ---- DMA reads: SPI3 RX = DMA1 S0 Ch0, TX (dummy 0xFF, MINC=0) = DMA1 S5 Ch0 ---- */

static volatile uint8_t s_dummy_ff = 0xFF;
#define W_RX_ALL (0x3Du << 0)    /* LIFCR/LISR stream 0 */
#define W_TX_ALL (0x3Du << 6)    /* HIFCR/HISR stream 5 */
static bool s_dma_active;

static void dma_stream_off(DMA_Stream_TypeDef *s)
{
    s->CR &= ~DMA_SxCR_EN;
    uint32_t n = 0;
    while ((s->CR & DMA_SxCR_EN) && ++n < 100000u) {
    }
}

/* Starts a command + address (+dummy) and a DMA transfer of n (1..65535) bytes; CS stays low. */
bool w25q_dma_start(uint32_t addr, void *buf, uint32_t n, bool fast)
{
    if (!range_ok(addr, n) || n == 0u || n > 65535u || s_dma_active) {
        return false;
    }
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;
    CS_LOW();
    send_cmd_addr(fast ? 0x0Bu : CMD_READ_DATA, addr);
    if (fast) {
        xfer(0xFF);
    }
    (void)SPI3->DR;
    (void)SPI3->SR;
    dma_stream_off(DMA1_Stream0);
    dma_stream_off(DMA1_Stream5);
    DMA1->LIFCR = W_RX_ALL;
    DMA1->HIFCR = W_TX_ALL;
    DMA1_Stream0->PAR = (uint32_t)&SPI3->DR;
    DMA1_Stream0->M0AR = (uint32_t)buf;
    DMA1_Stream0->NDTR = n;
    DMA1_Stream0->FCR = 0;
    DMA1_Stream0->CR = DMA_SxCR_MINC | (2u << DMA_SxCR_PL_Pos); /* CHSEL 0, P2M, 8-bit */
    DMA1_Stream5->PAR = (uint32_t)&SPI3->DR;
    DMA1_Stream5->M0AR = (uint32_t)&s_dummy_ff;
    DMA1_Stream5->NDTR = n;
    DMA1_Stream5->FCR = 0;
    DMA1_Stream5->CR = (1u << DMA_SxCR_DIR_Pos) | (1u << DMA_SxCR_PL_Pos); /* M2P, MINC off */
    DMA1_Stream0->CR |= DMA_SxCR_EN;
    DMA1_Stream5->CR |= DMA_SxCR_EN;
    SPI3->CR2 |= SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;
    s_dma_active = true;
    return true;
}

bool w25q_dma_done(void)
{
    return !s_dma_active || (DMA1->LISR & DMA_LISR_TCIF0) != 0u;
}

/* Waits for completion (bounded), releases CS and the DMA streams. */
bool w25q_dma_finish(void)
{
    if (!s_dma_active) {
        return true;
    }
    uint32_t n = 0;
    while (!(DMA1->LISR & DMA_LISR_TCIF0) && ++n < 20000000u) {
    }
    bool ok = (DMA1->LISR & DMA_LISR_TCIF0) != 0u;
    dma_stream_off(DMA1_Stream5);
    dma_stream_off(DMA1_Stream0);
    SPI3->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    end();
    (void)SPI3->DR;
    (void)SPI3->SR;
    DMA1->LIFCR = W_RX_ALL;
    DMA1->HIFCR = W_TX_ALL;
    s_dma_active = false;
    return ok;
}

w25q_status_t w25q_read_dma(uint32_t addr, void *buf, size_t len, bool fast)
{
    uint8_t *p = buf;
    if (!range_ok(addr, len) || (buf == NULL && len)) {
        return W25Q_ERR_PARAM;
    }
    while (len) {
        uint32_t k = (len > 32768u) ? 32768u : (uint32_t)len;
        if (!w25q_dma_start(addr, p, k, fast) || !w25q_dma_finish()) {
            return W25Q_ERR_TIMEOUT;
        }
        addr += k;
        p += k;
        len -= k;
    }
    return W25Q_OK;
}

/* Global Block/Sector Unlock (0x98): clears the volatile individual lock bits only (no data is touched). */
void w25q_global_unlock(void)
{
    write_enable();
    CS_LOW();
    xfer(0x98u);
    end();
}
