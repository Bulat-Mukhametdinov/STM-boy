#ifndef UI_STACK_H
#define UI_STACK_H

#include "ui/Theme.h"
#include "ui/Widget.h"

namespace ui {

// A flex container that arranges its children in one direction. Wraps LVGL's
// flex layout; defaults pull spacing from the active theme.
class Stack : public Widget {
public:
    enum class Dir { Vertical, Horizontal };

    static Stack make(Widget parent, Dir d = Dir::Vertical)
    {
        Stack s(detail::makeBare(parent));
        lv_obj_set_style_bg_opa(s.obj_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s.obj_, 0, 0);
        lv_obj_set_style_pad_all(s.obj_, 0, 0);
        lv_obj_set_flex_flow(s.obj_, d == Dir::Vertical ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
        // Size to content by default so containers don't collapse to 0 (no
        // inherited style after remove_style_all). Override with fill()/width().
        lv_obj_set_size(s.obj_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        // Don't clip children's ext-draw (focus outlines, slider knobs, etc.).
        lv_obj_add_flag(s.obj_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        s.gap(Theme::active().space.gap);
        return s;
    }

    Stack &gap(std::int32_t g) noexcept
    {
        lv_obj_set_style_pad_row(obj_, g, 0);
        lv_obj_set_style_pad_column(obj_, g, 0);
        return *this;
    }

    Stack &padAll(std::int32_t p) noexcept { lv_obj_set_style_pad_all(obj_, p, 0); return *this; }

    // Alignment along the main (flow) axis.
    Stack &mainAlign(lv_flex_align_t a) noexcept
    {
        lv_obj_set_flex_align(obj_, a, cross_, LV_FLEX_ALIGN_START);
        main_ = a;
        return *this;
    }

    // Alignment along the cross axis.
    Stack &crossAlign(lv_flex_align_t a) noexcept
    {
        lv_obj_set_flex_align(obj_, main_, a, LV_FLEX_ALIGN_START);
        cross_ = a;
        return *this;
    }

private:
    explicit Stack(lv_obj_t *o) noexcept : Widget(o) {}

    lv_flex_align_t main_ = LV_FLEX_ALIGN_START;
    lv_flex_align_t cross_ = LV_FLEX_ALIGN_START;
};

inline Stack VStack(Widget parent) { return Stack::make(parent, Stack::Dir::Vertical); }
inline Stack HStack(Widget parent) { return Stack::make(parent, Stack::Dir::Horizontal); }

} // namespace ui

#endif // UI_STACK_H
