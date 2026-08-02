/* Control surface shared between the render driver (render_main.cpp) and the
 * host stubs / display module. All firmware-facing behaviour the harness needs
 * to steer (input queue, button hold state, fake clock, settings values,
 * upload status, ROM-pack presence) is exposed here. */
#ifndef HOST_H
#define HOST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Display framebuffer (shared by LVGL flush and the ST7735 stub) ---- */
#define HOST_FB_W 160
#define HOST_FB_H 128

/* Raw 16-bit display words exactly as the firmware would push them to the
 * panel (RGB565, byte-swapped per LV_COLOR_16_SWAP=1). */
const uint16_t *host_fb(void);

/* ---- Fake millisecond clock (backs HAL_GetTick / LVGL tick) ---- */
extern uint32_t g_host_tick_ms;

/* ---- Input stub control ---- */
void host_input_reset(void);
void host_input_push(int button);             /* queue a one-shot action */
void host_input_set_down(int button, bool down); /* held-state for IsDown */
void host_input_clear_down(void);

/* ---- Settings stub control ---- */
void host_set_theme(int idx);
void host_set_volume(int v);

/* ---- Power stub control ---- */
void host_set_battery(int pct, bool charging);

/* ---- ROM-pack presence (W25Q128_Init result) ---- */
void host_set_rompack_present(bool present);

/* ---- USB upload status the screen reads back ---- */
void host_set_upload(int phase, uint32_t done, uint32_t total,
                     uint32_t jedec, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* HOST_H */
