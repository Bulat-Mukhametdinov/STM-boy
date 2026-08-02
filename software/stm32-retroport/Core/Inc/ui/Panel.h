#ifndef UI_PANEL_H
#define UI_PANEL_H

#include "ui/Theme.h"
#include "ui/Widget.h"

namespace ui {

// A filled rectangular surface. The framework's basic container/background.
class Panel : public Widget {
public:
    static Panel make(Widget parent)
    {
        return make(parent, Theme::active().color.surface);
    }

    static Panel make(Widget parent, lv_color_t fillColor)
    {
        Panel p(detail::makeBare(parent));
        lv_obj_set_style_bg_opa(p.obj_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(p.obj_, fillColor, 0);
        lv_obj_set_style_border_width(p.obj_, 0, 0);
        lv_obj_set_style_radius(p.obj_, Theme::active().space.radius, 0);
        lv_obj_set_style_pad_all(p.obj_, 0, 0);
        // Don't clip children's ext-draw (focus outlines, slider knobs, etc.).
        lv_obj_add_flag(p.obj_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        return p;
    }

    Panel &fill(lv_color_t c) noexcept
    {
        lv_obj_set_style_bg_color(obj_, c, 0);
        return *this;
    }

    Panel &radius(std::int32_t r) noexcept { lv_obj_set_style_radius(obj_, r, 0); return *this; }

    Panel &border(lv_color_t c, std::int32_t w) noexcept
    {
        lv_obj_set_style_border_color(obj_, c, 0);
        lv_obj_set_style_border_width(obj_, w, 0);
        return *this;
    }

    Panel &padAll(std::int32_t p) noexcept { lv_obj_set_style_pad_all(obj_, p, 0); return *this; }

protected:
    explicit Panel(lv_obj_t *o) noexcept : Widget(o) {}
};

} // namespace ui

#endif // UI_PANEL_H
