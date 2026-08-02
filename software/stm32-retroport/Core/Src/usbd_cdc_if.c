#include "usbd_cdc_if.h"

#include "rom_upload.h"
#include "usbd_cdc.h"
#include "usbd_core.h"

#define APP_RX_DATA_SIZE 64u
#define APP_TX_DATA_SIZE 1024u

static uint8_t UserRxBuffer[APP_RX_DATA_SIZE];
static uint8_t UserTxBuffer[APP_TX_DATA_SIZE];

extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ItfTypeDef USBD_CDC_fops = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS,
    CDC_TransmitCplt_FS,
};

static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBuffer, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBuffer);
    return USBD_OK;
}

static int8_t CDC_DeInit_FS(void)
{
    return USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)cmd;
    (void)pbuf;
    (void)length;
    return USBD_OK;
}

static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len)
{
    RomUpload_UsbReceive(pbuf, *Len);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBuffer);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
    (void)pbuf;
    (void)Len;
    (void)epnum;
    RomUpload_UsbTransmitComplete();
    return USBD_OK;
}
