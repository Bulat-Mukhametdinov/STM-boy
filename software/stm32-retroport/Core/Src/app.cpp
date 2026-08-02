#include "app.h"

#include "audio.h"
#include "display.h"
#include "emulators.h"
#include "input.h"
#include "main.h"
#include "power.h"
#include "rom_upload.h"
#include "settings.h"
#include "ui/ProgressBar.h"
#include "ui/ui.h"

#include <cstdint>
#include <cstdio>

using namespace ui;

namespace {

Screen g_menu;
Screen g_emulators;
Screen g_settings;
Screen g_upload_screen;
Screen *g_current = nullptr;
bool g_menu_built = false;
bool g_emulators_built = false;
bool g_settings_built = false;
bool g_upload_built = false;

constexpr std::uint32_t kUploadUiPeriodMs = 250;
constexpr std::uint32_t kUploadUiStepBytes = 4096;
constexpr std::uint32_t kBatteryUiPeriodMs = 1000;

enum MainMenuIndex : std::uint8_t {
    kMainPlay = 0,
    kMainUpload = 1,
    kMainSettings = 2,
};

enum class PendingAction : std::uint8_t {
    None,
    MainMenu,
    EmulatorMenu,
    EmulatorRomList,
    Upload,
    Settings,
    SettingsReload, // rebuild the settings screen in place (e.g. after a theme change)
};

// Focus index of the theme stepper in the settings screen.
constexpr std::uint8_t kSettingsThemeRow = 1;

const Menu::ItemDef kMainMenuItems[] = {
    {LV_SYMBOL_PLAY, "Play", "Choose Emulator"},
    {LV_SYMBOL_UPLOAD, "USB ROM Upload", "Write ROM Pack"},
    {LV_SYMBOL_SETTINGS, "Settings", "Audio & Theme"},
};

ProgressBar g_upload_progress;
lv_obj_t *g_upload_status_label = nullptr;
lv_obj_t *g_upload_detail_label = nullptr;
lv_obj_t *g_upload_percent_label = nullptr;
char g_upload_detail_text[72] = "Start upload on PC";
char g_upload_percent_text[16] = "0%";
Slider g_volume;
BatteryIndicator g_battery;
PendingAction g_pending_action = PendingAction::None;
std::uint8_t g_last_battery_percent = 0xFF;
bool g_last_battery_charging = false;
std::uint32_t g_last_battery_ui_ms = 0;
std::uint8_t g_selected_emulator = 0;

void build_boot_menu();
void build_emulator_menu();
void build_settings();
void build_upload_screen();
void destroy_main_menu();
void destroy_emulator_menu();
void destroy_upload_screen();
void destroy_settings_screen();

void navigate(Screen &s)
{
    g_current = &s;
    s.show();
}

// Build the battery status indicator on the top layer so it floats in the
// top-right corner above whichever screen is loaded. Rebuilt on theme changes
// so its colours follow the active palette.
void build_battery_indicator()
{
    g_battery.destroy();
    g_battery = BatteryIndicator::make(Widget(lv_layer_top()));
    g_battery.align(LV_ALIGN_TOP_RIGHT, -4, 4);
    g_last_battery_percent = 0xFF;
    g_last_battery_ui_ms = 0;
    g_battery.percent(Power_GetBatteryPercent()).charging(Power_IsCharging());
}

void refresh_battery_indicator()
{
    std::uint32_t now_ms = HAL_GetTick();
    std::uint8_t percent = Power_GetBatteryPercent();
    bool is_charging = Power_IsCharging();

    if ((now_ms - g_last_battery_ui_ms) < kBatteryUiPeriodMs &&
        percent == g_last_battery_percent &&
        is_charging == g_last_battery_charging) {
        return;
    }

    g_battery.percent(percent).charging(is_charging);
    g_last_battery_percent = percent;
    g_last_battery_charging = is_charging;
    g_last_battery_ui_ms = now_ms;
}

void request(PendingAction action)
{
    g_pending_action = action;
}

void emulator_navigate(Screen &s, void * /*ctx*/)
{
    navigate(s);
}

void drain_input_actions()
{
    InputButton ignored;
    while (Input_PopAction(&ignored)) {
    }
}

void on_emulators_back(void * /*ctx*/)
{
    request(PendingAction::MainMenu);
}

bool upload_exit_blocked()
{
    RomUploadStatus status{};
    RomUpload_GetStatus(&status);
    return status.phase == ROM_UPLOAD_PHASE_ERASING ||
           status.phase == ROM_UPLOAD_PHASE_WRITING ||
           status.phase == ROM_UPLOAD_PHASE_VERIFYING;
}

void on_emulator_back(void * /*ctx*/)
{
    request(PendingAction::EmulatorMenu);
}

void on_main_menu_select(std::uint8_t index, void * /*ctx*/)
{
    if (index == kMainPlay) {
        request(PendingAction::EmulatorMenu);
    } else if (index == kMainUpload) {
        request(PendingAction::Upload);
    } else if (index == kMainSettings) {
        request(PendingAction::Settings);
    }
}

void on_emulator_select(std::uint8_t index, void * /*ctx*/)
{
    if (index < emulators::count()) {
        g_selected_emulator = index;
        request(PendingAction::EmulatorRomList);
    }
}

void build_boot_menu()
{
    if (g_menu_built) {
        return;
    }

    const Theme &t = Theme::active();
    Widget root = g_menu.applyTheme();
    Stack column = VStack(root);
    column.padAll(t.space.padMd).gap(t.space.gap);
    column.fill();

    Label::make(column, "RetroPort", Label::Role::Title);
    Menu::make(column, g_menu).items(kMainMenuItems, on_main_menu_select);

    g_menu.focus(0);
    g_menu_built = true;
}

void build_emulator_menu()
{
    if (g_emulators_built) {
        return;
    }

    const Theme &t = Theme::active();
    Widget root = g_emulators.applyTheme();
    Stack column = VStack(root);
    column.padAll(t.space.padMd).gap(t.space.gap);
    column.fill();

    Label::make(column, "Play", Label::Role::Title);
    Menu menu = Menu::make(column, g_emulators).onSelect(on_emulator_select);
    for (std::uint8_t i = 0; i < emulators::count(); ++i) {
        const emulators::Driver *driver = emulators::get(i);
        if (driver == nullptr) {
            continue;
        }
        menu.item({driver->icon, driver->name, driver->meta});
    }
    Label::make(column, LV_SYMBOL_LEFT " B: back", Label::Role::Caption);

    g_emulators.onBack(on_emulators_back);
    g_emulators.focus(0);
    g_emulators_built = true;
}

void on_upload_back(void * /*ctx*/)
{
    if (upload_exit_blocked()) {
        drain_input_actions();
        return;
    }

    RomUpload_Stop();
    drain_input_actions();
    request(PendingAction::MainMenu);
}

void upload_refresh_ui()
{
    RomUploadStatus status{};
    std::uint32_t value = 0;
    static std::uint32_t last_value = 0xFFFFFFFFu;
    static RomUploadPhase last_phase = ROM_UPLOAD_PHASE_OFF;
    static const char *last_message = nullptr;
    static std::uint32_t last_done = 0xFFFFFFFFu;
    static std::uint32_t last_total = 0xFFFFFFFFu;
    static std::uint32_t last_jedec_id = 0xFFFFFFFFu;
    static std::uint32_t last_render_ms = 0;
    std::uint32_t now_ms = HAL_GetTick();

    RomUpload_GetStatus(&status);

    bool phase_changed = status.phase != last_phase;
    bool message_changed = status.message != last_message;
    bool total_changed = status.total != last_total;
    bool jedec_changed = status.jedec_id != last_jedec_id;
    bool step_changed = (status.done / kUploadUiStepBytes) != (last_done / kUploadUiStepBytes);
    bool force_update = phase_changed ||
                        message_changed ||
                        total_changed ||
                        jedec_changed ||
                        status.phase == ROM_UPLOAD_PHASE_DONE ||
                        status.phase == ROM_UPLOAD_PHASE_ERROR ||
                        (status.total > 0u && status.done >= status.total);

    if (status.phase == last_phase &&
        status.message == last_message &&
        status.done == last_done &&
        status.total == last_total &&
        status.jedec_id == last_jedec_id) {
        return;
    }
    if (!force_update &&
        !step_changed &&
        (now_ms - last_render_ms) < kUploadUiPeriodMs) {
        return;
    }

    last_phase = status.phase;
    last_message = status.message;
    last_done = status.done;
    last_total = status.total;
    last_jedec_id = status.jedec_id;
    last_render_ms = now_ms;

    if (status.total > 0u) {
        value = (status.done >= status.total) ? 1000u : (status.done * 1000u / status.total);
    } else if (status.phase == ROM_UPLOAD_PHASE_DONE) {
        value = 1000u;
    }

    if (value != last_value) {
        g_upload_progress.value(static_cast<std::int32_t>(value));
        last_value = value;
    }

    if (g_upload_status_label != nullptr) {
        lv_label_set_text_static(g_upload_status_label, status.message);
    }

    if (g_upload_detail_label != nullptr) {
        if (status.total == 0u) {
            std::snprintf(g_upload_detail_text, sizeof(g_upload_detail_text), "Start upload on PC");
        } else {
            std::snprintf(g_upload_detail_text, sizeof(g_upload_detail_text), "%lu/%lu bytes",
                          static_cast<unsigned long>(status.done),
                          static_cast<unsigned long>(status.total));
        }
        lv_label_set_text_static(g_upload_detail_label, g_upload_detail_text);
    }

    if (g_upload_percent_label != nullptr) {
        std::snprintf(g_upload_percent_text, sizeof(g_upload_percent_text), "%lu%%",
                      static_cast<unsigned long>(value / 10u));
        lv_label_set_text_static(g_upload_percent_label, g_upload_percent_text);
    }
}

void build_upload_screen()
{
    if (g_upload_built) {
        return;
    }

    const Theme &t = Theme::active();
    Widget root = g_upload_screen.applyTheme();
    Stack column = VStack(root);
    column.padAll(t.space.padMd).gap(t.space.gap);
    column.fill();

    Label::make(column, "USB Upload", Label::Role::Title);

    Panel panel = Panel::make(column);
    panel.padAll(t.space.padMd);
    panel.width(LV_PCT(100));
    panel.height(LV_SIZE_CONTENT);

    Stack rows = VStack(panel);
    rows.gap(t.space.padMd);
    rows.width(LV_PCT(100));
    rows.height(LV_SIZE_CONTENT);

    g_upload_status_label = Label::make(rows, "Waiting for upload", Label::Role::Body).raw();
    g_upload_progress = ProgressBar::make(rows);
    g_upload_percent_label = Label::make(rows, g_upload_percent_text, Label::Role::Body).raw();
    g_upload_detail_label = Label::make(rows, g_upload_detail_text, Label::Role::Caption).raw();
    lv_label_set_text_static(g_upload_status_label, "Waiting for upload");
    lv_label_set_text_static(g_upload_percent_label, g_upload_percent_text);
    lv_label_set_text_static(g_upload_detail_label, g_upload_detail_text);

    Label::make(column, LV_SYMBOL_LEFT " B: back when idle", Label::Role::Caption);

    g_upload_screen.onBack(on_upload_back);
    g_upload_built = true;
}

Stack settings_row(Widget parent, const char *label)
{
    const Theme &t = Theme::active();
    Stack row = HStack(parent);
    row.crossAlign(LV_FLEX_ALIGN_CENTER).gap(t.space.padMd);
    row.width(LV_PCT(100));
    row.padAll(t.space.padSm);
    lv_obj_add_style(row.raw(), styles::menuItemFocused(), LV_PART_MAIN | LV_STATE_FOCUSED);
    Label::make(row, label, Label::Role::Body).flexGrow(1);
    return row;
}

void on_volume(std::int32_t value, void * /*ctx*/)
{
    Audio_SetVolume(static_cast<std::uint8_t>(value));
    Settings_SetVolume(static_cast<std::uint8_t>(value));
}

void on_theme_step(std::int32_t dir, void * /*ctx*/)
{
    int count = themeCount();
    if (count <= 1) {
        return;
    }
    int current = Settings_GetTheme();
    if (current >= count) {
        current = 0;
    }
    int next = ((current + dir) % count + count) % count; // wrap both directions
    if (next == current) {
        return;
    }

    Settings_SetTheme(static_cast<std::uint8_t>(next));
    Theme::setActive(themeAt(next));

    // Widgets capture theme colours at creation, so prebuilt screens must be
    // rebuilt to pick up the new palette. Drop them; they rebuild on demand.
    destroy_main_menu();
    destroy_emulator_menu();
    emulators::destroyRomLists();
    build_battery_indicator(); // re-capture the new palette's colours

    request(PendingAction::SettingsReload);
}

void on_settings_back(void * /*ctx*/)
{
    (void)Settings_Flush();
    request(PendingAction::MainMenu);
}

void process_pending_action()
{
    PendingAction action = g_pending_action;
    g_pending_action = PendingAction::None;

    switch (action) {
    case PendingAction::None:
        break;
    case PendingAction::MainMenu:
        build_boot_menu();
        navigate(g_menu);
        destroy_upload_screen();
        destroy_settings_screen();
        emulators::destroyRomLists();
        break;
    case PendingAction::EmulatorMenu:
        build_emulator_menu();
        navigate(g_emulators);
        emulators::destroyRomLists();
        break;
    case PendingAction::EmulatorRomList: {
        const emulators::Driver *driver = emulators::get(g_selected_emulator);
        destroy_upload_screen();
        destroy_settings_screen();
        if (driver != nullptr && driver->showRomList != nullptr) {
            driver->showRomList();
        }
        break;
    }
    case PendingAction::Upload:
        drain_input_actions();
        destroy_settings_screen();
        emulators::destroyRomLists();
        build_upload_screen();
        RomUpload_Start();
        navigate(g_upload_screen);
        break;
    case PendingAction::Settings:
        destroy_upload_screen();
        emulators::destroyRomLists();
        build_settings();
        navigate(g_settings);
        break;
    case PendingAction::SettingsReload:
        destroy_settings_screen();
        build_settings();
        navigate(g_settings);
        g_settings.focus(kSettingsThemeRow); // keep the cursor on the theme row
        break;
    }
}

void destroy_main_menu()
{
    if (g_current == &g_menu) {
        g_current = nullptr;
    }
    g_menu.destroy();
    g_menu_built = false;
}

void destroy_emulator_menu()
{
    if (g_current == &g_emulators) {
        g_current = nullptr;
    }
    g_emulators.destroy();
    g_emulators_built = false;
}

void destroy_upload_screen()
{
    if (g_current == &g_upload_screen) {
        g_current = nullptr;
    }
    g_upload_screen.destroy();
    g_upload_built = false;
    g_upload_status_label = nullptr;
    g_upload_detail_label = nullptr;
    g_upload_percent_label = nullptr;
    g_upload_progress = ProgressBar{};
}

void destroy_settings_screen()
{
    if (g_current == &g_settings) {
        g_current = nullptr;
    }
    g_settings.destroy();
    g_settings_built = false;
    g_volume = Slider{};
}

void build_settings()
{
    if (g_settings_built) {
        return;
    }

    const Theme &t = Theme::active();
    Widget root = g_settings.applyTheme();
    Stack column = VStack(root);
    column.padAll(t.space.padMd).gap(t.space.padSm);
    column.fill();

    Label::make(column, "Settings", Label::Role::Title);

    Panel card = Panel::make(column);
    card.padAll(t.space.padSm);
    card.width(LV_PCT(100));
    card.height(LV_SIZE_CONTENT);

    Stack rows = VStack(card);
    rows.gap(t.space.padSm);
    rows.width(LV_PCT(100));
    rows.height(LV_SIZE_CONTENT);

    Stack volumeRow = settings_row(rows, "Volume");
    g_volume = Slider::make(volumeRow, 0, 100, Settings_GetVolume());
    g_volume.attachTo(g_settings, on_volume, 5, nullptr, volumeRow);
    g_volume.width(64);

    // Theme stepper: LEFT/RIGHT cycle the palette, name shown on the right.
    Stack themeRow = settings_row(rows, "Theme");
    char themeText[28];
    std::snprintf(themeText, sizeof(themeText), LV_SYMBOL_LEFT " %s " LV_SYMBOL_RIGHT,
                  themeName(Settings_GetTheme()));
    Label::make(themeRow, themeText, Label::Role::Caption);

    Focusable themeFocus;
    themeFocus.obj = themeRow.raw();
    themeFocus.kind = Focusable::Kind::Stepper;
    themeFocus.cb.stepped = on_theme_step;
    g_settings.add(themeFocus);

    Label::make(column, LV_SYMBOL_LEFT " B: back", Label::Role::Caption);

    g_settings.onBack(on_settings_back);
    g_settings.focus(0);
    g_settings_built = true;
}

static void app_shutdown()
{
    // Quiesce audio so the amp carrier settles, then persist pending settings
    // before the ATTINY power controller removes system power.
    Audio_ClearStream();
    Audio_StopMusic();
    Settings_Flush();

    // Acknowledge the shutdown request; the ATTINY cuts EN shortly after.
    Power_SetOffAck(true);
    for (;;) {
    }
}

} // namespace

extern "C" void App_Init(void)
{
    Power_Init();
    Input_Init();
    Audio_Init();
    Settings_Init();
    Audio_SetVolume(Settings_GetVolume());
    Display_Init();
    Theme::setActive(themeAt(Settings_GetTheme()));

    build_boot_menu();
    build_battery_indicator();
    emulators::initAll(emulator_navigate, nullptr, on_emulator_back, nullptr);

    navigate(g_menu);
}

extern "C" void App_Task(void)
{
    if (Power_PollShutdown()) {
        app_shutdown();
        return;
    }

    Power_Task();
    refresh_battery_indicator();
    Input_Update();
    Settings_Task();
    const emulators::Driver *active_game = emulators::activeGame(g_current);
    if (active_game != nullptr) {
        if (active_game->task != nullptr) {
            active_game->task();
        }
        Display_Task();
        if (active_game->isGameScreen != nullptr &&
            active_game->isGameScreen(g_current) &&
            active_game->refreshImageIfNeeded != nullptr) {
            active_game->refreshImageIfNeeded();
        }
    } else if (g_current == &g_upload_screen) {
        g_current->handleInput();
        process_pending_action();
        if (g_current != &g_upload_screen) {
            Display_Task();
            return;
        }
        RomUpload_Task();
        upload_refresh_ui();
        Display_Task();
    } else {
        g_current->handleInput();
        process_pending_action();
        Display_Task();
    }
}
