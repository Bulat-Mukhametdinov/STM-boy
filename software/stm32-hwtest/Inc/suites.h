#ifndef SUITES_H
#define SUITES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SUITE_PASS = 0,
    SUITE_FAIL,
    SUITE_ABORT,
} suite_verdict_t;

typedef suite_verdict_t (*suite_fn_t)(int argc, char **argv);

#define SUITE_F_USER 0x01u   /* needs a human at the device ([U]); every prompt times out */

typedef struct {
    const char *name;
    suite_fn_t fn;
    const char *help;
    unsigned flags;     /* SUITE_F_* */
} suite_t;

extern const suite_t g_suites[];
extern const int g_suite_count;

const suite_t *suite_find(const char *name);

/* Runs a suite: busy state, @DONE line, on-screen console. argv[0] = name. */
suite_verdict_t suite_run(const suite_t *s, int argc, char **argv);
const char *suite_verdict_str(suite_verdict_t v);

/* Argument helpers for "key=value" / bare-word tokens in argv[1..]. */
uint32_t suite_arg_u32(int argc, char **argv, const char *key, uint32_t def); /* base 0 */
const char *suite_arg_str(int argc, char **argv, const char *key, const char *def);
bool suite_has_word(int argc, char **argv, const char *word);

/* Standard opening of a [U] suite: prompt "<text>  A=start B=skip" with the
   timeout from the `t=<s>` argument (default 60 s). Returns true to go on;
   otherwise emits @R {"t":"skip",...} and the suite should return SUITE_ABORT. */
bool suite_user_start(const char *suite, const char *text, int argc, char **argv);

/* Suite implementations living in other files (Src/ui/t_*.c). */
suite_verdict_t suite_display_info(int argc, char **argv);
suite_verdict_t suite_display_cal(int argc, char **argv);
suite_verdict_t suite_display_orient(int argc, char **argv);
suite_verdict_t suite_display_rgb(int argc, char **argv);
suite_verdict_t suite_display_gamma(int argc, char **argv);
suite_verdict_t suite_display_invert(int argc, char **argv);
suite_verdict_t suite_display_bars(int argc, char **argv);
suite_verdict_t suite_display_gradient(int argc, char **argv);
suite_verdict_t suite_display_spi(int argc, char **argv);
suite_verdict_t suite_display_tear(int argc, char **argv);
suite_verdict_t suite_display_native(int argc, char **argv);
suite_verdict_t suite_display_vscroll(int argc, char **argv);
suite_verdict_t suite_demo(int argc, char **argv);
suite_verdict_t suite_pins(int argc, char **argv);
suite_verdict_t suite_input_live(int argc, char **argv);
suite_verdict_t suite_input_bounce(int argc, char **argv);
suite_verdict_t suite_input_chord(int argc, char **argv);
suite_verdict_t suite_backlight(int argc, char **argv);
suite_verdict_t suite_backlight_user(int argc, char **argv);

suite_verdict_t suite_audio_tone(int argc, char **argv);
suite_verdict_t suite_audio_sweep(int argc, char **argv);
suite_verdict_t suite_audio_chirp(int argc, char **argv);
suite_verdict_t suite_audio_noise(int argc, char **argv);
suite_verdict_t suite_audio_hiss(int argc, char **argv);
suite_verdict_t suite_audio_matrix(int argc, char **argv);
suite_verdict_t suite_audio_clip(int argc, char **argv);
suite_verdict_t suite_audio_pop(int argc, char **argv);
suite_verdict_t suite_audio_cpu(int argc, char **argv);

#endif
