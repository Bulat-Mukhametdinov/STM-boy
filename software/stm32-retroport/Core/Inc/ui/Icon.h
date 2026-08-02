#ifndef UI_ICON_H
#define UI_ICON_H

#include "ui/Theme.h"
#include "ui/Widget.h"

namespace ui {

// A themed glyph from the built-in LVGL symbol font (e.g. LV_SYMBOL_PLAY).
class Icon : public Widget {
public:
    static Icon make(Widget parent, const char *lvSymbol)
    {
        lv_obj_t *o = lv_label_create(parent.raw());
        lv_obj_set_style_pad_all(o, 0, 0);
        Icon i(o);
        i.symbol(lvSymbol);
        const Theme &t = Theme::active();
        lv_obj_set_style_text_font(o, t.font.body, 0);
        lv_obj_set_style_text_color(o, t.color.textPrimary, 0);
        return i;
    }

    Icon &symbol(const char *s) noexcept { lv_label_set_text(obj_, s); return *this; }
    Icon &color(lv_color_t c) noexcept { lv_obj_set_style_text_color(obj_, c, 0); return *this; }

private:
    explicit Icon(lv_obj_t *o) noexcept : Widget(o) {}
};

} // namespace ui

#endif // UI_ICON_H
