#ifndef USB_H
#define USB_H

/* USB CDC ACM device (VID 0483, PID 5740) — thin API over the ST stack.
   Src/usb/usb_device.c + usbd_cdc_if.c. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_RX_LINE_MAX 128u   /* incl. NUL; protocol max is 120 chars */
#define USB_LINE_TOOLONG "\001toolong"  /* queued instead of an overlong line */

void usb_init(void);
void usb_stop(void);          /* clean USBD_Stop (host sees a disconnect); no-op when not running */
void usb_start(void);         /* USBD_Start again (re-enumeration); needs a 48 MHz USB clock */
bool usb_is_running(void);
bool usb_configured(void);
bool usb_dtr(void);
uint32_t usb_baud(void);

/* TX: one transfer in flight at a time. Returns false if busy/unconfigured. */
bool usb_tx_busy(void);
bool usb_tx(const uint8_t *data, uint16_t len);
uint16_t usb_tx_max(void);

/* RX: complete lines ('\n' terminated, '\r' dropped) assembled in the USB ISR. */
bool usb_rx_pop_line(char *out, size_t max);
uint32_t usb_rx_drops(void);

/* Set from the USB ISR when a complete "abort" line arrives, so long-running
   suites can stop even before the main loop parses the queue. */
extern volatile bool g_usb_abort_req;
/* Set from the USB ISR on DTR rising edge; consumed by the main loop (@HELLO). */
extern volatile bool g_usb_dtr_rise;
/* usbbench rx: total bytes received on the OUT endpoint; while the sink flag is set the
   bytes are only counted and discarded (no line parsing). */
extern volatile uint32_t g_usb_rx_bytes;
extern volatile bool g_usb_rx_sink;

/* Raw capture ring for `media write` (see usbd_cdc_if.c): begin / pop / end. Thread context. */
void usb_cap_begin(uint8_t *buf, uint32_t size);
uint32_t usb_cap_pop(uint8_t *dst, uint32_t max);
void usb_cap_end(void);

#endif
