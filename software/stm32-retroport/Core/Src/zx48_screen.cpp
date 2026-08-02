#include "zx48_screen.h"

#include "audio.h"
#include "display_config.h"
#include "emulator_browser.h"
#include "input.h"
#include "main.h"
#include "rompack.h"
#include "runtime_workspace.h"
#include "st7735.h"
#include "ui/ui.h"
#include "zx48.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace ui;

namespace zx48ui {
namespace {

constexpr std::uint32_t kFramePeriodMs = 20;
constexpr std::uint32_t kRenderPeriodMs = 40;
constexpr std::uint32_t kExitHoldMs = 900;
constexpr std::uint32_t kModeCycleHoldMs = 500;

Screen g_game_screen;
NavigateFn g_navigate = nullptr;
void *g_navigate_ctx = nullptr;

Zx48 g_zx;
char g_current_title[ROMPACK_TITLE_SIZE] = "ZX Spectrum 48K";
std::uint8_t g_sna_header[RUNTIME_WORKSPACE_ZX48_SNA_HEADER_SIZE];
std::uint32_t g_last_frame_ms = 0;
std::uint32_t g_last_render_ms = 0;
std::uint32_t g_exit_combo_started_ms = 0;
bool g_game_built = false;
bool g_input_armed = false;
bool g_frame_dirty = false;
bool g_force_redraw = false;
std::uint32_t g_mode_combo_started_ms = 0;
bool g_mode_combo_consumed = false;

enum class ControlMode : std::uint8_t {
    Kempston = 0,
    Qaopm,
    Cursor,
    Sinclair2,
    Count,
};

ControlMode g_control_mode = ControlMode::Kempston;

struct ZxKey {
    std::uint8_t row;
    std::uint8_t bit;
};

constexpr ZxKey kKeyCapsShift{0u, 0u};
constexpr ZxKey kKeyB{7u, 4u};
constexpr ZxKey kKeySpace{7u, 0u};
constexpr ZxKey kKeyEnter{6u, 0u};
constexpr ZxKey kKeyA{1u, 0u};
constexpr ZxKey kKeyC{0u, 3u};
constexpr ZxKey kKeyQ{2u, 0u};
constexpr ZxKey kKeyJ{6u, 3u};
constexpr ZxKey kKeyK{6u, 2u};
constexpr ZxKey kKeyM{7u, 2u};
constexpr ZxKey kKeyO{5u, 1u};
constexpr ZxKey kKeyP{5u, 0u};
constexpr ZxKey kKey1{3u, 0u};
constexpr ZxKey kKey2{3u, 1u};
constexpr ZxKey kKey5{3u, 4u};
constexpr ZxKey kKey6{4u, 4u};
constexpr ZxKey kKey7{4u, 3u};
constexpr ZxKey kKey8{4u, 2u};
constexpr ZxKey kKey9{4u, 1u};
constexpr ZxKey kKey0{4u, 0u};

struct SnaLoadCtx {
    std::uint8_t *header;
    std::uint8_t *ram;
};

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

void build_game_screen()
{
    if (g_game_built) {
        return;
    }

    Widget root = g_game_screen.applyTheme();
    lv_obj_set_style_bg_color(root.raw(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root.raw(), LV_OPA_COVER, LV_PART_MAIN);
    g_game_built = true;
}

void destroy_game_screen()
{
    g_game_screen.destroy();
    g_game_built = false;
}

bool load_sna_chunk(const std::uint8_t *data, std::size_t size, std::size_t offset, void *ctx)
{
    SnaLoadCtx *load = static_cast<SnaLoadCtx *>(ctx);

    for (std::size_t i = 0; i < size; ++i) {
        std::size_t absolute = offset + i;
        if (absolute < RUNTIME_WORKSPACE_ZX48_SNA_HEADER_SIZE) {
            load->header[absolute] = data[i];
        } else if (absolute < RUNTIME_WORKSPACE_ZX48_SNA_SIZE) {
            load->ram[absolute - RUNTIME_WORKSPACE_ZX48_SNA_HEADER_SIZE] = data[i];
        } else {
            return false;
        }
    }
    return true;
}

std::uint8_t current_kempston_mask()
{
    std::uint8_t mask = 0;

    if (Input_IsDown(INPUT_BUTTON_RIGHT)) {
        mask |= 0x01u;
    }
    if (Input_IsDown(INPUT_BUTTON_LEFT)) {
        mask |= 0x02u;
    }
    if (Input_IsDown(INPUT_BUTTON_DOWN)) {
        mask |= 0x04u;
    }
    if (Input_IsDown(INPUT_BUTTON_UP)) {
        mask |= 0x08u;
    }
    if (Input_IsDown(INPUT_BUTTON_A)) {
        mask |= 0x10u;
    }
    return mask;
}

void press_key(ZxKey key)
{
    Zx48_SetKey(&g_zx, key.row, key.bit, true);
}

void press_cursor_key(ZxKey number_key)
{
    press_key(kKeyCapsShift);
    press_key(number_key);
}

ControlMode default_control_mode_for_title(const char *title)
{
    if (title != nullptr && std::strstr(title, "KOSMOS") != nullptr) {
        return ControlMode::Qaopm;
    }
    return ControlMode::Kempston;
}

void cycle_control_mode()
{
    std::uint8_t next = static_cast<std::uint8_t>(g_control_mode) + 1u;
    if (next >= static_cast<std::uint8_t>(ControlMode::Count)) {
        next = 0u;
    }
    g_control_mode = static_cast<ControlMode>(next);
    Audio_PlaySound(AUDIO_SOUND_SELECT);
}

bool handle_mode_combo(std::uint32_t now_ms)
{
    bool combo = Input_IsDown(INPUT_BUTTON_X) && Input_IsDown(INPUT_BUTTON_Y);

    if (!combo) {
        g_mode_combo_started_ms = 0u;
        g_mode_combo_consumed = false;
        return false;
    }

    if (g_mode_combo_started_ms == 0u) {
        g_mode_combo_started_ms = now_ms;
        return true;
    }

    if (!g_mode_combo_consumed && (now_ms - g_mode_combo_started_ms) >= kModeCycleHoldMs) {
        cycle_control_mode();
        g_mode_combo_consumed = true;
    }

    return true;
}

void apply_menu_keys()
{
    if (Input_IsDown(INPUT_BUTTON_X)) {
        press_key(kKey1);
        press_key(kKeyK);
    }
    if (Input_IsDown(INPUT_BUTTON_Y)) {
        press_key(kKey2);
        press_key(kKeyJ);
    }
    if (Input_IsDown(INPUT_BUTTON_B) && !Input_IsDown(INPUT_BUTTON_CENTER)) {
        press_key(kKey8);
        press_key(kKeyC);
    }
    if (Input_IsDown(INPUT_BUTTON_CENTER)) {
        press_key(kKeySpace);
        press_key(kKeyEnter);
        press_key(kKey0);
    }
}

void apply_direction_keys()
{
    switch (g_control_mode) {
    case ControlMode::Kempston:
        Zx48_SetKempston(&g_zx, current_kempston_mask());
        break;
    case ControlMode::Qaopm:
        if (Input_IsDown(INPUT_BUTTON_UP)) {
            press_key(kKeyQ);
        }
        if (Input_IsDown(INPUT_BUTTON_DOWN)) {
            press_key(kKeyA);
        }
        if (Input_IsDown(INPUT_BUTTON_LEFT)) {
            press_key(kKeyO);
        }
        if (Input_IsDown(INPUT_BUTTON_RIGHT)) {
            press_key(kKeyP);
        }
        break;
    case ControlMode::Cursor:
        if (Input_IsDown(INPUT_BUTTON_UP)) {
            press_cursor_key(kKey7);
        }
        if (Input_IsDown(INPUT_BUTTON_DOWN)) {
            press_cursor_key(kKey6);
        }
        if (Input_IsDown(INPUT_BUTTON_LEFT)) {
            press_cursor_key(kKey5);
        }
        if (Input_IsDown(INPUT_BUTTON_RIGHT)) {
            press_cursor_key(kKey8);
        }
        break;
    case ControlMode::Sinclair2:
        if (Input_IsDown(INPUT_BUTTON_UP)) {
            press_key(kKey9);
        }
        if (Input_IsDown(INPUT_BUTTON_DOWN)) {
            press_key(kKey8);
        }
        if (Input_IsDown(INPUT_BUTTON_LEFT)) {
            press_key(kKey6);
        }
        if (Input_IsDown(INPUT_BUTTON_RIGHT)) {
            press_key(kKey7);
        }
        break;
    case ControlMode::Count:
        break;
    }
}

void apply_fire_keys()
{
    if (!Input_IsDown(INPUT_BUTTON_A)) {
        return;
    }

    switch (g_control_mode) {
    case ControlMode::Kempston:
        Zx48_SetKempston(&g_zx, static_cast<std::uint8_t>(current_kempston_mask() | 0x10u));
        break;
    case ControlMode::Qaopm:
        press_key(kKeyM);
        break;
    case ControlMode::Cursor:
    case ControlMode::Sinclair2:
        press_key(kKey0);
        break;
    case ControlMode::Count:
        break;
    }
}

void set_mapped_input()
{
    std::uint32_t now_ms = HAL_GetTick();

    Zx48_ClearKeys(&g_zx);
    Zx48_SetKempston(&g_zx, 0u);

    if (!g_input_armed) {
        if (Input_GetDownMask() == 0u) {
            g_input_armed = true;
        } else {
            return;
        }
    }

    if (handle_mode_combo(now_ms)) {
        return;
    }

    apply_direction_keys();
    apply_fire_keys();
    apply_menu_keys();
}

void refresh_image()
{
    for (std::uint16_t y = 0; y < DISPLAY_VER_RES; y += RUNTIME_WORKSPACE_ZX48_STRIP_LINES) {
        std::uint16_t strip_lines = RUNTIME_WORKSPACE_ZX48_STRIP_LINES;
        if ((y + strip_lines) > DISPLAY_VER_RES) {
            strip_lines = DISPLAY_VER_RES - y;
        }

        for (std::uint16_t row = 0; row < strip_lines; ++row) {
            Zx48_RenderLine(&g_zx,
                            static_cast<std::uint16_t>(y + row),
                            &g_runtime_workspace.zx48.line[row * RUNTIME_WORKSPACE_ZX48_LINE_BYTES]);
        }

        (void)ST7735_WritePixels(0u,
                                 y,
                                 DISPLAY_HOR_RES,
                                 strip_lines,
                                 g_runtime_workspace.zx48.line,
                                 static_cast<std::size_t>(strip_lines) * RUNTIME_WORKSPACE_ZX48_LINE_BYTES);
    }

    g_frame_dirty = false;
    g_force_redraw = false;
    g_last_render_ms = HAL_GetTick();
}

void stop_game()
{
    Audio_ClearStream();
    Audio_PlaySound(AUDIO_SOUND_BACK);
    g_exit_combo_started_ms = 0;
    g_frame_dirty = false;
    g_force_redraw = false;
    drain_input_actions();
    showRomList();
    destroy_game_screen();
}

void start_game(const emulator_browser::RomItem &rom)
{
    std::uint32_t now_ms = HAL_GetTick();
    std::size_t rom_size = rom.size;
    SnaLoadCtx load{};

    if (rom.size != RUNTIME_WORKSPACE_ZX48_SNA_SIZE) {
        showRomList();
        return;
    }

    std::memset(&g_runtime_workspace.zx48, 0, sizeof(g_runtime_workspace.zx48));
    std::memset(g_sna_header, 0, sizeof(g_sna_header));
    Zx48_Init(&g_zx, g_runtime_workspace.zx48.ram);

    load.header = g_sna_header;
    load.ram = g_runtime_workspace.zx48.ram;
    if (!RomPack_LoadStream(rom.rompack_index,
                            g_runtime_workspace.zx48.scratch,
                            RUNTIME_WORKSPACE_ZX48_SCRATCH_SIZE,
                            load_sna_chunk,
                            &load,
                            &rom_size) ||
        rom_size != RUNTIME_WORKSPACE_ZX48_SNA_SIZE ||
        !Zx48_LoadSnaHeader(&g_zx, g_sna_header)) {
        showRomList();
        return;
    }

    std::snprintf(g_current_title,
                  sizeof(g_current_title),
                  "%s",
                  rom.title != nullptr ? rom.title : "ZX Spectrum 48K");

    g_last_frame_ms = now_ms;
    g_last_render_ms = 0u;
    g_exit_combo_started_ms = 0u;
    g_mode_combo_started_ms = 0u;
    g_mode_combo_consumed = false;
    g_control_mode = default_control_mode_for_title(g_current_title);
    g_input_armed = false;
    g_frame_dirty = true;
    g_force_redraw = true;

    Audio_StopMusic();
    Audio_SetStream(Zx48_AudioSample, &g_zx);
    build_game_screen();
    drain_input_actions();
    navigate(g_game_screen);
    emulator_browser::hide();
}

void on_rom_select(const emulator_browser::RomItem &rom, void * /*ctx*/)
{
    start_game(rom);
}

} // namespace

void init(NavigateFn navigateFn, void *navigateCtx, BackFn backFn, void *backCtx)
{
    g_navigate = navigateFn;
    g_navigate_ctx = navigateCtx;
    emulator_browser::init(navigateFn, navigateCtx, backFn, backCtx);
}

void showRomList()
{
    emulator_browser::Config config{};
    config.system_id = ROMPACK_SYSTEM_ZX48;
    config.title = "ZX48K"; // short form so it clears the battery indicator
    config.empty_hint = "Upload SNA pack first";
    config.max_rom_size = RUNTIME_WORKSPACE_ZX48_SNA_SIZE;
    config.on_select = on_rom_select;
    config.select_ctx = nullptr;
    emulator_browser::show(config);
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
    bool exit_combo = Input_IsDown(INPUT_BUTTON_CENTER) && Input_IsDown(INPUT_BUTTON_Y);

    drain_input_actions();
    set_mapped_input();

    if (exit_combo) {
        if (g_exit_combo_started_ms == 0u) {
            g_exit_combo_started_ms = now_ms;
        } else if ((now_ms - g_exit_combo_started_ms) >= kExitHoldMs) {
            stop_game();
            return;
        }
    } else {
        g_exit_combo_started_ms = 0u;
    }

    if ((now_ms - g_last_frame_ms) >= kFramePeriodMs) {
        Zx48_RunFrame(&g_zx);
        g_frame_dirty = true;
        g_last_frame_ms += kFramePeriodMs;
        if ((now_ms - g_last_frame_ms) >= (kFramePeriodMs * 2u)) {
            g_last_frame_ms = now_ms;
        }
    }

    if (Zx48_IsFaulted(&g_zx)) {
        Audio_ClearStream();
    }
}

void refreshImageIfNeeded()
{
    std::uint32_t now_ms = HAL_GetTick();

    if (g_force_redraw || (g_frame_dirty && (now_ms - g_last_render_ms) >= kRenderPeriodMs)) {
        refresh_image();
    }
}

} // namespace zx48ui
