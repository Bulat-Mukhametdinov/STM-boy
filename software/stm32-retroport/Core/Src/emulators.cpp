#include "emulators.h"

#include "chip8_screen.h"
#include "ui/ui.h"
#include "zx48_screen.h"

#include <cstddef>

namespace emulators {
namespace {

const Driver kDrivers[] = {
    {
        LV_SYMBOL_PLAY,
        "CHIP-8",
        "ROMs from flash",
        chip8ui::init,
        chip8ui::showRomList,
        chip8ui::destroyRomList,
        chip8ui::isGameScreen,
        chip8ui::task,
        chip8ui::refreshImageIfNeeded,
    },
    {
        LV_SYMBOL_PLAY,
        "SUPER-CHIP",
        "SCHIP 1.1 ROMs",
        schipui::init,
        schipui::showRomList,
        schipui::destroyRomList,
        schipui::isGameScreen,
        schipui::task,
        schipui::refreshImageIfNeeded,
    },
    {
        LV_SYMBOL_PLAY,
        "ZX Spectrum 48K",
        "SNA snapshots",
        zx48ui::init,
        zx48ui::showRomList,
        zx48ui::destroyRomList,
        zx48ui::isGameScreen,
        zx48ui::task,
        zx48ui::refreshImageIfNeeded,
    },
};

constexpr std::uint8_t kDriverCount = static_cast<std::uint8_t>(sizeof(kDrivers) / sizeof(kDrivers[0]));

} // namespace

void initAll(NavigateFn navigate, void *navigateCtx, BackFn back, void *backCtx)
{
    for (std::uint8_t i = 0; i < kDriverCount; ++i) {
        if (kDrivers[i].init != nullptr) {
            kDrivers[i].init(navigate, navigateCtx, back, backCtx);
        }
    }
}

std::uint8_t count()
{
    return kDriverCount;
}

const Driver *get(std::uint8_t index)
{
    if (index >= kDriverCount) {
        return nullptr;
    }
    return &kDrivers[index];
}

const Driver *activeGame(const ui::Screen *screen)
{
    for (std::uint8_t i = 0; i < kDriverCount; ++i) {
        if (kDrivers[i].isGameScreen != nullptr && kDrivers[i].isGameScreen(screen)) {
            return &kDrivers[i];
        }
    }
    return nullptr;
}

void destroyRomLists()
{
    for (std::uint8_t i = 0; i < kDriverCount; ++i) {
        if (kDrivers[i].destroyRomList != nullptr) {
            kDrivers[i].destroyRomList();
        }
    }
}

} // namespace emulators
