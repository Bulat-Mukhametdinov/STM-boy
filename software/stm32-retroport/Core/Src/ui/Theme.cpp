#include "ui/Theme.h"

namespace {

ui::Theme g_active;
bool g_active_init = false;

// Shared style objects. Static storage; lv_style_t is a few words each.
lv_style_t s_panel;
lv_style_t s_menu_item;
lv_style_t s_menu_item_focused;
bool g_styles_init = false;

// Shared defaults (fonts + spacing). Each theme overrides only its colours.
ui::Theme make_base()
{
    ui::Theme x{};
    x.font.title = &lv_font_montserrat_14;
    x.font.body = &lv_font_montserrat_12;
    x.font.caption = &lv_font_montserrat_8;
    x.space.padSm = 2;
    x.space.padMd = 6;
    x.space.padLg = 12;
    x.space.gap = 6;
    x.space.radius = 0;
    return x;
}

void rebuild_styles(const ui::Theme &t)
{
    if (!g_styles_init) {
        lv_style_init(&s_panel);
        lv_style_init(&s_menu_item);
        lv_style_init(&s_menu_item_focused);
        g_styles_init = true;
    } else {
        lv_style_reset(&s_panel);
        lv_style_reset(&s_menu_item);
        lv_style_reset(&s_menu_item_focused);
    }

    // Surface card.
    lv_style_set_bg_opa(&s_panel, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel, t.color.surface);
    lv_style_set_border_width(&s_panel, 0);
    lv_style_set_radius(&s_panel, t.space.radius);
    lv_style_set_pad_all(&s_panel, t.space.padMd);

    // Menu row, default.
    lv_style_set_bg_opa(&s_menu_item, LV_OPA_COVER);
    lv_style_set_bg_color(&s_menu_item, t.color.surface);
    lv_style_set_border_width(&s_menu_item, 0);
    lv_style_set_radius(&s_menu_item, t.space.radius);
    lv_style_set_pad_all(&s_menu_item, t.space.padSm);
    lv_style_set_pad_column(&s_menu_item, t.space.padMd);
    lv_style_set_text_color(&s_menu_item, t.color.textPrimary);

    // Menu row, focused overlay (applied on LV_STATE_FOCUSED). Sets bg_opa too
    // so it highlights even on rows that have no base menu-item style.
    lv_style_set_bg_opa(&s_menu_item_focused, LV_OPA_COVER);
    lv_style_set_bg_color(&s_menu_item_focused, t.color.surfaceAlt);
    lv_style_set_radius(&s_menu_item_focused, t.space.radius);
    lv_style_set_border_width(&s_menu_item_focused, 2);
    lv_style_set_border_color(&s_menu_item_focused, t.color.accent);
    lv_style_set_border_side(&s_menu_item_focused, LV_BORDER_SIDE_LEFT);
}

} // namespace

const ui::Theme &ui::retroTheme() noexcept
{
    static const Theme t = [] {
        Theme x = make_base();
        x.color.bg = lv_color_hex(0xF2E2CF);
        x.color.surface = lv_color_hex(0xFFF1DF);
        x.color.surfaceAlt = lv_color_hex(0xE7D0BA);
        x.color.accent = lv_color_hex(0x4B4038);
        x.color.textPrimary = lv_color_hex(0x3A332D);
        x.color.textSecondary = lv_color_hex(0x695B50);
        x.color.border = lv_color_hex(0xC9C2EF);
        x.space.radius = 0;
        return x;
    }();
    return t;
}

namespace {

// Terminal-style green-on-black.
const ui::Theme &hacker_theme() noexcept
{
    static const ui::Theme t = [] {
        ui::Theme x = make_base();
        x.color.bg = lv_color_hex(0x001A00);
        x.color.surface = lv_color_hex(0x0A1F0A);
        x.color.surfaceAlt = lv_color_hex(0x0F2C0F);
        x.color.accent = lv_color_hex(0x00FF5F);
        x.color.textPrimary = lv_color_hex(0x4DFF77);
        x.color.textSecondary = lv_color_hex(0x1FA34A);
        x.color.border = lv_color_hex(0x1B5E20);
        x.space.radius = 0;
        return x;
    }();
    return t;
}

// Soft, airy light palette (lavender base, pastel teal accent).
const ui::Theme &pastel_light_theme() noexcept
{
    static const ui::Theme t = [] {
        ui::Theme x = make_base();
        x.color.bg = lv_color_hex(0xF7F4FB);
        x.color.surface = lv_color_hex(0xFFFFFF);
        x.color.surfaceAlt = lv_color_hex(0xEBE5F6);
        x.color.accent = lv_color_hex(0x8FCFC0);
        x.color.textPrimary = lv_color_hex(0x5A5570);
        x.color.textSecondary = lv_color_hex(0x9A93AE);
        x.color.border = lv_color_hex(0xDED7EE);
        x.space.radius = 3;
        return x;
    }();
    return t;
}

// Muted dark slate with a soft pastel-purple accent.
const ui::Theme &pastel_dark_theme() noexcept
{
    static const ui::Theme t = [] {
        ui::Theme x = make_base();
        x.color.bg = lv_color_hex(0x2B2B38);
        x.color.surface = lv_color_hex(0x343442);
        x.color.surfaceAlt = lv_color_hex(0x40404F);
        x.color.accent = lv_color_hex(0xB9A3E3);
        x.color.textPrimary = lv_color_hex(0xE8E3F2);
        x.color.textSecondary = lv_color_hex(0xABA4BE);
        x.color.border = lv_color_hex(0x4B4B5D);
        x.space.radius = 3;
        return x;
    }();
    return t;
}

// Gentle violet.
const ui::Theme &soft_violet_theme() noexcept
{
    static const ui::Theme t = [] {
        ui::Theme x = make_base();
        x.color.bg = lv_color_hex(0xF1EBFA);
        x.color.surface = lv_color_hex(0xFBF8FF);
        x.color.surfaceAlt = lv_color_hex(0xE4D9F6);
        x.color.accent = lv_color_hex(0x9B7FD4);
        x.color.textPrimary = lv_color_hex(0x463B60);
        x.color.textSecondary = lv_color_hex(0x7B6F98);
        x.color.border = lv_color_hex(0xD7C9EF);
        x.space.radius = 4;
        return x;
    }();
    return t;
}

struct ThemeEntry {
    const char *name;
    const ui::Theme &(*get)() noexcept;
};

const ThemeEntry kThemes[] = {
    {"Retro", ui::retroTheme},
    {"Hacker", hacker_theme},
    {"Pastel Light", pastel_light_theme},
    {"Pastel Dark", pastel_dark_theme},
    {"Soft Violet", soft_violet_theme},
};

constexpr int kThemeCount = static_cast<int>(sizeof(kThemes) / sizeof(kThemes[0]));

int clamp_theme_index(int index) noexcept
{
    if (index < 0) {
        return 0;
    }
    if (index >= kThemeCount) {
        return kThemeCount - 1;
    }
    return index;
}

} // namespace

int ui::themeCount() noexcept
{
    return kThemeCount;
}

const ui::Theme &ui::themeAt(int index) noexcept
{
    return kThemes[clamp_theme_index(index)].get();
}

const char *ui::themeName(int index) noexcept
{
    return kThemes[clamp_theme_index(index)].name;
}

const ui::Theme &ui::Theme::active() noexcept
{
    if (!g_active_init) {
        g_active = retroTheme();
        g_active_init = true;
        rebuild_styles(g_active);
    }
    return g_active;
}

void ui::Theme::setActive(const Theme &t) noexcept
{
    g_active = t;
    g_active_init = true;
    rebuild_styles(g_active);
}

const lv_style_t *ui::styles::panel() noexcept
{
    Theme::active(); // ensure styles built
    return &s_panel;
}

const lv_style_t *ui::styles::menuItem() noexcept
{
    Theme::active();
    return &s_menu_item;
}

const lv_style_t *ui::styles::menuItemFocused() noexcept
{
    Theme::active();
    return &s_menu_item_focused;
}
