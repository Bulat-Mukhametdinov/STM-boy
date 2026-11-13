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

#endif
