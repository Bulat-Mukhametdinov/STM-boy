/* Host build of the firmware noise-shaping render core (Inc/ns_core.h, compiled unchanged) for tools/ns_sim.py.
     gcc -O2 -std=c11 -DNS_HOST -IInc tools/ns_host.c -o tools/build/ns_host -lm
     ns_host <bits 8|7> <order 0..2> <damp8> <in.s16> <out.u16> <trace.i32> [rng_seed] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "ns_core.h"

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: ns_host bits order damp8 in.s16 out.u16 trace.i32 [seed]\n");
        return 2;
    }
    unsigned bits = (unsigned)atoi(argv[1]), order = (unsigned)atoi(argv[2]), damp = (unsigned)atoi(argv[3]);
    FILE *fi = fopen(argv[4], "rb"), *fo = fopen(argv[5], "wb"), *ft = fopen(argv[6], "wb");
    if (!fi || !fo || !ft) return 1;
    fseek(fi, 0, SEEK_END);
    long n = ftell(fi) / 2;
    fseek(fi, 0, SEEK_SET);
    int16_t *x = malloc((size_t)n * 2);
    if (fread(x, 2, (size_t)n, fi) != (size_t)n) return 1;
    static ns_t s;
    ns_reset(&s);
    ns_config(&s, bits, order, damp);
    s.rng = argc > 7 ? (uint32_t)strtoul(argv[7], NULL, 0) : 0xC0FFEE11u;
    s.gcur = s.gtgt = 32768 << 8;
    s.vgain = 32768;
    s.gsh = 7;
    s.lim = (int32_t)(32767.0f * powf(10.0f, -9.0f * 0.05f));
    const uint32_t per = 4u * s.mper, blk = (bits == 7u) ? 12u : 25u;
    static uint16_t out[NS_MAXPCM * 40];
    static int32_t tr[NS_MAXPCM * 40], pcm[NS_MAXPCM];
    for (long i = 0; i < n; i += blk) {
        uint32_t m = (n - i < (long)blk) ? (uint32_t)(n - i) : blk;
        for (uint32_t k = 0; k < m; k++) pcm[k] = x[i + k];
        s.trace = tr;
        ns_render(&s, out, pcm, m);
        fwrite(out, 2, m * per, fo);
        fwrite(tr, 4, m * per, ft);
    }
    fprintf(stderr, "clipped %u\n", (unsigned)s.clipped);
    fclose(fo);
    fclose(ft);
    return 0;
}
