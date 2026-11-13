#include "power.h"
#include "board.h"
#include "gfx.h"
#include "lcd.h"
#include "log.h"
#include "stm32f4xx_hal.h"

#define OFF_REQ_DEBOUNCE_MS 30u

static bool s_armed;
static bool s_counting;
static uint32_t s_low_start;

void power_init(void)
{
    /* Pins are configured in board_init(): OFF_REQ input, OFF_ACK low. */
    s_armed = false;
    s_counting = false;
}

bool power_off_req(void) { return !gpio_read(OFF_REQ_PORT, OFF_REQ_PIN); }
bool power_vbus(void) { return gpio_read(VBUS_PORT, VBUS_PIN); }
bool power_charging(void) { return !gpio_read(CHG_PORT, CHG_PIN); }

static void shutdown(void)
{
    gpio_clr(AMP_SD_PORT, AMP_SD_PIN);
    log_printf("power: OFF_REQ confirmed, acknowledging shutdown\r\n");
    log_flush(20);

    lcd_fill_rect(0, 0, lcd_width(), lcd_height(), COLOR_BLACK);
    gfx_text(40, 56, "POWER OFF", COLOR_WHITE, COLOR_BLACK, 1);
    HAL_Delay(300);
    lcd_backlight(false);
    lcd_fill_rect(0, 0, lcd_width(), lcd_height(), COLOR_BLACK);

    gpio_set(OFF_ACK_PORT, OFF_ACK_PIN); /* ACK is active-high */

    /* The ATtiny now cuts power. Keep USB serviced meanwhile (IRQ driven). */
    for (;;) {
        log_poll();
    }
}

void power_poll(void)
{
    if (!power_off_req()) {
        s_armed = true;   /* seen idle-high: arm (prevents boot-time false trigger) */
        s_counting = false;
        return;
    }
    if (!s_armed) {
        return;
    }
    if (!s_counting) {
        s_counting = true;
        s_low_start = HAL_GetTick();
        return;
    }
    if ((uint32_t)(HAL_GetTick() - s_low_start) >= OFF_REQ_DEBOUNCE_MS) {
        s_armed = false;
        s_counting = false;
        shutdown();
    }
}
