/* Button input: 1 kHz scan from SysTick, per-button debounce, edge queue.
   Adapted from stm32-retroport Core/Src/input.c (20 ms debounce idea), but
   debounced per button (a bouncing button does not delay the others). */

#include "input.h"
#include "board.h"
#include "stm32f4xx_hal.h"

#define QUEUE_LEN 32u   /* power of two */
#define INPUT_PRESS_MS 1u   /* press accepted after 2 equal samples (R14 eager press) */

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
        /* R14 (review 2026-11-21): eager press, debounced release. A press is reported once the level has
           been stable for INPUT_PRESS_MS (1 ms = two consecutive samples, rejects single-sample spikes)
           instead of the full window; the release still needs `deb` ms of stable level, so contact bounce
           after the press (and the up-to-17.6 ms release bounce of B, F5) never produces a second event. */
        if ((raw ^ held) & m) {
            bool pressing = (raw & m) != 0u;
            if (s_stable_ms[i] >= (pressing ? INPUT_PRESS_MS : deb)) {
                held ^= m;
                push((uint8_t)i, pressing ? 1u : 0u, now);
            }
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

/* ---------------------------------------------------------------- suspend */

void input_suspend(bool on)
{
    if (on) {
        s_ready = false;
        return;
    }
    /* Resync to the current pin state so the pause creates no phantom edges. */
    uint16_t raw = read_raw();
    s_raw = raw;
    s_held = raw;
    for (uint32_t i = 0; i < BTN_COUNT; i++) {
        s_stable_ms[i] = 0;
    }
    s_q_tail = s_q_head;
    s_ready = true;
}

/* ---------------------------------------------------------------- EXTI edge log */

/* EXTI line -> button (PA9 = VBUS shares line 9 with PB9 = UP; VBUS is polled). */
static const struct {
    uint8_t line;
    uint8_t btn;
} s_exti[] = {
    {6, BTN_RIGHT}, {7, BTN_LEFT}, {8, BTN_DOWN}, {9, BTN_UP}, {10, BTN_CENTER},
    {12, BTN_X}, {13, BTN_A}, {14, BTN_B}, {15, BTN_Y},
};
#define N_EXTI (sizeof(s_exti) / sizeof(s_exti[0]))
#define EXTI_MASK ((1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 12) | \
                   (1u << 13) | (1u << 14) | (1u << 15))
#define EDGE_LEN 512u    /* power of two */

static input_edge_t s_edges[EDGE_LEN];
static volatile uint32_t s_e_head, s_e_tail, s_e_over;
static volatile uint32_t s_e_count[BTN_COUNT];

void input_exti_enable(bool on)
{
    if (!on) {
        EXTI->IMR &= ~EXTI_MASK;
        EXTI->RTSR &= ~EXTI_MASK;
        EXTI->FTSR &= ~EXTI_MASK;
        EXTI->PR = EXTI_MASK;
        NVIC_DisableIRQ(EXTI9_5_IRQn);
        NVIC_DisableIRQ(EXTI15_10_IRQn);
        return;
    }
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    for (uint32_t i = 0; i < N_EXTI; i++) {
        uint32_t line = s_exti[i].line;
        uint32_t port = (s_pins[s_exti[i].btn].port == GPIOA) ? 0u : 1u; /* 0 = PA, 1 = PB */
        volatile uint32_t *cr = &SYSCFG->EXTICR[line >> 2];
        uint32_t sh = (line & 3u) * 4u;
        *cr = (*cr & ~(0xFu << sh)) | (port << sh);
    }
    s_e_head = s_e_tail = 0;
    s_e_over = 0;
    for (uint32_t i = 0; i < BTN_COUNT; i++) {
        s_e_count[i] = 0;
    }
    EXTI->PR = EXTI_MASK;
    EXTI->RTSR |= EXTI_MASK;
    EXTI->FTSR |= EXTI_MASK;
    EXTI->IMR |= EXTI_MASK;
    NVIC_SetPriority(EXTI9_5_IRQn, 7);
    NVIC_SetPriority(EXTI15_10_IRQn, 7);
    NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
    NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

static void exti_isr(uint32_t lines)
{
    uint32_t pr = EXTI->PR & lines;
    EXTI->PR = pr;
    uint32_t t = TIM2->CNT;
    uint32_t a = GPIOA->IDR, b = GPIOB->IDR;
    for (uint32_t i = 0; i < N_EXTI; i++) {
        if (!(pr & (1u << s_exti[i].line))) {
            continue;
        }
        uint8_t btn = s_exti[i].btn;
        uint32_t idr = (s_pins[btn].port == GPIOA) ? a : b;
        s_e_count[btn]++;
        uint32_t h = s_e_head;
        if ((h - s_e_tail) >= EDGE_LEN) {
            s_e_over++;
            continue;
        }
        input_edge_t *e = &s_edges[h & (EDGE_LEN - 1u)];
        e->t_us = t;
        e->btn = btn;
        e->pressed = ((idr >> s_pins[btn].pin) & 1u) ? 0u : 1u;
        __DMB();
        s_e_head = h + 1u;
    }
}

void EXTI9_5_IRQHandler(void);
void EXTI15_10_IRQHandler(void);
void EXTI9_5_IRQHandler(void) { exti_isr(0x3C0u & EXTI_MASK); }
void EXTI15_10_IRQHandler(void) { exti_isr(0xFC00u & EXTI_MASK); }

bool input_edge_pop(input_edge_t *e)
{
    uint32_t t = s_e_tail;
    if (t == s_e_head) {
        return false;
    }
    __DMB();
    *e = s_edges[t & (EDGE_LEN - 1u)];
    s_e_tail = t + 1u;
    return true;
}

uint32_t input_edge_overflows(void) { return s_e_over; }
uint32_t input_edge_count(uint8_t btn) { return (btn < BTN_COUNT) ? s_e_count[btn] : 0u; }
