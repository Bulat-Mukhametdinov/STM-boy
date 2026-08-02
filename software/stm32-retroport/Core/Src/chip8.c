#include "chip8.h"

#include <string.h>

#define CHIP8_FONT_START 0x050u
#define CHIP8_FONT_BYTES_PER_DIGIT 5u
/* SUPER-CHIP large font lives right after the small font (0x050..0x09F),
 * occupying 0x0A0..0x13F (160 bytes), well below the ROM start at 0x200. */
#define CHIP8_LARGE_FONT_START 0x0A0u
#define CHIP8_LARGE_FONT_BYTES_PER_DIGIT 10u

static const uint8_t chip8_font[CHIP8_KEY_COUNT * CHIP8_FONT_BYTES_PER_DIGIT] = {
    0xF0u, 0x90u, 0x90u, 0x90u, 0xF0u,
    0x20u, 0x60u, 0x20u, 0x20u, 0x70u,
    0xF0u, 0x10u, 0xF0u, 0x80u, 0xF0u,
    0xF0u, 0x10u, 0xF0u, 0x10u, 0xF0u,
    0x90u, 0x90u, 0xF0u, 0x10u, 0x10u,
    0xF0u, 0x80u, 0xF0u, 0x10u, 0xF0u,
    0xF0u, 0x80u, 0xF0u, 0x90u, 0xF0u,
    0xF0u, 0x10u, 0x20u, 0x40u, 0x40u,
    0xF0u, 0x90u, 0xF0u, 0x90u, 0xF0u,
    0xF0u, 0x90u, 0xF0u, 0x10u, 0xF0u,
    0xF0u, 0x90u, 0xF0u, 0x90u, 0x90u,
    0xE0u, 0x90u, 0xE0u, 0x90u, 0xE0u,
    0xF0u, 0x80u, 0x80u, 0x80u, 0xF0u,
    0xE0u, 0x90u, 0x90u, 0x90u, 0xE0u,
    0xF0u, 0x80u, 0xF0u, 0x80u, 0xF0u,
    0xF0u, 0x80u, 0xF0u, 0x80u, 0x80u,
};

/* 8x10 pixel large hex glyphs (0-F). SUPER-CHIP 1.1 only defines 0-9; 0-F are
 * provided for Octo compatibility. Each row is the top 8 bits of a byte. */
static const uint8_t chip8_large_font[CHIP8_KEY_COUNT * CHIP8_LARGE_FONT_BYTES_PER_DIGIT] = {
    0x7Cu, 0xC6u, 0xCEu, 0xDEu, 0xD6u, 0xF6u, 0xE6u, 0xC6u, 0x7Cu, 0x00u, /* 0 */
    0x10u, 0x30u, 0xF0u, 0x30u, 0x30u, 0x30u, 0x30u, 0x30u, 0xFCu, 0x00u, /* 1 */
    0x78u, 0xCCu, 0x0Cu, 0x18u, 0x30u, 0x60u, 0xCCu, 0xFCu, 0x00u, 0x00u, /* 2 */
    0x78u, 0xCCu, 0x0Cu, 0x38u, 0x0Cu, 0x0Cu, 0xCCu, 0x78u, 0x00u, 0x00u, /* 3 */
    0x1Cu, 0x3Cu, 0x6Cu, 0xCCu, 0xFEu, 0x0Cu, 0x0Cu, 0x1Eu, 0x00u, 0x00u, /* 4 */
    0xFCu, 0xC0u, 0xC0u, 0xF8u, 0x0Cu, 0x0Cu, 0xCCu, 0x78u, 0x00u, 0x00u, /* 5 */
    0x38u, 0x60u, 0xC0u, 0xF8u, 0xCCu, 0xCCu, 0xCCu, 0x78u, 0x00u, 0x00u, /* 6 */
    0xFEu, 0xC6u, 0x06u, 0x0Cu, 0x18u, 0x30u, 0x30u, 0x30u, 0x00u, 0x00u, /* 7 */
    0x78u, 0xCCu, 0xCCu, 0x78u, 0xCCu, 0xCCu, 0xCCu, 0x78u, 0x00u, 0x00u, /* 8 */
    0x78u, 0xCCu, 0xCCu, 0xCCu, 0x7Cu, 0x0Cu, 0x18u, 0x70u, 0x00u, 0x00u, /* 9 */
    0x30u, 0x78u, 0xCCu, 0xCCu, 0xFCu, 0xCCu, 0xCCu, 0xCCu, 0x00u, 0x00u, /* A */
    0xFCu, 0x66u, 0x66u, 0x7Cu, 0x66u, 0x66u, 0xFCu, 0x00u, 0x00u, 0x00u, /* B */
    0x3Cu, 0x66u, 0xC6u, 0xC0u, 0xC0u, 0xC6u, 0x66u, 0x3Cu, 0x00u, 0x00u, /* C */
    0xF8u, 0x6Cu, 0x66u, 0x66u, 0x66u, 0x66u, 0x6Cu, 0xF8u, 0x00u, 0x00u, /* D */
    0xFEu, 0x62u, 0x60u, 0x7Cu, 0x60u, 0x62u, 0xFEu, 0x00u, 0x00u, 0x00u, /* E */
    0xFEu, 0x62u, 0x60u, 0x7Cu, 0x60u, 0x60u, 0xF0u, 0x00u, 0x00u, 0x00u, /* F */
};

static uint8_t chip8_random_byte(Chip8 *chip8)
{
    chip8->rng_state = chip8->rng_state * 1664525u + 1013904223u;
    return (uint8_t)(chip8->rng_state >> 24);
}

uint16_t Chip8_DisplayWidth(const Chip8 *chip8)
{
    return chip8->hires ? CHIP8_DISPLAY_WIDTH_HI : CHIP8_DISPLAY_WIDTH;
}

uint16_t Chip8_DisplayHeight(const Chip8 *chip8)
{
    return chip8->hires ? CHIP8_DISPLAY_HEIGHT_HI : CHIP8_DISPLAY_HEIGHT;
}

static uint16_t chip8_display_stride(const Chip8 *chip8)
{
    return (uint16_t)(Chip8_DisplayWidth(chip8) / 8u);
}

static bool chip8_display_get(const Chip8 *chip8, uint8_t x, uint8_t y)
{
    uint16_t index = (uint16_t)y * Chip8_DisplayWidth(chip8) + x;
    return (chip8->display[index >> 3] & (uint8_t)(0x80u >> (index & 7u))) != 0u;
}

static void chip8_display_flip(Chip8 *chip8, uint8_t x, uint8_t y)
{
    uint16_t index = (uint16_t)y * Chip8_DisplayWidth(chip8) + x;
    chip8->display[index >> 3] ^= (uint8_t)(0x80u >> (index & 7u));
}

/* SUPER-CHIP scrolling. Operates on the current resolution, MSB-first packing,
 * and never wraps: vacated rows/columns are cleared. */
static void chip8_scroll_down(Chip8 *chip8, uint8_t amount)
{
    uint16_t h = Chip8_DisplayHeight(chip8);
    uint16_t stride = chip8_display_stride(chip8);

    if (amount == 0u) {
        return;
    }
    if (amount >= h) {
        memset(chip8->display, 0, (size_t)stride * h);
        return;
    }
    for (int16_t y = (int16_t)(h - 1u); y >= (int16_t)amount; --y) {
        memcpy(&chip8->display[(uint16_t)y * stride],
               &chip8->display[(uint16_t)(y - amount) * stride],
               stride);
    }
    memset(&chip8->display[0], 0, (size_t)stride * amount);
}

static void chip8_scroll_right4(Chip8 *chip8)
{
    uint16_t h = Chip8_DisplayHeight(chip8);
    uint16_t stride = chip8_display_stride(chip8);

    for (uint16_t y = 0; y < h; ++y) {
        uint8_t *row = &chip8->display[(uint16_t)y * stride];
        uint8_t carry = 0u;
        for (uint16_t b = 0; b < stride; ++b) {
            uint8_t cur = row[b];
            row[b] = (uint8_t)((cur >> 4) | carry);
            carry = (uint8_t)(cur << 4);
        }
    }
}

static void chip8_scroll_left4(Chip8 *chip8)
{
    uint16_t h = Chip8_DisplayHeight(chip8);
    uint16_t stride = chip8_display_stride(chip8);

    for (uint16_t y = 0; y < h; ++y) {
        uint8_t *row = &chip8->display[(uint16_t)y * stride];
        uint8_t carry = 0u;
        for (int16_t b = (int16_t)(stride - 1u); b >= 0; --b) {
            uint8_t cur = row[b];
            row[b] = (uint8_t)((cur << 4) | carry);
            carry = (uint8_t)(cur >> 4);
        }
    }
}

static bool chip8_has_quirk(const Chip8 *chip8, uint16_t quirk)
{
    return (chip8->quirks & quirk) != 0u;
}

static bool chip8_halt(Chip8 *chip8)
{
    chip8->halted = true;
    return false;
}

void Chip8_Reset(Chip8 *chip8, const uint8_t *rom, size_t rom_size, uint32_t seed)
{
    size_t max_rom_size = CHIP8_MEMORY_SIZE - CHIP8_ROM_START;

    if (chip8 == NULL) {
        return;
    }

    memset(chip8, 0, sizeof(*chip8));
    memcpy(&chip8->memory[CHIP8_FONT_START], chip8_font, sizeof(chip8_font));
    memcpy(&chip8->memory[CHIP8_LARGE_FONT_START], chip8_large_font, sizeof(chip8_large_font));

    if (rom != NULL) {
        if (rom_size > max_rom_size) {
            rom_size = max_rom_size;
        }
        memcpy(&chip8->memory[CHIP8_ROM_START], rom, rom_size);
    }

    chip8->pc = CHIP8_ROM_START;
    chip8->rng_state = seed != 0u ? seed : 0xC0DEC0DEu;
    chip8->display_dirty = true;
}

void Chip8_Init(Chip8 *chip8, const uint8_t *rom, size_t rom_size, uint32_t seed)
{
    Chip8_Reset(chip8, rom, rom_size, seed);
}

void Chip8_SetQuirks(Chip8 *chip8, uint16_t quirks)
{
    if (chip8 == NULL) {
        return;
    }

    chip8->quirks = quirks;
}

void Chip8_SetVariant(Chip8 *chip8, uint8_t variant)
{
    if (chip8 == NULL) {
        return;
    }

    chip8->variant = variant;
}

void Chip8_SetKey(Chip8 *chip8, uint8_t key, bool down)
{
    uint8_t next = down ? 1u : 0u;
    uint8_t previous;

    if (chip8 == NULL || key >= CHIP8_KEY_COUNT) {
        return;
    }

    previous = chip8->keys[key];
    chip8->keys[key] = next;

    if (chip8->waiting_for_key &&
        chip8->waiting_for_key_release &&
        key == chip8->waiting_key &&
        next == 0u &&
        previous != 0u) {
        chip8->waiting_for_key = false;
        chip8->waiting_for_key_release = false;
        return;
    }

    if (chip8->waiting_for_key &&
        !chip8->waiting_for_key_release &&
        next != 0u &&
        previous == 0u) {
        chip8->v[chip8->waiting_register & 0x0Fu] = key;
        chip8->waiting_key = key;
        chip8->waiting_for_key_release = true;
    }
}

void Chip8_TickTimers(Chip8 *chip8)
{
    if (chip8 == NULL) {
        return;
    }

    if (chip8->delay_timer > 0u) {
        --chip8->delay_timer;
    }
    if (chip8->sound_timer > 0u) {
        --chip8->sound_timer;
    }
}

bool Chip8_Step(Chip8 *chip8)
{
    uint16_t opcode;
    uint8_t n;
    uint8_t x;
    uint8_t y;
    uint8_t kk;
    uint16_t nnn;

    if (chip8 == NULL || chip8->halted) {
        return false;
    }

    chip8->sprite_drawn = false;

    if (chip8->waiting_for_key) {
        return true;
    }

    if (chip8->pc >= CHIP8_MEMORY_SIZE - 1u) {
        return chip8_halt(chip8);
    }

    opcode = (uint16_t)((uint16_t)chip8->memory[chip8->pc] << 8) | chip8->memory[chip8->pc + 1u];
    chip8->pc = (uint16_t)(chip8->pc + 2u);

    n = (uint8_t)(opcode & 0x000Fu);
    x = (uint8_t)((opcode >> 8) & 0x000Fu);
    y = (uint8_t)((opcode >> 4) & 0x000Fu);
    kk = (uint8_t)(opcode & 0x00FFu);
    nnn = (uint16_t)(opcode & 0x0FFFu);

    switch (opcode & 0xF000u) {
    case 0x0000u:
        if (opcode == 0x00E0u) {
            memset(chip8->display, 0, sizeof(chip8->display));
            chip8->display_dirty = true;
            ++chip8->draw_count;
            return true;
        }
        if (opcode == 0x00EEu) {
            if (chip8->sp == 0u) {
                return chip8_halt(chip8);
            }
            chip8->pc = chip8->stack[--chip8->sp];
            return true;
        }
        if (chip8->variant == CHIP8_VARIANT_SCHIP) {
            if ((opcode & 0xFFF0u) == 0x00C0u) { /* 00CN: scroll down N pixels */
                chip8_scroll_down(chip8, (uint8_t)(opcode & 0x000Fu));
                chip8->display_dirty = true;
                ++chip8->draw_count;
                return true;
            }
            if (opcode == 0x00FBu) { /* scroll right 4 pixels */
                chip8_scroll_right4(chip8);
                chip8->display_dirty = true;
                ++chip8->draw_count;
                return true;
            }
            if (opcode == 0x00FCu) { /* scroll left 4 pixels */
                chip8_scroll_left4(chip8);
                chip8->display_dirty = true;
                ++chip8->draw_count;
                return true;
            }
            if (opcode == 0x00FDu) { /* exit interpreter */
                return chip8_halt(chip8);
            }
            if (opcode == 0x00FEu) { /* disable hires (lores 64x32) */
                chip8->hires = false;
                memset(chip8->display, 0, sizeof(chip8->display));
                chip8->display_dirty = true;
                ++chip8->draw_count;
                return true;
            }
            if (opcode == 0x00FFu) { /* enable hires (128x64) */
                chip8->hires = true;
                memset(chip8->display, 0, sizeof(chip8->display));
                chip8->display_dirty = true;
                ++chip8->draw_count;
                return true;
            }
        }
        return true; /* unknown 0x0NNN: machine-code call, ignored */

    case 0x1000u:
        chip8->pc = nnn;
        return true;

    case 0x2000u:
        if (chip8->sp >= 16u) {
            return chip8_halt(chip8);
        }
        chip8->stack[chip8->sp++] = chip8->pc;
        chip8->pc = nnn;
        return true;

    case 0x3000u:
        if (chip8->v[x] == kk) {
            chip8->pc = (uint16_t)(chip8->pc + 2u);
        }
        return true;

    case 0x4000u:
        if (chip8->v[x] != kk) {
            chip8->pc = (uint16_t)(chip8->pc + 2u);
        }
        return true;

    case 0x5000u:
        if (n == 0u) {
            if (chip8->v[x] == chip8->v[y]) {
                chip8->pc = (uint16_t)(chip8->pc + 2u);
            }
            return true;
        }
        return chip8_halt(chip8);

    case 0x6000u:
        chip8->v[x] = kk;
        return true;

    case 0x7000u:
        chip8->v[x] = (uint8_t)(chip8->v[x] + kk);
        return true;

    case 0x8000u:
        switch (n) {
        case 0x0u:
            chip8->v[x] = chip8->v[y];
            return true;
        case 0x1u:
            chip8->v[x] |= chip8->v[y];
            if (chip8_has_quirk(chip8, CHIP8_QUIRK_LOGIC_ZEROES_VF)) {
                chip8->v[0xFu] = 0u;
            }
            return true;
        case 0x2u:
            chip8->v[x] &= chip8->v[y];
            if (chip8_has_quirk(chip8, CHIP8_QUIRK_LOGIC_ZEROES_VF)) {
                chip8->v[0xFu] = 0u;
            }
            return true;
        case 0x3u:
            chip8->v[x] ^= chip8->v[y];
            if (chip8_has_quirk(chip8, CHIP8_QUIRK_LOGIC_ZEROES_VF)) {
                chip8->v[0xFu] = 0u;
            }
            return true;
        case 0x4u: {
            uint16_t result = (uint16_t)chip8->v[x] + chip8->v[y];
            chip8->v[x] = (uint8_t)result;
            chip8->v[0xFu] = result > 0xFFu ? 1u : 0u;
            return true;
        }
        case 0x5u: {
            uint8_t vx = chip8->v[x];
            uint8_t vy = chip8->v[y];
            uint8_t result = (uint8_t)(vx - vy);
            uint8_t flag = vx >= vy ? 1u : 0u;
            chip8->v[x] = result;
            chip8->v[0xFu] = flag;
            return true;
        }
        case 0x6u: {
            uint8_t value = chip8_has_quirk(chip8, CHIP8_QUIRK_SHIFT_USES_VX) ? chip8->v[x] : chip8->v[y];
            uint8_t result = (uint8_t)(value >> 1);
            uint8_t flag = (uint8_t)(value & 0x01u);
            chip8->v[x] = result;
            chip8->v[0xFu] = flag;
            return true;
        }
        case 0x7u: {
            uint8_t vx = chip8->v[x];
            uint8_t vy = chip8->v[y];
            uint8_t result = (uint8_t)(vy - vx);
            uint8_t flag = vy >= vx ? 1u : 0u;
            chip8->v[x] = result;
            chip8->v[0xFu] = flag;
            return true;
        }
        case 0xEu: {
            uint8_t value = chip8_has_quirk(chip8, CHIP8_QUIRK_SHIFT_USES_VX) ? chip8->v[x] : chip8->v[y];
            uint8_t result = (uint8_t)(value << 1);
            uint8_t flag = (uint8_t)((value >> 7) & 0x01u);
            chip8->v[x] = result;
            chip8->v[0xFu] = flag;
            return true;
        }
        default:
            return chip8_halt(chip8);
        }

    case 0x9000u:
        if (n == 0u) {
            if (chip8->v[x] != chip8->v[y]) {
                chip8->pc = (uint16_t)(chip8->pc + 2u);
            }
            return true;
        }
        return chip8_halt(chip8);

    case 0xA000u:
        chip8->i = nnn;
        return true;

    case 0xB000u:
        chip8->pc = (uint16_t)(nnn + chip8->v[chip8_has_quirk(chip8, CHIP8_QUIRK_JUMP_USES_VX) ? x : 0u]);
        return true;

    case 0xC000u:
        chip8->v[x] = (uint8_t)(chip8_random_byte(chip8) & kk);
        return true;

    case 0xD000u: {
        uint16_t w = Chip8_DisplayWidth(chip8);
        uint16_t h = Chip8_DisplayHeight(chip8);
        bool wrap = chip8_has_quirk(chip8, CHIP8_QUIRK_WRAP_SPRITES);
        /* SUPER-CHIP DXY0 draws a 16x16 sprite (32 bytes, two bytes per row). */
        bool wide = (chip8->variant == CHIP8_VARIANT_SCHIP) && (n == 0u);
        uint8_t rows = wide ? 16u : n;
        uint8_t cols = wide ? 16u : 8u;
        uint16_t addr = (uint16_t)(chip8->i & 0x0FFFu);
        uint8_t vx = (uint8_t)(chip8->v[x] % w);
        uint8_t vy = (uint8_t)(chip8->v[y] % h);
        /* VF is set to 1 on any pixel collision. The SUPER-CHIP "colliding-rows
         * plus clipped-rows" count is intentionally not implemented. */
        chip8->v[0xFu] = 0u;

        for (uint8_t row = 0; row < rows; ++row) {
            uint16_t bits;
            uint8_t py;

            if (wide) {
                uint8_t hi = chip8->memory[(uint16_t)(addr + (uint16_t)row * 2u) & 0x0FFFu];
                uint8_t lo = chip8->memory[(uint16_t)(addr + (uint16_t)row * 2u + 1u) & 0x0FFFu];
                bits = (uint16_t)(((uint16_t)hi << 8) | lo);
            } else {
                bits = (uint16_t)((uint16_t)chip8->memory[(uint16_t)(addr + row) & 0x0FFFu] << 8);
            }

            py = (uint8_t)(vy + row);
            if (py >= h) {
                if (!wrap) {
                    break;
                }
                py = (uint8_t)(py % h);
            }

            for (uint8_t bit = 0; bit < cols; ++bit) {
                uint8_t px;
                if ((bits & (uint16_t)(0x8000u >> bit)) == 0u) {
                    continue;
                }
                px = (uint8_t)(vx + bit);
                if (px >= w) {
                    if (!wrap) {
                        break;
                    }
                    px = (uint8_t)(px % w);
                }
                if (chip8_display_get(chip8, px, py)) {
                    chip8->v[0xFu] = 1u;
                }
                chip8_display_flip(chip8, px, py);
            }
        }

        chip8->display_dirty = true;
        chip8->sprite_drawn = true;
        ++chip8->draw_count;
        return true;
    }

    case 0xE000u:
        if (kk == 0x9Eu) {
            if (chip8->keys[chip8->v[x] & 0x0Fu] != 0u) {
                chip8->pc = (uint16_t)(chip8->pc + 2u);
            }
            return true;
        }
        if (kk == 0xA1u) {
            if (chip8->keys[chip8->v[x] & 0x0Fu] == 0u) {
                chip8->pc = (uint16_t)(chip8->pc + 2u);
            }
            return true;
        }
        return chip8_halt(chip8);

    case 0xF000u:
        switch (kk) {
        case 0x07u:
            chip8->v[x] = chip8->delay_timer;
            return true;
        case 0x0Au:
            chip8->waiting_for_key = true;
            chip8->waiting_register = x;
            return true;
        case 0x15u:
            chip8->delay_timer = chip8->v[x];
            return true;
        case 0x18u:
            chip8->sound_timer = chip8->v[x];
            return true;
        case 0x1Eu:
            chip8->i = (uint16_t)(chip8->i + chip8->v[x]);
            return true;
        case 0x29u:
            chip8->i = (uint16_t)(CHIP8_FONT_START + (chip8->v[x] & 0x0Fu) * CHIP8_FONT_BYTES_PER_DIGIT);
            return true;
        case 0x30u: /* FX30: I := large font sprite for digit Vx (SUPER-CHIP) */
            chip8->i = (uint16_t)(CHIP8_LARGE_FONT_START +
                                  (chip8->v[x] & 0x0Fu) * CHIP8_LARGE_FONT_BYTES_PER_DIGIT);
            return true;
        case 0x33u:
            chip8->memory[chip8->i & 0x0FFFu] = (uint8_t)(chip8->v[x] / 100u);
            chip8->memory[(chip8->i + 1u) & 0x0FFFu] = (uint8_t)((chip8->v[x] / 10u) % 10u);
            chip8->memory[(chip8->i + 2u) & 0x0FFFu] = (uint8_t)(chip8->v[x] % 10u);
            return true;
        case 0x55u:
            for (uint8_t reg = 0; reg <= x; ++reg) {
                chip8->memory[(chip8->i + reg) & 0x0FFFu] = chip8->v[reg];
            }
            if (!chip8_has_quirk(chip8, CHIP8_QUIRK_MEMORY_LEAVE_I_UNCHANGED)) {
                chip8->i = (uint16_t)(chip8->i + x);
                if (!chip8_has_quirk(chip8, CHIP8_QUIRK_MEMORY_INCREMENT_BY_X)) {
                    ++chip8->i;
                }
            }
            return true;
        case 0x65u:
            for (uint8_t reg = 0; reg <= x; ++reg) {
                chip8->v[reg] = chip8->memory[(chip8->i + reg) & 0x0FFFu];
            }
            if (!chip8_has_quirk(chip8, CHIP8_QUIRK_MEMORY_LEAVE_I_UNCHANGED)) {
                chip8->i = (uint16_t)(chip8->i + x);
                if (!chip8_has_quirk(chip8, CHIP8_QUIRK_MEMORY_INCREMENT_BY_X)) {
                    ++chip8->i;
                }
            }
            return true;
        case 0x75u: { /* FX75: store V0..Vx into RPL flags (x clamped to 7) */
            uint8_t count = (uint8_t)((x > 7u) ? 7u : x);
            for (uint8_t reg = 0; reg <= count; ++reg) {
                chip8->rpl[reg] = chip8->v[reg];
            }
            return true;
        }
        case 0x85u: { /* FX85: load V0..Vx from RPL flags (x clamped to 7) */
            uint8_t count = (uint8_t)((x > 7u) ? 7u : x);
            for (uint8_t reg = 0; reg <= count; ++reg) {
                chip8->v[reg] = chip8->rpl[reg];
            }
            return true;
        }
        default:
            return chip8_halt(chip8);
        }

    default:
        return chip8_halt(chip8);
    }
}

void Chip8_RunCycles(Chip8 *chip8, uint16_t cycles)
{
    for (uint16_t i = 0; i < cycles; ++i) {
        if (!Chip8_Step(chip8)) {
            break;
        }
    }
}
