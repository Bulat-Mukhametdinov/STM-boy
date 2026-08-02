#ifndef UI_COLOR_H
#define UI_COLOR_H

#include "lvgl.h"

#include <cstdint>

// Small color helpers for the UI framework. Header-only.
namespace ui {

// 0xRRGGBB literal -> lv_color_t.
inline lv_color_t rgb(std::uint32_t hex) noexcept
{
    return lv_color_hex(hex);
}

inline lv_color_t rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
{
    return lv_color_make(r, g, b);
}

} // namespace ui

#endif // UI_COLOR_H
