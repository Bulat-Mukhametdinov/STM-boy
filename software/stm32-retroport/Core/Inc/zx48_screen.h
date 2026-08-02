#ifndef ZX48_SCREEN_H
#define ZX48_SCREEN_H

#include "ui/Screen.h"

namespace zx48ui {

using NavigateFn = void (*)(ui::Screen &screen, void *ctx);
using BackFn = void (*)(void *ctx);

void init(NavigateFn navigateFn, void *navigateCtx, BackFn backFn, void *backCtx);
void showRomList();
void destroyRomList();
bool isGameScreen(const ui::Screen *screen);
ui::Screen &gameScreen();
void task();
void refreshImageIfNeeded();

} // namespace zx48ui

#endif
