/* pins (auto): pull-up / pull-down classification of every testable GPIO
   against the v0.1 netlist (hardware/pcb/schematics.pdf), plus an
   open-drain neighbour-bridge test on the "safe" pins (buttons, PB10, PC13-15).

   Method per pin (input mode, the pin is never driven except in the bridge
   test, which only drives open-drain LOW):
     ru = level with the internal pull-up        (settle `settle` ms)
     rd = level with the internal pull-down
     fl = level ~200 us after switching from pull-down to no pull
     fh = level ~200 us after switching from pull-up   to no pull
   Classes:  ru=1 rd=1 -> TIED_HIGH (external <= ~10k up, or driven)
             ru=0 rd=0 -> TIED_LOW
             ru=1 rd=0, fl=1 fh=1 -> WEAK_HIGH (e.g. external 100k up vs 40k internal)
             ru=1 rd=0, fl=0 fh=0 -> WEAK_LOW
             ru=1 rd=0, fl=0 fh=1 -> FLOATING (node keeps its precharge)
             anything else -> ODD
   "brief" pins (PB2 AMP_SD, PA3 OFF_ACK) are outputs to other chips: they are
   probed with interrupts off and ~20 us per phase, so the amplifier / ATtiny
   never see a meaningful high level (PB2 high time ~40 us, far below the
   amplifier start-up time).
   Skipped: PA11/PA12 (USB), PA13/PA14 (SWD), PH0/PH1 (HSE).

   During the test: the 1 kHz button scan is suspended, OFF_REQ handling is
   inhibited (PA2 is pulled down for a few ms), nothing touches SPI1/SPI3.
   Afterwards every GPIOA/B/C register is restored and the LCD is
   re-initialised (PB1 = LCD reset was pulled low). Results are buffered and
   emitted after the restore. */

#include "suites.h"
#include "board.h"
#include "cmd.h"
#include "gfx.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "power.h"
#include "timebase.h"
#include "ui.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    C_TIED_HIGH = 0,
    C_TIED_LOW,
    C_WEAK_HIGH,
    C_WEAK_LOW,
    C_FLOATING,
    C_ODD,
    C_COUNT
} pclass_t;

#define B(c) (1u << (c))
#define ANY  0xFFu

static const char *const s_cname[C_COUNT] = {
    "TIED_HIGH", "TIED_LOW", "WEAK_HIGH", "WEAK_LOW", "FLOATING", "ODD",
};

enum { M_NORMAL = 0, M_BRIEF };

typedef struct {
    GPIO_TypeDef *port;
    uint8_t pin;
    uint8_t mode;
    uint16_t settle_ms;
    uint8_t expect;        /* bitmask of acceptable classes, ANY = info only */
    const char *signal;
    const char *why;       /* netlist reason for the expectation */
} pin_spec_t;

static const pin_spec_t s_pins[] = {
    {GPIOA, 0, M_NORMAL, 3, B(C_TIED_HIGH) | B(C_TIED_LOW), "CHRG_STAT", "R9 10k up; low while charging"},
    {GPIOA, 1, M_NORMAL, 3, ANY, "BAT_STAT", "analog divider of VBAT (info only)"},
    {GPIOA, 2, M_NORMAL, 3, B(C_WEAK_HIGH) | B(C_TIED_HIGH), "OFF_REQ", "R17 100k up (+R18 10k to ATtiny Hi-Z)"},
    {GPIOA, 3, M_BRIEF, 0, B(C_FLOATING), "OFF_ACK", "R20 10k series to ATtiny PB2 input, no pull"},
    {GPIOA, 4, M_NORMAL, 3, B(C_TIED_HIGH), "CS_D", "R30 10k up"},
    {GPIOA, 5, M_NORMAL, 3, B(C_FLOATING), "SCK_D", "LCD input only"},
    {GPIOA, 6, M_NORMAL, 3, B(C_TIED_LOW), "BLK", "R27 1k -> NPN base, R28 10k base-GND"},
    {GPIOA, 7, M_NORMAL, 3, B(C_FLOATING), "MOSI_D", "LCD input only"},
    {GPIOA, 8, M_NORMAL, 300, B(C_FLOATING), "AUDIO_PWM", "R24 3k3 -> C24 2n2 + C25/C26 1u (DC open)"},
    {GPIOA, 9, M_NORMAL, 3, B(C_TIED_HIGH) | B(C_WEAK_HIGH), "VBUS", "R4 33k / R5 82k from 5V (USB present)"},
    {GPIOA, 10, M_NORMAL, 3, B(C_FLOATING), "CE_BTN", "button to GND, no ext pull"},
    {GPIOA, 15, M_NORMAL, 3, B(C_TIED_HIGH), "CS_F", "R3 10k up"},
    {GPIOB, 0, M_NORMAL, 3, B(C_FLOATING), "DC", "LCD input only"},
    {GPIOB, 1, M_NORMAL, 3, B(C_FLOATING), "RST_D", "LCD input only (LCD reset during test)"},
    {GPIOB, 2, M_BRIEF, 0, B(C_WEAK_LOW), "AMP_SD", "R23 100k down"},
    {GPIOB, 3, M_NORMAL, 3, B(C_FLOATING), "SCK_F", "W25Q CLK input"},
    {GPIOB, 4, M_NORMAL, 3, B(C_FLOATING), "MISO_F", "W25Q DO hi-Z (CS high)"},
    {GPIOB, 5, M_NORMAL, 3, B(C_FLOATING), "MOSI_F", "W25Q DI input"},
    {GPIOB, 6, M_NORMAL, 3, B(C_FLOATING), "RIGHT_BTN", "button to GND"},
    {GPIOB, 7, M_NORMAL, 3, B(C_FLOATING), "LEFT_BTN", "button to GND"},
    {GPIOB, 8, M_NORMAL, 3, B(C_FLOATING), "DOWN_BTN", "button to GND"},
    {GPIOB, 9, M_NORMAL, 3, B(C_FLOATING), "UP_BTN", "button to GND"},
    {GPIOB, 10, M_NORMAL, 3, B(C_FLOATING), "NC", "not connected (control)"},
    {GPIOB, 12, M_NORMAL, 3, B(C_FLOATING), "X_BTN", "button to GND"},
    {GPIOB, 13, M_NORMAL, 3, B(C_FLOATING), "A_BTN", "button to GND"},
    {GPIOB, 14, M_NORMAL, 3, B(C_FLOATING), "B_BTN", "button to GND"},
    {GPIOB, 15, M_NORMAL, 3, B(C_FLOATING), "Y_BTN", "button to GND"},
    {GPIOC, 13, M_NORMAL, 3, B(C_FLOATING), "NC", "not connected (control)"},
    {GPIOC, 14, M_NORMAL, 3, B(C_FLOATING), "NC", "not connected (control, LSE pin)"},
    {GPIOC, 15, M_NORMAL, 3, B(C_FLOATING), "NC", "not connected (control, LSE pin)"},
};
#define N_PINS ((int)(sizeof(s_pins) / sizeof(s_pins[0])))

/* Bridge test set: pins that may safely be driven open-drain low. */
static const struct {
    GPIO_TypeDef *port;
    uint8_t pin;
} s_bridge[] = {
    {GPIOB, 6}, {GPIOB, 7}, {GPIOB, 8}, {GPIOB, 9}, {GPIOA, 10}, {GPIOB, 12}, {GPIOB, 13},
    {GPIOB, 14}, {GPIOB, 15}, {GPIOB, 10}, {GPIOC, 13}, {GPIOC, 14}, {GPIOC, 15},
};
#define N_BRIDGE ((int)(sizeof(s_bridge) / sizeof(s_bridge[0])))

typedef struct {
    uint8_t ru, rd, fl, fh;
    uint8_t cls;
    uint16_t rise_us;   /* brief pins: time until the pull-up read 1 (BRIEF_RISE_MAX = never) */
    uint16_t fall_us;   /* brief pins: time until no-pull read 0 again after the pull-up */
} pin_res_t;

#define BRIEF_RISE_MAX 500u  /* us; longest pull-up phase on PB2/PA3 */

static pin_res_t s_res[N_PINS];
static uint16_t s_follow[N_BRIDGE];   /* bit i = bridge pin i followed */
static uint16_t s_bridge_excluded;    /* read low at baseline (held button?) */

typedef struct {
    uint32_t moder, otyper, ospeedr, pupdr, odr, afr0, afr1;
} port_regs_t;

static void save_port(GPIO_TypeDef *p, port_regs_t *r)
{
    r->moder = p->MODER;
    r->otyper = p->OTYPER;
    r->ospeedr = p->OSPEEDR;
    r->pupdr = p->PUPDR;
    r->odr = p->ODR;
    r->afr0 = p->AFR[0];
    r->afr1 = p->AFR[1];
}

static void restore_port(GPIO_TypeDef *p, const port_regs_t *r)
{
    p->ODR = r->odr;
    p->AFR[0] = r->afr0;
    p->AFR[1] = r->afr1;
    p->OTYPER = r->otyper;
    p->OSPEEDR = r->ospeedr;
    p->PUPDR = r->pupdr;
    p->MODER = r->moder;
}

static inline void set_pull(GPIO_TypeDef *p, uint32_t pin, uint32_t pull)
{
    p->PUPDR = (p->PUPDR & ~(3u << (pin * 2u))) | (pull << (pin * 2u));
}

static inline void set_mode(GPIO_TypeDef *p, uint32_t pin, uint32_t mode)
{
    p->MODER = (p->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}

static inline uint8_t rd_pin(GPIO_TypeDef *p, uint32_t pin) { return (uint8_t)((p->IDR >> pin) & 1u); }

static uint8_t classify(const pin_res_t *r)
{
    if (r->ru && r->rd) return C_TIED_HIGH;
    if (!r->ru && !r->rd) return C_TIED_LOW;
    if (r->ru && !r->rd) {
        if (r->fl && r->fh) return C_WEAK_HIGH;
        if (!r->fl && !r->fh) return C_WEAK_LOW;
        if (!r->fl && r->fh) return C_FLOATING;
    }
    return C_ODD;
}

static void wait_settle(uint16_t ms)
{
    delay_us((uint32_t)ms * 1000u);
}

static void measure(const pin_spec_t *s, pin_res_t *r)
{
    GPIO_TypeDef *p = s->port;
    uint32_t n = s->pin;
    if (s->mode == M_BRIEF) {
        /* Output-low pin of another chip: everything inside ~1 ms, IRQs off.
           The pull-up phase ends as soon as the pin reads 1 (rise time is
           reported), at the latest after BRIEF_RISE_MAX us. */
        __disable_irq();
        set_pull(p, n, GPIO_PULL_DOWN_);
        set_mode(p, n, GPIO_MODE_IN_);
        delay_us(20);
        r->rd = rd_pin(p, n);
        set_pull(p, n, GPIO_PULL_NONE_);
        delay_us(20);
        r->fl = rd_pin(p, n);
        uint32_t t0 = time_us(), el;
        set_pull(p, n, GPIO_PULL_UP_);
        while (!rd_pin(p, n) && (el = time_us() - t0) < BRIEF_RISE_MAX) {
        }
        el = time_us() - t0;
        r->ru = rd_pin(p, n);
        r->rise_us = (uint16_t)(r->ru ? el : BRIEF_RISE_MAX);
        set_pull(p, n, GPIO_PULL_NONE_);
        t0 = time_us();
        while (rd_pin(p, n) && (time_us() - t0) < 100u) {
        }
        r->fall_us = (uint16_t)(time_us() - t0);
        delay_us(20);
        r->fh = rd_pin(p, n);
        set_pull(p, n, GPIO_PULL_DOWN_);   /* discharge before driving low again */
        delay_us(10);
        p->BSRR = 1u << (n + 16u);
        set_mode(p, n, GPIO_MODE_OUT_);
        set_pull(p, n, GPIO_PULL_NONE_);
        __enable_irq();
    } else {
        set_mode(p, n, GPIO_MODE_IN_);
        set_pull(p, n, GPIO_PULL_UP_);
        wait_settle(s->settle_ms);
        r->ru = rd_pin(p, n);
        set_pull(p, n, GPIO_PULL_DOWN_);
        wait_settle(s->settle_ms);
        r->rd = rd_pin(p, n);
        set_pull(p, n, GPIO_PULL_NONE_);
        delay_us(200);
        r->fl = rd_pin(p, n);
        set_pull(p, n, GPIO_PULL_UP_);
        wait_settle(s->settle_ms);
        set_pull(p, n, GPIO_PULL_NONE_);
        delay_us(200);
        r->fh = rd_pin(p, n);
    }
    r->cls = classify(r);
}

static void bridge_test(void)
{
    for (int i = 0; i < N_BRIDGE; i++) {
        set_mode(s_bridge[i].port, s_bridge[i].pin, GPIO_MODE_IN_);
        set_pull(s_bridge[i].port, s_bridge[i].pin, GPIO_PULL_UP_);
        s_bridge[i].port->OTYPER &= ~(1u << s_bridge[i].pin);
    }
    delay_us(3000);
    s_bridge_excluded = 0;
    for (int i = 0; i < N_BRIDGE; i++) {
        if (!rd_pin(s_bridge[i].port, s_bridge[i].pin)) {
            s_bridge_excluded |= (uint16_t)(1u << i);
        }
    }
    for (int d = 0; d < N_BRIDGE; d++) {
        s_follow[d] = 0;
        if (s_bridge_excluded & (1u << d)) {
            continue;
        }
        GPIO_TypeDef *p = s_bridge[d].port;
        uint32_t n = s_bridge[d].pin;
        p->BSRR = 1u << (n + 16u);   /* ODR low */
        p->OTYPER |= 1u << n;        /* open drain */
        set_mode(p, n, GPIO_MODE_OUT_);
        delay_us(1000);
        for (int k = 0; k < N_BRIDGE; k++) {
            if (k == d || (s_bridge_excluded & (1u << k))) {
                continue;
            }
            if (!rd_pin(s_bridge[k].port, s_bridge[k].pin)) {
                s_follow[d] |= (uint16_t)(1u << k);
            }
        }
        set_mode(p, n, GPIO_MODE_IN_);
        p->OTYPER &= ~(1u << n);
        delay_us(1000);
    }
}

static char port_letter(GPIO_TypeDef *p)
{
    return (p == GPIOA) ? 'A' : (p == GPIOB) ? 'B' : 'C';
}

suite_verdict_t suite_pins(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    port_regs_t ra, rb, rc;

    ui_con_line(COLOR_WHITE, "testing %d pins...", N_PINS);
    log_flush(50);
    uint16_t held_before = input_held();

    /* ---- pins are reconfigured from here: no SPI, no OFF_REQ, no scan ---- */
    input_suspend(true);
    power_inhibit(true);
    save_port(GPIOA, &ra);
    save_port(GPIOB, &rb);
    save_port(GPIOC, &rc);
    uint32_t t0 = time_us();

    for (int i = 0; i < N_PINS; i++) {
        measure(&s_pins[i], &s_res[i]);
        /* back to the original configuration of this pin before the next */
        GPIO_TypeDef *p = s_pins[i].port;
        const port_regs_t *r = (p == GPIOA) ? &ra : (p == GPIOB) ? &rb : &rc;
        uint32_t n = s_pins[i].pin;
        set_pull(p, n, (r->pupdr >> (n * 2u)) & 3u);
        set_mode(p, n, (r->moder >> (n * 2u)) & 3u);
    }
    bridge_test();
    uint32_t dt = time_us() - t0;

    restore_port(GPIOA, &ra);
    restore_port(GPIOB, &rb);
    restore_port(GPIOC, &rc);
    delay_us(2000);
    power_inhibit(false);
    input_suspend(false);
    /* ---- restored ---- */

    lcd_init();                  /* PB1 (LCD reset) was pulled low */
    ui_con_begin("pins");
    bool w25q_ok = (w25q_read_jedec() & 0xFFFFFFu) == 0xEF4018u;

    int npass = 0, nfail = 0;
    for (int i = 0; i < N_PINS; i++) {
        const pin_spec_t *s = &s_pins[i];
        const pin_res_t *r = &s_res[i];
        bool info = (s->expect == ANY);
        bool pass = info || ((s->expect >> r->cls) & 1u);
        char name[8], exp[48];
        snprintf(name, sizeof(name), "P%c%u", port_letter(s->port), (unsigned)s->pin);
        size_t n = 0;
        exp[0] = '\0';
        if (info) {
            snprintf(exp, sizeof(exp), "INFO");
        } else {
            for (int c = 0; c < C_COUNT; c++) {
                if ((s->expect >> c) & 1u) {
                    n += (size_t)snprintf(&exp[n], sizeof(exp) - n, "%s%s", n ? "|" : "", s_cname[c]);
                }
            }
        }
        bool held = false;
        if (!pass && r->cls == C_TIED_LOW && strstr(s->signal, "_BTN")) {
            held = true; /* most likely a button held during the test */
        }
        if (s->mode == M_BRIEF) {
            res_printf("{\"t\":\"pin_timing\",\"suite\":\"pins\",\"name\":\"%s\",\"rise_us\":%u,\"fall_us\":%u,"
                       "\"rise_max_us\":%u}",
                       name, (unsigned)r->rise_us, (unsigned)r->fall_us, BRIEF_RISE_MAX);
        }
        res_printf("{\"t\":\"test\",\"suite\":\"pins\",\"name\":\"%s\",\"signal\":\"%s\",\"mode\":\"%s\","
                   "\"expect\":\"%s\",\"actual\":\"%s\",\"pass\":%s%s,\"ru\":%u,\"rd\":%u,\"fl\":%u,\"fh\":%u,"
                   "\"why\":\"%s\"%s}",
                   name, s->signal, s->mode == M_BRIEF ? "brief" : "normal", exp, s_cname[r->cls],
                   pass ? "true" : "false", info ? ",\"info\":true" : "", r->ru, r->rd, r->fl, r->fh,
                   s->why, held ? ",\"note\":\"button held?\"" : "");
        if (!info) {
            pass ? npass++ : nfail++;
        }
        if (!pass || info) {
            ui_con_line(pass ? COLOR_GRAY : COLOR_RED, "%s %-9s %s", name, s->signal, s_cname[r->cls]);
        }
        sys_service();
    }

    /* bridges */
    int nbridge_fail = 0;
    for (int d = 0; d < N_BRIDGE; d++) {
        char name[8], fol[64];
        snprintf(name, sizeof(name), "P%c%u", port_letter(s_bridge[d].port), (unsigned)s_bridge[d].pin);
        size_t n = 0;
        fol[0] = '\0';
        for (int k = 0; k < N_BRIDGE; k++) {
            if (s_follow[d] & (1u << k)) {
                n += (size_t)snprintf(&fol[n], sizeof(fol) - n, "%s\"P%c%u\"", n ? "," : "",
                                      port_letter(s_bridge[k].port), (unsigned)s_bridge[k].pin);
            }
        }
        bool excl = (s_bridge_excluded >> d) & 1u;
        bool pass = !excl && s_follow[d] == 0u;
        if (!pass) nbridge_fail++;
        res_printf("{\"t\":\"test\",\"suite\":\"pins\",\"name\":\"bridge_%s\",\"pass\":%s,\"followers\":[%s]%s}",
                   name, pass ? "true" : "false", fol, excl ? ",\"note\":\"read low at baseline (held?)\"" : "");
        if (!pass) {
            ui_con_line(COLOR_RED, "bridge %s %s", name, excl ? "held?" : "FOLLOW");
        }
    }

    res_printf("{\"t\":\"test\",\"suite\":\"pins\",\"name\":\"restore\",\"pass\":%s,\"w25q_jedec_ok\":%s,"
               "\"held_before\":%u,\"us\":%" PRIu32 "}",
               w25q_ok ? "true" : "false", w25q_ok ? "true" : "false", (unsigned)held_before, dt);
    ui_con_line(nfail || nbridge_fail ? COLOR_RED : COLOR_GREEN, "pins %d ok %d bad, bridges %d bad", npass,
                nfail, nbridge_fail);
    return (nfail == 0 && nbridge_fail == 0 && w25q_ok) ? SUITE_PASS : SUITE_FAIL;
}
