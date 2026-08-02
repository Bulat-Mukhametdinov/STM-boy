/* Host display backend.
 *
 * Reimplements Display_Init/Display_Task and the ST7735 pixel-push API against
 * an in-memory framebuffer instead of a real SPI panel. Crucially, BOTH the
 * LVGL flush callback and ST7735_WritePixels() write into the SAME framebuffer,
 * exactly as they share the one physical display on hardware. That lets the
 * emulator game screens (which blit their framebuffer directly via
 * ST7735_WritePixels, bypassing LVGL) composite correctly over the LVGL chrome.
 */
#include "display.h"
#include "display_config.h"
#include "st7735.h"
#include "host.h"
#include "lvgl.h"

#include <string.h>

static uint16_t g_fb[HOST_FB_W * HOST_FB_H];

/* Partial render buffer mirroring the firmware (20 lines). */
static uint16_t g_render_buf[DISPLAY_HOR_RES * DISPLAY_BUFFER_LINES];
static lv_display_t *g_display;

const uint16_t *host_fb(void)
{
    return g_fb;
}

static uint32_t host_lv_tick(void)
{
    return g_host_tick_ms;
}

/* Copy a rectangular block of raw 16-bit display words into the framebuffer.
 * Used by both the LVGL flush and the ST7735 stub. */
static void blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *src)
{
    for (uint16_t row = 0; row < h; ++row) {
        uint16_t dy = (uint16_t)(y + row);
        if (dy >= HOST_FB_H) {
            break;
        }
        for (uint16_t col = 0; col < w; ++col) {
            uint16_t dx = (uint16_t)(x + col);
            if (dx >= HOST_FB_W) {
                continue;
            }
            const uint8_t *p = src + ((size_t)row * w + col) * 2u;
            /* Keep the bytes in panel order; the PNG exporter unswaps. */
            g_fb[(size_t)dy * HOST_FB_W + dx] = (uint16_t)(p[0] | (p[1] << 8));
        }
    }
}

static void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixel_map)
{
    uint16_t x = (uint16_t)area->x1;
    uint16_t y = (uint16_t)area->y1;
    uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);
    blit(x, y, w, h, pixel_map);
    lv_display_flush_ready(disp);
}

void Display_Init(void)
{
    (void)ST7735_Init();

    lv_init();
    lv_tick_set_cb(host_lv_tick);

    g_display = lv_display_create(DISPLAY_HOR_RES, DISPLAY_VER_RES);
    lv_display_set_flush_cb(g_display, display_flush);
    lv_display_set_buffers(g_display, g_render_buf, NULL, sizeof(g_render_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void Display_Task(void)
{
    (void)lv_timer_handler();
}

/* ---- ST7735 driver stub (writes into the shared framebuffer) ---- */

HAL_StatusTypeDef ST7735_Init(void)
{
    return HAL_OK;
}

void ST7735_SetBacklight(uint8_t enabled)
{
    (void)enabled;
}

HAL_StatusTypeDef ST7735_WritePixels(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                     const uint8_t *pixels, size_t length)
{
    (void)length;
    blit(x, y, width, height, pixels);
    return HAL_OK;
}

HAL_StatusTypeDef ST7735_FillScreen(uint16_t color)
{
    for (size_t i = 0; i < (size_t)HOST_FB_W * HOST_FB_H; ++i) {
        g_fb[i] = color;
    }
    return HAL_OK;
}

HAL_StatusTypeDef ST7735_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    for (uint16_t row = 0; row < height; ++row) {
        uint16_t dy = (uint16_t)(y + row);
        if (dy >= HOST_FB_H) break;
        for (uint16_t col = 0; col < width; ++col) {
            uint16_t dx = (uint16_t)(x + col);
            if (dx >= HOST_FB_W) continue;
            g_fb[(size_t)dy * HOST_FB_W + dx] = color;
        }
    }
    return HAL_OK;
}

SPI_HandleTypeDef *ST7735_GetSpiHandle(void)
{
    return NULL;
}
