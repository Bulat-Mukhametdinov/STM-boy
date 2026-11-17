/* Display suites (P1): info (auto), cal (offsets), orient, rgb, gamma,
   invert, bars, gradient, spi (flicker A/B). Everything except display.info
   needs a human; every wait is bounded by a timeout and closed with @A.
   Results stay in RAM (lcd_cfg + g_hwcal) and go to the host as @R lines:
   W25Q writes (HWCAL save) are not allowed in P1. */

#include "suites.h"
#include "backlight.h"
#include "bench.h"
#include "font.h"
#include "board.h"
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

/* ---------------------------------------------------------------- helpers */

typedef struct {
    lcd_cfg_t cfg;
    uint32_t speed;
    bool f16;
} lcd_saved_t;

static void lcd_save(lcd_saved_t *s)
{
    s->cfg = lcd_cfg;
    s->speed = lcd_gpio_speed();
    s->f16 = lcd_frame16();
}

/* Restores the configuration that was active when the suite started and
   clears the whole GDDRAM so no test pattern survives in the margins. */
static void lcd_restore(const lcd_saved_t *s)
{
    lcd_set_frame16(s->f16);
    lcd_set_gpio_speed(s->speed);
    lcd_cfg = s->cfg;
    lcd_apply_cfg();
    lcd_clear_gddram(COLOR_BLACK);
    backlight_full();
}

static const char *orient_name(uint8_t madctl)
{
    return (madctl & LCD_MADCTL_MV) ? "land" : "port";
}

/* Waits for one debounced press. Returns the button, or -1 on timeout,
   -2 on host abort. Host `answer <id> ok|fail` maps to A / B. */
static int wait_press(const char *id, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        sys_service();
        input_event_t ev;
        if (input_pop(&ev) && ev.pressed && ev.button != BTN_POWER) {
            return ev.button;
        }
        ui_answer_t ha;
        if (id && ui_host_answer_take(id, &ha)) {
            return (ha == UI_ANS_OK) ? BTN_A : BTN_B;
        }
        if (abort_requested()) {
            return -2;
        }
        if (timeout_ms && (uint32_t)(HAL_GetTick() - t0) >= timeout_ms) {
            return -1;
        }
    }
}

/* Centered text helper. */
static void text_c(int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    int16_t w = (int16_t)(strlen(s) * GFX_CHAR_W * scale);
    gfx_text((int16_t)(((int16_t)lcd_width() - w) / 2), y, s, fg, bg, scale);
}

/* ================================================================ display.info */

suite_verdict_t suite_display_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static jb_t j;
    uint32_t cr1 = SPI1->CR1;
    uint32_t a_moder = GPIOA->MODER, a_afrl = GPIOA->AFR[0];
    uint32_t m5 = (a_moder >> 10) & 3u, m6 = (a_moder >> 12) & 3u, m7 = (a_moder >> 14) & 3u;
    uint32_t af5 = (a_afrl >> 20) & 0xFu, af6 = (a_afrl >> 24) & 0xFu, af7 = (a_afrl >> 28) & 0xFu;
    bool bidi = (cr1 & SPI_CR1_BIDIMODE) && (cr1 & SPI_CR1_BIDIOE);
    bool sck_ok = (m5 == GPIO_MODE_AF_ && af5 == 5u);
    bool mosi_ok = (m7 == GPIO_MODE_AF_ && af7 == 5u);
    bool pa6_not_spi = !(m6 == GPIO_MODE_AF_ && af6 == 5u);
    bool mstr = (cr1 & SPI_CR1_MSTR) != 0u;

    /* Full-frame fill time at the current SPI clock (8-bit frames). */
    uint32_t c0 = time_cycles();
    lcd_fill_rect(0, 0, (int16_t)lcd_width(), (int16_t)lcd_height(), COLOR_BLACK);
    uint32_t cyc = time_cycles() - c0;

    bool pass = bidi && sck_ok && mosi_ok && pa6_not_spi && mstr;
    jb_begin(&j, "test");
    jb_str(&j, "suite", "display.info");
    jb_str(&j, "name", "lcd_cfg");
    jb_bool(&j, "pass", pass);
    char b[8];
    snprintf(b, sizeof(b), "%02X", (unsigned)lcd_cfg.madctl);
    jb_str(&j, "madctl", b);
    jb_str(&j, "orient", orient_name(lcd_cfg.madctl));
    jb_u32(&j, "w", lcd_width());
    jb_u32(&j, "h", lcd_height());
    jb_u32(&j, "xoff", lcd_cfg.xoff);
    jb_u32(&j, "yoff", lcd_cfg.yoff);
    jb_u32(&j, "xoff_max", lcd_xoff_max());
    jb_u32(&j, "yoff_max", lcd_yoff_max());
    jb_u32(&j, "bgr", lcd_cfg.bgr);
    jb_u32(&j, "invert", lcd_cfg.invert);
    jb_str(&j, "gamma", lcd_gamma_name(lcd_cfg.gamma_id));
    jb_u32(&j, "spi_div", lcd_cfg.spi_div);
    jb_u32(&j, "spi_hz", lcd_spi_hz());
    jb_hex32(&j, "spi1_cr1", cr1);
    jb_bool(&j, "bidi_tx_only", bidi);
    jb_bool(&j, "pa5_af5", sck_ok);
    jb_bool(&j, "pa7_af5", mosi_ok);
    jb_bool(&j, "pa6_not_spi", pa6_not_spi);
    jb_u32(&j, "pa6_mode", m6);
    jb_u32(&j, "pa6_af", af6);
    jb_u32(&j, "gpio_speed", lcd_gpio_speed());
    jb_u32(&j, "fill_us", cyc / (SystemCoreClock / 1000000u));
    jb_end(&j);
    jb_emit(&j, "@R ");

    /* Screen: same reference picture as the menu "Display info". */
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    gfx_fill(COLOR_BLACK);
    gfx_rect(0, 0, w, h, COLOR_WHITE);
    lcd_fill_rect(1, 1, 6, 6, COLOR_RED);
    lcd_fill_rect((int16_t)(w - 7), 1, 6, 6, COLOR_GREEN);
    lcd_fill_rect(1, (int16_t)(h - 7), 6, 6, COLOR_BLUE);
    lcd_fill_rect((int16_t)(w - 7), (int16_t)(h - 7), 6, 6, COLOR_YELLOW);
    int16_t y = 14;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "MADCTL %02X %ux%u", (unsigned)lcd_cfg.madctl,
              (unsigned)w, (unsigned)h); y += 10;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "XOFF %u YOFF %u", (unsigned)lcd_cfg.xoff,
              (unsigned)lcd_cfg.yoff); y += 10;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "SPI1 %" PRIu32 " kHz", lcd_spi_hz() / 1000u); y += 10;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "fill %" PRIu32 " us", cyc / (SystemCoreClock / 1000000u));
    y += 10;
    gfx_text(12, y, pass ? "regs OK" : "regs FAIL", pass ? COLOR_GREEN : COLOR_RED, COLOR_BLACK, 1);
    return pass ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ display.cal */

static void cal_draw(const char *orient)
{
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    lcd_clear_gddram(COLOR_MAGENTA);           /* anything not covered stays magenta */
    lcd_fill_rect(0, 0, w, h, COLOR_BLACK);
    gfx_rect(0, 0, w, h, COLOR_WHITE);         /* the 1 px border under test */
    lcd_fill_rect(1, 1, 6, 6, COLOR_RED);
    lcd_fill_rect((int16_t)(w - 7), 1, 6, 6, COLOR_GREEN);
    lcd_fill_rect(1, (int16_t)(h - 7), 6, 6, COLOR_BLUE);
    lcd_fill_rect((int16_t)(w - 7), (int16_t)(h - 7), 6, 6, COLOR_YELLOW);
    gfx_text(8, 2, "TL", COLOR_RED, COLOR_BLACK, 1);
    gfx_text((int16_t)(w - 20), 2, "TR", COLOR_GREEN, COLOR_BLACK, 1);
    gfx_text(8, (int16_t)(h - 9), "BL", COLOR_BLUE, COLOR_BLACK, 1);
    gfx_text((int16_t)(w - 20), (int16_t)(h - 9), "BR", COLOR_YELLOW, COLOR_BLACK, 1);
    text_c(2, "top", COLOR_GRAY, COLOR_BLACK, 1);
    text_c((int16_t)(h - 9), "bottom", COLOR_GRAY, COLOR_BLACK, 1);
    gfx_text(2, (int16_t)(h / 2 - 3), "L", COLOR_GRAY, COLOR_BLACK, 1);
    gfx_text((int16_t)(w - 7), (int16_t)(h / 2 - 3), "R", COLOR_GRAY, COLOR_BLACK, 1);

    /* centre cross */
    gfx_hline((int16_t)(w / 2 - 6), (int16_t)(h / 2), 13, COLOR_WHITE);
    gfx_vline((int16_t)(w / 2), (int16_t)(h / 2 - 6), 13, COLOR_WHITE);

    char b[40];
    int16_t y = 13;
    snprintf(b, sizeof(b), "CAL %s MADCTL %02X", orient, (unsigned)lcd_cfg.madctl);
    text_c(y, b, COLOR_CYAN, COLOR_BLACK, 1); y += 10;
    snprintf(b, sizeof(b), "x%u y%u", (unsigned)lcd_cfg.xoff, (unsigned)lcd_cfg.yoff);
    text_c(y, b, COLOR_YELLOW, COLOR_BLACK, 2); y += 18;
    snprintf(b, sizeof(b), "max x%u y%u", (unsigned)lcd_xoff_max(), (unsigned)lcd_yoff_max());
    text_c(y, b, COLOR_GRAY, COLOR_BLACK, 1);

    y = (int16_t)(h / 2 + 10);
    text_c(y, "Need: white line on", COLOR_WHITE, COLOR_BLACK, 1); y += 9;
    text_c(y, "ALL 4 edges, 4 corner", COLOR_WHITE, COLOR_BLACK, 1); y += 9;
    text_c(y, "squares, no magenta", COLOR_WHITE, COLOR_BLACK, 1); y += 11;
    text_c(y, "Joy=move A=save B=quit", COLOR_CYAN, COLOR_BLACK, 1);
}

/* pass: 1 / 0, or -1 = progress event without a verdict */
static void cal_emit(const char *name, const char *orient, const char *action, int pass)
{
    res_printf("{\"t\":\"cal\",\"suite\":\"display.cal\",\"name\":\"%s\",\"orient\":\"%s\","
               "\"action\":\"%s\"%s,\"madctl\":\"%02X\",\"xoff\":%u,\"yoff\":%u,"
               "\"xoff_max\":%u,\"yoff_max\":%u,\"w\":%u,\"h\":%u,\"saved_to\":\"ram\"}",
               name, orient, action,
               pass < 0 ? "" : (pass ? ",\"pass\":true" : ",\"pass\":false"), (unsigned)lcd_cfg.madctl,
               (unsigned)lcd_cfg.xoff, (unsigned)lcd_cfg.yoff, (unsigned)lcd_xoff_max(),
               (unsigned)lcd_yoff_max(), (unsigned)lcd_width(), (unsigned)lcd_height());
}

/* display.cal [land|port] [madctl=0xA0] [x=N] [y=N] [t=<idle s>]
   Joystick nudges the window offsets live (LEFT/RIGHT = x -/+, UP/DOWN =
   y -/+; on the 0xA0 landscape this moves the picture in that direction);
   A saves into lcd_cfg + g_hwcal (RAM only), B cancels; idle timeout
   (default 300 s, restarted by every press) => abort. */
suite_verdict_t suite_display_cal(int argc, char **argv)
{
    bool port = suite_has_word(argc, argv, "port");
    uint32_t idle_ms = suite_arg_u32(argc, argv, "t", 300u) * 1000u;
    lcd_saved_t saved;
    lcd_save(&saved);

    if (port) {
        lcd_cfg.madctl = g_hwcal.madctl_port;
        lcd_cfg.xoff = g_hwcal.xoff_port;
        lcd_cfg.yoff = g_hwcal.yoff_port;
    }
    lcd_cfg.madctl = (uint8_t)suite_arg_u32(argc, argv, "madctl", lcd_cfg.madctl);
    lcd_apply_cfg(); /* geometry follows MV */
    lcd_cfg.xoff = (uint8_t)suite_arg_u32(argc, argv, "x", lcd_cfg.xoff);
    lcd_cfg.yoff = (uint8_t)suite_arg_u32(argc, argv, "y", lcd_cfg.yoff);
    if (lcd_cfg.xoff > lcd_xoff_max()) lcd_cfg.xoff = lcd_xoff_max();
    if (lcd_cfg.yoff > lcd_yoff_max()) lcd_cfg.yoff = lcd_yoff_max();
    const char *orient = orient_name(lcd_cfg.madctl);
    uint8_t x0 = lcd_cfg.xoff, y0 = lcd_cfg.yoff;

    char id[24];
    snprintf(id, sizeof(id), "display_cal_%s", orient);
    static const char *const answers[] = {"joystick=move", "A=save", "B=cancel", NULL};
    ui_prompt_emit(id, "Display offset calibration: move the picture with the joystick until the "
                       "1px white border is visible on all 4 edges and no magenta shows. A=save, B=cancel",
                   answers, idle_ms);
    cal_emit("start", orient, "start", -1);
    cal_draw(orient);
    input_flush();

    suite_verdict_t v;
    for (;;) {
        int b = wait_press(id, idle_ms);
        if (b == -2) {
            ui_answer_emit(id, "abort");
            v = SUITE_ABORT;
            break;
        }
        if (b == -1) {
            ui_answer_emit(id, "timeout");
            res_printf("{\"t\":\"skip\",\"suite\":\"display.cal\",\"reason\":\"timeout\"}");
            v = SUITE_ABORT;
            break;
        }
        if (b == BTN_A) {
            ui_answer_emit(id, "ok");
            if (lcd_cfg.madctl & LCD_MADCTL_MV) {
                g_hwcal.madctl_land = lcd_cfg.madctl;
                g_hwcal.xoff_land = lcd_cfg.xoff;
                g_hwcal.yoff_land = lcd_cfg.yoff;
            } else {
                g_hwcal.madctl_port = lcd_cfg.madctl;
                g_hwcal.xoff_port = lcd_cfg.xoff;
                g_hwcal.yoff_port = lcd_cfg.yoff;
            }
            cal_emit("offset", orient, "save", 1);
            v = SUITE_PASS;
            break;
        }
        if (b == BTN_B) {
            ui_answer_emit(id, "fail");
            cal_emit("offset", orient, "cancel", 0);
            lcd_cfg.xoff = x0;
            lcd_cfg.yoff = y0;
            v = SUITE_FAIL;
            break;
        }
        bool moved = true;
        switch (b) {
        case BTN_LEFT: if (lcd_cfg.xoff > 0u) lcd_cfg.xoff--; else moved = false; break;
        case BTN_RIGHT: if (lcd_cfg.xoff < lcd_xoff_max()) lcd_cfg.xoff++; else moved = false; break;
        case BTN_UP: if (lcd_cfg.yoff > 0u) lcd_cfg.yoff--; else moved = false; break;
        case BTN_DOWN: if (lcd_cfg.yoff < lcd_yoff_max()) lcd_cfg.yoff++; else moved = false; break;
        default: moved = false; break;
        }
        if (moved) {
            cal_draw(orient);
            cal_emit("nudge", orient, "nudge", -1);
        }
        continue;
    }

    /* Keep the result for the session: landscape values become the live
       config; a portrait calibration only updates g_hwcal. */
    lcd_cfg_t result = lcd_cfg;
    lcd_restore(&saved);
    if (v == SUITE_PASS && (result.madctl & LCD_MADCTL_MV)) {
        lcd_cfg.madctl = result.madctl;
        lcd_cfg.xoff = result.xoff;
        lcd_cfg.yoff = result.yoff;
        lcd_apply_cfg();
    }
    return v;
}

/* ================================================================ shared drawing */

static uint16_t s_row[LCD_GDDRAM_H];

typedef uint16_t (*pix_fn_t)(int x, int y, int w, int h);

/* Fills a logical rectangle row by row from a pixel function. */
static void draw_fn(int16_t x0, int16_t y0, int16_t w, int16_t h, pix_fn_t f)
{
    lcd_set_window((uint16_t)x0, (uint16_t)y0, (uint16_t)(x0 + w - 1), (uint16_t)(y0 + h - 1));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            s_row[x] = f(x, y, w, h);
        }
        lcd_push_pixels(s_row, (uint32_t)w);
    }
    lcd_end();
}

static const uint16_t s_bar_col[8] = {COLOR_WHITE, COLOR_YELLOW, COLOR_CYAN, COLOR_GREEN,
                                      COLOR_MAGENTA, COLOR_RED, COLOR_BLUE, COLOR_BLACK};
static const char *const s_bar_name[8] = {"W", "Y", "C", "G", "M", "R", "B", "K"};

static void draw_bars(int16_t y0, int16_t h)
{
    int16_t w = (int16_t)lcd_width();
    for (int i = 0; i < 8; i++) {
        int16_t x = (int16_t)(i * w / 8), x1 = (int16_t)((i + 1) * w / 8);
        lcd_fill_rect(x, y0, (int16_t)(x1 - x), h, s_bar_col[i]);
        uint16_t fg = (i < 4) ? COLOR_BLACK : COLOR_WHITE;
        gfx_text((int16_t)(x + (x1 - x) / 2 - 2), (int16_t)(y0 + h / 2 - 3), s_bar_name[i], fg, s_bar_col[i], 1);
    }
}

/* "Look and answer" step on the current screen: @P, wait for A/B or a host
   answer, @A. */
static ui_answer_t look(const char *id, const char *text, const char *a_txt, const char *b_txt, uint32_t to_ms)
{
    const char *ans[3] = {a_txt, b_txt, NULL};
    ui_prompt_emit(id, text, ans, to_ms);
    input_flush();
    for (;;) {
        int b = wait_press(id, to_ms);
        ui_answer_t a;
        if (b == BTN_A) a = UI_ANS_OK;
        else if (b == BTN_B) a = UI_ANS_FAIL;
        else if (b == -1) a = UI_ANS_TIMEOUT;
        else if (b == -2) a = UI_ANS_ABORT;
        else continue;
        ui_answer_emit(id, ui_answer_str(a));
        return a;
    }
}

static void footer(const char *s)
{
    int16_t h = (int16_t)lcd_height();
    lcd_fill_rect(0, (int16_t)(h - 10), (int16_t)lcd_width(), 10, COLOR_BLACK);
    gfx_text(1, (int16_t)(h - 9), s, COLOR_WHITE, COLOR_BLACK, 1);
}

static uint32_t step_to(int argc, char **argv, uint32_t def_s)
{
    return suite_arg_u32(argc, argv, "step", def_s) * 1000u;
}

/* @R {"t":"cal","suite":..,"name":..[,"pass":..]<extra>,"saved_to":"ram"}; pass -1 = none */
static void cal_res(const char *suite, const char *name, const char *extra, int pass)
{
    res_printf("{\"t\":\"cal\",\"suite\":\"%s\",\"name\":\"%s\"%s%s,\"saved_to\":\"ram\"}", suite, name,
               pass < 0 ? "" : (pass ? ",\"pass\":true" : ",\"pass\":false"), extra);
}

/* ================================================================ display.orient */

static void orient_draw(int idx, int n, const char *phase)
{
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    lcd_clear_gddram(COLOR_MAGENTA);
    lcd_fill_rect(0, 0, w, h, COLOR_BLACK);
    gfx_rect(0, 0, w, h, COLOR_WHITE);
    gfx_text(3, 3, "TL", COLOR_RED, COLOR_BLACK, 2);
    gfx_text((int16_t)(w - 27), 3, "TR", COLOR_GREEN, COLOR_BLACK, 2);
    gfx_text(3, (int16_t)(h - 19), "BL", COLOR_BLUE, COLOR_BLACK, 2);
    gfx_text((int16_t)(w - 27), (int16_t)(h - 19), "BR", COLOR_YELLOW, COLOR_BLACK, 2);
    text_c(4, "^UP^", COLOR_WHITE, COLOR_BLACK, 1);
    char b[40];
    snprintf(b, sizeof(b), "%s %d/%d MADCTL %02X", phase, idx + 1, n, (unsigned)lcd_cfg.madctl);
    text_c((int16_t)(h / 2 - 20), b, COLOR_CYAN, COLOR_BLACK, 1);
    text_c((int16_t)(h / 2 - 6), "Read me: ABC 123", COLOR_WHITE, COLOR_BLACK, 1);
    text_c((int16_t)(h / 2 + 6), "L/R: next  A: this", COLOR_GRAY, COLOR_BLACK, 1);
    text_c((int16_t)(h / 2 + 16), "one  B: skip", COLOR_GRAY, COLOR_BLACK, 1);
}

/* display.orient [land|port] [t=<start s>] [step=<idle s>]: cycles the four
   MX/MY variants of each orientation (landscape = MV set, portrait = MV
   clear); the user picks the one where TL is top-left and the text reads
   normally. Offsets for every candidate come from lcd_offsets_for(). */
suite_verdict_t suite_display_orient(int argc, char **argv)
{
    if (!suite_user_start("display.orient",
                          "Orientation: for landscape, then portrait, pick (A) the variant where TL is "
                          "top-left and the text reads normally; L/R cycles. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = step_to(argc, argv, 120u);
    bool only_land = suite_has_word(argc, argv, "land"), only_port = suite_has_word(argc, argv, "port");
    lcd_saved_t saved;
    lcd_save(&saved);
    suite_verdict_t v = SUITE_PASS;
    bool picked_land = false;

    for (int phase = 0; phase < 2; phase++) {
        bool land = (phase == 0);
        if ((land && only_port) || (!land && only_land)) continue;
        uint8_t base = land ? LCD_MADCTL_MV : 0u;
        uint8_t cur = land ? g_hwcal.madctl_land : g_hwcal.madctl_port;
        uint8_t cand[4] = {0, LCD_MADCTL_MX, LCD_MADCTL_MY, LCD_MADCTL_MX | LCD_MADCTL_MY};
        int idx = 0;
        for (int i = 0; i < 4; i++) {
            cand[i] = (uint8_t)(cand[i] | base);
            if (cand[i] == (cur & (LCD_MADCTL_MX | LCD_MADCTL_MY | LCD_MADCTL_MV))) idx = i;
        }
        const char *id = land ? "orient_land" : "orient_port";
        static const char *const ans[] = {"L/R=cycle", "A=this one", "B=skip", NULL};
        ui_prompt_emit(id, land ? "Landscape: pick the variant with TL top-left and readable text"
                                : "Portrait: pick the variant with TL top-left and readable text",
                       ans, to);
        const char *end = "abort";
        for (;;) {
            lcd_cfg.madctl = cand[idx];
            lcd_offsets_for(cand[idx], &lcd_cfg.xoff, &lcd_cfg.yoff);
            lcd_apply_cfg();
            orient_draw(idx, 4, land ? "LAND" : "PORT");
            res_printf("{\"t\":\"orient_show\",\"suite\":\"display.orient\",\"orient\":\"%s\",\"madctl\":\"%02X\","
                       "\"xoff\":%u,\"yoff\":%u}",
                       land ? "land" : "port", (unsigned)lcd_cfg.madctl, (unsigned)lcd_cfg.xoff,
                       (unsigned)lcd_cfg.yoff);
            int b = wait_press(id, to);
            if (b == BTN_LEFT || b == BTN_UP) { idx = (idx + 3) % 4; continue; }
            if (b == BTN_RIGHT || b == BTN_DOWN) { idx = (idx + 1) % 4; continue; }
            if (b == BTN_A) { end = "ok"; break; }
            if (b == BTN_B) { end = "fail"; break; }
            if (b == -1) { end = "timeout"; break; }
            if (b == -2) { end = "abort"; break; }
        }
        ui_answer_emit(id, end);
        if (!strcmp(end, "ok")) {
            char ex[72];
            snprintf(ex, sizeof(ex), ",\"orient\":\"%s\",\"madctl\":\"%02X\",\"xoff\":%u,\"yoff\":%u",
                     land ? "land" : "port", (unsigned)lcd_cfg.madctl, (unsigned)lcd_cfg.xoff,
                     (unsigned)lcd_cfg.yoff);
            cal_res("display.orient", id, ex, 1);
            if (land) {
                g_hwcal.madctl_land = lcd_cfg.madctl;
                g_hwcal.xoff_land = lcd_cfg.xoff;
                g_hwcal.yoff_land = lcd_cfg.yoff;
                picked_land = true;
            } else {
                g_hwcal.madctl_port = lcd_cfg.madctl;
                g_hwcal.xoff_port = lcd_cfg.xoff;
                g_hwcal.yoff_port = lcd_cfg.yoff;
            }
        } else {
            res_printf("{\"t\":\"skip\",\"suite\":\"display.orient\",\"name\":\"%s\",\"reason\":\"%s\"}", id, end);
            if (!strcmp(end, "abort") || !strcmp(end, "timeout")) {
                v = SUITE_ABORT;
                break;
            }
            v = SUITE_FAIL;
        }
    }
    lcd_restore(&saved);
    if (picked_land && g_hwcal.madctl_land != lcd_cfg.madctl) {
        lcd_cfg.madctl = g_hwcal.madctl_land;
        lcd_cfg.xoff = g_hwcal.xoff_land;
        lcd_cfg.yoff = g_hwcal.yoff_land;
        lcd_apply_cfg();
        lcd_clear_gddram(COLOR_BLACK);
    }
    return v;
}

/* ================================================================ display.rgb */

/* Full-screen red, green, blue with labels. If any colour is wrong, BGR is
   toggled and the three screens repeat once. */
suite_verdict_t suite_display_rgb(int argc, char **argv)
{
    if (!suite_user_start("display.rgb",
                          "RGB/BGR: three full-screen colours follow. Answer A if the screen colour matches "
                          "the word, B if not. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = step_to(argc, argv, 60u);
    lcd_saved_t saved;
    lcd_save(&saved);
    static const struct {
        uint16_t c;
        const char *name;
        uint16_t fg;
    } cols[3] = {{COLOR_RED, "RED", COLOR_WHITE}, {COLOR_GREEN, "GREEN", COLOR_BLACK}, {COLOR_BLUE, "BLUE", COLOR_WHITE}};
    int good_bgr = -1;
    bool stop = false;
    for (int round = 0; round < 2 && good_bgr < 0 && !stop; round++) {
        if (round == 1) {
            lcd_cfg.bgr ^= 1u;
            lcd_apply_cfg();
        }
        bool all = true;
        for (int i = 0; i < 3; i++) {
            gfx_fill(cols[i].c);
            text_c((int16_t)(lcd_height() / 2 - 16), cols[i].name, cols[i].fg, cols[i].c, 3);
            char sub[24];
            snprintf(sub, sizeof(sub), "BGR=%u  A=yes B=no", (unsigned)lcd_cfg.bgr);
            text_c((int16_t)(lcd_height() / 2 + 16), sub, cols[i].fg, cols[i].c, 1);
            char id[24], txt[64];
            snprintf(id, sizeof(id), "rgb_bgr%u_%s", (unsigned)lcd_cfg.bgr, cols[i].name);
            snprintf(txt, sizeof(txt), "Is the whole screen %s? (BGR=%u)", cols[i].name, (unsigned)lcd_cfg.bgr);
            ui_answer_t a = look(id, txt, "A=yes", "B=no", to);
            res_printf("{\"t\":\"look\",\"suite\":\"display.rgb\",\"name\":\"%s\",\"bgr\":%u,\"answer\":\"%s\"}",
                       cols[i].name, (unsigned)lcd_cfg.bgr, ui_answer_str(a));
            if (a == UI_ANS_ABORT || a == UI_ANS_TIMEOUT) {
                stop = true;
                break;
            }
            all = all && (a == UI_ANS_OK);
        }
        if (!stop && all) good_bgr = lcd_cfg.bgr;
    }
    lcd_restore(&saved);
    if (stop) return SUITE_ABORT;
    if (good_bgr >= 0) {
        g_hwcal.bgr = (uint8_t)good_bgr;
        lcd_cfg.bgr = (uint8_t)good_bgr;
        lcd_apply_cfg();
        char ex[16];
        snprintf(ex, sizeof(ex), ",\"bgr\":%d", good_bgr);
        cal_res("display.rgb", "bgr", ex, 1);
        return SUITE_PASS;
    }
    cal_res("display.rgb", "bgr", ",\"bgr\":null", 0);
    return SUITE_FAIL;
}

/* ================================================================ display.gamma */

static uint16_t px_ramp4(int x, int y, int w, int h)
{
    int band = y * 4 / h;
    int lvl = x * 32 / w; /* 32 steps */
    uint8_t v = (uint8_t)(lvl * 255 / 31);
    switch (band) {
    case 0: return RGB565(v, v, v);
    case 1: return RGB565(v, 0, 0);
    case 2: return RGB565(0, v, 0);
    default: return RGB565(0, 0, v);
    }
}

static void draw_gamma_screen(void)
{
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    gfx_fill(COLOR_BLACK);
    char b[32];
    snprintf(b, sizeof(b), "gamma %s (%u/%u)", lcd_gamma_name(lcd_cfg.gamma_id), (unsigned)lcd_cfg.gamma_id + 1u,
             (unsigned)LCD_GAMMA_COUNT);
    gfx_text(1, 2, b, COLOR_YELLOW, COLOR_BLACK, 1);
    draw_fn(0, 12, w, (int16_t)(h - 22), px_ramp4);
    footer("L/R switch A=best B=skip");
}

/* 32-step grey/R/G/B ramps under each E0/E1 table (default / retroport /
   sitronix); LEFT/RIGHT switches, A picks the most even one. */
suite_verdict_t suite_display_gamma(int argc, char **argv)
{
    if (!suite_user_start("display.gamma",
                          "Gamma: 32-step grey/R/G/B ramps. L/R switches the gamma table, A picks the one "
                          "with the most even steps. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = step_to(argc, argv, 180u);
    lcd_saved_t saved;
    lcd_save(&saved);
    static const char *const ans[] = {"L/R=switch table", "A=best", "B=skip", NULL};
    ui_prompt_emit("gamma_pick", "Pick the gamma table with the most even 32-step ramps", ans, to);
    const char *end = "abort";
    uint8_t g = lcd_cfg.gamma_id;
    for (;;) {
        lcd_cfg.gamma_id = g;
        lcd_apply_cfg(); /* gamma 0 after a table => full re-init */
        draw_gamma_screen();
        res_printf("{\"t\":\"gamma_show\",\"suite\":\"display.gamma\",\"gamma\":\"%s\"}", lcd_gamma_name(g));
        int b = wait_press("gamma_pick", to);
        if (b == BTN_LEFT || b == BTN_UP) { g = (uint8_t)((g + LCD_GAMMA_COUNT - 1u) % LCD_GAMMA_COUNT); continue; }
        if (b == BTN_RIGHT || b == BTN_DOWN) { g = (uint8_t)((g + 1u) % LCD_GAMMA_COUNT); continue; }
        if (b == BTN_A) { end = "ok"; break; }
        if (b == BTN_B) { end = "fail"; break; }
        if (b == -1) { end = "timeout"; break; }
        if (b == -2) { end = "abort"; break; }
    }
    ui_answer_emit("gamma_pick", end);
    lcd_restore(&saved);
    if (!strcmp(end, "ok")) {
        g_hwcal.gamma_id = g;
        lcd_cfg.gamma_id = g;
        lcd_apply_cfg();
        lcd_clear_gddram(COLOR_BLACK);
        char ex[48];
        snprintf(ex, sizeof(ex), ",\"gamma\":\"%s\",\"gamma_id\":%u", lcd_gamma_name(g), (unsigned)g);
        cal_res("display.gamma", "gamma", ex, 1);
        return SUITE_PASS;
    }
    res_printf("{\"t\":\"skip\",\"suite\":\"display.gamma\",\"reason\":\"%s\"}", end);
    return !strcmp(end, "fail") ? SUITE_FAIL : SUITE_ABORT;
}

/* ================================================================ display.invert */

static void draw_invert_screen(void)
{
    int16_t w = (int16_t)lcd_width();
    gfx_fill(COLOR_BLACK);
    char b[32];
    snprintf(b, sizeof(b), "INVERSION %s", lcd_cfg.invert ? "ON (21h)" : "OFF (20h)");
    text_c(2, b, COLOR_YELLOW, COLOR_BLACK, 1);
    draw_bars(12, 40);
    lcd_fill_rect(0, 56, (int16_t)(w / 2), 30, COLOR_WHITE);
    gfx_text(4, 67, "WHITE", COLOR_BLACK, COLOR_WHITE, 1);
    gfx_text((int16_t)(w / 2 + 4), 67, "BLACK", COLOR_WHITE, COLOR_BLACK, 1);
    gfx_rect((int16_t)(w / 2), 56, (int16_t)(w / 2), 30, COLOR_GRAY);
    footer("labels right? A=yes B=no");
}

/* The same bars with INVOFF and INVON; the setting where white is white and
   the bar letters match is the correct one. */
suite_verdict_t suite_display_invert(int argc, char **argv)
{
    if (!suite_user_start("display.invert",
                          "Inversion: colour bars W Y C G M R B K + white/black boxes, shown with INVOFF and "
                          "INVON. Answer A if colours match labels. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = step_to(argc, argv, 60u);
    lcd_saved_t saved;
    lcd_save(&saved);
    ui_answer_t ans[2] = {UI_ANS_TIMEOUT, UI_ANS_TIMEOUT};
    bool stop = false;
    for (int inv = 0; inv < 2; inv++) {
        lcd_cfg.invert = (uint8_t)inv;
        lcd_apply_cfg();
        draw_invert_screen();
        char id[16];
        snprintf(id, sizeof(id), "invert_%d", inv);
        ans[inv] = look(id, inv ? "INVON: do colours match the labels?" : "INVOFF: do colours match the labels?",
                        "A=yes", "B=no", to);
        res_printf("{\"t\":\"look\",\"suite\":\"display.invert\",\"name\":\"%s\",\"answer\":\"%s\"}",
                   inv ? "invon" : "invoff", ui_answer_str(ans[inv]));
        if (ans[inv] == UI_ANS_ABORT || ans[inv] == UI_ANS_TIMEOUT) {
            stop = true;
            break;
        }
    }
    lcd_restore(&saved);
    if (stop) return SUITE_ABORT;
    int good = (ans[0] == UI_ANS_OK && ans[1] != UI_ANS_OK) ? 0
             : (ans[1] == UI_ANS_OK && ans[0] != UI_ANS_OK) ? 1 : -1;
    char ex[24];
    snprintf(ex, sizeof(ex), ",\"invert\":%s", good < 0 ? "null" : (good ? "1" : "0"));
    cal_res("display.invert", "invert", ex, good >= 0);
    if (good >= 0) {
        g_hwcal.invert = (uint8_t)good;
        lcd_cfg.invert = (uint8_t)good;
        lcd_apply_cfg();
    }
    return good >= 0 ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ bars / gradient */

suite_verdict_t suite_display_bars(int argc, char **argv)
{
    if (!suite_user_start("display.bars", "Colour bars W Y C G M R B K: check they match. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = step_to(argc, argv, 60u);
    gfx_fill(COLOR_BLACK);
    draw_bars(0, (int16_t)(lcd_height() - 10));
    footer("match labels? A=yes B=no");
    ui_answer_t a = look("bars", "Do the 8 bars match their letters W Y C G M R B K, all clean?", "A=yes",
                         "B=no", to);
    res_printf("{\"t\":\"test\",\"suite\":\"display.bars\",\"name\":\"bars\",\"pass\":%s,\"answer\":\"%s\"}",
               a == UI_ANS_OK ? "true" : "false", ui_answer_str(a));
    lcd_clear_gddram(COLOR_BLACK);
    if (a == UI_ANS_ABORT || a == UI_ANS_TIMEOUT) return SUITE_ABORT;
    return a == UI_ANS_OK ? SUITE_PASS : SUITE_FAIL;
}

static uint16_t px_565(int x, int y, int w, int h)
{
    int band = y * 4 / h;
    switch (band) {
    case 0: return (uint16_t)((x * 32 / w) << 11); /* R: 32 levels */
    case 1: return (uint16_t)((x * 64 / w) << 5);  /* G: 64 levels */
    case 2: return (uint16_t)(x * 32 / w);         /* B: 32 levels */
    default: {
        uint16_t r = (uint16_t)(x * 32 / w), g = (uint16_t)(x * 64 / w);
        return (uint16_t)((r << 11) | (g << 5) | r); /* grey */
    }
    }
}

suite_verdict_t suite_display_gradient(int argc, char **argv)
{
    if (!suite_user_start("display.gradient",
                          "RGB565 gradients R(32) G(64) B(32) grey. Look for missing/reversed steps or "
                          "colour casts. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = step_to(argc, argv, 60u);
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    gfx_fill(COLOR_BLACK);
    draw_fn(0, 0, w, (int16_t)(h - 10), px_565);
    footer("smooth, no jumps? A=yes B=no");
    ui_answer_t a = look("gradient", "Are the 4 gradients smooth and monotonic (no missing or reversed steps)?",
                         "A=yes", "B=no", to);
    res_printf("{\"t\":\"test\",\"suite\":\"display.gradient\",\"name\":\"gradient565\",\"pass\":%s,"
               "\"answer\":\"%s\"}",
               a == UI_ANS_OK ? "true" : "false", ui_answer_str(a));
    lcd_clear_gddram(COLOR_BLACK);
    if (a == UI_ANS_ABORT || a == UI_ANS_TIMEOUT) return SUITE_ABORT;
    return a == UI_ANS_OK ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ display.spi */

static uint16_t px_stress(int x, int y, int w, int h)
{
    if (y < h / 3) return ((x ^ y) & 1) ? COLOR_WHITE : COLOR_BLACK; /* 1 px checker */
    if (y < 2 * h / 3) return (((x >> 3) ^ (y >> 3)) & 1) ? COLOR_RED : COLOR_BLUE;
    return (uint16_t)(((x * 32 / w) << 11) | ((y & 63) << 5) | (31 - x * 32 / w));
}

typedef struct {
    uint16_t div;
    bool f16;
    uint32_t speed;
} bus_t;

static void bus_apply(const bus_t *b)
{
    lcd_set_spi_div(b->div);
    lcd_set_frame16(b->f16);
    lcd_set_gpio_speed(b->speed);
}

/* Draws the stress picture + the label of the TEST setting (identical in
   both phases). Returns the CPU cycles of the picture transfer. */
static uint32_t draw_stress(const bus_t *test)
{
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    uint32_t c0 = time_cycles();
    draw_fn(0, 0, w, (int16_t)(h - 20), px_stress);
    uint32_t cyc = time_cycles() - c0;
    char b[32];
    snprintf(b, sizeof(b), "%2" PRIu32 "MHz %2ub %s", (HAL_RCC_GetPCLK2Freq() / test->div) / 1000000u,
             test->f16 ? 16u : 8u, test->speed == 3u ? "VHI" : "HI ");
    gfx_text_pad(1, (int16_t)(h - 19), b, COLOR_YELLOW, COLOR_BLACK, 1, w);
    gfx_text_pad(1, (int16_t)(h - 9), "A=stable B=flicker", COLOR_WHITE, COLOR_BLACK, 1, w);
    return cyc;
}

/* display.spi [div=2|4|8] [bits=8|16] [speed=2|3] [t=] [step=<s per setting>]
   Per bus setting the reference picture is drawn at 6 MHz (div 16, 8-bit,
   GPIO very high), then the identical picture at the test setting,
   alternating every second. A clean transfer does not change the picture;
   corrupted pixels or commands flicker. A = stable, B = flicker. Default
   matrix: div 8,4,2 (12/24/48 MHz at PCLK2 96 MHz) x 8/16-bit x GPIO
   very-high(3)/high(2). The controller is re-initialised before every
   setting and at the end. */
suite_verdict_t suite_display_spi(int argc, char **argv)
{
    if (!suite_user_start("display.spi",
                          "SPI flicker A/B: a picture alternates every second between 6 MHz and a test "
                          "clock. A = picture is stable, B = it flickers/changes. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = step_to(argc, argv, 30u);
    uint32_t only_div = suite_arg_u32(argc, argv, "div", 0u);
    uint32_t only_bits = suite_arg_u32(argc, argv, "bits", 0u);
    uint32_t only_speed = suite_arg_u32(argc, argv, "speed", 0u);
    lcd_saved_t saved;
    lcd_save(&saved);
    const bus_t ref = {16, false, 3};
    static const uint16_t divs[3] = {8, 4, 2};
    uint16_t best_all = 0, best_any = 0; /* smallest div with all / any variant stable */
    suite_verdict_t v = SUITE_PASS;
    bool stop = false;

    for (int d = 0; d < 3 && !stop; d++) {
        if (only_div && only_div != divs[d]) continue;
        bool all_ok = true, any_ok = false;
        for (int bits = 0; bits < 2 && !stop; bits++) {
            if (only_bits && only_bits != (bits ? 16u : 8u)) continue;
            for (int sp = 0; sp < 2 && !stop; sp++) {
                uint32_t speed = sp ? 2u : 3u;
                if (only_speed && only_speed != speed) continue;
                bus_t t = {divs[d], bits != 0, speed};
                uint32_t mhz = HAL_RCC_GetPCLK2Freq() / divs[d] / 1000000u;
                char id[32], txt[96];
                snprintf(id, sizeof(id), "spi_%" PRIu32 "mhz_%ub_%s", mhz, bits ? 16u : 8u,
                         speed == 3u ? "vhi" : "hi");
                snprintf(txt, sizeof(txt), "SPI %" PRIu32 " MHz %u-bit GPIO %s: is the picture stable?", mhz,
                         bits ? 16u : 8u, speed == 3u ? "very-high" : "high");
                static const char *const ans[] = {"A=stable", "B=flicker", NULL};
                ui_prompt_emit(id, txt, ans, to);
                input_flush();
                uint32_t t0 = HAL_GetTick(), sw = HAL_GetTick(), cyc_test = 0;
                bool phase_test = false;
                const char *end = "abort";
                bus_apply(&ref);
                lcd_init(); /* clean controller state for every setting */
                draw_stress(&t);
                for (;;) {
                    sys_service();
                    if (HAL_GetTick() - sw >= 1000u) {
                        sw = HAL_GetTick();
                        phase_test = !phase_test;
                        bus_apply(phase_test ? &t : &ref);
                        uint32_t c = draw_stress(&t);
                        if (phase_test) cyc_test = c;
                    }
                    input_event_t ev;
                    ui_answer_t ha;
                    if (ui_host_answer_take(id, &ha)) { end = ui_answer_str(ha); break; }
                    if (input_pop(&ev) && ev.pressed) {
                        if (ev.button == BTN_A) { end = "ok"; break; }
                        if (ev.button == BTN_B) { end = "fail"; break; }
                    }
                    if (abort_requested()) { end = "abort"; break; }
                    if (HAL_GetTick() - t0 >= to) { end = "timeout"; break; }
                }
                bus_apply(&ref);
                ui_answer_emit(id, end);
                bool ok = !strcmp(end, "ok");
                res_printf("{\"t\":\"test\",\"suite\":\"display.spi\",\"name\":\"%s\",\"pass\":%s,\"answer\":\"%s\","
                           "\"spi_hz\":%" PRIu32 ",\"bits\":%u,\"gpio_speed\":%" PRIu32 ",\"frame_us\":%" PRIu32 "}",
                           id, ok ? "true" : "false", end, HAL_RCC_GetPCLK2Freq() / divs[d], bits ? 16u : 8u, speed,
                           cyc_test / (SystemCoreClock / 1000000u));
                if (!strcmp(end, "abort") || !strcmp(end, "timeout")) {
                    stop = true;
                    v = SUITE_ABORT;
                    break;
                }
                all_ok = all_ok && ok;
                any_ok = any_ok || ok;
            }
        }
        if (stop) break;
        if (all_ok) best_all = divs[d];
        if (any_ok) best_any = divs[d];
        /* flicker above the default clock is the expected finding of a limit test;
           only the default 12 MHz (div 8) setting failing makes the suite FAIL */
        if (!all_ok && divs[d] == 8u && v == SUITE_PASS) v = SUITE_FAIL;
    }
    lcd_restore(&saved);
    lcd_init();
    if (best_all || best_any) {
        if (best_all) g_hwcal.spi1_div_ok = (uint8_t)best_all;
        if (best_any) g_hwcal.spi1_div_max = (uint8_t)best_any;
        char ex[80];
        snprintf(ex, sizeof(ex), ",\"div_all_ok\":%u,\"div_any_ok\":%u,\"pclk2\":%" PRIu32, (unsigned)best_all,
                 (unsigned)best_any, HAL_RCC_GetPCLK2Freq());
        cal_res("display.spi", "spi1_limit", ex, -1);
    }
    return v;
}

/* ================================================================ display.tear */

#define TW 160
#define TH 128
#define TFB ((uint16_t *)(void *)g_arena)

typedef struct { uint8_t b[3]; } frm_t;   /* FRMCTR1: RTNA, FPA, BPA */
/* ST7735S: Hz = 850 kHz / ((2*RTNA + 40) * (160 + FPA + BPA + 2)); 3rd entry = lcd_init default */
static const frm_t k_frm[] = {
    {{0x05, 0x3C, 0x3C}}, {{0x01, 0x36, 0x36}}, {{0x01, 0x2C, 0x2D}}, {{0x01, 0x1F, 0x20}}, {{0x00, 0x0F, 0x10}},
};
#define N_FRM ((int)(sizeof(k_frm) / sizeof(k_frm[0])))
static const uint16_t k_fps[3] = {30, 60, 0};   /* 0 = unlimited */

static bool s_tnative;

static uint32_t frm_hz_x10(const frm_t *f)
{
    return 8500000u / ((2u * (f->b[0] & 0xFu) + 40u) * (160u + (f->b[1] & 0x3Fu) + (f->b[2] & 0x3Fu) + 2u));
}

static inline void tpx(int lx, int ly, uint16_t c)
{
    if ((unsigned)lx < TW && (unsigned)ly < TH) TFB[s_tnative ? lx * TH + ly : ly * TW + lx] = c;
}

static void ttext(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += 6) {
        const uint8_t *g = font5x7_glyph(*s);
        for (int cx = 0; cx < 6; cx++) {
            for (int cy = 0; cy < 8; cy++) {
                bool on = cx < 5 && cy < 7 && ((g[cx] >> cy) & 1u);
                tpx(x + cx, y + cy, on ? fg : bg);
            }
        }
    }
}

static void tear_render(uint32_t bar_x)
{
    uint32_t *p = (uint32_t *)(void *)g_arena;
    const uint32_t bg = 0x20E420E4u;   /* dark, two pixels per word */
    for (uint32_t i = 0; i < TW * TH / 2u; i++) p[i] = bg;
    if (s_tnative) {
        uint32_t *q = (uint32_t *)(void *)&TFB[bar_x * TH];
        for (uint32_t i = 0; i < 8u * TH / 2u; i++) q[i] = 0xFFFFFFFFu;
    } else {
        for (int y = 0; y < TH; y++) {
            uint32_t *q = (uint32_t *)(void *)&TFB[y * TW + bar_x];   /* bar_x is even */
            q[0] = q[1] = q[2] = q[3] = 0xFFFFFFFFu;
        }
    }
}

static void tear_push(void)
{
    if (s_tnative) lcd_set_window(0, 0, TH - 1u, TW - 1u);
    else lcd_set_window(0, 0, TW - 1u, TH - 1u);
    spi1_dff(true);
    dma3_start(TFB, TW * TH, true, true, false);
    dma3_finish();
    lcd_end();
    lcd_set_frame16(false);
}

static void tear_order(bool native, const lcd_cfg_t *land)
{
    s_tnative = native;
    lcd_cfg.madctl = land->madctl;
    lcd_cfg.xoff = land->xoff;
    lcd_cfg.yoff = land->yoff;
    if (native) {
        /* MV=0 portrait addressing: stream rows run along the panel scan lines. MY=1 (not MX):
           MX alone showed the picture rotated 180 deg on the real panel (user, 2026-11-17). */
        lcd_cfg.madctl = LCD_MADCTL_MY;
        lcd_offsets_for(lcd_cfg.madctl, &lcd_cfg.xoff, &lcd_cfg.yoff);
    }
    lcd_apply_cfg();
}

static void tear_emit(const char *name, bool native, int fm, int fi, uint32_t fps_x10)
{
    const frm_t *f = &k_frm[fi];
    uint32_t hz = frm_hz_x10(f);
    res_printf("{\"t\":\"%s\",\"suite\":\"display.tear\",\"name\":\"tear\",\"order\":\"%s\",\"madctl\":%u,"
               "\"fps_target\":%u,\"fps\":%" PRIu32 ".%" PRIu32 ",\"frmctr1\":\"%02X %02X %02X\","
               "\"panel_hz\":%" PRIu32 ".%" PRIu32 "}",
               name, native ? "native" : "land_mv", (unsigned)lcd_cfg.madctl, (unsigned)k_fps[fm], fps_x10 / 10u,
               fps_x10 % 10u, f->b[0], f->b[1], f->b[2], hz / 10u, hz % 10u);
}

static void tear_bench(void)
{
    uint16_t *src = TFB, *dst = TFB + TW * TH;
    for (uint32_t i = 0; i < TW * TH; i++) src[i] = (uint16_t)i;
    uint32_t c0 = time_cycles();
    for (int ly = 0; ly < TH; ly++)
        for (int lx = 0; lx < 80; lx++) dst[lx * TH + ly] = src[ly * TW + lx];
    uint32_t rot_half = time_cycles() - c0;
    uint8_t *s8 = g_arena;
    uint16_t *pal = (uint16_t *)(void *)(g_arena + TW * TH);
    uint16_t *d16 = (uint16_t *)(void *)(g_arena + TW * TH + 512);
    for (uint32_t i = 0; i < TW * TH; i++) s8[i] = (uint8_t)i;
    for (uint32_t i = 0; i < 256; i++) pal[i] = (uint16_t)(i * 257u);
    c0 = time_cycles();
    for (int ly = 0; ly < TH; ly++) {
        const uint8_t *l = &s8[ly * TW];
        uint16_t *d = &d16[ly];
        for (int lx = 0; lx < TW; lx++) d[lx * TH] = pal[l[lx]];
    }
    uint32_t conv = time_cycles() - c0;
    uint32_t cpu = SystemCoreClock / 1000000u;
    res_printf("{\"t\":\"bench\",\"suite\":\"display.tear\",\"name\":\"rotate\",\"rot565_full_cyc\":%" PRIu32
               ",\"rot565_full_us\":%" PRIu32 ",\"conv8_rot_cyc\":%" PRIu32 ",\"conv8_rot_us\":%" PRIu32 "}",
               rot_half * 2u, rot_half * 2u / cpu, conv, conv / cpu);
}

/* display.tear [auto=1] [t=<s per mode in auto>]
   Full-frame 160x128 RGB565, 8 px white bar moving at 240 px/s, pushed with SPI1 DMA at 48 MHz.
   LEFT/RIGHT fps 30/60/max, UP/DOWN panel refresh (FRMCTR1 presets), CENTER transfer order
   (land MV = MADCTL 0xA0 writes vs native = panel scan direction), A = record as least tearing, B exit.
   auto=1 only measures the achieved fps per order x fps mode (and the rotation cost). */
suite_verdict_t suite_display_tear(int argc, char **argv)
{
    bool aut = suite_arg_u32(argc, argv, "auto", 0u) != 0u;
    if (!aut && !suite_user_start("display.tear",
                                  "Tearing finder: watch the moving bar. LEFT/RIGHT fps, UP/DOWN panel Hz, "
                                  "CENTER write order, A = least tearing, B = exit. A=start B=skip",
                                  argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t auto_ms = suite_arg_u32(argc, argv, "t", 3u) * 1000u;
    lcd_saved_t saved;
    lcd_save(&saved);
    lcd_cfg_t land = lcd_cfg;
    lcd_set_gpio_speed(3);
    lcd_set_spi_div(2);   /* 48 MHz */
    input_flush();
    int fm = 1, fi = 2, ord = 0;
    bool done = false;
    int auto_i = 0;
    suite_verdict_t v = SUITE_PASS;
    uint32_t fl_frames = 0, fl_t0 = HAL_GetTick(), cfg_frames = 0, cfg_t0 = fl_t0, fps_x10 = 0, rec_until = 0;
    char l1[24], l2[24];
    bool changed = true;

    while (!done) {
        if (changed) {
            changed = false;
            if (aut) { fm = auto_i % 3; ord = auto_i / 3; }
            tear_order(ord != 0, &land);
            lcd_set_frmctr1(k_frm[fi].b);
            cfg_frames = 0;
            cfg_t0 = HAL_GetTick();
            fl_frames = 0; fl_t0 = cfg_t0;
        }
        uint32_t t0 = time_us();
        uint32_t bar = (((t0 / 1000u) * 240u / 1000u) % ((TW - 8u) / 2u)) * 2u;
        tear_render(bar);
        uint32_t hz = frm_hz_x10(&k_frm[fi]);
        snprintf(l1, sizeof(l1), "%s %u.%uHz", ord ? "NATIVE" : "LAND MV", (unsigned)(hz / 10u), (unsigned)(hz % 10u));
        snprintf(l2, sizeof(l2), "T%s %u.%ufps", k_fps[fm] ? (k_fps[fm] == 30 ? "30" : "60") : "MAX",
                 (unsigned)(fps_x10 / 10u), (unsigned)(fps_x10 % 10u));
        ttext(1, 1, l1, COLOR_WHITE, COLOR_BLACK);
        ttext(1, 10, l2, COLOR_YELLOW, COLOR_BLACK);
        if ((int32_t)(rec_until - HAL_GetTick()) > 0) ttext(1, 119, "RECORDED", COLOR_GREEN, COLOR_BLACK);
        tear_push();
        fl_frames++;
        cfg_frames++;
        uint32_t now = HAL_GetTick();
        if (now - fl_t0 >= 1000u) {
            fps_x10 = fl_frames * 10000u / (now - fl_t0);
            fl_frames = 0;
            fl_t0 = now;
        }
        if (k_fps[fm]) {
            uint32_t period = 1000000u / k_fps[fm];
            while ((uint32_t)(time_us() - t0) < period) sys_service();
        }
        sys_service();
        if (abort_requested()) { v = SUITE_ABORT; break; }
        if (aut) {
            if (now - cfg_t0 >= auto_ms) {
                tear_emit("tear", ord != 0, fm, fi, cfg_frames * 10000u / (now - cfg_t0));
                if (++auto_i >= 6) done = true;
                changed = true;
            }
            continue;
        }
        input_event_t ev;
        while (input_pop(&ev)) {
            if (!ev.pressed) continue;
            int nfm = fm, nfi = fi, nord = ord;
            switch (ev.button) {
            case BTN_LEFT:   nfm = (fm + 2) % 3; break;
            case BTN_RIGHT:  nfm = (fm + 1) % 3; break;
            case BTN_UP:     nfi = (fi + 1) % N_FRM; break;
            case BTN_DOWN:   nfi = (fi + N_FRM - 1) % N_FRM; break;
            case BTN_CENTER: nord = !ord; break;
            case BTN_A:
                tear_emit("cal", ord != 0, fm, fi, fps_x10);
                rec_until = HAL_GetTick() + 1500u;
                break;
            case BTN_B: done = true; break;
            default: break;
            }
            if (nfm != fm || nfi != fi || nord != ord) {
                if (HAL_GetTick() - cfg_t0 >= 500u)
                    tear_emit("tear", ord != 0, fm, fi, cfg_frames * 10000u / (HAL_GetTick() - cfg_t0));
                fm = nfm; fi = nfi; ord = nord;
                changed = true;
            }
        }
    }
    if (aut) tear_bench();
    s_tnative = false;
    lcd_restore(&saved);
    lcd_init();
    return v;
}
