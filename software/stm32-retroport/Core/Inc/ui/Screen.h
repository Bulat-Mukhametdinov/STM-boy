#ifndef UI_SCREEN_H
#define UI_SCREEN_H

#include "ui/Focusable.h"
#include "ui/Widget.h"

#include <cstdint>

namespace ui {

// Owns the focus order for one screen and routes the firmware's Input_* API to
// the focused element each tick. No lv_group/lv_indev: for a handful of GPIO
// buttons direct routing is smaller and clearer.
class Screen {
public:
    static constexpr std::uint8_t kMaxFocus = 12;
    using BackFn = void (*)(void *ctx);
    using EdgeFn = bool (*)(bool forward, void *ctx);

    Screen() = default;

    // This screen's own LVGL screen object, created lazily on first use (the
    // constructor may run before lv_init() for statically-stored Screens, so
    // nothing LVGL touches it until App_Init builds the UI).
    Widget root() noexcept { return Widget(ensure()); }

    // Paint the root with the active theme's background/text/padding and return
    // it, ready to host layout. Keeps raw style calls out of call sites.
    Widget applyTheme() noexcept;

    // Make this screen the visible one.
    void show() noexcept;
    void clear() noexcept;
    void destroy() noexcept;

    // Register an element in tab order. Returns false when the screen is full
    // or the focus target is invalid.
    bool add(const Focusable &f) noexcept;

    void focus(std::uint8_t index) noexcept;
    void next() noexcept;
    void prev() noexcept;
    std::uint8_t cursor() const noexcept { return cursor_; }
    std::uint8_t count() const noexcept { return count_; }

    void onBack(BackFn cb, void *ctx = nullptr) noexcept { back_ = cb; backCtx_ = ctx; }
    void onEdge(EdgeFn cb, void *ctx = nullptr) noexcept { edge_ = cb; edgeCtx_ = ctx; }

    // Call once per App_Task tick; it consumes one queued input action.
    void handleInput() noexcept;

private:
    lv_obj_t *ensure() noexcept;
    void applyFocus(std::uint8_t index, bool on) noexcept;
    void activate(const Focusable &f) noexcept;

    lv_obj_t *obj_ = nullptr;
    Focusable items_[kMaxFocus];
    std::uint8_t count_ = 0;
    std::uint8_t cursor_ = 0;
    BackFn back_ = nullptr;
    void *backCtx_ = nullptr;
    EdgeFn edge_ = nullptr;
    void *edgeCtx_ = nullptr;
    // Tick at which the X+Y "back" combo first went down (0 = not held).
    std::uint32_t comboBackStart_ = 0;
};

} // namespace ui

#endif // UI_SCREEN_H
