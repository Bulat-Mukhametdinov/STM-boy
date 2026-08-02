#ifndef ZX48_H
#define ZX48_H

#include "runtime_workspace.h"
#include "vendor/superzazu_z80/z80.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZX48_DISPLAY_WIDTH 256u
#define ZX48_DISPLAY_HEIGHT 192u
#define ZX48_FRAME_TSTATES 69888ul
#define ZX48_KEY_ROW_COUNT 8u
#define ZX48_KEY_BITS_PER_ROW 5u

typedef struct {
    z80 cpu;
    uint8_t *ram;
    uint8_t keyboard_rows[ZX48_KEY_ROW_COUNT];
    uint8_t border_color;
    uint8_t kempston;
    volatile uint8_t beeper_level;
    bool loaded;
    bool faulted;
} Zx48;

void Zx48_Init(Zx48 *zx, uint8_t *ram);
bool Zx48_LoadSnaHeader(Zx48 *zx, const uint8_t header[RUNTIME_WORKSPACE_ZX48_SNA_HEADER_SIZE]);
void Zx48_SetKempston(Zx48 *zx, uint8_t mask);
void Zx48_ClearKeys(Zx48 *zx);
void Zx48_SetKey(Zx48 *zx, uint8_t row, uint8_t bit, bool down);
void Zx48_RunFrame(Zx48 *zx);
void Zx48_RenderLine(const Zx48 *zx, uint16_t y, uint8_t *rgb565_line);
int8_t Zx48_AudioSample(void *ctx);
bool Zx48_IsFaulted(const Zx48 *zx);

#ifdef __cplusplus
}
#endif

#endif
