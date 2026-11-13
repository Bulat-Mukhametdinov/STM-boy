/* stm32-hwtest — STM-boy v0.1 hardware test / calibration / benchmark firmware.
   P0: skeleton, DFU loop, CDC protocol, LCD + menu, input, HWCAL, CRC. */

#include "main.h"
#include "board.h"
#include "clock.h"
#include "cmd.h"
#include "fw_id.h"
#include "gfx.h"
#include "hwcal.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "noinit.h"
#include "power.h"
#include "timebase.h"
#include "ui.h"
#include "usb.h"
#include "w25q.h"

#include <inttypes.h>

/* Test arena (P3/P5: RAM march, memcpy/DMA benchmarks, frame buffers).
   NOLOAD: not zeroed at boot, contents are undefined. */
#define ARENA_SIZE (72u * 1024u)
uint8_t g_arena[ARENA_SIZE] __attribute__((section(".noinit_arena"), aligned(8)));

static void apply_hwcal_to_hw(void)
{
    if (hwcal_state() != HWCAL_VALID) {
        return; /* keep the compiled-in defaults */
    }
    lcd_cfg.madctl = g_hwcal.madctl_land;
    lcd_cfg.xoff = g_hwcal.xoff_land;
    lcd_cfg.yoff = g_hwcal.yoff_land;
    lcd_cfg.bgr = g_hwcal.bgr;
    lcd_cfg.invert = g_hwcal.invert;
    lcd_cfg.gamma_id = g_hwcal.gamma_id;
    if (g_hwcal.spi1_div_ok >= 2u) {
        lcd_cfg.spi_div = g_hwcal.spi1_div_ok;
    }
    if (g_hwcal.debounce_ms >= 1u && g_hwcal.debounce_ms <= 100u) {
        input_set_debounce_ms(g_hwcal.debounce_ms);
    }
}

int main(void)
{
    /* Interrupts are still masked from Reset_Handler (cpsid i). */
    HAL_Init();               /* SysTick 1 kHz @ HSI, NVIC grouping, ART */
    cmd_capture_reset_cause(); /* read + clear RCC->CSR */
    board_init();             /* amp off, OFF_ACK low, CS high */

    /* Re-enable interrupts as soon as SysTick exists: HAL timeouts and
       HAL_Delay depend on the tick. */
    __enable_irq();

    clock_init_96mhz();
    timebase_init();
    log_init();
    cmd_init();
    power_init();
    input_init();

    usb_init(); /* start enumeration early; LCD init below takes ~0.6 s */

    lcd_cfg_defaults(&lcd_cfg);
    w25q_init();
    hwcal_load();
    apply_hwcal_to_hw();

    lcd_init();
    gfx_fill(COLOR_BLACK);
    lcd_backlight(true);
    ui_init();

    /* Normal boot reached: allow one more bootloader-exit clean reset later. */
    g_noinit.clean_reset_count = 0;

    log_printf("stm32-hwtest %s (%s) clk=%" PRIu32 " reset=%s w25q=%s hwcal=%s\r\n", g_fw_hash,
               g_fw_opt, SystemCoreClock, cmd_reset_cause(), w25q_present() ? "ok" : "missing",
               hwcal_state_str(hwcal_state()));

    for (;;) {
        log_poll();
        power_poll();
        cmd_poll();
        ui_task();
    }
}

void Error_Handler(void)
{
    /* Unrecoverable init failure (e.g. USB core). Hang with IRQs off; recover
       with BOOT0 + RESET (system bootloader) or SWD. */
    __disable_irq();
    while (1) {
    }
}
