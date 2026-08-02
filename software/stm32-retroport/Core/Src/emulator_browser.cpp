#include "emulator_browser.h"

#include "rompack.h"
#include "ui/ui.h"

#include <cstdint>
#include <cstdio>

using namespace ui;

namespace emulator_browser {
namespace {

constexpr std::uint8_t kWindowSize = 4;

Screen g_screen;
NavigateFn g_navigate = nullptr;
void *g_navigate_ctx = nullptr;
BackFn g_back = nullptr;
void *g_back_ctx = nullptr;

Config g_config{};
RomItem g_roms[ROMPACK_MAX_ENTRIES];
char g_titles[ROMPACK_MAX_ENTRIES][ROMPACK_TITLE_SIZE];
char g_meta[ROMPACK_MAX_ENTRIES][24];
char g_status_text[64] = "ROM pack not loaded";
lv_obj_t *g_title_labels[kWindowSize];
lv_obj_t *g_meta_labels[kWindowSize];
std::uint16_t g_rom_count = 0;
std::uint16_t g_window_start = 0;
std::uint16_t g_selected_rom = 0;
std::uint8_t g_row_count = 0;
bool g_built = false;

void set_status(const char *text)
{
    std::snprintf(g_status_text, sizeof(g_status_text), "%s", text != nullptr ? text : "ROM pack error");
}

std::uint8_t window_count()
{
    if (g_rom_count <= g_window_start) {
        return 0;
    }

    std::uint16_t count = static_cast<std::uint16_t>(g_rom_count - g_window_start);
    return count > kWindowSize ? kWindowSize : static_cast<std::uint8_t>(count);
}

void keep_selected_visible()
{
    if (g_rom_count == 0u) {
        g_selected_rom = 0;
        g_window_start = 0;
        return;
    }

    if (g_selected_rom >= g_rom_count) {
        g_selected_rom = static_cast<std::uint16_t>(g_rom_count - 1u);
    }
    if (g_window_start >= g_rom_count) {
        g_window_start = g_selected_rom;
    }
    if (g_selected_rom < g_window_start) {
        g_window_start = g_selected_rom;
    }
    if (g_selected_rom >= static_cast<std::uint16_t>(g_window_start + kWindowSize)) {
        g_window_start = static_cast<std::uint16_t>(g_selected_rom - kWindowSize + 1u);
    }
}

void focus_selected()
{
    if (g_rom_count > 0u && g_selected_rom >= g_window_start) {
        g_screen.focus(static_cast<std::uint8_t>(g_selected_rom - g_window_start));
    }
}

void update_window_labels()
{
    for (std::uint8_t i = 0; i < g_row_count; ++i) {
        std::uint16_t rom_index = static_cast<std::uint16_t>(g_window_start + i);
        if (rom_index >= g_rom_count) {
            return;
        }

        if (g_title_labels[i] != nullptr) {
            lv_label_set_text(g_title_labels[i], g_roms[rom_index].title);
        }
        if (g_meta_labels[i] != nullptr) {
            lv_label_set_text(g_meta_labels[i], g_roms[rom_index].meta);
        }
    }
}

void on_select(std::uint8_t index, void * /*ctx*/)
{
    std::uint16_t rom_index = static_cast<std::uint16_t>(g_window_start + index);
    if (index < window_count() && rom_index < g_rom_count && g_config.on_select != nullptr) {
        g_selected_rom = rom_index;
        g_config.on_select(g_roms[rom_index], g_config.select_ctx);
    }
}

bool on_edge(bool forward, void * /*ctx*/)
{
    if (g_rom_count == 0u) {
        return false;
    }

    std::uint16_t current = static_cast<std::uint16_t>(g_window_start + g_screen.cursor());

    if (forward) {
        if (current + 1u >= g_rom_count) {
            return false;
        }
        g_selected_rom = static_cast<std::uint16_t>(current + 1u);
    } else {
        if (current == 0u) {
            return false;
        }
        g_selected_rom = static_cast<std::uint16_t>(current - 1u);
    }

    keep_selected_visible();
    update_window_labels();
    focus_selected();
    return true;
}

void on_back(void * /*ctx*/)
{
    if (g_back != nullptr) {
        g_back(g_back_ctx);
    }
}

void build_screen()
{
    if (g_built) {
        return;
    }

    const Theme &t = Theme::active();
    Widget root = g_screen.applyTheme();
    Stack column = VStack(root);
    column.padAll(t.space.padMd).gap(t.space.gap);
    column.fill();

    Label::make(column, g_config.title != nullptr ? g_config.title : "ROMs", Label::Role::Title);

    if (g_rom_count == 0u) {
        Label::make(column, g_status_text, Label::Role::Body);
        if (g_config.empty_hint != nullptr) {
            Label::make(column, g_config.empty_hint, Label::Role::Caption);
        }
    } else {
        Stack list = VStack(column);
        lv_obj_set_width(list.raw(), LV_PCT(100));
        lv_obj_set_height(list.raw(), 0);
        lv_obj_set_flex_grow(list.raw(), 1);
        lv_obj_remove_flag(list.raw(), LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_add_flag(list.raw(), LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(list.raw(), LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list.raw(), LV_SCROLLBAR_MODE_OFF);

        g_row_count = window_count();
        for (std::uint8_t i = 0; i < g_row_count; ++i) {
            lv_obj_t *row = detail::makeBare(list);
            lv_obj_add_style(row, styles::menuItem(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_style(row, styles::menuItemFocused(), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            Widget row_widget(row);
            Icon::make(row_widget, LV_SYMBOL_PLAY);

            Stack text = VStack(row_widget);
            lv_obj_set_height(text.raw(), LV_SIZE_CONTENT);
            text.gap(0).flexGrow(1);
            g_title_labels[i] = Label::make(text, "", Label::Role::Body).raw();
            g_meta_labels[i] = Label::make(text, "", Label::Role::Caption).raw();

            Focusable f;
            f.obj = row;
            f.kind = Focusable::Kind::MenuItem;
            f.cb.selected = on_select;
            f.index = i;
            g_screen.add(f);
        }

        update_window_labels();
    }

    Label::make(column, LV_SYMBOL_LEFT " B: back", Label::Role::Caption);
    g_screen.onBack(on_back);
    g_screen.onEdge(on_edge);
    g_built = true;
}

void rebuild()
{
    std::uint16_t previous_selection = g_selected_rom;
    std::uint16_t system_seen = 0;
    std::uint16_t oversized = 0;
    bool pack_ready;

    g_rom_count = 0;
    pack_ready = RomPack_Init();
    if (pack_ready) {
        std::uint16_t count = RomPack_Count();
        for (std::uint16_t i = 0; i < count && g_rom_count < ROMPACK_MAX_ENTRIES; ++i) {
            RomPackEntry entry{};
            if (!RomPack_Get(i, &entry) || entry.system_id != g_config.system_id) {
                continue;
            }

            ++system_seen;
            if (g_config.max_rom_size > 0u && entry.size > g_config.max_rom_size) {
                ++oversized;
                continue;
            }

            std::snprintf(g_titles[g_rom_count], sizeof(g_titles[g_rom_count]), "%s", entry.title);
            std::snprintf(g_meta[g_rom_count], sizeof(g_meta[g_rom_count]),
                          "%lu bytes", static_cast<unsigned long>(entry.size));

            RomItem &rom = g_roms[g_rom_count];
            rom.title = g_titles[g_rom_count];
            rom.meta = g_meta[g_rom_count];
            rom.size = entry.size;
            rom.rompack_index = i;
            rom.option_flags = entry.option_flags;
            rom.tickrate = entry.tickrate;
            ++g_rom_count;
        }
    }

    if (!pack_ready) {
        set_status(RomPack_Status());
    } else if (system_seen == 0u) {
        std::snprintf(g_status_text, sizeof(g_status_text), "No %s ROMs",
                      g_config.title != nullptr ? g_config.title : "system");
    } else if (g_rom_count == 0u && oversized > 0u) {
        set_status("ROMs too large");
    } else {
        set_status(RomPack_Status());
    }

    g_selected_rom = previous_selection;
    keep_selected_visible();
    g_screen.clear();
    for (std::uint8_t i = 0; i < kWindowSize; ++i) {
        g_title_labels[i] = nullptr;
        g_meta_labels[i] = nullptr;
    }
    g_row_count = 0;
    g_built = false;
    build_screen();
    focus_selected();
}

void navigate(Screen &screen)
{
    if (g_navigate != nullptr) {
        g_navigate(screen, g_navigate_ctx);
    }
}

void destroy_screen()
{
    g_screen.destroy();
    for (std::uint8_t i = 0; i < kWindowSize; ++i) {
        g_title_labels[i] = nullptr;
        g_meta_labels[i] = nullptr;
    }
    g_rom_count = 0;
    g_row_count = 0;
    g_built = false;
}

} // namespace

void init(NavigateFn navigateFn, void *navigateCtx, BackFn backFn, void *backCtx)
{
    g_navigate = navigateFn;
    g_navigate_ctx = navigateCtx;
    g_back = backFn;
    g_back_ctx = backCtx;
}

void show(const Config &config)
{
    g_config = config;
    rebuild();
    navigate(g_screen);
}

void hide()
{
    destroy_screen();
}

void destroy()
{
    destroy_screen();
    g_window_start = 0;
    g_selected_rom = 0;
}

bool isScreen(const ui::Screen *screen)
{
    return screen == &g_screen;
}

ui::Screen &screen()
{
    return g_screen;
}

} // namespace emulator_browser
