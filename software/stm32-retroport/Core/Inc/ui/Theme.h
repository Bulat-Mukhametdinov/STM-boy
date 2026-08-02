#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"

#include <cstdint>

namespace ui {

// Semantic styling tokens resolved to LVGL primitives. One global active theme
// for now; the abstraction allows swapping more in later.
struct Theme {
    struct Colors {
        lv_color_t bg;            // screen background
        lv_color_t surface;       // panels / cards
        lv_color_t surfaceAlt;    // raised / header surface
        lv_color_t accent;        // selection bar, active toggle
        lv_color_t textPrimary;
        lv_color_t textSecondary;
        lv_color_t border;
    } color;

    struct Fonts {                // semantic roles -> already-enabled Montserrat
        const lv_font_t *title;   // montserrat 14
        const lv_font_t *body;    // montserrat 12
        const lv_font_t *caption; // montserrat 8
    } font;

    struct Scale {
        std::int8_t padSm;
        std::int8_t padMd;
        std::int8_t padLg;
        std::int8_t gap;          // default flex gap
        std::int8_t radius;       // corner radius (0 = flat/retro)
    } space;

    // Active theme. LV_OS_NONE, so no synchronization needed.
    static const Theme &active() noexcept;
    static void setActive(const Theme &t) noexcept;
};

// The shipped theme (defined in Theme.cpp).
const Theme &retroTheme() noexcept;

// Built-in selectable themes, in display order. retroTheme() is index 0.
// Accessors clamp out-of-range indices to the valid range.
int themeCount() noexcept;
const Theme &themeAt(int index) noexcept;
const char *themeName(int index) noexcept;

// Shared, theme-bound lv_style_t objects. Built once from the active theme and
// rebuilt on setActive(). Components add these instead of spraying per-object
// setters, so selection visuals come from LVGL's state machine for free.
namespace styles {

const lv_style_t *panel() noexcept;            // surface card
const lv_style_t *menuItem() noexcept;         // menu row, default state
const lv_style_t *menuItemFocused() noexcept;  // menu row, focused overlay

} // namespace styles

} // namespace ui

#endif // UI_THEME_H
