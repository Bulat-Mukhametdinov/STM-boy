#include "rom_upload.h"

#include "flash_layout.h"
#include "main.h"
#include "runtime_workspace.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "w25q128.h"

#include <stddef.h>
#include <stdio.h>

#define ROM_UPLOAD_MAGIC 0x50553843u
#define ROM_UPLOAD_HEADER_SIZE 20u
#define ROM_UPLOAD_RING_SIZE RUNTIME_WORKSPACE_UPLOAD_RX_RING_SIZE
#define ROM_UPLOAD_MAX_PAYLOAD RUNTIME_WORKSPACE_UPLOAD_MAX_PAYLOAD
#define ROM_UPLOAD_VERIFY_CHUNK RUNTIME_WORKSPACE_UPLOAD_VERIFY_CHUNK

#define ROM_UPLOAD_CMD_HELLO 1u
#define ROM_UPLOAD_CMD_BEGIN 2u
#define ROM_UPLOAD_CMD_WRITE 3u
#define ROM_UPLOAD_CMD_END 4u
#define ROM_UPLOAD_CMD_ABORT 5u

typedef struct {
    uint8_t cmd;
    uint32_t offset;
    uint32_t length;
    uint32_t crc;
} UploadHeader;

typedef struct {
    bool active;
    UploadHeader header;
    uint32_t received;
} PendingWrite;

USBD_HandleTypeDef hUsbDeviceFS;

#define rx_ring (g_runtime_workspace.upload.rx_ring)
#define payload (g_runtime_workspace.upload.payload)
#define verify_buf (g_runtime_workspace.upload.verify_buf)

static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint16_t rx_count;
static volatile bool tx_busy;

static char tx_line[96];
static uint16_t tx_len;
static bool tx_pending;
static bool usb_started;

static RomUploadPhase phase = ROM_UPLOAD_PHASE_OFF;
static const char *message = "Off";
static uint32_t total_size;
static uint32_t done_size;
static uint32_t written_size;
static uint32_t expected_crc;
static uint32_t running_crc;
static uint32_t verify_offset;
static uint32_t erase_offset;
static uint32_t jedec_id;
static PendingWrite pending_write;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    while (size-- > 0u) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return crc;
}

static uint32_t crc32_finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

static void set_error(const char *text)
{
    phase = ROM_UPLOAD_PHASE_ERROR;
    message = text;
}

static void queue_text(const char *prefix, const char *cmd)
{
    if (tx_pending) {
        return;
    }

    int n = snprintf(tx_line, sizeof(tx_line), "%s %s %lu %lu\n",
                     prefix, cmd,
                     (unsigned long)done_size,
                     (unsigned long)total_size);

    if (n < 0) {
        return;
    }
    tx_len = (n < (int)sizeof(tx_line)) ? (uint16_t)n : (uint16_t)(sizeof(tx_line) - 1u);
    tx_pending = true;
}

static void queue_ok(const char *cmd)
{
    queue_text("OK", cmd);
}

static void queue_err(const char *cmd)
{
    queue_text("ERR", cmd);
}

static void service_tx(void)
{
    USBD_CDC_HandleTypeDef *hcdc;

    if (!usb_started || !tx_pending || tx_busy) {
        if (!usb_started) {
            return;
        }

        hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassDataCmsit[hUsbDeviceFS.classId];
        if (hcdc != NULL && hcdc->TxState == 0u) {
            tx_busy = false;
        }
        if (!tx_pending || tx_busy) {
            return;
        }
    }

    hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassDataCmsit[hUsbDeviceFS.classId];
    if (hcdc == NULL || hcdc->TxState != 0u) {
        return;
    }

    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, (uint8_t *)tx_line, tx_len);
    if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) == USBD_OK) {
        tx_busy = true;
        tx_pending = false;
    }
}

static bool configure_usb_clock(void)
{
    RCC_OscInitTypeDef osc = {0};

    if ((RCC->CR & RCC_CR_PLLRDY) != 0u) {
        return true;
    }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 4;
    osc.PLL.PLLN = 192;
    osc.PLL.PLLP = RCC_PLLP_DIV4;
    osc.PLL.PLLQ = 8;
    return HAL_RCC_OscConfig(&osc) == HAL_OK;
}

static bool ring_peek(uint16_t offset, uint8_t *out)
{
    if (offset >= rx_count) {
        return false;
    }
    *out = rx_ring[(uint16_t)((rx_tail + offset) % ROM_UPLOAD_RING_SIZE)];
    return true;
}

static bool ring_pop(uint8_t *out)
{
    uint32_t primask;

    if (rx_count == 0u) {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    *out = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) % ROM_UPLOAD_RING_SIZE);
    --rx_count;
    if (primask == 0u) {
        __enable_irq();
    }
    return true;
}

static void ring_drop(uint16_t size)
{
    uint8_t ignored;

    while (size-- > 0u) {
        if (!ring_pop(&ignored)) {
            return;
        }
    }
}

static bool parse_header(UploadHeader *header)
{
    uint8_t raw[ROM_UPLOAD_HEADER_SIZE];
    uint8_t b;

    while (rx_count >= 4u) {
        if (!ring_peek(0, &raw[0]) || !ring_peek(1, &raw[1]) ||
            !ring_peek(2, &raw[2]) || !ring_peek(3, &raw[3])) {
            return false;
        }
        if (rd32(raw) == ROM_UPLOAD_MAGIC) {
            break;
        }
        (void)ring_pop(&b);
    }

    if (rx_count < ROM_UPLOAD_HEADER_SIZE) {
        return false;
    }

    for (uint16_t i = 0; i < ROM_UPLOAD_HEADER_SIZE; ++i) {
        if (!ring_peek(i, &raw[i])) {
            return false;
        }
    }

    header->cmd = raw[4];
    header->offset = rd32(&raw[8]);
    header->length = rd32(&raw[12]);
    header->crc = rd32(&raw[16]);
    return true;
}

static void handle_hello(void)
{
    message = "USB ready";
    queue_ok("HELLO");
}

static void handle_begin(const UploadHeader *header)
{
    if (header->length == 0u || header->length > FLASH_LAYOUT_ROMPACK_SIZE) {
        set_error("Bad size");
        queue_err("BEGIN");
        return;
    }

    total_size = header->length;
    expected_crc = header->crc;
    done_size = 0;
    written_size = 0;
    erase_offset = 0;
    pending_write.active = false;
    phase = ROM_UPLOAD_PHASE_ERASING;
    message = "Erasing flash";
    queue_ok("BEGIN");
}

static void start_write(const UploadHeader *header)
{
    if (phase != ROM_UPLOAD_PHASE_WRITING && phase != ROM_UPLOAD_PHASE_WAITING) {
        set_error("Unexpected write");
        queue_err("WRITE");
        return;
    }
    if (header->length == 0u || header->length > ROM_UPLOAD_MAX_PAYLOAD ||
        header->offset > total_size || header->length > total_size - header->offset) {
        set_error("Bad write");
        queue_err("WRITE");
        return;
    }

    pending_write.active = true;
    pending_write.header = *header;
    pending_write.received = 0;
    message = "Writing";
}

static void finish_write(void)
{
    const UploadHeader *header = &pending_write.header;
    uint32_t crc;

    pending_write.active = false;
    crc = crc32_finish(crc32_update(0xFFFFFFFFu, payload, header->length));
    if (crc != header->crc) {
        set_error("Chunk CRC error");
        queue_err("WRITE");
        return;
    }
    if (!W25Q128_Write(FLASH_LAYOUT_ROMPACK_OFFSET + header->offset, payload, header->length)) {
        set_error("Flash write error");
        queue_err("WRITE");
        return;
    }

    phase = ROM_UPLOAD_PHASE_WRITING;
    if (header->offset + header->length > written_size) {
        written_size = header->offset + header->length;
    }
    done_size = written_size;
    message = "Writing";
    queue_ok("WRITE");
}

static void handle_end(const UploadHeader *header)
{
    if (header->length != total_size || header->crc != expected_crc || written_size != total_size) {
        set_error("Incomplete image");
        queue_err("END");
        return;
    }

    verify_offset = 0;
    running_crc = 0xFFFFFFFFu;
    done_size = 0;
    phase = ROM_UPLOAD_PHASE_VERIFYING;
    message = "Verifying";
}

static void handle_abort(void)
{
    total_size = 0;
    done_size = 0;
    written_size = 0;
    pending_write.active = false;
    phase = ROM_UPLOAD_PHASE_WAITING;
    message = "Waiting for upload";
    queue_ok("ABORT");
}

static void service_pending_write(void)
{
    uint8_t b;

    if (!pending_write.active) {
        return;
    }

    while (pending_write.received < pending_write.header.length) {
        if (!ring_pop(&b)) {
            return;
        }
        payload[pending_write.received++] = b;
    }

    finish_write();
}

static void process_frames(void)
{
    UploadHeader header;

    if (phase == ROM_UPLOAD_PHASE_ERASING || phase == ROM_UPLOAD_PHASE_VERIFYING ||
        pending_write.active || tx_pending || tx_busy) {
        return;
    }

    if (!parse_header(&header)) {
        return;
    }

    ring_drop(ROM_UPLOAD_HEADER_SIZE);

    switch (header.cmd) {
    case ROM_UPLOAD_CMD_HELLO:
        handle_hello();
        break;
    case ROM_UPLOAD_CMD_BEGIN:
        handle_begin(&header);
        break;
    case ROM_UPLOAD_CMD_WRITE:
        start_write(&header);
        break;
    case ROM_UPLOAD_CMD_END:
        handle_end(&header);
        break;
    case ROM_UPLOAD_CMD_ABORT:
        handle_abort();
        break;
    default:
        set_error("Unknown command");
        queue_err("CMD");
        break;
    }
}

static void erase_step(void)
{
    uint32_t erase_total = (total_size + W25Q128_SECTOR_SIZE - 1u) & ~(W25Q128_SECTOR_SIZE - 1u);
    uint32_t physical_offset;
    bool ok;

    if (erase_offset >= erase_total) {
        if (tx_pending || tx_busy) {
            return;
        }
        done_size = 0;
        phase = ROM_UPLOAD_PHASE_WRITING;
        message = "Ready to write";
        queue_ok("ERASE");
        return;
    }

    if (tx_pending || tx_busy) {
        return;
    }

    physical_offset = FLASH_LAYOUT_ROMPACK_OFFSET + erase_offset;

    if ((physical_offset % W25Q128_BLOCK64_SIZE) == 0u &&
        (erase_total - erase_offset) >= W25Q128_BLOCK64_SIZE) {
        ok = W25Q128_EraseBlock64(physical_offset);
        erase_offset += ok ? W25Q128_BLOCK64_SIZE : 0u;
    } else {
        ok = W25Q128_EraseSector(physical_offset);
        erase_offset += ok ? W25Q128_SECTOR_SIZE : 0u;
    }

    if (!ok) {
        set_error("Flash erase error");
        queue_err("BEGIN");
        return;
    }

    done_size = (erase_offset > total_size) ? total_size : erase_offset;

    if (erase_offset >= erase_total) {
        done_size = 0;
        phase = ROM_UPLOAD_PHASE_WRITING;
        message = "Ready to write";
        queue_ok("ERASE");
        return;
    }

    queue_text("PROG", "ERASE");
}

static void verify_step(void)
{
    uint32_t chunk;
    uint32_t final_crc;

    if (verify_offset >= total_size) {
        final_crc = crc32_finish(running_crc);
        done_size = total_size;
        if (final_crc == expected_crc) {
            phase = ROM_UPLOAD_PHASE_DONE;
            message = "Upload complete";
            queue_ok("END");
        } else {
            set_error("Image CRC error");
            queue_err("END");
        }
        return;
    }

    chunk = total_size - verify_offset;
    if (chunk > ROM_UPLOAD_VERIFY_CHUNK) {
        chunk = ROM_UPLOAD_VERIFY_CHUNK;
    }

    if (!W25Q128_Read(FLASH_LAYOUT_ROMPACK_OFFSET + verify_offset, verify_buf, chunk)) {
        set_error("Flash read error");
        queue_err("END");
        return;
    }

    running_crc = crc32_update(running_crc, verify_buf, chunk);
    verify_offset += chunk;
    done_size = verify_offset;
}

void RomUpload_Start(void)
{
    if (usb_started) {
        return;
    }

    rx_head = 0;
    rx_tail = 0;
    rx_count = 0;
    tx_busy = false;
    tx_pending = false;
    total_size = 0;
    done_size = 0;
    written_size = 0;
    expected_crc = 0;
    jedec_id = 0;
    pending_write.active = false;

    if (!W25Q128_Init()) {
        set_error("W25Q128 not found");
        return;
    }
    jedec_id = W25Q128_ReadJedecId();

    if (!configure_usb_clock()) {
        set_error("USB clock error");
        return;
    }
    if (USBD_Init(&hUsbDeviceFS, &VCP_Desc, 0) != USBD_OK ||
        USBD_RegisterClass(&hUsbDeviceFS, USBD_CDC_CLASS) != USBD_OK ||
        USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_CDC_fops) != USBD_OK ||
        USBD_Start(&hUsbDeviceFS) != USBD_OK) {
        set_error("USB start error");
        return;
    }

    usb_started = true;
    phase = ROM_UPLOAD_PHASE_WAITING;
    message = "Waiting for upload";
}

void RomUpload_Stop(void)
{
    if (usb_started) {
        (void)USBD_Stop(&hUsbDeviceFS);
        (void)USBD_DeInit(&hUsbDeviceFS);
    }
    usb_started = false;
    phase = ROM_UPLOAD_PHASE_OFF;
    message = "Off";
}

void RomUpload_Task(void)
{
    if (phase == ROM_UPLOAD_PHASE_ERASING) {
        erase_step();
    } else if (phase == ROM_UPLOAD_PHASE_VERIFYING) {
        verify_step();
    } else if (pending_write.active) {
        service_pending_write();
    } else {
        process_frames();
    }

    service_tx();
}

void RomUpload_GetStatus(RomUploadStatus *status)
{
    if (status == NULL) {
        return;
    }

    status->phase = phase;
    status->done = done_size;
    status->total = total_size;
    status->jedec_id = jedec_id;
    status->message = message;
}

void RomUpload_UsbReceive(const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        if (rx_count >= ROM_UPLOAD_RING_SIZE) {
            phase = ROM_UPLOAD_PHASE_ERROR;
            message = "USB RX overflow";
            return;
        }
        rx_ring[rx_head] = data[i];
        rx_head = (uint16_t)((rx_head + 1u) % ROM_UPLOAD_RING_SIZE);
        ++rx_count;
    }
}

void RomUpload_UsbTransmitComplete(void)
{
    tx_busy = false;
}
