/* Host-side shim for the STM32 HAL header.
 *
 * The firmware UI headers (main.h, st7735.h, display_config.h) pull in
 * "stm32f4xx_hal.h" only for a handful of types/macros. On the PC render
 * harness we shadow the real vendor header with this minimal stand-in so the
 * application code compiles without the ARM HAL. Nothing here talks to real
 * hardware; the matching functions are implemented as stubs in host_stubs.c.
 */
#ifndef HOST_SHIM_STM32F4XX_HAL_H
#define HOST_SHIM_STM32F4XX_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_OK = 0x00,
    HAL_ERROR = 0x01,
    HAL_BUSY = 0x02,
    HAL_TIMEOUT = 0x03
} HAL_StatusTypeDef;

/* Opaque placeholders: the harness never dereferences these. */
typedef struct { int _unused; } SPI_HandleTypeDef;
typedef struct { int _unused; } GPIO_TypeDef;

/* Tokens referenced (but never expanded into real register access) by
 * display_config.h / board_pins.h. */
#define SPI1 ((void *)0)
#define SPI_BAUDRATEPRESCALER_4 0u
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0

/* Millisecond tick, driven by the harness (see host_stubs.c). */
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* HOST_SHIM_STM32F4XX_HAL_H */
