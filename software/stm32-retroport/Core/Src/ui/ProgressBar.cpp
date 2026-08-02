#include "ui/ProgressBar.h"

#include "ui/Theme.h"

namespace ui {

ProgressBar ProgressBar::make(Widget parent, std::int32_t min, std::int32_t max)
{
    const Theme &t = Theme::active();
    lv_obj_t *o = lv_bar_create(parent.raw());

    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), 12);
    lv_bar_set_range(o, min, max);
    lv_bar_set_value(o, min, LV_ANIM_OFF);

    // MAIN and INDICATOR radii must stay equal: a mismatch makes LVGL take a
    // temporary ARGB mask path that can exhaust the render heap (see AGENTS.md).
    std::int32_t radius = t.space.radius < 2 ? 2 : t.space.radius;

    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, t.color.border, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, t.color.accent, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o, t.color.accent, LV_PART_INDICATOR);
    lv_obj_set_style_radius(o, radius, LV_PART_INDICATOR);

    return ProgressBar(o);
}

} // namespace ui
