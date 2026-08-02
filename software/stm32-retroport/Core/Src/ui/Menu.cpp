#include "ui/Menu.h"

#include "ui/Icon.h"
#include "ui/Label.h"
#include "ui/Stack.h"
#include "ui/Theme.h"

namespace ui {

Menu Menu::make(Widget parent, Screen &screen)
{
    Stack col = VStack(parent);
    lv_obj_set_width(col.raw(), LV_PCT(100));
    lv_obj_set_height(col.raw(), 0);
    lv_obj_set_flex_grow(col.raw(), 1);
    lv_obj_remove_flag(col.raw(), LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(col.raw(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(col.raw(), LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col.raw(), LV_SCROLLBAR_MODE_OFF);

    Menu m(col.raw());
    m.screen_ = &screen;
    return m;
}

Menu &Menu::item(const ItemDef &def)
{
    if (screen_ == nullptr || screen_->count() >= Screen::kMaxFocus) {
        return *this;
    }

    // Row surface: shared menu-item style + focused-state overlay.
    lv_obj_t *row = detail::makeBare(Widget(obj_));
    lv_obj_add_style(row, styles::menuItem(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(row, styles::menuItemFocused(), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    Widget rowWidget(row);
    if (def.icon != nullptr) {
        Icon::make(rowWidget, def.icon);
    }

    Stack text = VStack(rowWidget);
    lv_obj_set_height(text.raw(), LV_SIZE_CONTENT);
    text.gap(0).flexGrow(1);
    Label::make(text, def.title, Label::Role::Body);
    if (def.meta != nullptr) {
        Label::make(text, def.meta, Label::Role::Caption);
    }

    Focusable f;
    f.obj = row;
    f.kind = Focusable::Kind::MenuItem;
    f.cb.selected = selectCb_;
    f.ctx = selectCtx_;
    f.index = count_;
    if (screen_->add(f)) {
        ++count_;
    } else {
        lv_obj_delete(row);
    }
    return *this;
}

} // namespace ui
