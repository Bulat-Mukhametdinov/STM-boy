#ifndef UI_BATTERY_INDICATOR_H
#define UI_BATTERY_INDICATOR_H

#include "ui/Widget.h"

#include <cstdint>

namespace ui {

// A small battery glyph with a fill bar proportional to the charge level and a
// percentage label to its left. Drawn from primitives (not a font symbol) so the
// fill width tracks the real percentage. Meant to live on lv_layer_top() so it
// floats in the top-right corner above whichever screen is loaded.
class BatteryIndicator : public Widget {
public:
    BatteryIndicator() = default; // invalid handle, for storing as a member/global

    static BatteryIndicator make(Widget parent);

    // Set the displayed charge level (clamped to 0..100). Updates both the fill
    // bar width and the percentage text.
    BatteryIndicator &percent(std::uint8_t pct) noexcept;
    BatteryIndicator &charging(bool enabled) noexcept;

private:
    explicit BatteryIndicator(lv_obj_t *o) noexcept : Widget(o) {}

    void refreshFillColor() noexcept;

    lv_obj_t *label_ = nullptr;  // percentage text on the left
    lv_obj_t *fill_ = nullptr;   // charge fill bar inside the body
    std::int32_t fillTrack_ = 0; // usable inner width for a full battery
    std::uint8_t percent_ = 0;
    bool charging_ = false;
};

} // namespace ui

#endif // UI_BATTERY_INDICATOR_H
