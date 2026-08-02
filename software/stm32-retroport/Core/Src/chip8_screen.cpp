#include "chip8_screen.h"

#include "audio.h"
#include "chip8.h"
#include "emulator_browser.h"
#include "input.h"
#include "main.h"
#include "rompack.h"
#include "runtime_workspace.h"
#include "st7735.h"
#include "ui/ui.h"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>

using namespace ui;

/* Shared CHIP-8 / SUPER-CHIP runtime. Both the chip8ui and schipui drivers
 * forward to these internals; only one game runs at a time, so they share a
 * single game screen and workspace. The active engine variant is locked in at
 * start_game() time and is independent of which driver's task() runs. */
namespace chip8_detail {

using NavigateFn = void (*)(ui::Screen &screen, void *ctx);
using BackFn = void (*)(void *ctx);

namespace {

constexpr std::uint32_t kTimerHz = 60;
constexpr std::uint32_t kTimerAccumulatorScale = 1000;
constexpr std::uint32_t kMaxFrameElapsedMs = 100;
constexpr std::uint16_t kDefaultCyclesPerTick = 10;
constexpr std::uint16_t kMaxCyclesPerTick = 1000;
constexpr std::uint32_t kExitHoldMs = 900;
constexpr std::uint16_t kScreenX = 16;
constexpr std::uint16_t kScreenY = 34;
/* On-screen viewport is always 128x64: lores 64x32 @ scale 2, hires 128x64 @ scale 1. */
constexpr std::uint16_t kOutW = CHIP8_DISPLAY_WIDTH_HI;
constexpr std::size_t kMaxRomSize = CHIP8_MEMORY_SIZE - CHIP8_ROM_START;
constexpr std::uint8_t kColorOnHi = 0xFF;
constexpr std::uint8_t kColorOnLo = 0xFF;
constexpr std::uint8_t kColorOffHi = 0x00;
constexpr std::uint8_t kColorOffLo = 0x00;

Screen g_game_screen;
NavigateFn g_navigate = nullptr;
void *g_navigate_ctx = nullptr;

#define g_chip8 (g_runtime_workspace.chip8.chip8)
#define g_rom_buffer (g_runtime_workspace.chip8.rom_buffer)
#define g_previous_display (g_runtime_workspace.chip8.previous_display)
#define g_line (g_runtime_workspace.chip8.line)

char g_current_title[ROMPACK_TITLE_SIZE] = "CHIP-8";
lv_obj_t *g_title_label = nullptr;
std::uint32_t g_last_timer_ms = 0;
std::uint32_t g_timer_accumulator = 0;
std::uint32_t g_exit_combo_started_ms = 0;
std::uint16_t g_buzzer_phase = 0;
std::uint16_t g_cycles_per_tick = kDefaultCyclesPerTick;
bool g_game_built = false;
bool g_sound_active = false;
bool g_input_armed = false;
bool g_force_full_redraw = false;
bool g_last_hires = false;

void navigate(Screen &screen)
{
    if (g_navigate != nullptr) {
        g_navigate(screen, g_navigate_ctx);
    }
}

void drain_input_actions()
{
    InputButton ignored;
    while (Input_PopAction(&ignored)) {
    }
}

int8_t chip8_buzzer_sample(void * /*ctx*/)
{
    g_buzzer_phase = static_cast<std::uint16_t>(g_buzzer_phase + 3584u);
    return (g_buzzer_phase & 0x8000u) != 0u ? 36 : -36;
}

void build_game_screen()
{
    if (g_game_built) {
        return;
    }

    const Theme &t = Theme::active();
    Widget root = g_game_screen.applyTheme();
    Stack column = VStack(root);
    column.padAll(t.space.padSm).gap(t.space.padSm);
    column.fill();

    g_title_label = Label::make(column, "CHIP-8", Label::Role::Title).raw();
    // Cap the title to a single line that clears the battery indicator in the
    // top-right corner. DOTS only ellipsizes when both width and height are
    // bounded; with auto height it would wrap instead, so pin the height to one
    // line of the title font.
    lv_obj_set_width(g_title_label, 100);
    lv_obj_set_height(g_title_label, lv_font_get_line_height(t.font.title));
    lv_label_set_long_mode(g_title_label, LV_LABEL_LONG_MODE_DOTS);

    Panel viewport = Panel::make(column, lv_color_hex(0x111515));
    viewport.width(LV_PCT(100));
    viewport.flexGrow(1);
    viewport.radius(2);

    Label::make(column, "Hold B + Center to exit", Label::Role::Caption);
    g_game_built = true;
}

void destroy_game_screen()
{
    g_game_screen.destroy();
    g_game_built = false;
    g_title_label = nullptr;
}

bool display_pixel(std::uint16_t x, std::uint16_t y)
{
    std::uint16_t width = Chip8_DisplayWidth(&g_chip8);
    std::uint16_t index = static_cast<std::uint16_t>(y * width + x);
    return (g_chip8.display[index >> 3] & static_cast<std::uint8_t>(0x80u >> (index & 7u))) != 0u;
}

void refresh_image()
{
    const std::uint16_t width = Chip8_DisplayWidth(&g_chip8);   /* 64 or 128 */
    const std::uint16_t height = Chip8_DisplayHeight(&g_chip8); /* 32 or 64  */
    const std::uint16_t stride = static_cast<std::uint16_t>(width / 8u);
    const std::uint16_t scale = g_chip8.hires ? 1u : 2u;       /* both -> 128 px wide */
    const std::uint16_t bytes_per_out_row = static_cast<std::uint16_t>(kOutW * 2u); /* 256 */

    /* A resolution change invalidates previous_display (stored at the old stride),
     * so a full redraw is forced and the whole 128x64 box is rewritten. */
    const bool full = g_force_full_redraw || (g_last_hires != g_chip8.hires);

    for (std::uint16_t y = 0; y < height; ++y) {
        std::uint16_t row_off = static_cast<std::uint16_t>(y * stride);
        bool row_changed = full;

        if (!row_changed) {
            for (std::uint16_t b = 0; b < stride; ++b) {
                if (g_chip8.display[row_off + b] != g_previous_display[row_off + b]) {
                    row_changed = true;
                    break;
                }
            }
        }

        if (!row_changed) {
            continue;
        }

        for (std::uint16_t x = 0; x < width; ++x) {
            bool on = display_pixel(x, y);
            std::uint8_t hi = on ? kColorOnHi : kColorOffHi;
            std::uint8_t lo = on ? kColorOnLo : kColorOffLo;
            for (std::uint16_t sx = 0; sx < scale; ++sx) {
                std::uint16_t ox = static_cast<std::uint16_t>((x * scale + sx) * 2u);
                for (std::uint16_t sy = 0; sy < scale; ++sy) {
                    std::uint16_t base = static_cast<std::uint16_t>(sy * bytes_per_out_row + ox);
                    g_line[base] = hi;
                    g_line[base + 1u] = lo;
                }
            }
        }

        (void)ST7735_WritePixels(
            kScreenX,
            static_cast<std::uint16_t>(kScreenY + y * scale),
            kOutW,
            scale,
            g_line,
            static_cast<std::size_t>(bytes_per_out_row * scale));

        for (std::uint16_t b = 0; b < stride; ++b) {
            g_previous_display[row_off + b] = g_chip8.display[row_off + b];
        }
    }

    g_chip8.display_dirty = false;
    g_chip8.draw_count = 0;
    g_force_full_redraw = false;
    g_last_hires = g_chip8.hires;
}

void set_mapped_keys()
{
    if (!g_input_armed) {
        if (Input_GetDownMask() == 0u) {
            g_input_armed = true;
        } else {
            for (std::uint8_t key = 0; key < CHIP8_KEY_COUNT; ++key) {
                Chip8_SetKey(&g_chip8, key, false);
            }
            return;
        }
    }

    Chip8_SetKey(&g_chip8, 0x2u, Input_IsDown(INPUT_BUTTON_UP));
    Chip8_SetKey(&g_chip8, 0x4u, Input_IsDown(INPUT_BUTTON_LEFT));
    Chip8_SetKey(&g_chip8, 0x6u, Input_IsDown(INPUT_BUTTON_RIGHT));
    Chip8_SetKey(&g_chip8, 0x8u, Input_IsDown(INPUT_BUTTON_DOWN));
    Chip8_SetKey(&g_chip8, 0x5u, Input_IsDown(INPUT_BUTTON_CENTER));
    Chip8_SetKey(&g_chip8, 0x7u, Input_IsDown(INPUT_BUTTON_A));
    Chip8_SetKey(&g_chip8, 0x9u, Input_IsDown(INPUT_BUTTON_B));
    Chip8_SetKey(&g_chip8, 0xEu, Input_IsDown(INPUT_BUTTON_X));
    Chip8_SetKey(&g_chip8, 0xFu, Input_IsDown(INPUT_BUTTON_Y));
}

bool should_refresh_image()
{
    return g_chip8.display_dirty;
}

void run_frame_cycles()
{
    for (std::uint16_t i = 0; i < g_cycles_per_tick; ++i) {
        if (!Chip8_Step(&g_chip8)) {
            break;
        }
        if (g_chip8.sprite_drawn && (g_chip8.quirks & CHIP8_QUIRK_VBLANK) != 0u) {
            break;
        }
    }
}

std::uint16_t cycles_per_tick(std::uint16_t requested)
{
    if (requested == 0u) {
        return kDefaultCyclesPerTick;
    }
    if (requested > kMaxCyclesPerTick) {
        return kMaxCyclesPerTick;
    }
    return requested;
}

void show_rom_list(std::uint8_t system_id, const char *title, emulator_browser::SelectFn on_select)
{
    emulator_browser::Config config{};
    config.system_id = system_id;
    config.title = title;
    config.empty_hint = "Upload ROM pack first";
    config.max_rom_size = kMaxRomSize;
    config.on_select = on_select;
    config.select_ctx = nullptr;
    emulator_browser::show(config);
}

void stop_game(std::uint8_t system_id, const char *title, emulator_browser::SelectFn on_select)
{
    Audio_PlaySound(AUDIO_SOUND_BACK);
    g_exit_combo_started_ms = 0;
    g_chip8.display_dirty = false;
    g_force_full_redraw = false;
    g_sound_active = false;
    Audio_ClearStream();
    drain_input_actions();
    show_rom_list(system_id, title, on_select);
    destroy_game_screen();
}

void start_game(const emulator_browser::RomItem &rom, std::uint8_t variant,
                std::uint8_t system_id, const char *list_title, emulator_browser::SelectFn on_select)
{
    std::uint32_t now_ms = HAL_GetTick();
    std::size_t rom_size = rom.size;

    if (rom.size > sizeof(g_rom_buffer) ||
        !RomPack_Load(rom.rompack_index, g_rom_buffer, sizeof(g_rom_buffer), &rom_size)) {
        show_rom_list(system_id, list_title, on_select);
        return;
    }

    std::snprintf(g_current_title, sizeof(g_current_title), "%s",
                  rom.title != nullptr ? rom.title : list_title);
    g_cycles_per_tick = cycles_per_tick(rom.tickrate);

    Chip8_Reset(&g_chip8, g_rom_buffer, rom_size, now_ms);
    Chip8_SetVariant(&g_chip8, variant);
    Chip8_SetQuirks(&g_chip8, rom.option_flags);
    g_last_timer_ms = now_ms;
    g_timer_accumulator = 0;
    g_exit_combo_started_ms = 0;
    g_buzzer_phase = 0;
    g_sound_active = false;
    g_input_armed = false;
    g_force_full_redraw = true;
    g_last_hires = false;
    build_game_screen();
    for (std::uint16_t i = 0; i < CHIP8_DISPLAY_SIZE; ++i) {
        g_previous_display[i] = 0u;
    }
    if (g_title_label != nullptr) {
        lv_label_set_text(g_title_label, g_current_title);
    }
    drain_input_actions();
    navigate(g_game_screen);
    emulator_browser::hide();
}

/* The active game's "back to its own list" context, captured at launch so the
 * exit gesture returns to the right (CHIP-8 vs SUPER-CHIP) browser. */
std::uint8_t g_active_system_id = ROMPACK_SYSTEM_CHIP8;
const char *g_active_title = "CHIP-8";
emulator_browser::SelectFn g_active_on_select = nullptr;

void on_rom_select_chip8(const emulator_browser::RomItem &rom, void * /*ctx*/);
void on_rom_select_schip(const emulator_browser::RomItem &rom, void * /*ctx*/);

void on_rom_select_chip8(const emulator_browser::RomItem &rom, void * /*ctx*/)
{
    g_active_system_id = ROMPACK_SYSTEM_CHIP8;
    g_active_title = "CHIP-8";
    g_active_on_select = on_rom_select_chip8;
    start_game(rom, CHIP8_VARIANT_CHIP8, ROMPACK_SYSTEM_CHIP8, "CHIP-8", on_rom_select_chip8);
}

void on_rom_select_schip(const emulator_browser::RomItem &rom, void * /*ctx*/)
{
    g_active_system_id = ROMPACK_SYSTEM_SCHIP;
    g_active_title = "SUPER-CHIP";
    g_active_on_select = on_rom_select_schip;
    start_game(rom, CHIP8_VARIANT_SCHIP, ROMPACK_SYSTEM_SCHIP, "SUPER-CHIP", on_rom_select_schip);
}

} // namespace

void init(NavigateFn navigateFn, void *navigateCtx, BackFn backFn, void *backCtx)
{
    g_navigate = navigateFn;
    g_navigate_ctx = navigateCtx;
    emulator_browser::init(navigateFn, navigateCtx, backFn, backCtx);
}

void destroyRomList()
{
    emulator_browser::destroy();
}

bool isGameScreen(const Screen *screen)
{
    return screen == &g_game_screen;
}

Screen &gameScreen()
{
    return g_game_screen;
}

void task()
{
    std::uint32_t now_ms = HAL_GetTick();
    bool exit_combo = Input_IsDown(INPUT_BUTTON_CENTER) && Input_IsDown(INPUT_BUTTON_B);

    drain_input_actions();
    set_mapped_keys();

    if (exit_combo) {
        if (g_exit_combo_started_ms == 0u) {
            g_exit_combo_started_ms = now_ms;
        } else if ((now_ms - g_exit_combo_started_ms) >= kExitHoldMs) {
            stop_game(g_active_system_id, g_active_title, g_active_on_select);
            return;
        }
    } else {
        g_exit_combo_started_ms = 0u;
    }

    if (now_ms != g_last_timer_ms) {
        std::uint32_t elapsed_ms = now_ms - g_last_timer_ms;
        if (elapsed_ms > kMaxFrameElapsedMs) {
            elapsed_ms = kMaxFrameElapsedMs;
        }
        g_last_timer_ms = now_ms;
        g_timer_accumulator += elapsed_ms * kTimerHz;

        while (g_timer_accumulator >= kTimerAccumulatorScale) {
            g_timer_accumulator -= kTimerAccumulatorScale;

            Chip8_TickTimers(&g_chip8);
            run_frame_cycles();
        }
    }

    if (g_chip8.sound_timer > 0u && !g_sound_active) {
        Audio_SetStream(chip8_buzzer_sample, nullptr);
        g_sound_active = true;
    } else if (g_chip8.sound_timer == 0u) {
        if (g_sound_active) {
            Audio_ClearStream();
        }
        g_sound_active = false;
    }
}

void refreshImageIfNeeded()
{
    if (should_refresh_image()) {
        refresh_image();
    }
}

void showRomListChip8()
{
    show_rom_list(ROMPACK_SYSTEM_CHIP8, "CHIP-8", on_rom_select_chip8);
}

void showRomListSchip()
{
    show_rom_list(ROMPACK_SYSTEM_SCHIP, "SUPER-CHIP", on_rom_select_schip);
}

} // namespace chip8_detail

namespace chip8ui {

void init(NavigateFn navigateFn, void *navigateCtx, BackFn backFn, void *backCtx)
{
    chip8_detail::init(navigateFn, navigateCtx, backFn, backCtx);
}

void showRomList()
{
    chip8_detail::showRomListChip8();
}

void destroyRomList()
{
    chip8_detail::destroyRomList();
}

bool isGameScreen(const ui::Screen *screen)
{
    return chip8_detail::isGameScreen(screen);
}

ui::Screen &gameScreen()
{
    return chip8_detail::gameScreen();
}

void task()
{
    chip8_detail::task();
}

void refreshImageIfNeeded()
{
    chip8_detail::refreshImageIfNeeded();
}

} // namespace chip8ui

namespace schipui {

void init(NavigateFn navigateFn, void *navigateCtx, BackFn backFn, void *backCtx)
{
    chip8_detail::init(navigateFn, navigateCtx, backFn, backCtx);
}

void showRomList()
{
    chip8_detail::showRomListSchip();
}

void destroyRomList()
{
    chip8_detail::destroyRomList();
}

bool isGameScreen(const ui::Screen *screen)
{
    return chip8_detail::isGameScreen(screen);
}

ui::Screen &gameScreen()
{
    return chip8_detail::gameScreen();
}

void task()
{
    chip8_detail::task();
}

void refreshImageIfNeeded()
{
    chip8_detail::refreshImageIfNeeded();
}

} // namespace schipui
