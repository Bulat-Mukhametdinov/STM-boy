#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED

#if !defined  (HSE_VALUE)
#define HSE_VALUE    8000000U
#endif

#if !defined  (HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT    100U
#endif

#if !defined  (HSI_VALUE)
#define HSI_VALUE    16000000U
#endif

#if !defined  (LSI_VALUE)
#define LSI_VALUE  32000U
#endif
#if !defined  (LSE_VALUE)
#define LSE_VALUE  32768U
#endif

#if !defined  (LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT    5000U
#endif

#if !defined  (EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE    12288000U
#endif

#define  VDD_VALUE                    3300U
#define  TICK_INT_PRIORITY            0x0FU
#define  USE_RTOS                     0U
#define  PREFETCH_ENABLE               1U
#define  INSTRUCTION_CACHE_ENABLE      1U
#define  DATA_CACHE_ENABLE             1U

#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_cortex.h"
#include "stm32f4xx_hal_pwr.h"
#include "stm32f4xx_hal_pcd.h"
#include "stm32f4xx_hal_exti.h"
#include "stm32f4xx_hal_spi.h"
#include "stm32f4xx_hal_adc.h"
#include "stm32f4xx_hal_tim.h"

#ifdef  USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif
