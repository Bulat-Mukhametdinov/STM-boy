#ifndef EMULATOR_BROWSER_H
#define EMULATOR_BROWSER_H

#include "ui/Screen.h"

#include <cstddef>
#include <cstdint>

namespace emulator_browser {

using NavigateFn = void (*)(ui::Screen &screen, void *ctx);
using BackFn = void (*)(void *ctx);

struct RomItem {
    const char *title;
    const char *meta;
    std::uint32_t size;
    std::uint16_t rompack_index;
    std::uint16_t option_flags;
    std::uint16_t tickrate;
};

using SelectFn = void (*)(const RomItem &rom, void *ctx);

struct Config {
    std::uint8_t system_id;
    const char *title;
    const char *empty_hint;
    std::uint32_t max_rom_size;
    SelectFn on_select;
    void *select_ctx;
};

void init(NavigateFn navigate, void *navigate_ctx, BackFn back, void *back_ctx);
void show(const Config &config);
void hide();
void destroy();
bool isScreen(const ui::Screen *screen);
ui::Screen &screen();

} // namespace emulator_browser

#endif
