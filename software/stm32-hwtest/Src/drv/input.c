/* Button input: 1 kHz scan from SysTick, per-button debounce, edge queue.
   Adapted from stm32-retroport Core/Src/input.c (20 ms debounce idea), but
   debounced per button (a bouncing button does not delay the others). */

#include "input.h"
#include "board.h"
#include "stm32f4xx_hal.h"

#define QUEUE_LEN 32u   /* power of two */

typedef struct {
    GPIO_TypeDef *port;
    uint8_t pin;
} btn_pin_t;

static const btn_pin_t s_pins[BTN_COUNT] = {
    [BTN_UP] = {BTN_UP_PORT, BTN_UP_PIN},
    [BTN_DOWN] = {BTN_DOWN_PORT, BTN_DOWN_PIN},
    [BTN_LEFT] = {BTN_LEFT_PORT, BTN_LEFT_PIN},
    [BTN_RIGHT] = {BTN_RIGHT_PORT, BTN_RIGHT_PIN},
    [BTN_CENTER] = {BTN_CENTER_PORT, BTN_CENTER_PIN},
    [BTN_A] = {BTN_A_PORT, BTN_A_PIN},
    [BTN_B] = {BTN_B_PORT, BTN_B_PIN},
    [BTN_X] = {BTN_X_PORT, BTN_X_PIN},
    [BTN_Y] = {BTN_Y_PORT, BTN_Y_PIN},
    [BTN_POWER] = {OFF_REQ_PORT, OFF_REQ_PIN},
};

static const char *const s_names[BTN_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "CENTER", "A", "B", "X", "Y", "POWER",
};

static volatile uint16_t s_held;
static volatile uint16_t s_raw;
static uint8_t s_stable_ms[BTN_COUNT];
static volatile uint8_t s_debounce_ms = 20u;
static volatile bool s_ready;

static input_event_t s_q[QUEUE_LEN];
static volatile uint32_t s_q_head; /* written by ISR */
static volatile uint32_t s_q_tail; /* written by thread */
static volatile uint32_t s_overflows;

static uint16_t read_raw(void)
{
    uint32_t a = GPIOA->IDR;
    uint32_t b = GPIOB->IDR;
    uint16_t m = 0;
    for (uint32_t i = 0; i < BTN_COUNT; i++) {
        uint32_t idr = (s_pins[i].port == GPIOA) ? a : b;
        if (((idr >> s_pins[i].pin) & 1u) == 0u) { /* all active-low */
            m |= BTN_MASK(i);
        }
    }
    return m;
}

void input_init(void)
{
    for (uint32_t i = 0; i < BTN_COUNT; i++) {
        if (i == BTN_POWER) {
            continue; /* OFF_REQ: external pull-up, configured in board_init */
        }
        gpio_config(s_pins[i].port, s_pins[i].pin, GPIO_MODE_IN_, GPIO_PULL_UP_,
                    GPIO_SPEED_LOW_, 0);
    }
    for (volatile uint32_t d = 0; d < 1000u; d++) {
    } /* let pull-ups settle */
    uint16_t raw = read_raw();
    s_raw = raw;
    s_held = raw; /* buttons held at boot don't generate a press event */
    for (uint32_t i = 0; i < BTN_COUNT; i++) {
        s_stable_ms[i] = 0;
    }
    s_q_head = s_q_tail = 0;
    s_ready = true;
}

static void push(uint8_t b, uint8_t pressed, uint32_t t)
{
    uint32_t h = s_q_head;
    if ((h - s_q_tail) >= QUEUE_LEN) {
        s_overflows++;
        return;
    }
    s_q[h & (QUEUE_LEN - 1u)].button = b;
    s_q[h & (QUEUE_LEN - 1u)].pressed = pressed;
    s_q[h & (QUEUE_LEN - 1u)].t_ms = t;
    __DMB();
    s_q_head = h + 1u;
}

void input_tick_1ms(void)
{
    if (!s_ready) {
        return;
    }
    uint16_t raw = read_raw();
    uint16_t prev_raw = s_raw;
    uint16_t held = s_held;
    uint32_t now = HAL_GetTick();
    uint8_t deb = s_debounce_ms;

    for (uint32_t i = 0; i < BTN_COUNT; i++) {
        uint16_t m = BTN_MASK(i);
        if ((raw ^ prev_raw) & m) {
            s_stable_ms[i] = 0; /* raw changed: restart the stability window */
            continue;
        }
        if (s_stable_ms[i] < 255u) {
            s_stable_ms[i]++;
        }
        if (s_stable_ms[i] >= deb && ((raw ^ held) & m)) {
            held ^= m;
            push((uint8_t)i, (raw & m) ? 1u : 0u, now);
        }
    }
    s_raw = raw;
    s_held = held;
}

uint16_t input_held(void) { return s_held; }
uint16_t input_raw(void) { return s_raw; }

bool input_pop(input_event_t *ev)
{
    uint32_t t = s_q_tail;
    if (t == s_q_head) {
        return false;
    }
    __DMB();
    *ev = s_q[t & (QUEUE_LEN - 1u)];
    s_q_tail = t + 1u;
    return true;
}

void input_flush(void)
{
    s_q_tail = s_q_head;
}

const char *input_name(button_t b)
{
    return (b < BTN_COUNT) ? s_names[b] : "?";
}

void input_set_debounce_ms(uint8_t ms)
{
    s_debounce_ms = (ms == 0u) ? 1u : ms;
}

uint32_t input_overflows(void) { return s_overflows; }
