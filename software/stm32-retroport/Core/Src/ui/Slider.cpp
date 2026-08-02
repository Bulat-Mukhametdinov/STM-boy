#include "ui/Slider.h"

#include "ui/Theme.h"

namespace ui {

namespace {

constexpr std::int32_t kKnob = 14;      // knob diameter
constexpr std::int32_t kRadius = kKnob / 2;
constexpr std::int32_t kTrackH = 6;

// The slider is drawn from three child objects so we control geometry exactly.
// The track is inset by the knob radius on each side, so the knob centre can
// reach both ends (knob "at the end") while the knob body stays within the
// component (no overflow, no clipping). The active fill always matches the
// track width. Child order: 0=track, 1=fill, 2=knob.
void relayout(lv_obj_t *slider)
{
    lv_obj_t *track = lv_obj_get_child(slider, 0);
    lv_obj_t *fill = lv_obj_get_child(slider, 1);
    lv_obj_t *knob = lv_obj_get_child(slider, 2);
    if (!track || !fill || !knob) {
        return;
    }
    std::int32_t w = lv_obj_get_width(slider);
    if (w <= 0) {
        return;
    }
    std::int32_t span = w - 2 * kRadius; // track length = knob-centre travel
    if (span < 0) {
        span = 0;
    }

    lv_obj_set_size(track, span, kTrackH);
    lv_obj_align(track, LV_ALIGN_LEFT_MID, kRadius, 0);

    std::int32_t lo = lv_slider_get_min_value(slider);
    std::int32_t hi = lv_slider_get_max_value(slider);
    std::int32_t range = hi - lo;
    if (range <= 0) {
        range = 1;
    }
    std::int32_t val = lv_slider_get_value(slider);
    if (val < lo) {
        val = lo;
    }
    if (val > hi) {
        val = hi;
    }
    std::int32_t fillLen = (val - lo) * span / range;

    lv_obj_set_size(fill, fillLen, kTrackH);
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, kRadius, 0);

    // Knob centre sits at the fill edge (kRadius + fillLen); its left is that
    // minus the radius, so at the extremes the knob body just touches 0 / w.
    lv_obj_align(knob, LV_ALIGN_LEFT_MID, fillLen, 0);
}

void slider_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_SIZE_CHANGED) {
        relayout(static_cast<lv_obj_t *>(lv_event_get_target(e)));
    }
}

lv_obj_t *make_child(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    return c;
}

} // namespace

Slider Slider::make(Widget parent, std::int32_t min, std::int32_t max, std::int32_t val)
{
    const Theme &t = Theme::active();
    lv_obj_t *o = lv_slider_create(parent.raw());
    lv_obj_set_size(o, 70, kKnob); // component is knob-tall so the knob fits
    lv_slider_set_range(o, min, max);
    lv_slider_set_value(o, val, LV_ANIM_OFF);

    // Hide lv_slider's own parts; we draw our own children instead.
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(o, 0, LV_PART_KNOB);

    lv_obj_t *track = make_child(o); // index 0
    lv_obj_set_style_bg_color(track, t.color.border, 0);

    lv_obj_t *fill = make_child(o); // index 1
    lv_obj_set_style_bg_color(fill, t.color.accent, 0);

    lv_obj_t *knob = make_child(o); // index 2
    lv_obj_set_size(knob, kKnob, kKnob);
    lv_obj_set_style_bg_color(knob, t.color.surface, 0);
    lv_obj_set_style_border_color(knob, t.color.accent, 0);
    lv_obj_set_style_border_width(knob, 2, 0);

    lv_obj_add_event_cb(o, slider_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(o, slider_event_cb, LV_EVENT_SIZE_CHANGED, nullptr);
    relayout(o);

    return Slider(o);
}

bool Slider::attachTo(Screen &s, OnChange cb, std::int32_t step, void *ctx, Widget focus)
{
    Focusable f;
    f.obj = focus.valid() ? focus.raw() : obj_;
    f.control = focus.valid() ? obj_ : nullptr;
    f.kind = Focusable::Kind::Slider;
    f.cb.changed = cb;
    f.ctx = ctx;
    f.step = step;
    return s.add(f);
}

} // namespace ui
