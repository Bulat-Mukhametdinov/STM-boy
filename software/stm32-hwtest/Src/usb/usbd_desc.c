/* USB descriptors — same VID/PID/serial algorithm as stm32-bringup
   (Src/usbd_desc.c) so hosts and the DFU bootloader see the same serial. */

#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"
#include "board.h"

#define USBD_VID                     0x0483
#define USBD_PID                     0x5740
#define USBD_LANGID_STRING           0x409
#define USBD_MANUFACTURER_STRING     "DIY"
#define USBD_PRODUCT_FS_STRING       "STM-boy hwtest"
#define USBD_CONFIGURATION_FS_STRING "VCP Config"
#define USBD_INTERFACE_FS_STRING     "VCP Interface"

#define USB_SIZ_STRING_SERIAL 0x1AU /* 2 + 12 chars * 2 */

static uint8_t *dev_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *langid_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *mfg_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *product_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *serial_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *config_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *iface_desc(USBD_SpeedTypeDef speed, uint16_t *length);

USBD_DescriptorsTypeDef VCP_Desc = {
    dev_desc, langid_desc, mfg_desc, product_desc, serial_desc, config_desc, iface_desc,
};

__ALIGN_BEGIN static uint8_t s_dev_desc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12,                 /* bLength */
    USB_DESC_TYPE_DEVICE, /* bDescriptorType */
    0x00, 0x02,           /* bcdUSB = 2.00 */
    0x02,                 /* bDeviceClass (CDC) */
    0x02,                 /* bDeviceSubClass */
    0x00,                 /* bDeviceProtocol */
    USB_MAX_EP0_SIZE,     /* bMaxPacketSize */
    LOBYTE(USBD_VID), HIBYTE(USBD_VID),
    LOBYTE(USBD_PID), HIBYTE(USBD_PID),
    0x00, 0x02,           /* bcdDevice = 2.00 */
    USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION,
};

__ALIGN_BEGIN static uint8_t s_langid_desc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING,
    LOBYTE(USBD_LANGID_STRING), HIBYTE(USBD_LANGID_STRING),
};

__ALIGN_BEGIN static uint8_t s_serial_desc[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
    USB_SIZ_STRING_SERIAL, USB_DESC_TYPE_STRING,
};

__ALIGN_BEGIN static uint8_t s_str_desc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

static uint8_t *dev_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(s_dev_desc);
    return s_dev_desc;
}

static uint8_t *langid_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(s_langid_desc);
    return s_langid_desc;
}

static uint8_t *product_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_PRODUCT_FS_STRING, s_str_desc, length);
    return s_str_desc;
}

static uint8_t *mfg_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, s_str_desc, length);
    return s_str_desc;
}

static uint8_t *serial_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    char s[13];
    (void)speed;
    board_usb_serial(s);
    for (unsigned i = 0; i < 12u; i++) {
        s_serial_desc[2u + 2u * i] = (uint8_t)s[i];
        s_serial_desc[3u + 2u * i] = 0;
    }
    *length = USB_SIZ_STRING_SERIAL;
    return s_serial_desc;
}

static uint8_t *config_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_FS_STRING, s_str_desc, length);
    return s_str_desc;
}

static uint8_t *iface_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_INTERFACE_FS_STRING, s_str_desc, length);
    return s_str_desc;
}
