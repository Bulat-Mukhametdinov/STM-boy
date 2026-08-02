#ifndef UI_LABEL_H
#define UI_LABEL_H

#include "ui/Theme.h"
#include "ui/Widget.h"

#include <cstdarg>
#include <cstdio>

namespace ui {

class Label : public Widget {
public:
    enum class Role { Title, Body, Caption };

    static Label make(Widget parent, const char *text, Role r = Role::Body)
    {
        lv_obj_t *o = lv_label_create(parent.raw());
        lv_obj_set_style_pad_all(o, 0, 0);
        Label l(o);
        l.text(text);
        l.role(r);
        return l;
    }

    Label &text(const char *t) noexcept { lv_label_set_text(obj_, t); return *this; }

    Label &textFmt(const char *fmt, ...)
    {
        char buf[64];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        lv_label_set_text(obj_, buf);
        return *this;
    }

    Label &color(lv_color_t c) noexcept { lv_obj_set_style_text_color(obj_, c, 0); return *this; }

    Label &role(Role r) noexcept
    {
        const Theme &t = Theme::active();
        switch (r) {
        case Role::Title:
            lv_obj_set_style_text_font(obj_, t.font.title, 0);
            lv_obj_set_style_text_color(obj_, t.color.textPrimary, 0);
            break;
        case Role::Body:
            lv_obj_set_style_text_font(obj_, t.font.body, 0);
            lv_obj_set_style_text_color(obj_, t.color.textPrimary, 0);
            break;
        case Role::Caption:
            lv_obj_set_style_text_font(obj_, t.font.caption, 0);
            lv_obj_set_style_text_color(obj_, t.color.textSecondary, 0);
            break;
        }
        return *this;
    }

private:
    explicit Label(lv_obj_t *o) noexcept : Widget(o) {}
};

} // namespace ui

#endif // UI_LABEL_H
