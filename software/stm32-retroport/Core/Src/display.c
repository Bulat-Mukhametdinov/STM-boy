#include "display.h"

#include "display_config.h"
#include "lvgl.h"
#include "main.h"
#include "st7735.h"

static lv_display_t *display;
static uint16_t framebuffer[DISPLAY_HOR_RES * DISPLAY_BUFFER_LINES] __attribute__((aligned(4)));

static void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixel_map)
{
    uint16_t x = (uint16_t)area->x1;
    uint16_t y = (uint16_t)area->y1;
    uint16_t width = (uint16_t)(area->x2 - area->x1 + 1);
    uint16_t height = (uint16_t)(area->y2 - area->y1 + 1);
    size_t length = (size_t)width * height * sizeof(uint16_t);

    (void)ST7735_WritePixels(x, y, width, height, pixel_map, length);
    lv_display_flush_ready(disp);
}

void Display_Init(void)
{
    if (ST7735_Init() != HAL_OK) {
        Error_Handler();
    }

    lv_init();

    display = lv_display_create(DISPLAY_HOR_RES, DISPLAY_VER_RES);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, framebuffer, NULL, sizeof(framebuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void Display_Task(void)
{
    (void)lv_timer_handler();
}
