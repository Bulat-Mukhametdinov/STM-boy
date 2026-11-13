#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

/* On-device UI: status bar + menu (Src/ui/menu.c), console screen used by
   suites, and the @P/@A prompt plumbing (Src/ui/prompt.c). */

void ui_init(void);
void ui_task(void);          /* main loop: handle input events, redraw */
void ui_invalidate(void);    /* force a full redraw of the current screen */
void ui_status_bar(void);

/* Console screen (title + scrolling lines), used while a suite runs. */
void ui_con_begin(const char *title);
void ui_con_line(uint16_t color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

typedef enum {
    UI_ANS_OK = 0,      /* A */
    UI_ANS_FAIL,        /* B */
    UI_ANS_ABORT,       /* host abort */
    UI_ANS_TIMEOUT,
} ui_answer_t;

/* Shows `text` on screen, emits @P {"id","text","answers"}, waits for A (ok) /
   B (fail), host abort or timeout (0 = none), emits @A {"id","answer"} and
   returns the answer. USB/OFF_REQ stay serviced while waiting.
   `answers` = NULL-terminated list, NULL => {"A=ok","B=fail"}. */
ui_answer_t ui_prompt(const char *id, const char *text, const char *const *answers,
                      uint32_t timeout_ms);
const char *ui_answer_str(ui_answer_t a);

/* Building blocks for custom interactive screens (calibration, live views):
   ui_prompt_emit() sends the @P line only (no screen change); every @P must
   later be closed by exactly one ui_answer_emit() with the same id (the host
   suspends its idle timeout while a prompt is open). `answer` is free text,
   e.g. "ok", "fail", "timeout", "abort", "done", "skip". */
void ui_prompt_emit(const char *id, const char *text, const char *const *answers,
                    uint32_t timeout_ms);
void ui_answer_emit(const char *id, const char *answer);

/* Host-side answer (`answer <id> ok|fail` command). The pending answer is
   consumed by ui_prompt() / ui_host_answer_take() when the id matches the
   open prompt. */
void ui_host_answer_set(const char *id, ui_answer_t a);
bool ui_host_answer_take(const char *id, ui_answer_t *a);

/* Button-map drawing shared by the menu "Button view" and input.live. */
void ui_draw_button_map(uint16_t held, bool full, uint16_t seen);

#endif
