#ifndef GAMEPIPE_H
#define GAMEPIPE_H

/* The demo's 8bpp + palette native-order DMA present pipeline (Src/ui/t_demo.c), for the stress / video suites.
   Arena use: FB0 0, FB1 20480, palette 40960 (512 B), PP0/PP1 41472 (2 x 2048), textures 45568 (16 KB)
   -> 61952 B; the suites may use g_arena[GP_ARENA_USED ..] (72 KB total). */

#include <stdbool.h>
#include <stdint.h>

#define GP_ARENA_USED 61952u
#define GP_IDX_BLACK 254
#define GP_IDX_WHITE 255

void gp_open(void);                 /* native order, SPI1 48 MHz 8-bit frames, DMA2 S3 hook + IRQ (prio 2), black screen */
void gp_close(void);                /* waits for the last present, restores the LCD settings */
uint8_t *gp_fb(int i);              /* i = 0/1: 160x128 column-major framebuffer fb[x * 128 + y] */
uint16_t *gp_pal(void);             /* 256 RGB565 entries */
void gp_start(const uint8_t *fb);   /* convert + stream fb in the background (previous present must be finished) */
void gp_start_pal(const uint8_t *fb, const uint16_t *pal);   /* same with a caller-owned palette (double-buffered video) */
uint32_t gp_wait(void);             /* WFI until the present is done; returns the cycles spent waiting */
void gp_text(uint8_t *fb, int x, int y, const char *s, uint8_t fg, uint8_t bg);
void gp_box(uint8_t *fb, int x, int y, int w, int h, uint8_t c);
void gp_game_enter(void);           /* raycaster palette, textures, camera; overlay colours 254/255 */
void gp_game_render(uint8_t *fb, uint32_t t_ms);   /* raycaster autopilot frame */

extern volatile uint32_t g_gp_dma_err;   /* DMA2 S3 TEIF/DMEIF flags seen by the present */
extern volatile uint32_t g_gp_dma_fe;    /* DMA2 S3 FEIF flags */

#endif
