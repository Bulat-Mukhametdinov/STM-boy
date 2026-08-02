#ifndef ROMPACK_H
#define ROMPACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROMPACK_SYSTEM_NONE 0u
#define ROMPACK_SYSTEM_CHIP8 1u
#define ROMPACK_SYSTEM_ZX48 2u
#define ROMPACK_SYSTEM_SCHIP 3u

/* Maximum entries displayed for a single system (browser arrays). */
#define ROMPACK_MAX_ENTRIES 128u
/* Maximum entries stored across all systems in one ROM pack. */
#define ROMPACK_MAX_TOTAL 160u
#define ROMPACK_TITLE_SIZE 55u

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t crc;
    uint16_t option_flags;
    uint16_t tickrate;
    uint8_t system_id;
    char title[ROMPACK_TITLE_SIZE];
} RomPackEntry;

typedef bool (*RomPackChunkFn)(const uint8_t *data, size_t size, size_t offset, void *ctx);

bool RomPack_Init(void);
uint16_t RomPack_Count(void);
uint16_t RomPack_CountBySystem(uint8_t system_id);
bool RomPack_Get(uint16_t index, RomPackEntry *entry);
bool RomPack_GetBySystem(uint8_t system_id, uint16_t system_index, RomPackEntry *entry);
bool RomPack_Load(uint16_t index, uint8_t *buffer, size_t buffer_size, size_t *rom_size);
bool RomPack_LoadStream(uint16_t index,
                        uint8_t *scratch,
                        size_t scratch_size,
                        RomPackChunkFn chunk_fn,
                        void *ctx,
                        size_t *rom_size);
const char *RomPack_Status(void);

#ifdef __cplusplus
}
#endif

#endif
