#ifndef EMULATORS_H
#define EMULATORS_H

#include "ui/Screen.h"

#include <cstdint>

namespace emulators {

using NavigateFn = void (*)(ui::Screen &screen, void *ctx);
using BackFn = void (*)(void *ctx);

struct Driver {
    const char *icon;
    const char *name;
    const char *meta;
    void (*init)(NavigateFn navigate, void *navigate_ctx, BackFn back, void *back_ctx);
    void (*showRomList)();
    void (*destroyRomList)();
    bool (*isGameScreen)(const ui::Screen *screen);
    void (*task)();
    void (*refreshImageIfNeeded)();
};

void initAll(NavigateFn navigate, void *navigate_ctx, BackFn back, void *back_ctx);
std::uint8_t count();
const Driver *get(std::uint8_t index);
const Driver *activeGame(const ui::Screen *screen);
void destroyRomLists();

} // namespace emulators

#endif
