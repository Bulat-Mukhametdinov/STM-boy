#ifndef UI_FOCUSABLE_H
#define UI_FOCUSABLE_H

#include "lvgl.h"

#include <cstdint>

namespace ui {

// A single input-receiving element registered with a Screen. All callback data
// lives inline here (Screen owns a fixed array of these), so there is no heap
// allocation and no dangling pointers when builder temporaries go out of scope.
struct Focusable {
    enum class Kind : std::uint8_t {
        Action,   // A/CENTER fires cb.action(ctx)
        Toggle,   // A/CENTER flips checked state, fires cb.toggled(value, ctx)
        Slider,   // LEFT/RIGHT step the value, fires cb.changed(value, ctx)
        MenuItem, // A/CENTER fires cb.selected(index, ctx)
        Stepper,  // LEFT/RIGHT cycle a choice, fires cb.stepped(dir, ctx)
    };

    lv_obj_t *obj = nullptr;     // receives LV_STATE_FOCUSED (the highlight)
    lv_obj_t *control = nullptr; // widget acted on; falls back to obj if null
    Kind kind = Kind::Action;
    union Callback {
        void (*action)(void *ctx);
        void (*toggled)(bool value, void *ctx);
        void (*changed)(std::int32_t value, void *ctx);
        void (*selected)(std::uint8_t index, void *ctx);
        void (*stepped)(std::int32_t dir, void *ctx);
        Callback() : action(nullptr) {}
    } cb;
    void *ctx = nullptr;
    std::int32_t step = 1;   // Slider step per LEFT/RIGHT press
    std::uint8_t index = 0;  // MenuItem index payload

    // The widget to act on (toggle state, slider value). The highlight target
    // (obj) may be a wrapping row instead.
    lv_obj_t *target() const noexcept { return control ? control : obj; }

    // Does this element consume LEFT/RIGHT instead of letting them navigate?
    bool consumesHorizontal() const noexcept
    {
        return kind == Kind::Slider || kind == Kind::Stepper;
    }
};

} // namespace ui

#endif // UI_FOCUSABLE_H
