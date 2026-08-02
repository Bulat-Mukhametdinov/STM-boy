#include "settings.h"

#include "flash_layout.h"
#include "main.h"
#include "w25q128.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_MAGIC 0x31534652u
#define SETTINGS_VERSION 1u
#define SETTINGS_RECORD_SIZE 16u
#define SETTINGS_STORAGE_OFFSET FLASH_LAYOUT_STATE_OFFSET
#define SETTINGS_STORAGE_SIZE W25Q128_SECTOR_SIZE
#define SETTINGS_SAVE_DELAY_MS 750u
#define SETTINGS_SAVE_RETRY_MS 1000u

static uint8_t volume = SETTINGS_DEFAULT_VOLUME;
static uint8_t theme = SETTINGS_DEFAULT_THEME;
static bool storage_ready;
static bool dirty;
static bool compact_before_write;
static uint32_t next_record_offset;
static uint32_t changed_ms;
static uint32_t last_save_attempt_ms;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    while (size-- > 0u) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint32_t crc32_finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

static bool elapsed(uint32_t start_ms, uint32_t period_ms)
{
    return (HAL_GetTick() - start_ms) >= period_ms;
}

static bool is_erased(const uint8_t *record)
{
    for (uint8_t i = 0; i < SETTINGS_RECORD_SIZE; ++i) {
        if (record[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static bool valid_record(const uint8_t *record)
{
    uint32_t expected_crc;
    uint32_t actual_crc;

    if (rd32(&record[0]) != SETTINGS_MAGIC ||
        rd16(&record[4]) != SETTINGS_VERSION ||
        rd16(&record[6]) != SETTINGS_RECORD_SIZE ||
        record[8] > 100u) {
        return false;
    }

    expected_crc = rd32(&record[12]);
    actual_crc = crc32_finish(crc32_update(0xFFFFFFFFu, record, 12u));
    return expected_crc == actual_crc;
}

static void make_record(uint8_t *record)
{
    for (uint8_t i = 0; i < SETTINGS_RECORD_SIZE; ++i) {
        record[i] = 0xFFu;
    }

    wr32(&record[0], SETTINGS_MAGIC);
    wr16(&record[4], SETTINGS_VERSION);
    wr16(&record[6], SETTINGS_RECORD_SIZE);
    record[8] = volume;
    record[9] = theme;
    wr32(&record[12], crc32_finish(crc32_update(0xFFFFFFFFu, record, 12u)));
}

bool Settings_Init(void)
{
    bool found = false;
    uint8_t record[SETTINGS_RECORD_SIZE];

    volume = SETTINGS_DEFAULT_VOLUME;
    theme = SETTINGS_DEFAULT_THEME;
    storage_ready = false;
    dirty = false;
    compact_before_write = false;
    next_record_offset = 0u;
    changed_ms = 0u;
    last_save_attempt_ms = 0u;

    if (!W25Q128_Init()) {
        return false;
    }

    storage_ready = true;

    for (uint32_t offset = 0u; offset < SETTINGS_STORAGE_SIZE; offset += SETTINGS_RECORD_SIZE) {
        if (!W25Q128_Read(SETTINGS_STORAGE_OFFSET + offset, record, sizeof(record))) {
            storage_ready = false;
            return false;
        }

        if (is_erased(record)) {
            next_record_offset = offset;
            return found;
        }

        if (!valid_record(record)) {
            compact_before_write = true;
            next_record_offset = offset;
            return found;
        }

        volume = record[8];
        /* Records written before themes existed leave byte 9 erased (0xFF). */
        theme = (record[9] == 0xFFu) ? SETTINGS_DEFAULT_THEME : record[9];
        found = true;
        next_record_offset = offset + SETTINGS_RECORD_SIZE;
    }

    compact_before_write = true;
    return found;
}

void Settings_Task(void)
{
    if (!dirty || !storage_ready || !elapsed(changed_ms, SETTINGS_SAVE_DELAY_MS)) {
        return;
    }
    if (last_save_attempt_ms != 0u && !elapsed(last_save_attempt_ms, SETTINGS_SAVE_RETRY_MS)) {
        return;
    }

    last_save_attempt_ms = HAL_GetTick();
    (void)Settings_Flush();
}

uint8_t Settings_GetVolume(void)
{
    return volume;
}

void Settings_SetVolume(uint8_t value)
{
    if (value > 100u) {
        value = 100u;
    }
    if (volume == value) {
        return;
    }

    volume = value;
    if (storage_ready) {
        dirty = true;
        changed_ms = HAL_GetTick();
    }
}

uint8_t Settings_GetTheme(void)
{
    return theme;
}

void Settings_SetTheme(uint8_t value)
{
    if (theme == value) {
        return;
    }

    theme = value;
    if (storage_ready) {
        dirty = true;
        changed_ms = HAL_GetTick();
    }
}

bool Settings_Flush(void)
{
    uint8_t record[SETTINGS_RECORD_SIZE];

    if (!dirty) {
        return true;
    }
    if (!storage_ready) {
        return false;
    }

    if (compact_before_write || next_record_offset + SETTINGS_RECORD_SIZE > SETTINGS_STORAGE_SIZE) {
        if (!W25Q128_EraseSector(SETTINGS_STORAGE_OFFSET)) {
            return false;
        }
        next_record_offset = 0u;
        compact_before_write = false;
    }

    make_record(record);
    if (!W25Q128_Write(SETTINGS_STORAGE_OFFSET + next_record_offset, record, sizeof(record))) {
        return false;
    }

    next_record_offset += SETTINGS_RECORD_SIZE;
    dirty = false;
    last_save_attempt_ms = 0u;
    return true;
}
