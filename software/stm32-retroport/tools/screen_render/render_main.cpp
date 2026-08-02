/* Render driver.
 *
 * Boots the real RetroPort app (App_Init/App_Task) on the host LVGL backend,
 * drives the firmware's own input queue to navigate to one requested screen,
 * then dumps the shared framebuffer as an upscaled PPM (P6). One screen per
 * process invocation keeps app state clean and the navigation trivial to reason
 * about. A small Python helper converts the PPMs to PNG.
 */
#include "host.h"
#include "input.h"
#include "rom_upload.h"
#include "lvgl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" void App_Init(void);
extern "C" void App_Task(void);

namespace {

constexpr int kScale = 4;

void advance(uint32_t ms) { g_host_tick_ms += ms; }

/* Run the main loop n times, advancing the fake clock between iterations. */
void run(int n, uint32_t step_ms = 16)
{
    for (int i = 0; i < n; ++i) {
        advance(step_ms);
        App_Task();
    }
}

/* Queue one button action and let the app consume + act on it. */
void press(int button)
{
    host_input_push(button);
    run(2);
}

uint16_t unswap565(uint16_t stored)
{
    /* LV_COLOR_16_SWAP=1: panel bytes are byte-swapped vs native RGB565. */
    return (uint16_t)(((stored & 0xFFu) << 8) | (stored >> 8));
}

bool write_ppm(const char *path)
{
    const uint16_t *fb = host_fb();
    const int W = HOST_FB_W, H = HOST_FB_H;
    const int OW = W * kScale, OH = H * kScale;

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
    fprintf(f, "P6\n%d %d\n255\n", OW, OH);

    for (int y = 0; y < OH; ++y) {
        int sy = y / kScale;
        for (int x = 0; x < OW; ++x) {
            int sx = x / kScale;
            uint16_t v = unswap565(fb[sy * W + sx]);
            uint8_t r5 = (v >> 11) & 0x1F;
            uint8_t g6 = (v >> 5) & 0x3F;
            uint8_t b5 = v & 0x1F;
            uint8_t rgb[3] = {
                (uint8_t)((r5 * 255 + 15) / 31),
                (uint8_t)((g6 * 255 + 31) / 63),
                (uint8_t)((b5 * 255 + 15) / 31),
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    return true;
}

/* ---- per-screen navigation from a fresh boot (main menu, focus = Play) ---- */

/* Main-menu focus indices. */
enum { MAIN_PLAY = 0, MAIN_UPLOAD = 1, MAIN_SETTINGS = 2 };
/* Emulator-menu focus indices. */
enum { EMU_CHIP8 = 0, EMU_SCHIP = 1, EMU_ZX = 2 };

void go_emulator_menu()      { press(INPUT_BUTTON_A); }
void go_settings()           { press(INPUT_BUTTON_DOWN); press(INPUT_BUTTON_DOWN); press(INPUT_BUTTON_A); }
void go_upload()             { press(INPUT_BUTTON_DOWN); press(INPUT_BUTTON_A); }

/* Enter emulator menu then open the Nth emulator's ROM list. */
void open_rom_list(int emu)
{
    go_emulator_menu();
    for (int i = 0; i < emu; ++i) press(INPUT_BUTTON_DOWN);
    press(INPUT_BUTTON_A);
}

/* From a ROM list, step down `down` times then launch the selected ROM and let
 * the emulator run for ~roughly `frames` main-loop iterations to draw a frame. */
void launch_rom(int down, int frames)
{
    for (int i = 0; i < down; ++i) press(INPUT_BUTTON_DOWN);
    press(INPUT_BUTTON_A);
    run(frames, 16);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <screen> <out.ppm> [select_down]\n", argv[0]);
        return 1;
    }
    std::string screen = argv[1];
    const char *out = argv[2];
    int sel = (argc > 3) ? atoi(argv[3]) : 0;

    /* Theme selection for theme_* screens, read before App_Init applies it. */
    if (screen.rfind("theme_", 0) == 0) {
        host_set_theme(atoi(screen.c_str() + 6));
    }

    /* Upload status must be set before the screen reads it. */
    if (screen == "upload_progress") {
        host_set_upload(ROM_UPLOAD_PHASE_WRITING, 81920, 143360, 0xEF4018, "Writing ROM pack");
    } else {
        host_set_upload(ROM_UPLOAD_PHASE_WAITING, 0, 0, 0, "Waiting for upload");
    }

    if (screen == "rom_list_empty") {
        host_set_rompack_present(false);
    }

    App_Init();
    run(3); /* settle + first flush of the main menu */

    if (screen == "main_menu" || screen.rfind("theme_", 0) == 0) {
        /* already on the main menu */
    } else if (screen == "emulator_menu") {
        go_emulator_menu();
    } else if (screen == "settings") {
        go_settings();
    } else if (screen == "upload_idle" || screen == "upload_progress") {
        go_upload();
        run(6, 300); /* clear the upload UI throttle so labels/bar update */
    } else if (screen == "rom_list_chip8" || screen == "rom_list_empty") {
        open_rom_list(EMU_CHIP8);
    } else if (screen == "rom_list_schip") {
        open_rom_list(EMU_SCHIP);
    } else if (screen == "rom_list_zx") {
        open_rom_list(EMU_ZX);
    } else if (screen == "chip8_game") {
        open_rom_list(EMU_CHIP8);
        launch_rom(sel, 150);
    } else if (screen == "schip_game") {
        open_rom_list(EMU_SCHIP);
        launch_rom(sel, 150);
    } else if (screen == "zx48_game") {
        open_rom_list(EMU_ZX);
        launch_rom(sel, 150);
    } else {
        fprintf(stderr, "unknown screen '%s'\n", screen.c_str());
        return 1;
    }

    run(2); /* final settle / flush */

    if (!write_ppm(out)) return 1;
    printf("rendered %s -> %s\n", screen.c_str(), out);
    return 0;
}
