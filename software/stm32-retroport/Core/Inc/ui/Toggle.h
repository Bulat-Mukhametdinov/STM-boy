#ifndef UI_TOGGLE_H
#define UI_TOGGLE_H

#include "ui/Screen.h"
#include "ui/Theme.h"
#include "ui/Widget.h"

namespace ui {

// An on/off switch (LVGL switch). Activation is handled by Screen, which flips
// LV_STATE_CHECKED and fires the change callback.
class Toggle : public Widget {
public:
    using OnChange = void (*)(bool value, void *ctx);

    static Toggle make(Widget parent, bool on = false)
    {
        const Theme &t = Theme::active();
        lv_obj_t *o = lv_switch_create(parent.raw());
        lv_obj_set_size(o, 36, 18); // explicit: default LVGL theme is disabled

        // Track (main).
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(o, t.color.border, LV_PART_MAIN);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);

        // Track when on (indicator).
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(o, t.color.accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

        // Knob. Negative pad shrinks it inside the track so it isn't oversized.
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_bg_color(o, t.color.surface, LV_PART_KNOB);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_pad_all(o, -3, LV_PART_KNOB);

        Toggle tg(o);
        tg.value(on);
        return tg;
    }

    bool value() const noexcept { return lv_obj_has_state(obj_, LV_STATE_CHECKED); }

    Toggle &value(bool on) noexcept
    {
        if (on) {
            lv_obj_add_state(obj_, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(obj_, LV_STATE_CHECKED);
        }
        return *this;
    }

    // `focus` is the element highlighted when selected (e.g. a wrapping row);
    // if invalid, the toggle itself is highlighted.
    bool attachTo(Screen &s, OnChange cb, void *ctx = nullptr, Widget focus = Widget{})
    {
        Focusable f;
        f.obj = focus.valid() ? focus.raw() : obj_;
        f.control = focus.valid() ? obj_ : nullptr;
        f.kind = Focusable::Kind::Toggle;
        f.cb.toggled = cb;
        f.ctx = ctx;
        return s.add(f);
    }

private:
    explicit Toggle(lv_obj_t *o) noexcept : Widget(o) {}
};

} // namespace ui

#endif // UI_TOGGLE_H
