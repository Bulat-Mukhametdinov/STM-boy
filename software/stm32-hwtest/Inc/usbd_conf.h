#ifndef USBD_CONF_H
#define USBD_CONF_H

/* ST USB device library configuration (from stm32-bringup), with a static
   allocator instead of malloc: the CDC class allocates exactly one handle. */

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES     1U
#define USBD_MAX_NUM_CONFIGURATION  1U
#define USBD_MAX_STR_DESC_SIZ       0x100U
#define USBD_SELF_POWERED           1U
#define USBD_DEBUG_LEVEL            0U
#define USBD_LPM_ENABLED            0U
#define USBD_CLASS_BOS_ENABLED      0U
#define USBD_CLASS_USER_STRING_DESC 0U

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#define USBD_malloc  USBD_static_malloc
#define USBD_free    USBD_static_free
#define USBD_memset  memset
#define USBD_memcpy  memcpy
#define USBD_Delay   HAL_Delay

#define USBD_UsrLog(...)
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)

#endif
