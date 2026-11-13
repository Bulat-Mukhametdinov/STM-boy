#ifndef SUITES_H
#define SUITES_H

#include <stdbool.h>

typedef enum {
    SUITE_PASS = 0,
    SUITE_FAIL,
    SUITE_ABORT,
} suite_verdict_t;

typedef suite_verdict_t (*suite_fn_t)(int argc, char **argv);

typedef struct {
    const char *name;
    suite_fn_t fn;
    const char *help;
} suite_t;

extern const suite_t g_suites[];
extern const int g_suite_count;

const suite_t *suite_find(const char *name);

/* Runs a suite: busy state, @DONE line, on-screen console. argv[0] = name. */
suite_verdict_t suite_run(const suite_t *s, int argc, char **argv);
const char *suite_verdict_str(suite_verdict_t v);

#endif
