#include "zx48.h"

#include <string.h>

#define ZX48_SCREEN_BYTES 6144u
#define ZX48_DISPLAY_Y 4u
#define ZX48_DISPLAY_W 160u
#define ZX48_DISPLAY_H 120u

static uint8_t zx48_read_byte(void *ctx, uint16_t addr);
static void zx48_write_byte(void *ctx, uint16_t addr, uint8_t value);
static uint8_t zx48_port_in(z80 *cpu, uint16_t port);
static void zx48_port_out(z80 *cpu, uint16_t port, uint8_t value);
static uint8_t rom_read(uint16_t addr);

#define ZX48_ROM_RST38 0x0038u
#define ZX48_ROM_NMI 0x0066u
#define ZX48_ROM_KEY_SCAN 0x028Eu
#define ZX48_KEY_CAPS_SHIFT_CODE 0x27u
#define ZX48_KEY_SYMBOL_SHIFT_CODE 0x18u
#define ZX48_KEY_B_CODE 0x00u
#define ZX48_KEY_SPACE_CODE 0x20u
#define ZX48_KEY_ENTER_CODE 0x21u
#define ZX48_KEY_A_CODE 0x26u
#define ZX48_KEY_Q_CODE 0x25u
#define ZX48_KEY_O_CODE 0x1Au
#define ZX48_KEY_P_CODE 0x22u
#define ZX48_KEY_1_CODE 0x24u
#define ZX48_KEY_2_CODE 0x1Cu
#define ZX48_KEY_0_CODE 0x23u

static const uint16_t zx48_palette[2][8] = {
    {
        0x0000u, 0x0019u, 0xC800u, 0xC819u,
        0x0660u, 0x0679u, 0xCE60u, 0xCE79u,
    },
    {
        0x0000u, 0x001Fu, 0xF800u, 0xF81Fu,
        0x07E0u, 0x07FFu, 0xFFE0u, 0xFFFFu,
    },
};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint8_t zx48_read_mem_raw(const Zx48 *zx, uint16_t addr)
{
    if (addr < 0x4000u) {
        return rom_read(addr);
    }
    return zx->ram[addr - 0x4000u];
}

static uint16_t zx48_pop_word(Zx48 *zx)
{
    uint16_t sp = zx->cpu.sp;
    uint16_t value = (uint16_t)((uint16_t)zx48_read_mem_raw(zx, sp) |
                                ((uint16_t)zx48_read_mem_raw(zx, (uint16_t)(sp + 1u)) << 8));
    zx->cpu.sp = (uint16_t)(sp + 2u);
    return value;
}

static uint8_t rom_read(uint16_t addr)
{
    switch (addr) {
    case 0x0038u:
        return 0xFBu; /* EI */
    case 0x0039u:
        return 0xC9u; /* RET */
    case 0x0066u:
        return 0xEDu;
    case 0x0067u:
        return 0x45u; /* RETN */
    default:
        return 0xFFu;
    }
}

static void install_callbacks(Zx48 *zx)
{
    zx->cpu.read_byte = zx48_read_byte;
    zx->cpu.write_byte = zx48_write_byte;
    zx->cpu.port_in = zx48_port_in;
    zx->cpu.port_out = zx48_port_out;
    zx->cpu.userdata = zx;
}

static void release_all_keys(Zx48 *zx)
{
    for (uint8_t row = 0; row < ZX48_KEY_ROW_COUNT; ++row) {
        zx->keyboard_rows[row] = 0x1Fu;
    }
}

static void set_flags(z80 *cpu, uint8_t f)
{
    cpu->cf = (f >> 0) & 1u;
    cpu->nf = (f >> 1) & 1u;
    cpu->pf = (f >> 2) & 1u;
    cpu->xf = (f >> 3) & 1u;
    cpu->hf = (f >> 4) & 1u;
    cpu->yf = (f >> 5) & 1u;
    cpu->zf = (f >> 6) & 1u;
    cpu->sf = (f >> 7) & 1u;
}

static void set_af(z80 *cpu, uint16_t af)
{
    cpu->a = (uint8_t)(af >> 8);
    set_flags(cpu, (uint8_t)af);
}

static void set_bc(z80 *cpu, uint16_t bc)
{
    cpu->b = (uint8_t)(bc >> 8);
    cpu->c = (uint8_t)bc;
}

static void set_de(z80 *cpu, uint16_t de)
{
    cpu->d = (uint8_t)(de >> 8);
    cpu->e = (uint8_t)de;
}

static void set_hl(z80 *cpu, uint16_t hl)
{
    cpu->h = (uint8_t)(hl >> 8);
    cpu->l = (uint8_t)hl;
}

static uint8_t zx48_read_byte(void *ctx, uint16_t addr)
{
    const Zx48 *zx = (const Zx48 *)ctx;

    if (addr < 0x4000u) {
        return rom_read(addr);
    }
    return zx->ram[addr - 0x4000u];
}

static void zx48_write_byte(void *ctx, uint16_t addr, uint8_t value)
{
    Zx48 *zx = (Zx48 *)ctx;

    if (addr >= 0x4000u) {
        zx->ram[addr - 0x4000u] = value;
    }
}

static uint8_t zx48_read_keyboard(const Zx48 *zx, uint16_t port)
{
    uint8_t high = (uint8_t)(port >> 8);
    uint8_t value = 0x1Fu;

    for (uint8_t row = 0; row < ZX48_KEY_ROW_COUNT; ++row) {
        if ((high & (uint8_t)(1u << row)) == 0u) {
            value &= zx->keyboard_rows[row];
        }
    }

    return (uint8_t)(0xE0u | value);
}

static uint8_t zx48_port_in(z80 *cpu, uint16_t port)
{
    const Zx48 *zx = (const Zx48 *)cpu->userdata;

    if ((port & 0x00FFu) == 0x001Fu) {
        return zx->kempston;
    }
    if ((port & 1u) == 0u) {
        return zx48_read_keyboard(zx, port);
    }
    return 0xFFu;
}

static void zx48_port_out(z80 *cpu, uint16_t port, uint8_t value)
{
    Zx48 *zx = (Zx48 *)cpu->userdata;

    if ((port & 1u) == 0u) {
        zx->border_color = value & 0x07u;
        zx->beeper_level = (value & 0x10u) != 0u ? 1u : 0u;
    }
}

static uint16_t zx48_color(uint8_t index, bool bright)
{
    return zx48_palette[bright ? 1u : 0u][index & 0x07u];
}

static void write_pixel(uint8_t *line, uint16_t x, uint16_t color)
{
    line[x * 2u] = (uint8_t)(color >> 8);
    line[x * 2u + 1u] = (uint8_t)color;
}

void Zx48_Init(Zx48 *zx, uint8_t *ram)
{
    if (zx == NULL) {
        return;
    }

    memset(zx, 0, sizeof(*zx));
    zx->ram = ram;
    release_all_keys(zx);
    z80_init(&zx->cpu);
    install_callbacks(zx);
}

bool Zx48_LoadSnaHeader(Zx48 *zx, const uint8_t header[RUNTIME_WORKSPACE_ZX48_SNA_HEADER_SIZE])
{
    uint16_t sp;
    uint16_t pc;
    bool iff2;

    if (zx == NULL || zx->ram == NULL || header == NULL) {
        return false;
    }

    z80_init(&zx->cpu);
    install_callbacks(zx);
    release_all_keys(zx);

    zx->cpu.i = header[0];

    zx->cpu.l_ = header[1];
    zx->cpu.h_ = header[2];
    zx->cpu.e_ = header[3];
    zx->cpu.d_ = header[4];
    zx->cpu.c_ = header[5];
    zx->cpu.b_ = header[6];
    zx->cpu.f_ = header[7];
    zx->cpu.a_ = header[8];

    set_hl(&zx->cpu, rd16(&header[9]));
    set_de(&zx->cpu, rd16(&header[11]));
    set_bc(&zx->cpu, rd16(&header[13]));
    zx->cpu.iy = rd16(&header[15]);
    zx->cpu.ix = rd16(&header[17]);

    iff2 = (header[19] & 0x04u) != 0u;
    zx->cpu.iff1 = iff2;
    zx->cpu.iff2 = iff2;
    zx->cpu.r = header[20];
    set_af(&zx->cpu, rd16(&header[21]));

    sp = rd16(&header[23]);
    if (sp < 0x4000u || sp > 0xFFFEu) {
        zx->faulted = true;
        return false;
    }

    pc = rd16(&zx->ram[sp - 0x4000u]);
    zx->cpu.sp = (uint16_t)(sp + 2u);
    zx->cpu.pc = pc;
    zx->cpu.mem_ptr = pc;
    zx->cpu.interrupt_mode = header[25] <= 2u ? header[25] : 1u;
    zx->border_color = header[26] & 0x07u;
    zx->beeper_level = 0u;
    zx->cpu.cyc = 0u;
    zx->cpu.halted = 0u;
    zx->cpu.faulted = 0u;
    zx->faulted = false;
    zx->loaded = true;
    return true;
}

void Zx48_SetKempston(Zx48 *zx, uint8_t mask)
{
    if (zx != NULL) {
        zx->kempston = mask & 0x1Fu;
    }
}

void Zx48_ClearKeys(Zx48 *zx)
{
    if (zx != NULL) {
        release_all_keys(zx);
    }
}

void Zx48_SetKey(Zx48 *zx, uint8_t row, uint8_t bit, bool down)
{
    uint8_t mask;

    if (zx == NULL || row >= ZX48_KEY_ROW_COUNT || bit >= ZX48_KEY_BITS_PER_ROW) {
        return;
    }

    mask = (uint8_t)(1u << bit);
    if (down) {
        zx->keyboard_rows[row] &= (uint8_t)~mask;
    } else {
        zx->keyboard_rows[row] |= mask;
    }
}

static uint8_t zx48_key_scan_code(uint8_t row, uint8_t bit)
{
    return (uint8_t)((0x2Fu - row) - (8u * (bit + 1u)));
}

static bool zx48_key_code_is_shift(uint8_t code)
{
    return code == ZX48_KEY_CAPS_SHIFT_CODE || code == ZX48_KEY_SYMBOL_SHIFT_CODE;
}

static bool zx48_key_scan_has_code(const uint8_t *codes, uint8_t count, uint8_t code)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (codes[i] == code) {
            return true;
        }
    }
    return false;
}

static bool zx48_key_scan_collapse(const uint8_t *codes, uint8_t count, uint8_t *code)
{
    static const uint8_t priority[] = {
        ZX48_KEY_SPACE_CODE,
        ZX48_KEY_Q_CODE,
        ZX48_KEY_A_CODE,
        ZX48_KEY_O_CODE,
        ZX48_KEY_P_CODE,
        ZX48_KEY_0_CODE,
        ZX48_KEY_1_CODE,
        ZX48_KEY_2_CODE,
        ZX48_KEY_ENTER_CODE,
        ZX48_KEY_B_CODE,
    };

    for (uint8_t i = 0; i < (uint8_t)(sizeof(priority) / sizeof(priority[0])); ++i) {
        if (zx48_key_scan_has_code(codes, count, priority[i])) {
            *code = priority[i];
            return true;
        }
    }

    return false;
}

static bool zx48_key_scan_result(const Zx48 *zx, uint8_t *d, uint8_t *e)
{
    uint8_t codes[ZX48_KEY_ROW_COUNT * ZX48_KEY_BITS_PER_ROW];
    uint8_t count = 0u;

    for (uint8_t row = 0; row < ZX48_KEY_ROW_COUNT; ++row) {
        uint8_t pressed = (uint8_t)(~zx->keyboard_rows[row] & 0x1Fu);
        for (uint8_t bit = 0; bit < ZX48_KEY_BITS_PER_ROW; ++bit) {
            if ((pressed & (uint8_t)(1u << bit)) == 0u) {
                continue;
            }
            codes[count] = zx48_key_scan_code(row, bit);
            ++count;
        }
    }

    if (count == 0u) {
        *d = 0xFFu;
        *e = 0xFFu;
        return true;
    }
    if (count == 1u) {
        *d = 0xFFu;
        *e = codes[0];
        return true;
    }
    if (count == 2u) {
        bool first_shift = zx48_key_code_is_shift(codes[0]);
        bool second_shift = zx48_key_code_is_shift(codes[1]);

        if (first_shift != second_shift) {
            *d = first_shift ? codes[0] : codes[1];
            *e = first_shift ? codes[1] : codes[0];
            return true;
        }
    }

    if (zx48_key_scan_collapse(codes, count, e)) {
        *d = 0xFFu;
        return true;
    }

    *d = codes[0];
    *e = count > 1u ? codes[1] : 0xFFu;
    return false;
}

static void zx48_set_key_scan_flags(z80 *cpu, bool valid)
{
    cpu->sf = 0u;
    cpu->zf = valid ? 1u : 0u;
    cpu->yf = 0u;
    cpu->hf = 0u;
    cpu->xf = 0u;
    cpu->pf = valid ? 1u : 0u;
    cpu->nf = 0u;
    cpu->cf = 0u;
}

static void zx48_rom_ret(Zx48 *zx)
{
    uint16_t pc = zx48_pop_word(zx);
    zx->cpu.pc = pc;
    zx->cpu.mem_ptr = pc;
    zx->cpu.cyc += 10u;
}

static bool zx48_try_rom_trap(Zx48 *zx)
{
    uint16_t pc = zx->cpu.pc;

    if (pc == ZX48_ROM_KEY_SCAN) {
        bool valid = zx48_key_scan_result(zx, &zx->cpu.d, &zx->cpu.e);
        zx48_set_key_scan_flags(&zx->cpu, valid);
        zx48_rom_ret(zx);
        return true;
    }

    if (pc < 0x4000u && pc != ZX48_ROM_RST38 && pc != (ZX48_ROM_RST38 + 1u) && pc != ZX48_ROM_NMI) {
        zx48_rom_ret(zx);
        return true;
    }

    return false;
}

void Zx48_RunFrame(Zx48 *zx)
{
    unsigned long start;

    if (zx == NULL || !zx->loaded || zx->faulted) {
        return;
    }

    start = zx->cpu.cyc;
    z80_gen_int(&zx->cpu, 0xFFu);
    while ((unsigned long)(zx->cpu.cyc - start) < ZX48_FRAME_TSTATES) {
        if (!zx48_try_rom_trap(zx)) {
            z80_step(&zx->cpu);
        }
        if (zx->cpu.faulted) {
            zx->faulted = true;
            break;
        }
    }
}

void Zx48_RenderLine(const Zx48 *zx, uint16_t y, uint8_t *rgb565_line)
{
    uint16_t border;
    uint16_t sy;
    uint16_t bitmap_y_base;
    uint16_t attr_y_base;

    if (zx == NULL || zx->ram == NULL || rgb565_line == NULL) {
        return;
    }

    border = zx48_color(zx->border_color, false);
    if (y < ZX48_DISPLAY_Y || y >= (ZX48_DISPLAY_Y + ZX48_DISPLAY_H)) {
        for (uint16_t x = 0; x < ZX48_DISPLAY_W; ++x) {
            write_pixel(rgb565_line, x, border);
        }
        return;
    }

    sy = (uint16_t)(((uint32_t)(y - ZX48_DISPLAY_Y) * 8u) / 5u);
    bitmap_y_base = (uint16_t)(((sy & 0xC0u) << 5) |
                               ((sy & 0x07u) << 8) |
                               ((sy & 0x38u) << 2));
    attr_y_base = (uint16_t)(ZX48_SCREEN_BYTES + (sy >> 3) * 32u);

    for (uint16_t group = 0; group < 32u; ++group) {
        uint8_t pixel_byte = zx->ram[bitmap_y_base + group];
        uint8_t attr = zx->ram[attr_y_base + group];
        bool bright = (attr & 0x40u) != 0u;
        uint16_t ink = zx48_color(attr & 0x07u, bright);
        uint16_t paper = zx48_color((attr >> 3) & 0x07u, bright);
        uint16_t out = (uint16_t)(group * 5u);

        write_pixel(rgb565_line, out + 0u, (pixel_byte & 0x80u) != 0u ? ink : paper);
        write_pixel(rgb565_line, out + 1u, (pixel_byte & 0x40u) != 0u ? ink : paper);
        write_pixel(rgb565_line, out + 2u, (pixel_byte & 0x10u) != 0u ? ink : paper);
        write_pixel(rgb565_line, out + 3u, (pixel_byte & 0x08u) != 0u ? ink : paper);
        write_pixel(rgb565_line, out + 4u, (pixel_byte & 0x02u) != 0u ? ink : paper);
    }
}

int8_t Zx48_AudioSample(void *ctx)
{
    const Zx48 *zx = (const Zx48 *)ctx;

    if (zx == NULL || !zx->loaded || zx->faulted) {
        return 0;
    }
    return zx->beeper_level != 0u ? 42 : -42;
}

bool Zx48_IsFaulted(const Zx48 *zx)
{
    return zx == NULL || zx->faulted || zx->cpu.faulted;
}
