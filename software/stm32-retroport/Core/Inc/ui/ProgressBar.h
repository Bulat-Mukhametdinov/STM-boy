#ifndef UI_PROGRESS_BAR_H
#define UI_PROGRESS_BAR_H

#include "ui/Widget.h"

#include <cstdint>

namespace ui {

class ProgressBar : public Widget {
public:
    ProgressBar() = default;

    static ProgressBar make(Widget parent, std::int32_t min = 0, std::int32_t max = 1000);

    ProgressBar &value(std::int32_t v) noexcept
    {
        if (obj_ != nullptr) {
            lv_bar_set_value(obj_, v, LV_ANIM_OFF);
        }
        return *this;
    }

private:
    explicit ProgressBar(lv_obj_t *o) noexcept : Widget(o) {}
};

} // namespace ui

#endif
