#ifndef CHIP8_H
#define CHIP8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHIP8_MEMORY_SIZE 4096u
/* Low-resolution (classic CHIP-8) geometry, also the SUPER-CHIP lores mode. */
#define CHIP8_DISPLAY_WIDTH 64u
#define CHIP8_DISPLAY_HEIGHT 32u
/* High-resolution (SUPER-CHIP) geometry. */
#define CHIP8_DISPLAY_WIDTH_HI 128u
#define CHIP8_DISPLAY_HEIGHT_HI 64u
/* Display buffer is sized for the maximum (hi-res) resolution: 128*64/8 = 1024 bytes. */
#define CHIP8_DISPLAY_SIZE (CHIP8_DISPLAY_WIDTH_HI * CHIP8_DISPLAY_HEIGHT_HI / 8u)
#define CHIP8_KEY_COUNT 16u
#define CHIP8_ROM_START 0x200u

#define CHIP8_QUIRK_SHIFT_USES_VX 0x0001u
#define CHIP8_QUIRK_MEMORY_INCREMENT_BY_X 0x0002u
#define CHIP8_QUIRK_MEMORY_LEAVE_I_UNCHANGED 0x0004u
#define CHIP8_QUIRK_WRAP_SPRITES 0x0008u
#define CHIP8_QUIRK_JUMP_USES_VX 0x0010u
#define CHIP8_QUIRK_VBLANK 0x0020u
#define CHIP8_QUIRK_LOGIC_ZEROES_VF 0x0040u

typedef enum {
    CHIP8_VARIANT_CHIP8 = 0,
    CHIP8_VARIANT_SCHIP = 1,
} Chip8Variant;

typedef struct {
    uint8_t memory[CHIP8_MEMORY_SIZE];
    uint8_t v[16];
    uint16_t i;
    uint16_t pc;
    uint16_t stack[16];
    uint8_t sp;
    uint8_t delay_timer;
    uint8_t sound_timer;
    uint8_t keys[CHIP8_KEY_COUNT];
    uint8_t display[CHIP8_DISPLAY_SIZE];
    bool display_dirty;
    bool waiting_for_key;
    bool waiting_for_key_release;
    uint8_t waiting_register;
    uint8_t waiting_key;
    bool sprite_drawn;
    uint8_t draw_count;
    uint16_t quirks;
    uint32_t rng_state;
    bool halted;
    bool hires;            /* false = 64x32 lores, true = 128x64 hires (SUPER-CHIP) */
    uint8_t variant;       /* Chip8Variant: gates SUPER-CHIP-only opcode behavior */
    uint8_t rpl[8];        /* SUPER-CHIP RPL flag registers (FX75/FX85) */
} Chip8;

void Chip8_Init(Chip8 *chip8, const uint8_t *rom, size_t rom_size, uint32_t seed);
void Chip8_Reset(Chip8 *chip8, const uint8_t *rom, size_t rom_size, uint32_t seed);
void Chip8_SetQuirks(Chip8 *chip8, uint16_t quirks);
void Chip8_SetVariant(Chip8 *chip8, uint8_t variant);
uint16_t Chip8_DisplayWidth(const Chip8 *chip8);
uint16_t Chip8_DisplayHeight(const Chip8 *chip8);
bool Chip8_Step(Chip8 *chip8);
void Chip8_RunCycles(Chip8 *chip8, uint16_t cycles);
void Chip8_TickTimers(Chip8 *chip8);
void Chip8_SetKey(Chip8 *chip8, uint8_t key, bool down);

#ifdef __cplusplus
}
#endif

#endif
