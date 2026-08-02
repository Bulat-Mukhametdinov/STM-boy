#ifndef CHIP8_SCREEN_H
#define CHIP8_SCREEN_H

#include "ui/Screen.h"

namespace chip8ui {

using NavigateFn = void (*)(ui::Screen &screen, void *ctx);
using BackFn = void (*)(void *ctx);

void init(NavigateFn navigate, void *navigateCtx, BackFn back, void *backCtx);
void showRomList();
void destroyRomList();
bool isGameScreen(const ui::Screen *screen);
ui::Screen &gameScreen();
void task();
void refreshImageIfNeeded();

} // namespace chip8ui

namespace schipui {

using NavigateFn = void (*)(ui::Screen &screen, void *ctx);
using BackFn = void (*)(void *ctx);

void init(NavigateFn navigate, void *navigateCtx, BackFn back, void *backCtx);
void showRomList();
void destroyRomList();
bool isGameScreen(const ui::Screen *screen);
ui::Screen &gameScreen();
void task();
void refreshImageIfNeeded();

} // namespace schipui

#endif
