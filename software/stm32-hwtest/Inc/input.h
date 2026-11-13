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

/* Pause the 1 kHz scan (pins suite reconfigures button pins). Resuming
   resyncs held/raw to the current pins and drops queued events. */
void input_suspend(bool on);

/* Raw edge log (P1 bounce analysis): EXTI on both edges of lines 6-10 and
   12-15 (PB6-9, PA10, PB12-15; EXTI9_5 / EXTI15_10, priority 7), each edge
   time-stamped with TIM2 (1 us). Independent of the debounced scan. */
typedef struct {
    uint32_t t_us;      /* TIM2->CNT at ISR entry */
    uint8_t btn;        /* button_t */
    uint8_t pressed;    /* pin level read in the ISR: 1 = low (pressed) */
} input_edge_t;

void input_exti_enable(bool on);
bool input_edge_pop(input_edge_t *e);
uint32_t input_edge_overflows(void);
uint32_t input_edge_count(uint8_t btn);   /* edges since enable (incl. dropped) */

#endif
