/* CDC interface callbacks (adapted from stm32-bringup Src/usbd_cdc_if.c).
 *
 * Everything here except usb_tx()/usb_rx_pop_line() runs in the OTG_FS ISR.
 *  - SET_LINE_CODING with 1200 baud => dfu_request() right here in the ISR
 *    ("1200-baud touch"), so it works while the main loop is busy. The reset
 *    happens before the control transfer's status stage, so the host sees the
 *    SET_LINE_CODING (port open/close) fail - documented in PROTOCOL.md.
 *  - SET_CONTROL_LINE_STATE: track DTR (pbuf is the setup request itself,
 *    because the request has wLength == 0).
 *  - OUT data: assembled into '\n'-terminated lines, queued for the main loop.
 */

#include "usbd_cdc_if.h"
#include "usb.h"
#include "dfu.h"

#include <string.h>

#define TX_BUF_SIZE    512u
#define RX_QUEUE_LINES 8u     /* power of two */

USBD_HandleTypeDef hUsbDeviceFS;

volatile bool g_usb_abort_req;
volatile bool g_usb_dtr_rise;

static uint8_t s_rx_pkt[CDC_DATA_FS_MAX_PACKET_SIZE];
static uint8_t s_tx_buf[TX_BUF_SIZE];
static volatile bool s_tx_busy;
static volatile bool s_dtr;

static USBD_CDC_LineCodingTypeDef s_line = {115200, 0x00, 0x00, 0x08};

/* RX line assembly (ISR) + SPSC queue of complete lines (ISR -> thread). */
static char s_cur[USB_RX_LINE_MAX];
static uint32_t s_cur_len;
static bool s_cur_overflow;
static char s_lines[RX_QUEUE_LINES][USB_RX_LINE_MAX];
static volatile uint32_t s_q_head, s_q_tail;
static volatile uint32_t s_rx_drops;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *buf, uint32_t *len);
static int8_t CDC_TransmitCplt_FS(uint8_t *buf, uint32_t *len, uint8_t epnum);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
    CDC_Init_FS, CDC_DeInit_FS, CDC_Control_FS, CDC_Receive_FS, CDC_TransmitCplt_FS,
};

static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, s_tx_buf, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, s_rx_pkt);
    s_tx_busy = false;
    s_cur_len = 0;
    s_cur_overflow = false;
    return USBD_OK;
}

static int8_t CDC_DeInit_FS(void)
{
    s_tx_busy = false;
    s_dtr = false;
    return USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    switch (cmd) {
    case CDC_SET_LINE_CODING:
        if (length >= 7u) {
            s_line.bitrate = (uint32_t)pbuf[0] | ((uint32_t)pbuf[1] << 8) |
                             ((uint32_t)pbuf[2] << 16) | ((uint32_t)pbuf[3] << 24);
            s_line.format = pbuf[4];
            s_line.paritytype = pbuf[5];
            s_line.datatype = pbuf[6];
            if (s_line.bitrate == 1200u) {
                dfu_request(); /* never returns */
            }
        }
        break;

    case CDC_GET_LINE_CODING:
        pbuf[0] = (uint8_t)(s_line.bitrate);
        pbuf[1] = (uint8_t)(s_line.bitrate >> 8);
        pbuf[2] = (uint8_t)(s_line.bitrate >> 16);
        pbuf[3] = (uint8_t)(s_line.bitrate >> 24);
        pbuf[4] = s_line.format;
        pbuf[5] = s_line.paritytype;
        pbuf[6] = s_line.datatype;
        break;

    case CDC_SET_CONTROL_LINE_STATE: {
        const USBD_SetupReqTypedef *req = (const USBD_SetupReqTypedef *)(void *)pbuf;
        bool dtr = (req->wValue & 0x0001u) != 0u;
        if (dtr && !s_dtr) {
            g_usb_dtr_rise = true;
        }
        s_dtr = dtr;
        break;
    }

    default:
        break;
    }
    return USBD_OK;
}

static void line_complete(void)
{
    s_cur[s_cur_len] = '\0';
    if (s_cur_overflow) {
        /* Deliver a marker so the dispatcher can answer "@ERR ... too long". */
        strcpy(s_cur, USB_LINE_TOOLONG);
    }
    if (strcmp(s_cur, "abort") == 0) {
        g_usb_abort_req = true;
    }
    uint32_t h = s_q_head;
    if ((h - s_q_tail) >= RX_QUEUE_LINES) {
        s_rx_drops++;
    } else {
        memcpy(s_lines[h & (RX_QUEUE_LINES - 1u)], s_cur, sizeof(s_cur));
        __DMB();
        s_q_head = h + 1u;
    }
    s_cur_len = 0;
    s_cur_overflow = false;
}

static int8_t CDC_Receive_FS(uint8_t *buf, uint32_t *len)
{
    for (uint32_t i = 0; i < *len; i++) {
        char c = (char)buf[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (s_cur_len > 0u || s_cur_overflow) {
                line_complete();
            }
            continue;
        }
        if (s_cur_len < USB_RX_LINE_MAX - 1u) {
            s_cur[s_cur_len++] = c;
        } else {
            s_cur_overflow = true;
        }
    }
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, s_rx_pkt);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

static int8_t CDC_TransmitCplt_FS(uint8_t *buf, uint32_t *len, uint8_t epnum)
{
    (void)buf;
    (void)len;
    (void)epnum;
    s_tx_busy = false;
    return USBD_OK;
}

/* ---------------------------------------------------------------- thread-side API */

bool usb_configured(void) { return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED; }
bool usb_dtr(void) { return s_dtr; }
uint32_t usb_baud(void) { return s_line.bitrate; }
bool usb_tx_busy(void) { return s_tx_busy; }
uint16_t usb_tx_max(void) { return (uint16_t)TX_BUF_SIZE; }
uint32_t usb_rx_drops(void) { return s_rx_drops; }

bool usb_tx(const uint8_t *data, uint16_t len)
{
    if (!usb_configured() || s_tx_busy || len == 0u) {
        return false;
    }
    if (len > TX_BUF_SIZE) {
        len = TX_BUF_SIZE;
    }
    memcpy(s_tx_buf, data, len);
    s_tx_busy = true;
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, s_tx_buf, len);
    if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) != USBD_OK) {
        s_tx_busy = false;
        return false;
    }
    return true;
}

bool usb_rx_pop_line(char *out, size_t max)
{
    uint32_t t = s_q_tail;
    if (t == s_q_head || max == 0u) {
        return false;
    }
    __DMB();
    strncpy(out, s_lines[t & (RX_QUEUE_LINES - 1u)], max - 1u);
    out[max - 1u] = '\0';
    s_q_tail = t + 1u;
    return true;
}
