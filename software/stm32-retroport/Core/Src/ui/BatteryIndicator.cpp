#include "ui/BatteryIndicator.h"

#include "ui/Theme.h"

#include <cstdio>

namespace ui {

namespace {

constexpr std::int32_t kBodyW = 22;
constexpr std::int32_t kBodyH = 11;
constexpr std::int32_t kBorder = 1;
constexpr std::int32_t kInnerPad = 1;
constexpr std::int32_t kNubW = 2;
constexpr std::int32_t kNubH = 5;

// At or below this charge the fill turns red to warn of a low battery.
constexpr std::uint8_t kLowPercent = 15;
constexpr std::uint32_t kLowColor = 0xE53935; // red
constexpr std::uint32_t kChargingColor = 0x2E7D32; // green

lv_obj_t *makeBare(lv_obj_t *parent) noexcept
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

} // namespace

BatteryIndicator BatteryIndicator::make(Widget parent)
{
    const Theme &t = Theme::active();

    // Root: transparent flex row holding [percent][battery], packed to the end.
    lv_obj_t *root = makeBare(parent.raw());
    lv_obj_set_size(root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_column(root, 3, 0);

    // Percentage text to the left of the glyph.
    lv_obj_t *label = lv_label_create(root);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_font(label, t.font.caption, 0);
    lv_obj_set_style_text_color(label, t.color.textSecondary, 0);
    lv_label_set_text(label, "0%");

    // Battery group: outlined body + a small terminal nub, tightly packed.
    lv_obj_t *group = makeBare(root);
    lv_obj_set_size(group, kBodyW + kNubW, kBodyH);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_set_style_pad_column(group, 0, 0);

    // Body outline.
    lv_obj_t *body = makeBare(group);
    lv_obj_set_size(body, kBodyW, kBodyH);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_set_style_border_width(body, kBorder, 0);
    lv_obj_set_style_border_color(body, t.color.textPrimary, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body, kInnerPad, 0);

    // Charge fill, anchored to the left edge of the body's content area.
    lv_obj_t *fill = makeBare(body);
    lv_obj_set_height(fill, kBodyH - 2 * (kBorder + kInnerPad));
    lv_obj_set_style_radius(fill, 1, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, t.color.accent, 0);
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);

    // Terminal nub on the right.
    lv_obj_t *nub = makeBare(group);
    lv_obj_set_size(nub, kNubW, kNubH);
    lv_obj_set_style_radius(nub, 0, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(nub, t.color.textPrimary, 0);

    BatteryIndicator b(root);
    b.label_ = label;
    b.fill_ = fill;
    b.fillTrack_ = kBodyW - 2 * (kBorder + kInnerPad);
    return b;
}

void BatteryIndicator::refreshFillColor() noexcept
{
    if (fill_ == nullptr) {
        return;
    }

    lv_color_t color = Theme::active().color.accent;
    if (charging_) {
        color = lv_color_hex(kChargingColor);
    } else if (percent_ <= kLowPercent) {
        color = lv_color_hex(kLowColor);
    }
    lv_obj_set_style_bg_color(fill_, color, 0);
}

BatteryIndicator &BatteryIndicator::percent(std::uint8_t pct) noexcept
{
    if (pct > 100) {
        pct = 100;
    }
    percent_ = pct;

    if (fill_ != nullptr) {
        refreshFillColor();

        std::int32_t w = (fillTrack_ * pct + 50) / 100; // rounded
        if (w <= 0) {
            lv_obj_add_flag(fill_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(fill_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(fill_, w);
            lv_obj_align(fill_, LV_ALIGN_LEFT_MID, 0, 0);
        }
    }

    if (label_ != nullptr) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(pct));
        lv_label_set_text(label_, buf);
    }

    return *this;
}

BatteryIndicator &BatteryIndicator::charging(bool enabled) noexcept
{
    charging_ = enabled;
    refreshFillColor();
    return *this;
}

} // namespace ui
