#ifndef UI_MENU_H
#define UI_MENU_H

#include "ui/Screen.h"
#include "ui/Widget.h"

#include <cstddef>
#include <cstdint>

namespace ui {

// A vertical list of selectable rows. Sugar over a VStack of themed rows that
// self-register as Focusables in the owning Screen, so Up/Down/A navigation
// works with no extra wiring.
class Menu : public Widget {
public:
    using OnSelect = void (*)(std::uint8_t index, void *ctx);

    struct ItemDef {
        const char *icon;   // LV_SYMBOL_* (may be nullptr)
        const char *title;
        const char *meta;   // secondary line (may be nullptr)
    };

    static Menu make(Widget parent, Screen &screen);

    // Append a single row. Selection fires the menu's onSelect callback.
    Menu &item(const ItemDef &def);

    // Append several rows and set the selection callback in one call.
    template <std::size_t N>
    Menu &items(const ItemDef (&defs)[N], OnSelect cb, void *ctx = nullptr)
    {
        onSelect(cb, ctx);
        for (std::size_t i = 0; i < N; ++i) {
            item(defs[i]);
        }
        return *this;
    }

    Menu &onSelect(OnSelect cb, void *ctx = nullptr) noexcept
    {
        selectCb_ = cb;
        selectCtx_ = ctx;
        return *this;
    }

    std::uint8_t count() const noexcept { return count_; }

private:
    explicit Menu(lv_obj_t *o) noexcept : Widget(o) {}

    Screen *screen_ = nullptr;
    OnSelect selectCb_ = nullptr;
    void *selectCtx_ = nullptr;
    std::uint8_t count_ = 0;
};

} // namespace ui

#endif // UI_MENU_H
