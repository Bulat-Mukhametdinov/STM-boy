#include "main.h"
#include "usbd_cdc_if.h"
#include <string.h>

#define APP_RX_DATA_SIZE  64
#define APP_TX_DATA_SIZE  128

static uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
static uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];
static volatile uint8_t TxBusy = 0;

extern USBD_HandleTypeDef hUsbDeviceFS;

static USBD_CDC_LineCodingTypeDef LineCoding = {
  115200, 0x00, 0x00, 0x08
};

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

static int8_t CDC_Init_FS(void)
{
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  return (USBD_OK);
}

static int8_t CDC_DeInit_FS(void)
{
  return (USBD_OK);
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  switch (cmd)
  {
  case CDC_SET_LINE_CODING:
    LineCoding.bitrate    = (uint32_t)(pbuf[0] | (pbuf[1] << 8) | (pbuf[2] << 16) | (pbuf[3] << 24));
    LineCoding.format     = pbuf[4];
    LineCoding.paritytype = pbuf[5];
    LineCoding.datatype   = pbuf[6];
    break;

  case CDC_GET_LINE_CODING:
    pbuf[0] = (uint8_t)(LineCoding.bitrate);
    pbuf[1] = (uint8_t)(LineCoding.bitrate >> 8);
    pbuf[2] = (uint8_t)(LineCoding.bitrate >> 16);
    pbuf[3] = (uint8_t)(LineCoding.bitrate >> 24);
    pbuf[4] = LineCoding.format;
    pbuf[5] = LineCoding.paritytype;
    pbuf[6] = LineCoding.datatype;
    break;

  default:
    break;
  }

  return (USBD_OK);
}

/* Debug channel is output-only from the device; incoming host data is discarded */
static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
  UNUSED(Buf);
  UNUSED(Len);

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
}

static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);

  TxBusy = 0;
  return (USBD_OK);
}

/* Non-blocking: copies up to APP_TX_DATA_SIZE bytes and sends them.
   Returns USBD_BUSY (drops the message) if a previous transmit hasn't completed yet. */
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
  if (TxBusy)
  {
    return USBD_BUSY;
  }

  if (Len > APP_TX_DATA_SIZE)
  {
    Len = APP_TX_DATA_SIZE;
  }

  memcpy(UserTxBufferFS, Buf, Len);

  TxBusy = 1;
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, Len);
  if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) != USBD_OK)
  {
    TxBusy = 0;
    return USBD_BUSY;
  }

  return USBD_OK;
}
