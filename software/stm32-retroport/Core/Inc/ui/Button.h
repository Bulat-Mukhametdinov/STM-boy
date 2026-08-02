#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include "ui/Screen.h"
#include "ui/Theme.h"
#include "ui/Widget.h"

namespace ui {

class Button : public Widget {
public:
    using OnPress = void (*)(void *ctx);

    static Button make(Widget parent, const char *text)
    {
        const Theme &t = Theme::active();
        lv_obj_t *o = lv_button_create(parent.raw());
        lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, t.color.surfaceAlt, 0);
        lv_obj_set_style_text_color(o, t.color.textPrimary, 0);
        lv_obj_set_style_text_font(o, t.font.body, 0);
        lv_obj_set_style_radius(o, t.space.radius, 0);
        lv_obj_set_style_pad_all(o, t.space.padSm, 0);
        // Constant-width border so focus only changes its color, never the
        // content-sized geometry. Invisible (matches bg) until focused.
        lv_obj_set_style_border_width(o, 2, 0);
        lv_obj_set_style_border_color(o, t.color.surfaceAlt, 0);
        lv_obj_set_style_border_color(o, t.color.accent, LV_STATE_FOCUSED);

        lv_obj_t *lbl = lv_label_create(o);
        lv_obj_center(lbl);

        Button b(o);
        b.label_ = lbl;
        b.label(text);
        return b;
    }

    Button &label(const char *t) noexcept { lv_label_set_text(label_, t); return *this; }

    bool attachTo(Screen &s, OnPress cb, void *ctx = nullptr, Widget focus = Widget{})
    {
        Focusable f;
        f.obj = focus.valid() ? focus.raw() : obj_;
        f.control = focus.valid() ? obj_ : nullptr;
        f.kind = Focusable::Kind::Action;
        f.cb.action = cb;
        f.ctx = ctx;
        return s.add(f);
    }

private:
    explicit Button(lv_obj_t *o) noexcept : Widget(o) {}

    lv_obj_t *label_ = nullptr;
};

} // namespace ui

#endif // UI_BUTTON_H
