/* bench.cpu: integer / float / double / math / game kernels.
   Every kernel is a dependency chain or has an OPAQUE() barrier per operation so the compiler can
   neither hoist, fold nor delete it; results end in volatile sinks (checked with objdump). */

#include "bench.h"
#include "cmd.h"
#include "log.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define R4(s)  s s s s
#define R8(s)  R4(s) R4(s)
#define R16(s) R8(s) R8(s)

#define OPAQUEF(x) __asm volatile("" : "+t"(x))

static volatile uint32_t g_sink;

/* __SMMUL is missing from the CMSIS version in use */
static inline int32_t smmul(int32_t a, int32_t b) { int32_t r; __asm("smmul %0, %1, %2" : "=r"(r) : "r"(a), "r"(b)); return r; }
static volatile int32_t g_isink;
static volatile uint64_t g_sink64;
static volatile float g_fsink;
static volatile double g_dsink;
/* volatile inputs: the compiler cannot constant-fold anything derived from them */
static volatile uint32_t v_a = 0x9E3779B1u, v_small = 100u, v_div_s = 7u, v_big = 0xFFFFFFF1u, v_div_l = 3u;
static volatile int32_t v_sneg = -100, v_sbig = (int32_t)0x80000011;
static volatile uint64_t v_x64 = 0x0123456789ABCDEFull, v_y64 = 0x12345ull;
static volatile float v_f1 = 1.0000001f, v_f2 = 0.9999999f, v_f3 = 1.5f;
static volatile double v_d1 = 1.0000000001, v_d2 = 1.5;

/* ---------------------------------------------------------------- integer */

static void k_add(uint32_t n) { uint32_t a = 1, b = v_a; while (n--) { R16(a += b; OPAQUE(a);) } g_sink = a; }
static void k_mul(uint32_t n) { uint32_t a = 3, b = v_a; while (n--) { R16(a *= b; OPAQUE(a);) } g_sink = a; }
static void k_mla(uint32_t n) { uint32_t a = 3, b = v_a, c = 5; while (n--) { R16(a = a * b + c; OPAQUE(a);) } g_sink = a; }

#define DIVK(name, T, X, Y, OPS)                                         \
    static void name(uint32_t n)                                         \
    {                                                                    \
        T x = (T)(X), y = (T)(Y), acc = 0, q;                            \
        while (n--) {                                                    \
            OPS(OPAQUE(x); q = x / y; acc += q;)                         \
        }                                                                \
        g_isink = (int32_t)acc;                                          \
    }
DIVK(k_udiv_s, uint32_t, v_small, v_div_s, R16)
DIVK(k_udiv_l, uint32_t, v_big, v_div_l, R16)
DIVK(k_sdiv_s, int32_t, v_sneg, v_div_s, R16)
DIVK(k_sdiv_l, int32_t, v_sbig, v_div_l, R16)

static void k_mul64(uint32_t n) { uint64_t a = v_x64, b = v_y64 | 1u; while (n--) { R8(a *= b; OPAQUE(a);) } g_sink64 = a; }
static void k_div64(uint32_t n)
{
    uint64_t x = v_x64, y = v_y64, acc = 0, q;
    while (n--) { R4(OPAQUE(x); q = x / y; acc += q;) }
    g_sink64 = acc;
}
static void k_smlad(uint32_t n)
{
    uint32_t x = v_a, y = v_a ^ 0x55555555u, acc = 0;
    while (n--) { R16(acc = __SMLAD(x, y, acc); OPAQUE(x);) }
    g_sink = acc;
}
static void k_q16mul(uint32_t n)
{
    int32_t a = 70000, b = (int32_t)(v_a >> 2);
    while (n--) { R16(a = (int32_t)(((int64_t)a * b) >> 16); a |= 0x10000; OPAQUE(a);) }
    g_isink = a;
}
static void k_q16mul_smmul(uint32_t n) /* Q16.16 via SMMUL (top 32 bits, <<n adjust): lossy but 1 instruction */
{
    int32_t a = 70000, b = (int32_t)(v_a >> 2);
    while (n--) { R16(a = smmul(a, b) << 4; a |= 0x10000; OPAQUE(a);) }
    g_isink = a;
}
static void k_clz(uint32_t n) { uint32_t a = v_a, b = v_a >> 3; while (n--) { R16(a = __CLZ(a ^ b) + 0x1234567u; OPAQUE(a);) } g_sink = a; }
static void k_xorshift(uint32_t n)
{
    uint32_t x = v_a;
    while (n--) { R16(x ^= x << 13; x ^= x >> 17; x ^= x << 5; OPAQUE(x);) }
    g_sink = x;
}

/* ---------------------------------------------------------------- float / double */

static void k_fadd(uint32_t n) { float a = 1.0f, b = v_f1; while (n--) { R16(a = a + b; OPAQUEF(a);) } g_fsink = a; }
static void k_fmul(uint32_t n) { float a = 1.0f, b = v_f1; while (n--) { R16(a = a * b; OPAQUEF(a);) } g_fsink = a; }
static void k_ffma(uint32_t n)
{
    float a = 1.0f, b = v_f1, c = v_f2;
    while (n--) { R16(__asm volatile("vfma.f32 %0, %1, %2" : "+t"(a) : "t"(b), "t"(c));) }
    g_fsink = a;
}
static void k_fdiv(uint32_t n) { float a = 1.0f, b = v_f3; while (n--) { R16(a = b / a; OPAQUEF(a);) } g_fsink = a; }
static void k_fsqrt(uint32_t n)
{
    float a = v_f3, r = 0;
    while (n--) { R16(__asm volatile("vsqrt.f32 %0, %1" : "=t"(r) : "t"(a)); a = r + 1.0f; OPAQUEF(a);) }
    g_fsink = a;
}
static void k_dadd(uint32_t n) { double a = 1.0, b = v_d1; while (n--) { R8(a = a + b; OPAQUE(a);) } g_dsink = a; }
static void k_dmul(uint32_t n) { double a = 1.0, b = v_d1; while (n--) { R8(a = a * b; OPAQUE(a);) } g_dsink = a; }
static void k_ddiv(uint32_t n) { double a = 1.0, b = v_d2; while (n--) { R4(a = b / a; OPAQUE(a);) } g_dsink = a; }

/* ---------------------------------------------------------------- math functions */

#define NARGS 128u
#define s_lut_sin ((float *)(void *)(g_arena + 9216))
#define s_lut_exp ((float *)(void *)(g_arena + 10496))

static void lut_init(void)
{
    for (int i = 0; i < 257; i++) {
        s_lut_sin[i] = sinf((float)i * (6.2831853f / 256.0f));
        s_lut_exp[i] = exp2f((float)i / 256.0f);
    }
}

static float sin_lut(float x)
{
    float t = x * 40.7436654f; /* 256 / 2pi, x >= 0 */
    int i = (int)t;
    float fr = t - (float)i;
    i &= 255;
    return s_lut_sin[i] + fr * (s_lut_sin[i + 1] - s_lut_sin[i]);
}

static float sin_poly(float x)
{
    float k = (float)(int)(x * 0.15915494f + (x >= 0.0f ? 0.5f : -0.5f));
    x -= k * 6.2831853f;
    if (x > 1.5707964f) {
        x = 3.1415927f - x;
    } else if (x < -1.5707964f) {
        x = -3.1415927f - x;
    }
    float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.008333333f + x2 * (-0.00019841270f + x2 * 0.0000027557319f))));
}

static float atan2_poly(float y, float x)
{
    float ax = fabsf(x), ay = fabsf(y);
    float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.0f) r = 3.14159274f - r;
    if (y < 0.0f) r = -r;
    return r;
}

static float exp_poly(float x)
{
    float t = x * 1.44269504f;
    int n = (int)floorf(t);
    float f = t - (float)n;
    float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    union { float f; uint32_t u; } v = {p};
    v.u += (uint32_t)n << 23;
    return v.f;
}

static float exp_lut(float x)
{
    float t = x * 1.44269504f;
    int n = (int)floorf(t);
    float f = (t - (float)n) * 256.0f;
    int i = (int)f;
    float fr = f - (float)i;
    float p = s_lut_exp[i] + fr * (s_lut_exp[i + 1] - s_lut_exp[i]);
    union { float f; uint32_t u; } v = {p};
    v.u += (uint32_t)n << 23;
    return v.f;
}

static float call_sinf(float x, float y) { (void)y; return sinf(x); }
static float call_sin_lut(float x, float y) { (void)y; return sin_lut(x); }
static float call_sin_poly(float x, float y) { (void)y; return sin_poly(x); }
static float call_atan2f(float x, float y) { return atan2f(y, x); }
static float call_atan2_poly(float x, float y) { return atan2_poly(y, x); }
static float call_expf(float x, float y) { (void)y; return expf(x); }
static float call_exp_poly(float x, float y) { (void)y; return exp_poly(x); }
static float call_exp_lut(float x, float y) { (void)y; return exp_lut(x); }

typedef float (*f2_t)(float, float);
static f2_t s_mathf;
static float s_math_x0, s_math_dx;

static void k_math(uint32_t n)
{
    f2_t f = s_mathf;
    float x = s_math_x0, dx = s_math_dx, acc = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        acc += f(x, 1.0f + x * 0.37f);
        x += dx;
    }
    g_fsink = acc;
}

static void k_math_base(uint32_t n)
{
    float x = s_math_x0, dx = s_math_dx, acc = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float y = 1.0f + x * 0.37f;
        OPAQUEF(y);
        acc += y;
        x += dx;
        OPAQUEF(x);
    }
    g_fsink = acc;
}

typedef struct {
    const char *name;
    f2_t f;
    float x0, dx;
    float (*ref)(float);
    float lo, hi;
} math_case_t;

static float ref_sin(float x) { return (float)sin((double)x); }
static float ref_exp(float x) { return (float)exp((double)x); }
static float ref_atan2x(float x) { return (float)atan2(1.0 + (double)x * 0.37, (double)x); }

static uint32_t max_err_ppm(const math_case_t *m)
{
    /* max |f - ref| / max(1, |ref|) * 1e6 over 1000 points; atan2 uses y = 1 + 0.37x */
    float worst = 0.0f;
    for (int i = 0; i < 1000; i++) {
        float x = m->lo + (m->hi - m->lo) * (float)i / 1000.0f;
        float r = m->ref(x);
        float v = m->f(x, 1.0f + x * 0.37f);
        float e = fabsf(v - r) / (fabsf(r) > 1.0f ? fabsf(r) : 1.0f);
        if (e > worst) worst = e;
    }
    return (uint32_t)(worst * 1e6f);
}

static bool run_math(void)
{
    static const math_case_t cases[] = {
        {"sinf_libm", call_sinf, 0.1f, 0.0491f, ref_sin, 0.0f, 6.28f},
        {"sin_lut_lerp", call_sin_lut, 0.1f, 0.0491f, ref_sin, 0.0f, 6.28f},
        {"sin_poly", call_sin_poly, 0.1f, 0.0491f, ref_sin, -6.28f, 6.28f},
        {"atan2f_libm", call_atan2f, -3.0f, 0.047f, ref_atan2x, -3.0f, 3.0f},
        {"atan2_poly", call_atan2_poly, -3.0f, 0.047f, ref_atan2x, -3.0f, 3.0f},
        {"expf_libm", call_expf, -5.0f, 0.0781f, ref_exp, -5.0f, 5.0f},
        {"exp_poly", call_exp_poly, -5.0f, 0.0781f, ref_exp, -5.0f, 5.0f},
        {"exp_lut_lerp", call_exp_lut, -5.0f, 0.0781f, ref_exp, -5.0f, 5.0f},
    };
    lut_init();
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        s_math_x0 = cases[i].x0;
        s_math_dx = cases[i].dx;
        bench_stat_t base = bench_measure(k_math_base, NARGS);
        s_mathf = cases[i].f;
        bench_stat_t s = bench_measure(k_math, NARGS);
        s.cyc_min = s.cyc_min > base.cyc_min ? s.cyc_min - base.cyc_min : 0u;
        s.cyc_med = s.cyc_med > base.cyc_med ? s.cyc_med - base.cyc_med : 0u;
        char ex[40];
        snprintf(ex, sizeof(ex), ",\"maxerr_ppm\":%" PRIu32, max_err_ppm(&cases[i]));
        bench_emit("bench.cpu", cases[i].name, s, NARGS, "call", 0, "flash", ex);
        sys_service();
        if (abort_requested()) return false;
    }
    return true;
}

/* ---------------------------------------------------------------- game kernels */

typedef struct { int16_t x, y, w, h; } box_t;
#define s_boxes ((box_t *)(void *)(g_arena))
#define s_map (g_arena + 1024)
#define s_tiles (g_arena + 5120)

static void k_aabb(uint32_t n)
{
    uint32_t hits = 0;
    while (n--) {
        for (int i = 0; i < 64; i++) {
            const box_t a = s_boxes[i];
            for (int j = i + 1; j < 64; j++) {
                const box_t *b = &s_boxes[j];
                if (a.x < b->x + b->w && b->x < a.x + a.w && a.y < b->y + b->h && b->y < a.y + a.h) {
                    hits++;
                }
            }
        }
    }
    g_sink = hits;
}

static void k_tilescroll(uint32_t n)
{
    uint32_t sum = 0, sx = 37, sy = 11;
    while (n--) {
        uint32_t cx = sx >> 3, cy = sy >> 3;
        for (uint32_t ty = 0; ty < 16; ty++) {
            for (uint32_t tx = 0; tx < 20; tx++) {
                uint32_t idx = s_map[(((cy + ty) & 63u) << 6) + ((cx + tx) & 63u)] & 63u;
                const uint8_t *src = &s_tiles[idx << 6];
                sum += (uint32_t)(uintptr_t)src + (uint32_t)(tx << 3) + (sy & 7u) * 8u;
            }
        }
        sx += 3;
        sy += 1;
    }
    g_sink = sum;
}

suite_verdict_t suite_bench_cpu(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t x = 12345;
    for (int i = 0; i < 64; i++) {
        x = x * 1664525u + 1013904223u;
        s_boxes[i].x = (int16_t)((x >> 8) % 150u);
        s_boxes[i].y = (int16_t)((x >> 16) % 120u);
        s_boxes[i].w = (int16_t)(4 + ((x >> 4) & 7u));
        s_boxes[i].h = (int16_t)(4 + ((x >> 12) & 7u));
    }
    for (unsigned i = 0; i < 4096u; i++) {
        x = x * 1664525u + 1013904223u;
        s_map[i] = (uint8_t)(x >> 24);
    }
    static const bench_case_t cases[] = {
        {"int_add", k_add, 64, 64 * 16, "op", 0},
        {"int_mul", k_mul, 64, 64 * 16, "op", 0},
        {"int_mla", k_mla, 64, 64 * 16, "op", 0},
        {"udiv_small", k_udiv_s, 32, 32 * 16, "op", 0},
        {"udiv_large", k_udiv_l, 32, 32 * 16, "op", 0},
        {"sdiv_small", k_sdiv_s, 32, 32 * 16, "op", 0},
        {"sdiv_large", k_sdiv_l, 32, 32 * 16, "op", 0},
        {"mul64", k_mul64, 64, 64 * 8, "op", 0},
        {"div64", k_div64, 16, 16 * 4, "op", 0},
        {"smlad", k_smlad, 64, 64 * 16, "op", 0},
        {"q16_mul_smull", k_q16mul, 64, 64 * 16, "op", 0},
        {"q16_mul_smmul", k_q16mul_smmul, 64, 64 * 16, "op", 0},
        {"clz", k_clz, 64, 64 * 16, "op", 0},
        {"xorshift32", k_xorshift, 64, 64 * 16, "rand", 0},
        {"float_add", k_fadd, 64, 64 * 16, "op", 0},
        {"float_mul", k_fmul, 64, 64 * 16, "op", 0},
        {"float_fma", k_ffma, 64, 64 * 16, "op", 0},
        {"float_div", k_fdiv, 32, 32 * 16, "op", 0},
        {"float_sqrt", k_fsqrt, 32, 32 * 16, "op", 0},
        {"double_add_soft", k_dadd, 32, 32 * 8, "op", 0},
        {"double_mul_soft", k_dmul, 32, 32 * 8, "op", 0},
        {"double_div_soft", k_ddiv, 16, 16 * 4, "op", 0},
    };
    if (!bench_run_cases("bench.cpu", cases, (int)(sizeof(cases) / sizeof(cases[0])), "flash")) {
        return SUITE_ABORT;
    }
    if (!run_math()) {
        return SUITE_ABORT;
    }
    static const bench_case_t game[] = {
        {"aabb_64obj_allpairs", k_aabb, 1, 64 * 63 / 2, "pair", 0},
        {"tile_scroll_20x16", k_tilescroll, 8, 8 * 320, "tile", 0},
    };
    if (!bench_run_cases("bench.cpu", game, 2, "flash")) {
        return SUITE_ABORT;
    }
    return SUITE_PASS;
}
