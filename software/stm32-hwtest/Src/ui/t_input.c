/* Input suites (P1, all [U]): input.live, input.bounce, input.chord.

   POWER is excluded everywhere. Per software/attiny13/power_fsm.c the ATtiny
   asserts OFF_REQ only after the POWER button was held >= 500 ms
   (T_SHUT_HOLD_MS) AND released, and then removes EN after OFF_ACK or after
   2.5 s (T_OFF_ACK_TIMEOUT_MS) whatever the STM32 does; shorter presses never
   change OFF_REQ. So the STM32 cannot observe a POWER press without the board
   switching off, and POWER is not a usable game button on v0.1.

   The 1 kHz debounced scan keeps running; input.bounce additionally enables
   the EXTI edge log (both edges, TIM2 us timestamps). */

#include "suites.h"
#include "cmd.h"
#include "gfx.h"
#include "hwcal.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "timebase.h"
#include "ui.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

extern uint8_t g_arena[];

#define GAME_BTNS 9   /* BTN_UP .. BTN_Y; BTN_POWER excluded */
#define GAME_MASK ((uint16_t)((1u << GAME_BTNS) - 1u))

static const char *const POWER_NOTE =
    "POWER not testable: ATtiny asserts OFF_REQ only after a >=500 ms hold + release and then "
    "cuts power within 2.5 s (attiny13/power_fsm.c)";

static void emit_power_skip(const char *suite)
{
    res_printf("{\"t\":\"skip\",\"suite\":\"%s\",\"name\":\"btn_POWER\",\"reason\":\"%s\"}", suite, POWER_NOTE);
}

static uint32_t step_ms(int argc, char **argv, uint32_t def_s)
{
    return suite_arg_u32(argc, argv, "step", def_s) * 1000u;
}

/* ================================================================ input.live */

suite_verdict_t suite_input_live(int argc, char **argv)
{
    if (!suite_user_start("input.live",
                          "Live button test: press and release every button once (POWER excluded). "
                          "A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = step_ms(argc, argv, 60u);
    static const char *const ans[] = {"press all 9 buttons", NULL};
    ui_prompt_emit("input_live", "Press and release every button once (POWER excluded)", ans, to);
    emit_power_skip("input.live");

    input_exti_enable(true);
    input_flush();
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    gfx_text(2, 114, "press each button once", COLOR_GRAY, COLOR_BLACK, 1);
    ui_draw_button_map(input_held(), true, 0);

    uint16_t pressed = 0, released = 0;
    uint16_t presses[BTN_COUNT] = {0};
    uint32_t t0 = HAL_GetTick(), last_s = 0xFFFFFFFFu;
    const char *end = "done";
    for (;;) {
        sys_service();
        input_event_t ev;
        while (input_pop(&ev)) {
            if (ev.button >= GAME_BTNS) {
                continue;
            }
            uint16_t m = BTN_MASK(ev.button);
            if (ev.pressed) {
                pressed |= m;
                presses[ev.button]++;
            } else if (pressed & m) {
                released |= m;
            }
            res_printf("{\"t\":\"edge\",\"suite\":\"input.live\",\"button\":\"%s\",\"pressed\":%u,"
                       "\"t_ms\":%" PRIu32 ",\"raw_edges\":%" PRIu32 "}",
                       input_name((button_t)ev.button), ev.pressed, ev.t_ms, input_edge_count(ev.button));
        }
        ui_draw_button_map(input_held(), false, (uint16_t)(pressed & released));
        uint32_t el = HAL_GetTick() - t0;
        uint32_t left = (to > el) ? (to - el + 999u) / 1000u : 0u;
        if (left != last_s) {
            last_s = left;
            gfx_textf(2, 12, COLOR_WHITE, COLOR_BLACK, 1, "seen %d/9  %3" PRIu32 "s ",
                      __builtin_popcount((unsigned)(pressed & released)), left);
        }
        if ((pressed & released & GAME_MASK) == GAME_MASK) {
            HAL_Delay(300);
            break;
        }
        if (abort_requested()) { end = "abort"; break; }
        if (el >= to) { end = "timeout"; break; }
    }
    ui_answer_emit("input_live", end);
    input_exti_enable(false);

    int n_ok = 0;
    for (int i = 0; i < GAME_BTNS; i++) {
        bool ok = ((pressed & released) >> i) & 1u;
        n_ok += ok;
        res_printf("{\"t\":\"test\",\"suite\":\"input.live\",\"name\":\"btn_%s\",\"pass\":%s,\"presses\":%u,"
                   "\"raw_edges\":%" PRIu32 "}",
                   input_name((button_t)i), ok ? "true" : "false", presses[i], input_edge_count((uint8_t)i));
    }
    ui_con_begin("input.live");
    ui_con_line(n_ok == GAME_BTNS ? COLOR_GREEN : COLOR_RED, "%d/9 buttons seen (%s)", n_ok, end);
    if (!strcmp(end, "abort")) return SUITE_ABORT;
    return (n_ok == GAME_BTNS) ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ input.bounce */

#define QUIET_US   20000u   /* burst ends after 20 ms without an edge */
#define MAX_BURSTS 160u     /* per button */

typedef struct {
    uint32_t dur_us;
    uint16_t edges;
    uint8_t pressed;   /* final level of the burst: 1 = press, 0 = release */
} burst_t;

/* Lives in the shared test arena (72 KB): 9 * 160 * 8 B = 11.5 KB. */
#define BURSTS ((burst_t (*)[MAX_BURSTS])(void *)g_arena)

static void sort_u32(uint32_t *v, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++) {
        uint32_t x = v[i];
        uint32_t j = i;
        while (j > 0 && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
}

static uint32_t pct(const uint32_t *sorted, uint32_t n, uint32_t p)
{
    if (n == 0) return 0;
    uint32_t idx = (p * (n - 1u) + 50u) / 100u; /* nearest rank on 0..n-1 */
    return sorted[idx];
}

/* sort scratch, also in the arena (after the bursts) */
#define s_tmp ((uint32_t *)(void *)(g_arena + sizeof(burst_t) * MAX_BURSTS * GAME_BTNS))

typedef struct {
    uint32_t n, min, med, p95, max;
} stat_t;

static stat_t stats(uint32_t *v, uint32_t n)
{
    stat_t s = {n, 0, 0, 0, 0};
    if (!n) return s;
    sort_u32(v, n);
    s.min = v[0];
    s.med = pct(v, n, 50);
    s.p95 = pct(v, n, 95);
    s.max = v[n - 1];
    return s;
}

static void bounce_screen(int b, uint32_t presses, uint32_t target, uint32_t left_s)
{
    char t[24];
    snprintf(t, sizeof(t), "%s", input_name((button_t)b));
    lcd_fill_rect(0, 24, (int16_t)lcd_width(), 64, COLOR_BLACK);
    int16_t w = (int16_t)(strlen(t) * GFX_CHAR_W * 3);
    gfx_text((int16_t)((lcd_width() - w) / 2), 28, t, COLOR_YELLOW, COLOR_BLACK, 3);
    gfx_textf(30, 60, COLOR_WHITE, COLOR_BLACK, 2, "%2" PRIu32 "/%" PRIu32, presses, target);
    gfx_textf(110, 66, COLOR_GRAY, COLOR_BLACK, 1, "%3" PRIu32 "s", left_s);
}

suite_verdict_t suite_input_bounce(int argc, char **argv)
{
    uint32_t target = suite_arg_u32(argc, argv, "n", 30u);
    uint32_t idle = step_ms(argc, argv, 20u);
    const char *only = suite_arg_str(argc, argv, "btn", NULL);
    if (target < 1u) target = 1u;
    if (target > 60u) target = 60u;

    if (!suite_user_start("input.bounce",
                          "Bounce analyser: for each button shown, press it ~30 times at a normal pace. "
                          "Hold another button 1 s to skip one. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    emit_power_skip("input.bounce");
    memset(g_arena, 0, sizeof(burst_t) * MAX_BURSTS * GAME_BTNS);
    uint32_t nb[GAME_BTNS] = {0};
    uint32_t presses_total = 0;
    bool aborted = false;
    int done_btns = 0;

    input_exti_enable(true);
    for (int b = 0; b < GAME_BTNS && !aborted; b++) {
        if (only && strcmp(only, input_name((button_t)b)) != 0) {
            continue;
        }
        char id[24];
        snprintf(id, sizeof(id), "bounce_%s", input_name((button_t)b));
        char txt[64];
        snprintf(txt, sizeof(txt), "Press %s about %" PRIu32 " times", input_name((button_t)b), target);
        static const char *const ans[] = {"press the shown button", "hold another 1s=skip", NULL};
        ui_prompt_emit(id, txt, ans, idle);

        gfx_fill(COLOR_BLACK);
        ui_status_bar();
        gfx_text(2, 12, "Bounce test: press", COLOR_CYAN, COLOR_BLACK, 1);
        gfx_text(2, 100, "normal pace, full press", COLOR_GRAY, COLOR_BLACK, 1);
        gfx_text(2, 110, "hold other btn 1s: skip", COLOR_GRAY, COLOR_BLACK, 1);

        input_edge_t e;
        while (input_edge_pop(&e)) {
        } /* drop old edges */
        input_flush();
        uint32_t presses = 0, last_act = HAL_GetTick(), last_s = 0xFFFFFFFFu;
        bool open = false;
        uint32_t first = 0, last = 0;
        uint16_t edges = 0;
        uint8_t lvl = 0;
        uint32_t other_since = 0, shown_p = 0xFFFFFFFFu;
        const char *end = "done";
        bounce_screen(b, 0, target, idle / 1000u);

        for (;;) {
            sys_service();
            while (input_edge_pop(&e)) {
                if (e.btn != b) continue;
                if (open && (uint32_t)(e.t_us - last) > QUIET_US) {
                    if (nb[b] < MAX_BURSTS) {
                        BURSTS[b][nb[b]].dur_us = last - first;
                        BURSTS[b][nb[b]].edges = edges;
                        BURSTS[b][nb[b]].pressed = lvl;
                        nb[b]++;
                    }
                    open = false;
                }
                if (!open) {
                    open = true;
                    first = e.t_us;
                    edges = 0;
                }
                last = e.t_us;
                edges++;
                lvl = e.pressed;
                last_act = HAL_GetTick();
            }
            if (open && (uint32_t)(time_us() - last) > QUIET_US) {
                if (nb[b] < MAX_BURSTS) {
                    BURSTS[b][nb[b]].dur_us = last - first;
                    BURSTS[b][nb[b]].edges = edges;
                    BURSTS[b][nb[b]].pressed = lvl;
                    nb[b]++;
                }
                open = false;
            }
            input_event_t ev;
            while (input_pop(&ev)) {
                if (ev.button == b && ev.pressed) {
                    presses++;
                }
            }
            uint16_t others = (uint16_t)(input_held() & GAME_MASK & ~BTN_MASK(b));
            if (others) {
                if (!other_since) other_since = HAL_GetTick() | 1u;
                else if (HAL_GetTick() - other_since >= 1000u) { end = "skip"; break; }
            } else {
                other_since = 0;
            }
            uint32_t el = HAL_GetTick() - last_act;
            uint32_t left = (idle > el) ? (idle - el + 999u) / 1000u : 0u;

            if (left != last_s || presses != shown_p) {
                last_s = left;
                shown_p = presses;
                bounce_screen(b, presses, target, left);
            }
            if (presses >= target && !open && !(input_held() & BTN_MASK(b))) break;
            if (abort_requested()) { end = "abort"; aborted = true; break; }
            if (el >= idle) { end = "timeout"; break; }
        }
        ui_answer_emit(id, end);
        presses_total += presses;
        if (!strcmp(end, "done")) done_btns++;

        /* per-button stats */
        uint32_t n = nb[b], np = 0, nr = 0;
        for (uint32_t i = 0; i < n; i++) s_tmp[i] = BURSTS[b][i].dur_us;
        stat_t sd = stats(s_tmp, n);
        for (uint32_t i = 0; i < n; i++) s_tmp[i] = BURSTS[b][i].edges;
        stat_t se = stats(s_tmp, n);
        for (uint32_t i = 0; i < n; i++) if (BURSTS[b][i].pressed) s_tmp[np++] = BURSTS[b][i].dur_us;
        stat_t sp = stats(s_tmp, np);
        for (uint32_t i = 0; i < n; i++) if (!BURSTS[b][i].pressed) s_tmp[nr++] = BURSTS[b][i].dur_us;
        stat_t sr = stats(s_tmp, nr);
        uint32_t rec = (2u * sd.p95 + 999u) / 1000u;
        if (rec < 5u) rec = 5u;
        res_printf("{\"t\":\"bounce\",\"suite\":\"input.bounce\",\"name\":\"%s\",\"end\":\"%s\",\"presses\":%" PRIu32
                   ",\"bursts\":%" PRIu32 ",\"dur_us\":{\"min\":%" PRIu32 ",\"med\":%" PRIu32 ",\"p95\":%" PRIu32
                   ",\"max\":%" PRIu32 "},\"edges\":{\"min\":%" PRIu32 ",\"med\":%" PRIu32 ",\"p95\":%" PRIu32
                   ",\"max\":%" PRIu32 "},\"press_p95_us\":%" PRIu32 ",\"release_p95_us\":%" PRIu32
                   ",\"rec_debounce_ms\":%" PRIu32 ",\"edge_overflows\":%" PRIu32 "}",
                   input_name((button_t)b), end, presses, n, sd.min, sd.med, sd.p95, sd.max, se.min, se.med, se.p95,
                   se.max, sp.p95, sr.p95, rec, input_edge_overflows());
    }
    input_exti_enable(false);

    /* overall */
    uint32_t n = 0;
    for (int b = 0; b < GAME_BTNS; b++) {
        for (uint32_t i = 0; i < nb[b]; i++) s_tmp[n++] = BURSTS[b][i].dur_us;
    }
    stat_t all = stats(s_tmp, n);
    uint32_t rec = (2u * all.p95 + 999u) / 1000u;
    if (rec < 5u) rec = 5u;
    bool have = (n >= 10u);
    if (have && !aborted) {
        g_hwcal.debounce_ms = (uint8_t)(rec > 100u ? 100u : rec); /* RAM only (no W25Q writes in P1) */
    }
    res_printf("{\"t\":\"cal\",\"suite\":\"input.bounce\",\"name\":\"debounce\",\"pass\":%s,\"bursts\":%" PRIu32
               ",\"dur_us\":{\"min\":%" PRIu32 ",\"med\":%" PRIu32 ",\"p95\":%" PRIu32 ",\"max\":%" PRIu32
               "},\"rec_debounce_ms\":%" PRIu32 ",\"rule\":\"max(2*p95,5ms)\",\"buttons_done\":%d,"
               "\"saved_to\":\"%s\"}",
               have ? "true" : "false", n, all.min, all.med, all.p95, all.max, rec, done_btns,
               (have && !aborted) ? "ram" : "none");
    ui_con_begin("input.bounce");
    ui_con_line(COLOR_WHITE, "bursts %" PRIu32 " p95 %" PRIu32 " us", n, all.p95);
    ui_con_line(have ? COLOR_GREEN : COLOR_RED, "debounce >= %" PRIu32 " ms", rec);
    if (aborted) return SUITE_ABORT;
    (void)presses_total;
    return have ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ input.chord */

typedef struct {
    uint16_t mask;
    bool info;     /* mechanically questionable (joystick diagonal): no verdict */
} chord_t;

#define M(b) BTN_MASK(b)
static const chord_t s_chords[] = {
    {M(BTN_A) | M(BTN_B), false},
    {M(BTN_X) | M(BTN_Y), false},
    {M(BTN_A) | M(BTN_X), false},
    {M(BTN_B) | M(BTN_Y), false},
    {M(BTN_A) | M(BTN_B) | M(BTN_X) | M(BTN_Y), false},
    {M(BTN_UP) | M(BTN_A), false},
    {M(BTN_DOWN) | M(BTN_B), false},
    {M(BTN_LEFT) | M(BTN_X), false},
    {M(BTN_RIGHT) | M(BTN_Y), false},
    {M(BTN_CENTER) | M(BTN_A), false},
    {M(BTN_LEFT) | M(BTN_A) | M(BTN_B), false},
    {M(BTN_UP) | M(BTN_RIGHT), true},
    {M(BTN_DOWN) | M(BTN_LEFT), true},
};
#define N_CHORDS ((int)(sizeof(s_chords) / sizeof(s_chords[0])))

static void mask_names(uint16_t m, char *out, size_t n)
{
    size_t k = 0;
    out[0] = '\0';
    for (int i = 0; i < GAME_BTNS; i++) {
        if (m & BTN_MASK(i)) {
            k += (size_t)snprintf(&out[k], n - k, "%s%s", k ? "+" : "", input_name((button_t)i));
            if (k >= n) break;
        }
    }
}

suite_verdict_t suite_input_chord(int argc, char **argv)
{
    if (!suite_user_start("input.chord",
                          "Chord test: hold the shown button combination until it turns green, then release "
                          "everything. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    emit_power_skip("input.chord");
    uint32_t to = step_ms(argc, argv, 20u);
    int fails = 0;
    bool aborted = false;

    for (int c = 0; c < N_CHORDS && !aborted; c++) {
        const chord_t *ch = &s_chords[c];
        char names[48], id[16];
        mask_names(ch->mask, names, sizeof(names));
        snprintf(id, sizeof(id), "chord_%d", c);
        static const char *const ans[] = {"hold the combination", NULL};
        char txt[112];
        snprintf(txt, sizeof(txt), "Hold %s%s", names, ch->info ? " (joystick diagonal, may be impossible)" : "");
        ui_prompt_emit(id, txt, ans, to);

        gfx_fill(COLOR_BLACK);
        ui_status_bar();
        gfx_textf(2, 12, COLOR_CYAN, COLOR_BLACK, 1, "Chord %d/%d: hold", c + 1, N_CHORDS);
        gfx_text(2, 114, names, COLOR_YELLOW, COLOR_BLACK, 1);
        ui_draw_button_map(input_held(), true, ch->mask);

        uint32_t t0 = HAL_GetTick(), match_since = 0, last_s = 0xFFFFFFFFu;
        uint16_t extra = 0, seen_max = 0;
        const char *end = "ok";
        for (;;) {
            sys_service();
            input_event_t ev;
            while (input_pop(&ev)) {
            }
            uint16_t h = (uint16_t)(input_held() & GAME_MASK);
            ui_draw_button_map(h, false, ch->mask);
            seen_max |= h;
            if ((h & ch->mask) == ch->mask) {
                extra |= (uint16_t)(h & ~ch->mask);
            }
            if (h == ch->mask) {
                if (!match_since) match_since = HAL_GetTick() | 1u;
                else if (HAL_GetTick() - match_since >= 150u) break;
            } else {
                match_since = 0;
            }
            uint32_t el = HAL_GetTick() - t0;
            uint32_t left = (to > el) ? (to - el + 999u) / 1000u : 0u;
            if (left != last_s) {
                last_s = left;
                gfx_textf(120, 12, COLOR_GRAY, COLOR_BLACK, 1, "%3" PRIu32 "s", left);
            }
            if (abort_requested()) { end = "abort"; aborted = true; break; }
            if (el >= to) { end = "timeout"; break; }
        }
        uint32_t t_ms = HAL_GetTick() - t0;
        ui_answer_emit(id, end);
        bool ok = !strcmp(end, "ok");
        if (ok) {
            gfx_text(2, 24, "OK - release all", COLOR_GREEN, COLOR_BLACK, 1);
        }
        if (!ok && !ch->info && !aborted) fails++;
        char ex[48];
        mask_names(extra, ex, sizeof(ex));
        if (ch->info) {
            res_printf("{\"t\":\"chord\",\"suite\":\"input.chord\",\"name\":\"%s\",\"info\":true,\"result\":\"%s\","
                       "\"ms\":%" PRIu32 ",\"mask\":%u,\"extra\":\"%s\",\"seen\":%u}",
                       names, end, t_ms, ch->mask, ex, seen_max);
        } else {
            res_printf("{\"t\":\"test\",\"suite\":\"input.chord\",\"name\":\"%s\",\"pass\":%s,\"result\":\"%s\","
                       "\"ms\":%" PRIu32 ",\"mask\":%u,\"extra\":\"%s\",\"seen\":%u}",
                       names, ok ? "true" : "false", end, t_ms, ch->mask, ex, seen_max);
        }
        /* wait for release (max 5 s) */
        uint32_t tr = HAL_GetTick();
        while ((input_held() & GAME_MASK) && HAL_GetTick() - tr < 5000u && !abort_requested()) {
            sys_service();
        }
        input_flush();
    }
    ui_con_begin("input.chord");
    ui_con_line(fails ? COLOR_RED : COLOR_GREEN, "%d chord(s) failed", fails);
    if (aborted) return SUITE_ABORT;
    return fails ? SUITE_FAIL : SUITE_PASS;
}
