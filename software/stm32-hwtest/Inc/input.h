#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* 10-button contract (bit order is part of the host protocol / doc):
   bit 0 UP, 1 DOWN, 2 LEFT, 3 RIGHT, 4 CENTER, 5 A, 6 B, 7 X, 8 Y,
   9 POWER (= OFF_REQ asserted low by the ATtiny). */
typedef enum {
    BTN_UP = 0,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_CENTER,
    BTN_A,
    BTN_B,
    BTN_X,
    BTN_Y,
    BTN_POWER,
    BTN_COUNT
} button_t;

#define BTN_MASK(b) ((uint16_t)(1u << (b)))

typedef struct {
    uint8_t button;     /* button_t */
    uint8_t pressed;    /* 1 = press, 0 = release */
    uint32_t t_ms;      /* HAL tick of the debounced edge */
} input_event_t;

void input_init(void);
void input_tick_1ms(void);              /* from SysTick, 1 kHz */
uint16_t input_held(void);              /* debounced held mask */
uint16_t input_raw(void);               /* last raw sample (pressed = 1) */
bool input_pop(input_event_t *ev);      /* next edge event (thread context) */
void input_flush(void);
const char *input_name(button_t b);
void input_set_debounce_ms(uint8_t ms); /* default 20 */
uint32_t input_overflows(void);

#endif
