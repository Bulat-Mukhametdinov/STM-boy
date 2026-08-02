#include "usbd_conf.h"
#include "usbd_core.h"
#include "usbd_desc.h"

#define USBD_VID 0x0483
#define USBD_PID 0x5740
#define USBD_LANGID_STRING 0x0409
#define USBD_MANUFACTURER_STRING "RetroPort"
#define USBD_PRODUCT_FS_STRING "RetroPort ROM Uploader"
#define USBD_CONFIGURATION_FS_STRING "CDC Config"
#define USBD_INTERFACE_FS_STRING "CDC Interface"

static uint8_t *device_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *lang_id_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *manufacturer_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *product_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *serial_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *config_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *interface_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static void int_to_unicode(uint32_t value, uint8_t *pbuf, uint8_t len);
static void get_serial_num(void);

USBD_DescriptorsTypeDef VCP_Desc = {
    device_descriptor,
    lang_id_descriptor,
    manufacturer_descriptor,
    product_descriptor,
    serial_descriptor,
    config_descriptor,
    interface_descriptor,
};

__ALIGN_BEGIN static uint8_t device_desc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12,
    USB_DESC_TYPE_DEVICE,
    0x00,
    0x02,
    0x02,
    0x02,
    0x00,
    USB_MAX_EP0_SIZE,
    LOBYTE(USBD_VID),
    HIBYTE(USBD_VID),
    LOBYTE(USBD_PID),
    HIBYTE(USBD_PID),
    0x00,
    0x02,
    USBD_IDX_MFC_STR,
    USBD_IDX_PRODUCT_STR,
    USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION,
};

__ALIGN_BEGIN static uint8_t lang_id_desc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC,
    USB_DESC_TYPE_STRING,
    LOBYTE(USBD_LANGID_STRING),
    HIBYTE(USBD_LANGID_STRING),
};

__ALIGN_BEGIN static uint8_t string_serial[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
    USB_SIZ_STRING_SERIAL,
    USB_DESC_TYPE_STRING,
};

__ALIGN_BEGIN static uint8_t str_desc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

static uint8_t *device_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(device_desc);
    return device_desc;
}

static uint8_t *lang_id_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(lang_id_desc);
    return lang_id_desc;
}

static uint8_t *manufacturer_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, str_desc, length);
    return str_desc;
}

static uint8_t *product_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_PRODUCT_FS_STRING, str_desc, length);
    return str_desc;
}

static uint8_t *serial_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = USB_SIZ_STRING_SERIAL;
    get_serial_num();
    return string_serial;
}

static uint8_t *config_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_FS_STRING, str_desc, length);
    return str_desc;
}

static uint8_t *interface_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_INTERFACE_FS_STRING, str_desc, length);
    return str_desc;
}

static void get_serial_num(void)
{
    uint32_t serial0 = *(uint32_t *)DEVICE_ID1;
    uint32_t serial1 = *(uint32_t *)DEVICE_ID2;
    uint32_t serial2 = *(uint32_t *)DEVICE_ID3;

    serial0 += serial2;
    if (serial0 != 0u) {
        int_to_unicode(serial0, &string_serial[2], 8);
        int_to_unicode(serial1, &string_serial[18], 4);
    }
}

static void int_to_unicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
    for (uint8_t idx = 0; idx < len; ++idx) {
        uint8_t digit = (uint8_t)((value >> 28) & 0x0F);
        pbuf[2u * idx] = (digit < 10u) ? (uint8_t)('0' + digit) : (uint8_t)('A' + digit - 10u);
        pbuf[2u * idx + 1u] = 0;
        value <<= 4;
    }
}
