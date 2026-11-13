/* USB device low-level glue (PCD <-> USB device library).
   Adapted from stm32-bringup Src/usbd_conf.c; static class allocator added. */

#include "main.h"
#include "usbd_core.h"
#include "usbd_cdc.h"

#include <stdbool.h>

PCD_HandleTypeDef hpcd;

/* ---------------------------------------------------------------- static allocator */

static uint32_t s_cdc_mem[(sizeof(USBD_CDC_HandleTypeDef) + 3u) / 4u];
static bool s_cdc_mem_used;

void *USBD_static_malloc(uint32_t size)
{
    if (size > sizeof(s_cdc_mem) || s_cdc_mem_used) {
        return NULL;
    }
    s_cdc_mem_used = true;
    return s_cdc_mem;
}

void USBD_static_free(void *p)
{
    if (p == (void *)s_cdc_mem) {
        s_cdc_mem_used = false;
    }
}

/* ---------------------------------------------------------------- MSP */

void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd_ptr)
{
    GPIO_InitTypeDef gi = {0};

    if (hpcd_ptr->Instance == USB_OTG_FS) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        /* PA11 = D-, PA12 = D+ */
        gi.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        gi.Mode = GPIO_MODE_AF_PP;
        gi.Pull = GPIO_NOPULL;
        gi.Speed = GPIO_SPEED_FREQ_HIGH;
        gi.Alternate = GPIO_AF10_OTG_FS;
        HAL_GPIO_Init(GPIOA, &gi);

        __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
        HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *hpcd_ptr)
{
    if (hpcd_ptr->Instance == USB_OTG_FS) {
        __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
        HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    }
}

/* ---------------------------------------------------------------- PCD -> library */

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *h)
{
    USBD_LL_SetupStage((USBD_HandleTypeDef *)h->pData, (uint8_t *)h->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *h, uint8_t epnum)
{
    USBD_LL_DataOutStage((USBD_HandleTypeDef *)h->pData, epnum, h->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *h, uint8_t epnum)
{
    USBD_LL_DataInStage((USBD_HandleTypeDef *)h->pData, epnum, h->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *h)
{
    USBD_LL_SOF((USBD_HandleTypeDef *)h->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *h)
{
    USBD_LL_SetSpeed((USBD_HandleTypeDef *)h->pData, USBD_SPEED_FULL);
    USBD_LL_Reset((USBD_HandleTypeDef *)h->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *h)
{
    USBD_LL_Suspend((USBD_HandleTypeDef *)h->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *h)
{
    USBD_LL_Resume((USBD_HandleTypeDef *)h->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *h, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)h->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *h, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)h->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *h)
{
    USBD_LL_DevConnected((USBD_HandleTypeDef *)h->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *h)
{
    USBD_LL_DevDisconnected((USBD_HandleTypeDef *)h->pData);
}

/* ---------------------------------------------------------------- library -> PCD */

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    hpcd.Instance = USB_OTG_FS;
    hpcd.Init.dev_endpoints = 4;
    hpcd.Init.use_dedicated_ep1 = 0;
    hpcd.Init.dma_enable = 0;
    hpcd.Init.low_power_enable = 0;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd.Init.Sof_enable = 0;
    hpcd.Init.speed = PCD_SPEED_FULL;
    hpcd.Init.vbus_sensing_enable = 0;
    hpcd.Init.lpm_enable = 0;

    hpcd.pData = pdev;
    pdev->pData = &hpcd;

    if (HAL_PCD_Init(&hpcd) != HAL_OK) {
        Error_Handler();
    }

    HAL_PCDEx_SetRxFiFo(&hpcd, 0x80);
    HAL_PCDEx_SetTxFiFo(&hpcd, 0, 0x40);
    HAL_PCDEx_SetTxFiFo(&hpcd, 1, 0x80);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_DeInit(pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_Start(pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_Stop(pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type,
                                  uint16_t ep_mps)
{
    HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_Close(pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_Flush(pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_SetStall(pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);
    return USBD_OK;
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *h = pdev->pData;
    if ((ep_addr & 0x80u) == 0x80u) {
        return h->IN_ep[ep_addr & 0x7Fu].is_stall;
    }
    return h->OUT_ep[ep_addr & 0x7Fu].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
    HAL_PCD_SetAddress(pdev->pData, dev_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf,
                                    uint32_t size)
{
    HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf,
                                          uint32_t size)
{
    HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);
    return USBD_OK;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return HAL_PCD_EP_GetRxCount(pdev->pData, ep_addr);
}

void USBD_LL_Delay(uint32_t Delay)
{
    HAL_Delay(Delay);
}
