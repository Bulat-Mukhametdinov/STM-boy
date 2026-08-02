#ifndef UI_WIDGET_H
#define UI_WIDGET_H

#include "lvgl.h"

#include <cstdint>

namespace ui {

// Non-owning, trivially-copyable handle over an lv_obj_t*.
//
// LVGL owns the object tree and frees children when the parent is deleted, so a
// Widget never deletes its underlying object in a destructor (that would
// double-free). Wrappers live on the stack while building UI, or as members of
// a statically-stored screen. Use destroy() for the rare explicit removal.
// Public mutators require a valid lv_obj_t*. Default-constructed handles are
// only placeholders until assigned from make().
class Widget {
public:
    constexpr Widget() noexcept : obj_(nullptr) {}
    explicit constexpr Widget(lv_obj_t *obj) noexcept : obj_(obj) {}

    lv_obj_t *raw() const noexcept { return obj_; }
    bool valid() const noexcept { return obj_ != nullptr; }
    explicit operator bool() const noexcept { return obj_ != nullptr; }

    // Geometry (chainable). lv_coord_t is int32_t in LVGL v9.
    Widget &size(std::int32_t w, std::int32_t h) noexcept { lv_obj_set_size(obj_, w, h); return *this; }
    Widget &width(std::int32_t w) noexcept { lv_obj_set_width(obj_, w); return *this; }
    Widget &height(std::int32_t h) noexcept { lv_obj_set_height(obj_, h); return *this; }
    Widget &pos(std::int32_t x, std::int32_t y) noexcept { lv_obj_set_pos(obj_, x, y); return *this; }
    Widget &align(lv_align_t a, std::int32_t dx = 0, std::int32_t dy = 0) noexcept
    {
        lv_obj_align(obj_, a, dx, dy);
        return *this;
    }
    Widget &flexGrow(std::uint8_t g) noexcept { lv_obj_set_flex_grow(obj_, g); return *this; }
    Widget &fill() noexcept { lv_obj_set_size(obj_, LV_PCT(100), LV_PCT(100)); return *this; }

    // Visibility / flags.
    Widget &hidden(bool h) noexcept
    {
        if (h) {
            lv_obj_add_flag(obj_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj_, LV_OBJ_FLAG_HIDDEN);
        }
        return *this;
    }
    Widget &scrollable(bool s) noexcept
    {
        if (s) {
            lv_obj_add_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
        } else {
            lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
        }
        return *this;
    }

    void invalidate() noexcept { if (obj_) lv_obj_invalidate(obj_); }
    void destroy() noexcept
    {
        if (obj_) {
            lv_obj_delete(obj_);
            obj_ = nullptr;
        }
    }

protected:
    lv_obj_t *obj_;
};

static_assert(sizeof(Widget) == sizeof(void *), "Widget must be a thin pointer handle");

namespace detail {

// Create a bare object: no inherited style, not scrollable. Mirrors the
// remove_style_all + clear-scroll pattern repeated across the old app.cpp.
inline lv_obj_t *makeBare(Widget parent) noexcept
{
    lv_obj_t *o = lv_obj_create(parent.raw());
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

} // namespace detail

} // namespace ui

#endif // UI_WIDGET_H
