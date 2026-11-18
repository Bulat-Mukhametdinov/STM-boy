#include "usb.h"
#include "usbd_cdc_if.h"
#include "usbd_core.h"
#include "usbd_desc.h"

static bool s_usb_running;
static bool s_usb_inited;

void usb_init(void)
{
    USBD_Init(&hUsbDeviceFS, &VCP_Desc, 0);
    USBD_RegisterClass(&hUsbDeviceFS, USBD_CDC_CLASS);
    USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS);
    USBD_Start(&hUsbDeviceFS);
    s_usb_running = true;
    s_usb_inited = true;
}

void usb_stop(void)
{
    if (!s_usb_running) {
        return;
    }
    USBD_Stop(&hUsbDeviceFS);
    hUsbDeviceFS.dev_state = USBD_STATE_DEFAULT;
    s_usb_running = false;
}

void usb_start(void)
{
    if (s_usb_running || !s_usb_inited) {
        return;
    }
    USBD_Start(&hUsbDeviceFS);
    s_usb_running = true;
}

bool usb_is_running(void) { return s_usb_running; }
