/* Host stubs for the firmware's hardware modules.
 *
 * These replace the real input/audio/settings/power/rom_upload/w25q128 drivers
 * with deterministic, harness-steerable versions so the unmodified UI/app code
 * can run on a PC. The W25Q128 stub is backed by a real RPRP rom-pack image on
 * disk, so the *real* rompack.c parser runs unchanged on top of it. */
#include "host.h"

#include "input.h"
#include "audio.h"
#include "settings.h"
#include "power.h"
#include "rom_upload.h"
#include "w25q128.h"
#include "flash_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================== fake clock + misc HAL ===================== */
uint32_t g_host_tick_ms = 0;

uint32_t HAL_GetTick(void) { return g_host_tick_ms; }
void HAL_Delay(uint32_t ms) { g_host_tick_ms += ms; }
void Error_Handler(void) { fprintf(stderr, "Error_Handler() called\n"); exit(2); }

/* ===================== input ===================== */
#define HOST_INPUT_QUEUE 64
static int g_queue[HOST_INPUT_QUEUE];
static int g_q_head, g_q_tail;
static InputButtonMask g_down_mask;

void host_input_reset(void)
{
    g_q_head = g_q_tail = 0;
    g_down_mask = 0;
}

void host_input_push(int button)
{
    int next = (g_q_tail + 1) % HOST_INPUT_QUEUE;
    if (next == g_q_head) return; /* full, drop */
    g_queue[g_q_tail] = button;
    g_q_tail = next;
}

void host_input_set_down(int button, bool down)
{
    if (down) g_down_mask |= INPUT_BUTTON_MASK(button);
    else g_down_mask &= (InputButtonMask)~INPUT_BUTTON_MASK(button);
}

void host_input_clear_down(void) { g_down_mask = 0; }

void Input_Init(void) { host_input_reset(); }
void Input_Update(void) {}
void Input_Tick1ms(void) {}
bool Input_IsDown(InputButton button) { return (g_down_mask & INPUT_BUTTON_MASK(button)) != 0; }
bool Input_WasPressed(InputButton button) { (void)button; return false; }
bool Input_WasReleased(InputButton button) { (void)button; return false; }

bool Input_PopAction(InputButton *button)
{
    if (g_q_head == g_q_tail) return false;
    if (button) *button = (InputButton)g_queue[g_q_head];
    g_q_head = (g_q_head + 1) % HOST_INPUT_QUEUE;
    return true;
}

InputButtonMask Input_GetDownMask(void) { return g_down_mask; }
InputButtonMask Input_GetPressedMask(void) { return 0; }
InputButtonMask Input_GetReleasedMask(void) { return 0; }

/* ===================== audio (all no-ops) ===================== */
static uint8_t g_volume = 70;

void Audio_Init(void) {}
void Audio_SetVolume(uint8_t volume) { g_volume = volume; }
uint8_t Audio_GetVolume(void) { return g_volume; }
void Audio_PlaySound(AudioSoundId sound) { (void)sound; }
void Audio_PlayMusic(AudioMusicId music, bool loop) { (void)music; (void)loop; }
void Audio_StopMusic(void) {}
void Audio_SetStream(AudioStreamFn fn, void *ctx) { (void)fn; (void)ctx; }
void Audio_ClearStream(void) {}
void Audio_TIM5_IRQHandler(void) {}

/* ===================== settings ===================== */
static uint8_t g_set_volume = 70;
static uint8_t g_set_theme = 0;

void host_set_theme(int idx) { g_set_theme = (uint8_t)idx; }
void host_set_volume(int v) { g_set_volume = (uint8_t)v; }

bool Settings_Init(void) { return true; }
void Settings_Task(void) {}
uint8_t Settings_GetVolume(void) { return g_set_volume; }
void Settings_SetVolume(uint8_t volume) { g_set_volume = volume; }
uint8_t Settings_GetTheme(void) { return g_set_theme; }
void Settings_SetTheme(uint8_t theme) { g_set_theme = theme; }
bool Settings_Flush(void) { return true; }

/* ===================== power ===================== */
static uint8_t g_bat_pct = 82;
static bool g_bat_charging = false;

void host_set_battery(int pct, bool charging) { g_bat_pct = (uint8_t)pct; g_bat_charging = charging; }

void Power_Init(void) {}
void Power_Task(void) {}
uint8_t Power_GetBatteryPercent(void) { return g_bat_pct; }
uint16_t Power_GetBatteryMillivolts(void) { return 3700; }
bool Power_IsCharging(void) { return g_bat_charging; }
bool Power_IsUsbPresent(void) { return g_bat_charging; }
bool Power_IsOffRequested(void) { return false; }
void Power_SetOffAck(bool acknowledged) { (void)acknowledged; }

/* ===================== rom upload ===================== */
static RomUploadStatus g_upload = { ROM_UPLOAD_PHASE_WAITING, 0, 0, 0, "Waiting for upload" };
static char g_upload_msg[64] = "Waiting for upload";

void host_set_upload(int phase, uint32_t done, uint32_t total, uint32_t jedec, const char *message)
{
    g_upload.phase = (RomUploadPhase)phase;
    g_upload.done = done;
    g_upload.total = total;
    g_upload.jedec_id = jedec;
    snprintf(g_upload_msg, sizeof(g_upload_msg), "%s", message ? message : "");
    g_upload.message = g_upload_msg;
}

void RomUpload_Start(void) {}
void RomUpload_Stop(void) {}
void RomUpload_Task(void) {}
void RomUpload_GetStatus(RomUploadStatus *status) { if (status) *status = g_upload; }
void RomUpload_UsbReceive(const uint8_t *data, uint32_t size) { (void)data; (void)size; }
void RomUpload_UsbTransmitComplete(void) {}

/* ===================== W25Q128 (backed by a rom-pack image file) ===================== */
static uint8_t *g_pack;       /* contents that live at FLASH_LAYOUT_ROMPACK_OFFSET */
static size_t g_pack_size;
static bool g_pack_loaded;
static bool g_present = true;  /* host_set_rompack_present toggles W25Q128_Init */

void host_set_rompack_present(bool present) { g_present = present; }

static void ensure_pack_loaded(void)
{
    if (g_pack_loaded) return;
    g_pack_loaded = true;

    const char *path = getenv("ROMPACK_BIN");
    if (!path) path = "build/rompack.bin";

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[w25q128 stub] could not open rom-pack '%s'\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        g_pack = (uint8_t *)malloc((size_t)sz);
        if (g_pack && fread(g_pack, 1, (size_t)sz, f) == (size_t)sz) {
            g_pack_size = (size_t)sz;
        }
    }
    fclose(f);
}

bool W25Q128_Init(void)
{
    if (!g_present) return false;
    ensure_pack_loaded();
    return true;
}

uint32_t W25Q128_ReadJedecId(void) { return 0xEF4018u; /* W25Q128 */ }

bool W25Q128_Read(uint32_t address, uint8_t *data, size_t size)
{
    if (!data) return false;
    for (size_t i = 0; i < size; ++i) {
        uint32_t a = address + (uint32_t)i;
        uint8_t b = 0xFF;
        if (a >= FLASH_LAYOUT_ROMPACK_OFFSET) {
            size_t off = a - FLASH_LAYOUT_ROMPACK_OFFSET;
            if (g_pack && off < g_pack_size) b = g_pack[off];
        }
        data[i] = b;
    }
    return true;
}

bool W25Q128_EraseSector(uint32_t address) { (void)address; return true; }
bool W25Q128_EraseBlock64(uint32_t address) { (void)address; return true; }
bool W25Q128_Write(uint32_t address, const uint8_t *data, size_t size)
{
    (void)address; (void)data; (void)size; return true;
}
