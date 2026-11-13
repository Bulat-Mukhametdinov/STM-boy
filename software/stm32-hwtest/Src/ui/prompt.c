/* Console screen for suites + @P/@A prompt plumbing. */

#include "ui.h"
#include "cmd.h"
#include "gfx.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

#define CON_TOP   22
#define CON_LINE  9

static int16_t s_con_y = CON_TOP;

void ui_con_begin(const char *title)
{
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    gfx_text_pad(2, 12, title, COLOR_CYAN, COLOR_BLACK, 1, (int16_t)(lcd_width() - 4));
    s_con_y = CON_TOP;
}

void ui_con_line(uint16_t color, const char *fmt, ...)
{
    char buf[40];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (s_con_y + GFX_CHAR_H > (int16_t)lcd_height()) {
        /* wrap: clear the console area and start again from the top */
        lcd_fill_rect(0, CON_TOP, (int16_t)lcd_width(), (int16_t)(lcd_height() - CON_TOP), COLOR_BLACK);
        s_con_y = CON_TOP;
    }
    gfx_text_pad(2, s_con_y, buf, color, COLOR_BLACK, 1, (int16_t)(lcd_width() - 4));
    s_con_y = (int16_t)(s_con_y + CON_LINE);
}

const char *ui_answer_str(ui_answer_t a)
{
    switch (a) {
    case UI_ANS_OK: return "ok";
    case UI_ANS_FAIL: return "fail";
    case UI_ANS_ABORT: return "abort";
    case UI_ANS_TIMEOUT: return "timeout";
    default: return "?";
    }
}

static void emit_answer(const char *id, ui_answer_t a)
{
    static jb_t j;
    /* PROTOCOL.md: @A {"id","answer"}; the extra "t" is harmless and uniform. */
    jb_begin(&j, "answer");
    jb_str(&j, "id", id);
    jb_str(&j, "answer", ui_answer_str(a));
    jb_end(&j);
    jb_emit(&j, "@A ");
}

ui_answer_t ui_prompt(const char *id, const char *text, const char *const *answers,
                      uint32_t timeout_ms)
{
    static const char *const def_answers[] = {"A=ok", "B=fail", NULL};
    static jb_t j;
    char arr[96];
    size_t n = 0;

    if (!answers) {
        answers = def_answers;
    }

    /* @P {"t":"prompt","id":..,"text":..,"answers":[..]} */
    arr[n++] = '[';
    for (int i = 0; answers[i] && n < sizeof(arr) - 8u; i++) {
        n += (size_t)snprintf(&arr[n], sizeof(arr) - n, "%s\"%s\"", i ? "," : "", answers[i]);
    }
    if (n > sizeof(arr) - 2u) {
        n = sizeof(arr) - 2u;
    }
    arr[n++] = ']';
    arr[n] = '\0';
    jb_begin(&j, "prompt");
    jb_str(&j, "id", id);
    jb_str(&j, "text", text);
    jb_raw(&j, "answers", arr);
    jb_u32(&j, "timeout_ms", timeout_ms);
    jb_end(&j);
    jb_emit(&j, "@P ");

    /* Screen */
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    gfx_text(2, 12, "ACTION REQUIRED", COLOR_YELLOW, COLOR_BLACK, 1);
    gfx_text_wrap(2, 24, (int16_t)(lcd_width() - 4), text, COLOR_WHITE, COLOR_BLACK, 1);
    int16_t yb = (int16_t)(lcd_height() - 10);
    int16_t x = 2;
    for (int i = 0; answers[i]; i++) {
        x = gfx_text(x, yb, answers[i], COLOR_CYAN, COLOR_BLACK, 1);
        x = (int16_t)(x + 8);
    }

    input_flush();
    uint32_t t0 = HAL_GetTick();
    uint32_t last_s = 0xFFFFFFFFu;
    ui_answer_t ans;
    for (;;) {
        sys_service();
        input_event_t ev;
        if (input_pop(&ev) && ev.pressed) {
            if (ev.button == BTN_A) { ans = UI_ANS_OK; break; }
            if (ev.button == BTN_B) { ans = UI_ANS_FAIL; break; }
        }
        if (abort_requested()) { ans = UI_ANS_ABORT; break; }
        uint32_t el = HAL_GetTick() - t0;
        if (timeout_ms) {
            if (el >= timeout_ms) { ans = UI_ANS_TIMEOUT; break; }
            uint32_t left = (timeout_ms - el + 999u) / 1000u;
            if (left != last_s) {
                last_s = left;
                char b[12];
                snprintf(b, sizeof(b), "%4" PRIu32 "s", left);
                gfx_text((int16_t)(lcd_width() - 32), yb, b, COLOR_GRAY, COLOR_BLACK, 1);
            }
        }
    }
    emit_answer(id, ans);
    ui_con_begin(id);
    ui_con_line(ans == UI_ANS_OK ? COLOR_GREEN : COLOR_RED, "answer: %s", ui_answer_str(ans));
    return ans;
}
