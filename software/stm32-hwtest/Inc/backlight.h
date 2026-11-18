#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <stdbool.h>
#include <stdint.h>

/* LCD backlight: PA6 (BLK -> 1k -> NPN S8050 base, 10k base pull-down),
   TIM3_CH1 PWM, AF2. TIM3 runs from the APB1 timer clock (96 MHz at the P1
   clock preset). SPI1 is BIDIMODE TX-only, so PA6 is never SPI1_MISO.
   Default after backlight_init(): 1 kHz, output low (off); lcd/menu code
   then calls backlight_full(). */

#define BL_LEVELS 16u   /* perceptual levels 0..15 (0 = dimmest non-zero) */

void backlight_init(void);
bool backlight_set_freq(uint32_t hz);       /* 50 Hz .. 100 kHz; keeps the duty */
void backlight_retune(void);                /* re-derive PSC/ARR after a SYSCLK change */
uint32_t backlight_freq_hz(void);           /* actual (after integer PSC/ARR) */
void backlight_set_ppm(uint32_t ppm);       /* duty in ppm: 0 = off, 1000000 = always on */
uint32_t backlight_ppm(void);               /* requested duty */
void backlight_set_level(uint8_t level);    /* 0..15, duty = ((level+1)/16)^2.2 */
uint32_t backlight_level_ppm(uint8_t level);
void backlight_full(void);
void backlight_off(void);

/* Raw register view for the auto check. */
typedef struct {
    uint32_t psc, arr, ccr1, cr1, ccmr1, ccer, timclk;
    uint32_t moder6, afr6;
} backlight_regs_t;
void backlight_regs(backlight_regs_t *r);

#endif
