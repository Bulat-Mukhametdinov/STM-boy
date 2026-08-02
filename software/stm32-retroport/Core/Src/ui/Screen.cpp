#include "ui/Screen.h"

#include "audio.h"
#include "input.h"
#include "main.h"
#include "ui/Theme.h"

namespace ui {

// Navigation model (the joystick works on this board, so it drives the list):
//   UP/DOWN     - move the cursor (LEFT/RIGHT also move it on plain items)
//   LEFT/RIGHT  - adjust sliders/steppers
//   A or CENTER - activate the focused item
//   B           - back (matches the on-screen "B: back" hints)

lv_obj_t *Screen::ensure() noexcept
{
    if (obj_ == nullptr) {
        obj_ = lv_obj_create(nullptr);
    }
    return obj_;
}

Widget Screen::applyTheme() noexcept
{
    const Theme &t = Theme::active();
    lv_obj_t *r = ensure();
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, t.color.bg, 0);
    lv_obj_set_style_text_color(r, t.color.textPrimary, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    return Widget(r);
}

void Screen::show() noexcept
{
    lv_screen_load(ensure());
    if (count_ > 0) {
        if (cursor_ >= count_) {
            cursor_ = 0;
        }
        applyFocus(cursor_, true);
    }
}

void Screen::clear() noexcept
{
    lv_obj_clean(ensure());
    count_ = 0;
    cursor_ = 0;
}

void Screen::destroy() noexcept
{
    if (obj_ != nullptr) {
        lv_obj_delete(obj_);
        obj_ = nullptr;
    }

    for (std::uint8_t i = 0; i < kMaxFocus; ++i) {
        items_[i] = Focusable{};
    }
    count_ = 0;
    cursor_ = 0;
    back_ = nullptr;
    backCtx_ = nullptr;
    edge_ = nullptr;
    edgeCtx_ = nullptr;
}

bool Screen::add(const Focusable &f) noexcept
{
    if (count_ >= kMaxFocus || f.obj == nullptr) {
        return false;
    }
    bool first = count_ == 0;
    items_[count_] = f;
    ++count_;
    if (first) {
        cursor_ = 0;
        applyFocus(cursor_, true);
    }
    return true;
}

void Screen::applyFocus(std::uint8_t index, bool on) noexcept
{
    if (index >= count_ || items_[index].obj == nullptr) {
        return;
    }
    if (on) {
        lv_obj_add_state(items_[index].obj, LV_STATE_FOCUSED);
        lv_obj_scroll_to_view(items_[index].obj, LV_ANIM_OFF);
    } else {
        lv_obj_remove_state(items_[index].obj, LV_STATE_FOCUSED);
    }
}

void Screen::focus(std::uint8_t index) noexcept
{
    if (index >= count_) {
        return;
    }
    applyFocus(cursor_, false);
    cursor_ = index;
    applyFocus(cursor_, true);
}

void Screen::next() noexcept
{
    if (count_ == 0) {
        return;
    }
    if (cursor_ + 1u >= count_) {
        if (edge_ != nullptr) {
            if (edge_(true, edgeCtx_)) {
                Audio_PlaySound(AUDIO_SOUND_NAV);
            }
            return;
        }
    }

    focus(static_cast<std::uint8_t>((cursor_ + 1u) % count_));
    Audio_PlaySound(AUDIO_SOUND_NAV);
}

void Screen::prev() noexcept
{
    if (count_ == 0) {
        return;
    }
    if (cursor_ == 0u) {
        if (edge_ != nullptr) {
            if (edge_(false, edgeCtx_)) {
                Audio_PlaySound(AUDIO_SOUND_NAV);
            }
            return;
        }
    }

    focus(static_cast<std::uint8_t>((cursor_ + count_ - 1u) % count_));
    Audio_PlaySound(AUDIO_SOUND_NAV);
}

void Screen::activate(const Focusable &f) noexcept
{
    switch (f.kind) {
    case Focusable::Kind::Action:
        Audio_PlaySound(AUDIO_SOUND_SELECT);
        if (f.cb.action) {
            f.cb.action(f.ctx);
        }
        break;
    case Focusable::Kind::Toggle: {
        lv_obj_t *sw = f.target();
        bool value = !lv_obj_has_state(sw, LV_STATE_CHECKED);
        Audio_PlaySound(AUDIO_SOUND_SELECT);
        if (value) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
        if (f.cb.toggled) {
            f.cb.toggled(value, f.ctx);
        }
        break;
    }
    case Focusable::Kind::MenuItem:
        Audio_PlaySound(AUDIO_SOUND_SELECT);
        if (f.cb.selected) {
            f.cb.selected(f.index, f.ctx);
        }
        break;
    case Focusable::Kind::Slider:
    case Focusable::Kind::Stepper:
        break; // these react to LEFT/RIGHT, not A
    }
}

void Screen::handleInput() noexcept
{
    InputButton button;
    if (!Input_PopAction(&button)) {
        return;
    }

    // Back: single press of B.
    if (button == INPUT_BUTTON_B) {
        if (back_ != nullptr) {
            Audio_PlaySound(AUDIO_SOUND_BACK);
            back_(backCtx_);
        }
        return;
    }

    if (count_ == 0) {
        return;
    }

    // Joystick UP/DOWN move the cursor.
    if (button == INPUT_BUTTON_UP) {
        prev();
        return;
    }
    if (button == INPUT_BUTTON_DOWN) {
        next();
        return;
    }

    Focusable &cur = items_[cursor_];

    if (cur.consumesHorizontal()) {
        std::int32_t dir = 0;
        if (button == INPUT_BUTTON_LEFT) {
            dir = -1;
        } else if (button == INPUT_BUTTON_RIGHT) {
            dir = 1;
        }
        if (dir != 0 && cur.kind == Focusable::Kind::Stepper) {
            if (cur.cb.stepped) {
                cur.cb.stepped(dir, cur.ctx);
            }
            Audio_PlaySound(AUDIO_SOUND_NAV);
            return;
        }
        if (dir != 0) {
            lv_obj_t *sl = cur.target();
            std::int32_t lo = lv_slider_get_min_value(sl);
            std::int32_t hi = lv_slider_get_max_value(sl);
            std::int32_t oldValue = lv_slider_get_value(sl);
            std::int32_t value = oldValue + dir * cur.step;
            if (value < lo) {
                value = lo;
            }
            if (value > hi) {
                value = hi; // clamp ourselves: set_value may not, causing dead presses
            }
            if (value != oldValue) {
                lv_slider_set_value(sl, value, LV_ANIM_OFF);
                lv_obj_send_event(sl, LV_EVENT_VALUE_CHANGED, nullptr); // move custom knob
                if (cur.cb.changed) {
                    cur.cb.changed(lv_slider_get_value(sl), cur.ctx);
                }
                Audio_PlaySound(AUDIO_SOUND_NAV);
            }
            return;
        }
    } else {
        if (button == INPUT_BUTTON_LEFT) {
            prev();
            return;
        }
        if (button == INPUT_BUTTON_RIGHT) {
            next();
            return;
        }
    }

    if (button == INPUT_BUTTON_A || button == INPUT_BUTTON_CENTER) {
        activate(items_[cursor_]);
        return;
    }
}

} // namespace ui
