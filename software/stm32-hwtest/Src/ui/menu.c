/* Main menu + simple info screens. Non-blocking: ui_task() is called from the
   main loop, so host commands keep working while a screen is shown. */

#include "ui.h"
#include "gauge_fw.h"
#include "board.h"
#include "clock.h"
#include "cmd.h"
#include "dfu.h"
#include "fw_id.h"
#include "gfx.h"
#include "hwcal.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "power.h"
#include "suites.h"
#include "usb.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>

typedef enum {
    SCR_MENU = 0,
    SCR_RESULT,   /* suite console left on screen until A/B */
    SCR_BUTTONS,
    SCR_DISPLAY,
    SCR_USBINFO,
    SCR_TESTS,    /* scrolling list of all suites */
} screen_t;

static const char *const s_items[] = {
    "Self test", "Tests...", "Button view", "Display info", "USB/Info", "Enter DFU", "Reboot",
};
#define N_ITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))

#define MENU_Y0   14
#define MENU_DY   14
#define BAR_H     10

static screen_t s_scr = SCR_MENU;
static int s_sel;
static bool s_dirty = true;
static uint32_t s_last_refresh;
static uint8_t s_usb_state = 0xFF;
static uint16_t s_btn_drawn = 0xFFFF;
static uint32_t s_b_hold_start;
static int s_tsel, s_ttop;    /* Tests list: selection, first visible row */

#define T_Y0   12
#define T_DY   11
#define T_ROWS 9

void ui_invalidate(void)
{
    s_dirty = true;
    s_usb_state = 0xFF;
    s_btn_drawn = 0xFFFF;
}

static uint8_t usb_state(void)
{
    return (uint8_t)((usb_configured() ? 1u : 0u) | (usb_dtr() ? 2u : 0u));
}

static uint32_t s_gauge_sig;

void ui_status_bar(void)
{
    uint8_t st = usb_state();
    uint16_t usb_col = (st & 2u) ? COLOR_GREEN : (st & 1u) ? COLOR_YELLOW : COLOR_GRAY;
    char b[32];
    snprintf(b, sizeof(b), "%.13s", g_fw_hash);   /* short: leaves room for battery icon + USB */
    lcd_fill_rect(0, 0, (int16_t)lcd_width(), BAR_H, COLOR_NAVY);
    gfx_text(1, 1, b, COLOR_WHITE, COLOR_NAVY, 1);
    gfx_text((int16_t)(lcd_width() - 19), 1, "USB", usb_col, COLOR_NAVY, 1);
    gauge_fw_draw_icon((int16_t)(lcd_width() - 19 - 36), 1, COLOR_NAVY);
    s_gauge_sig = gauge_fw_sig();
    s_usb_state = st;
}

void ui_init(void)
{
    s_scr = SCR_MENU;
    s_sel = 0;
    ui_invalidate();
}

/* ---------------------------------------------------------------- menu */

static void draw_menu_item(int i)
{
    bool sel = (i == s_sel);
    uint16_t fg = sel ? COLOR_BLACK : COLOR_WHITE;
    uint16_t bg = sel ? COLOR_CYAN : COLOR_BLACK;
    int16_t y = (int16_t)(MENU_Y0 + i * MENU_DY);
    lcd_fill_rect(0, (int16_t)(y - 2), (int16_t)lcd_width(), 12, bg);
    gfx_text(8, y, s_items[i], fg, bg, 1);
}

static void draw_menu(void)
{
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    for (int i = 0; i < N_ITEMS; i++) {
        draw_menu_item(i);
    }
    gfx_text(2, (int16_t)(lcd_height() - 9), "UP/DN move  A select", COLOR_GRAY, COLOR_BLACK, 1);
}

/* ---------------------------------------------------------------- buttons view */

typedef struct {
    int16_t x, y, w, h;
    const char *label;
    uint16_t color;
} btn_box_t;

static const btn_box_t s_boxes[BTN_COUNT] = {
    [BTN_UP] = {26, 40, 20, 20, "U", COLOR_RED},
    [BTN_DOWN] = {26, 88, 20, 20, "D", COLOR_RED},
    [BTN_LEFT] = {4, 64, 20, 20, "L", COLOR_RED},
    [BTN_RIGHT] = {48, 64, 20, 20, "R", COLOR_RED},
    [BTN_CENTER] = {26, 64, 20, 20, "C", COLOR_YELLOW},
    /* physical gamepad layout (stm32-bringup): Y top, B right, A bottom, X left */
    [BTN_Y] = {116, 40, 20, 20, "Y", COLOR_YELLOW},
    [BTN_B] = {138, 64, 20, 20, "B", COLOR_RED},
    [BTN_A] = {116, 88, 20, 20, "A", COLOR_GREEN},
    [BTN_X] = {94, 64, 20, 20, "X", COLOR_BLUE},
    [BTN_POWER] = {64, 14, 32, 14, "PWR", COLOR_MAGENTA},
};

static void draw_box_ex(int i, bool on, bool seen)
{
    const btn_box_t *b = &s_boxes[i];
    uint16_t bg = on ? b->color : (seen ? COLOR_DARKGRAY : COLOR_BLACK);
    uint16_t fg = on ? COLOR_BLACK : b->color;
    lcd_fill_rect(b->x, b->y, b->w, b->h, bg);
    gfx_rect(b->x, b->y, b->w, b->h, b->color);
    int16_t tw = (int16_t)(6 * (int16_t)__builtin_strlen(b->label) - 1);
    gfx_text((int16_t)(b->x + (b->w - tw) / 2), (int16_t)(b->y + (b->h - 7) / 2), b->label, fg, bg, 1);
}

static void draw_box(int i, bool on) { draw_box_ex(i, on, false); }

/* Shared with input.live: redraws boxes whose held/seen state changed
   (all of them when `full`). Seen-but-released boxes are dark grey. */
void ui_draw_button_map(uint16_t held, bool full, uint16_t seen)
{
    static uint16_t s_h, s_s;
    for (int i = 0; i < BTN_COUNT; i++) {
        uint16_t m = BTN_MASK(i);
        if (full || ((held ^ s_h) & m) || ((seen ^ s_s) & m)) {
            draw_box_ex(i, (held & m) != 0u, (seen & m) != 0u);
        }
    }
    s_h = held;
    s_s = seen;
}

static void draw_buttons(bool full)
{
    uint16_t held = input_held();
    if (full) {
        gfx_fill(COLOR_BLACK);
        ui_status_bar();
        gfx_text(2, (int16_t)(lcd_height() - 9), "hold B 1s: back", COLOR_GRAY, COLOR_BLACK, 1);
        s_btn_drawn = (uint16_t)~held;
    }
    uint16_t changed = (uint16_t)(held ^ s_btn_drawn);
    for (int i = 0; i < BTN_COUNT; i++) {
        if (changed & BTN_MASK(i)) {
            draw_box(i, (held & BTN_MASK(i)) != 0u);
        }
    }
    if (changed) {
        gfx_textf(2, 114, COLOR_WHITE, COLOR_BLACK, 1, "mask 0x%03X", (unsigned)held);
    }
    s_btn_drawn = held;
}

/* ---------------------------------------------------------------- display info */

static void draw_display_info(void)
{
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    gfx_fill(COLOR_BLACK);
    gfx_rect(0, 0, w, h, COLOR_WHITE);                 /* 1 px border */
    lcd_fill_rect(1, 1, 6, 6, COLOR_RED);               /* TL */
    lcd_fill_rect((int16_t)(w - 7), 1, 6, 6, COLOR_GREEN);          /* TR */
    lcd_fill_rect(1, (int16_t)(h - 7), 6, 6, COLOR_BLUE);           /* BL */
    lcd_fill_rect((int16_t)(w - 7), (int16_t)(h - 7), 6, 6, COLOR_YELLOW); /* BR */
    int16_t y = 14;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "MADCTL 0x%02X", (unsigned)lcd_cfg.madctl); y += 10;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "XOFF %u YOFF %u", (unsigned)lcd_cfg.xoff,
              (unsigned)lcd_cfg.yoff); y += 10;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "%ux%u BGR %u INV %u", (unsigned)w, (unsigned)h,
              (unsigned)lcd_cfg.bgr, (unsigned)lcd_cfg.invert); y += 10;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "SPI1 /%u = %" PRIu32 " kHz", (unsigned)lcd_cfg.spi_div,
              lcd_spi_hz() / 1000u); y += 10;
    gfx_textf(12, y, COLOR_WHITE, COLOR_BLACK, 1, "gamma %u", (unsigned)lcd_cfg.gamma_id); y += 14;
    gfx_text(12, y, "TL red  TR green", COLOR_GRAY, COLOR_BLACK, 1); y += 10;
    gfx_text(12, y, "BL blue BR yellow", COLOR_GRAY, COLOR_BLACK, 1); y += 14;
    gfx_text(12, y, "B: back", COLOR_GRAY, COLOR_BLACK, 1);
}

/* ---------------------------------------------------------------- USB / info */

static void draw_usb_info(bool full)
{
    char uid[25], ser[13];
    if (full) {
        gfx_fill(COLOR_BLACK);
    }
    ui_status_bar();
    board_uid_hex(uid);
    board_usb_serial(ser);
    int16_t y = 13, wpx = (int16_t)(lcd_width() - 4);
    char b[40];
    gfx_text_pad(2, y, "UID", COLOR_GRAY, COLOR_BLACK, 1, wpx); y += 9;
    gfx_text_pad(2, y, uid, COLOR_WHITE, COLOR_BLACK, 1, wpx); y += 10;
    snprintf(b, sizeof(b), "SN  %s", ser);
    gfx_text_pad(2, y, b, COLOR_WHITE, COLOR_BLACK, 1, wpx); y += 10;
    snprintf(b, sizeof(b), "FW  %s %s", g_fw_hash, g_fw_opt);
    gfx_text_pad(2, y, b, COLOR_WHITE, COLOR_BLACK, 1, wpx); y += 10;
    snprintf(b, sizeof(b), "CLK %" PRIu32 " MHz %s", SystemCoreClock / 1000000u,
             clock_status_str(clock_last_status()));
    gfx_text_pad(2, y, b, COLOR_WHITE, COLOR_BLACK, 1, wpx); y += 10;
    snprintf(b, sizeof(b), "COM %s %s %" PRIu32, usb_configured() ? "cfg" : "---",
             usb_dtr() ? "open" : "closed", usb_baud());
    gfx_text_pad(2, y, b, usb_dtr() ? COLOR_GREEN : COLOR_YELLOW, COLOR_BLACK, 1, wpx); y += 10;
    snprintf(b, sizeof(b), "drops %" PRIu32 " rx %" PRIu32, log_drops(), usb_rx_drops());
    gfx_text_pad(2, y, b, COLOR_WHITE, COLOR_BLACK, 1, wpx); y += 10;
    snprintf(b, sizeof(b), "HWCAL %s seq %" PRIu32, hwcal_state_str(hwcal_state()), hwcal_seq());
    gfx_text_pad(2, y, b, COLOR_WHITE, COLOR_BLACK, 1, wpx); y += 10;
    snprintf(b, sizeof(b), "VBUS %u CHG %u reset %s", power_vbus() ? 1u : 0u,
             power_charging() ? 1u : 0u, cmd_reset_cause());
    gfx_text_pad(2, y, b, COLOR_WHITE, COLOR_BLACK, 1, wpx); y += 10;
    snprintf(b, sizeof(b), "up %" PRIu32 " s   B: back", HAL_GetTick() / 1000u);
    gfx_text_pad(2, y, b, COLOR_GRAY, COLOR_BLACK, 1, wpx);
}

/* ---------------------------------------------------------------- actions */

static void run_selftest(void)
{
    static char name[] = "selftest";
    char *argv[1] = {name};
    const suite_t *s = suite_find("selftest");
    if (s) {
        suite_run(s, 1, argv);
    }
    gfx_text(2, (int16_t)(lcd_height() - 9), "A/B: back", COLOR_GRAY, COLOR_BLACK, 1);
    s_scr = SCR_RESULT;
    s_dirty = false; /* keep the console visible */
}

/* ---------------------------------------------------------------- tests list */

static void draw_test_row(int i)
{
    int row = i - s_ttop;
    if (row < 0 || row >= T_ROWS || i >= g_suite_count) {
        return;
    }
    bool sel = (i == s_tsel);
    uint16_t fg = sel ? COLOR_BLACK : ((g_suites[i].flags & SUITE_F_USER) ? COLOR_YELLOW : COLOR_WHITE);
    uint16_t bg = sel ? COLOR_CYAN : COLOR_BLACK;
    int16_t y = (int16_t)(T_Y0 + row * T_DY);
    lcd_fill_rect(0, (int16_t)(y - 1), (int16_t)lcd_width(), T_DY, bg);
    char b[28];
    snprintf(b, sizeof(b), "%s%s", g_suites[i].name, (g_suites[i].flags & SUITE_F_USER) ? " [U]" : "");
    gfx_text(4, (int16_t)(y + 1), b, fg, bg, 1);
}

static void draw_tests(void)
{
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    for (int i = s_ttop; i < s_ttop + T_ROWS && i < g_suite_count; i++) {
        draw_test_row(i);
    }
    gfx_text(2, (int16_t)(lcd_height() - 9), "A run  B back  [U]=human", COLOR_GRAY, COLOR_BLACK, 1);
}

static void tests_move(int d)
{
    int old = s_tsel;
    s_tsel = (s_tsel + d + g_suite_count) % g_suite_count;
    if (s_tsel < s_ttop || s_tsel >= s_ttop + T_ROWS) {
        s_ttop = (s_tsel < s_ttop) ? s_tsel : s_tsel - T_ROWS + 1;
        draw_tests();
        return;
    }
    draw_test_row(old);
    draw_test_row(s_tsel);
}

static void run_suite_index(int i)
{
    char *argv[1] = {(char *)g_suites[i].name};
    input_flush();
    suite_run(&g_suites[i], 1, argv);
    gfx_text(2, (int16_t)(lcd_height() - 9), "A/B: back", COLOR_GRAY, COLOR_BLACK, 1);
    s_scr = SCR_RESULT;
    s_dirty = false; /* keep the console visible */
}

static void select_item(int i)
{
    switch (i) {
    case 0:
        run_selftest();
        return;
    case 1: s_scr = SCR_TESTS; break;
    case 2: s_scr = SCR_BUTTONS; s_b_hold_start = 0; break;
    case 3: s_scr = SCR_DISPLAY; break;
    case 4: s_scr = SCR_USBINFO; break;
    case 5:
        ui_con_begin("Enter DFU");
        ui_con_line(COLOR_YELLOW, "Rebooting to the ST");
        ui_con_line(COLOR_YELLOW, "system bootloader...");
        ui_con_line(COLOR_GRAY, "Do not press buttons.");
        log_printf("ui: entering DFU\r\n");
        log_flush(50);
        HAL_Delay(300);
        dfu_request();
    case 6:
        log_printf("ui: reboot\r\n");
        log_flush(50);
        NVIC_SystemReset();
    default: break;
    }
    ui_invalidate();
}

void ui_task(void)
{
    input_event_t ev;
    uint32_t now = HAL_GetTick();

    if (s_dirty) {
        s_dirty = false;
        switch (s_scr) {
        case SCR_MENU: draw_menu(); break;
        case SCR_BUTTONS: draw_buttons(true); break;
        case SCR_DISPLAY: draw_display_info(); break;
        case SCR_USBINFO: draw_usb_info(true); break;
        case SCR_TESTS: draw_tests(); break;
        case SCR_RESULT: break;
        }
        s_last_refresh = now;
    }

    while (input_pop(&ev)) {
        if (!ev.pressed) {
            continue;
        }
        switch (s_scr) {
        case SCR_MENU:
            if (ev.button == BTN_UP || ev.button == BTN_DOWN) {
                int old = s_sel;
                s_sel = (ev.button == BTN_UP) ? (s_sel + N_ITEMS - 1) % N_ITEMS : (s_sel + 1) % N_ITEMS;
                draw_menu_item(old);
                draw_menu_item(s_sel);
            } else if (ev.button == BTN_A || ev.button == BTN_CENTER) {
                select_item(s_sel);
                return;
            }
            break;
        case SCR_BUTTONS:
            break; /* all buttons are under test; exit = hold B */
        case SCR_TESTS:
            if (ev.button == BTN_UP || ev.button == BTN_DOWN) {
                tests_move(ev.button == BTN_UP ? -1 : 1);
            } else if (ev.button == BTN_A || ev.button == BTN_CENTER) {
                run_suite_index(s_tsel);
                return;
            } else if (ev.button == BTN_B) {
                s_scr = SCR_MENU;
                ui_invalidate();
                return;
            }
            break;
        default:
            if (ev.button == BTN_B || (s_scr == SCR_RESULT && ev.button == BTN_A)) {
                s_scr = SCR_MENU;
                ui_invalidate();
                return;
            }
            break;
        }
    }

    /* periodic refresh */
    if (s_scr == SCR_BUTTONS) {
        draw_buttons(false);
        if (input_held() & BTN_MASK(BTN_B)) {
            if (s_b_hold_start == 0u) {
                s_b_hold_start = now ? now : 1u;
            } else if ((uint32_t)(now - s_b_hold_start) >= 1000u) {
                s_scr = SCR_MENU;
                input_flush();
                ui_invalidate();
            }
        } else {
            s_b_hold_start = 0;
        }
    } else if ((uint32_t)(now - s_last_refresh) >= 500u) {
        s_last_refresh = now;
        if (s_scr == SCR_USBINFO) {
            draw_usb_info(false);
        } else if (s_scr == SCR_MENU && (usb_state() != s_usb_state || gauge_fw_sig() != s_gauge_sig)) {
            ui_status_bar();
        }
    }
}
