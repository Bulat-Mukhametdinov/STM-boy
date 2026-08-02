#ifndef UI_SLIDER_H
#define UI_SLIDER_H

#include "ui/Screen.h"
#include "ui/Widget.h"

#include <cstdint>

namespace ui {

// A horizontal value slider. When focused, LEFT/RIGHT step the value (handled
// by Screen); the change callback fires with the new value.
class Slider : public Widget {
public:
    using OnChange = void (*)(std::int32_t value, void *ctx);

    Slider() = default; // invalid handle, for storing as a member/global

    static Slider make(Widget parent, std::int32_t min, std::int32_t max, std::int32_t val);

    std::int32_t value() const noexcept { return lv_slider_get_value(obj_); }
    Slider &value(std::int32_t v) noexcept
    {
        lv_slider_set_value(obj_, v, LV_ANIM_OFF);
        // Programmatic set_value doesn't emit VALUE_CHANGED; emit it so the
        // custom knob repositions.
        lv_obj_send_event(obj_, LV_EVENT_VALUE_CHANGED, nullptr);
        return *this;
    }

    // `focus` is the element highlighted when selected (e.g. a wrapping row);
    // if invalid, the slider itself is highlighted.
    bool attachTo(Screen &s, OnChange cb, std::int32_t step = 1, void *ctx = nullptr,
                  Widget focus = Widget{});

private:
    explicit Slider(lv_obj_t *o) noexcept : Widget(o) {}
};

} // namespace ui

#endif // UI_SLIDER_H
