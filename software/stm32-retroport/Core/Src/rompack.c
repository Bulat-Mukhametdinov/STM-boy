#include "rompack.h"

#include "flash_layout.h"
#include "w25q128.h"

#include <string.h>

#define RPRP_MAGIC 0x50525052u
#define ROMPACK_VERSION 2u
#define ROMPACK_HEADER_SIZE 64u
#define ROMPACK_ENTRY_SIZE 72u

static RomPackEntry entries[ROMPACK_MAX_TOTAL];
static uint16_t entry_count;
static const char *status_text = "ROM pack not loaded";

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
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

bool RomPack_Init(void)
{
    uint8_t header[ROMPACK_HEADER_SIZE];
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t entry_size;
    uint16_t count;
    uint32_t data_offset;
    uint32_t pack_size;
    uint16_t loaded = 0;

    entry_count = 0;
    status_text = "ROM pack not loaded";

    if (!W25Q128_Init()) {
        status_text = "W25Q128 not found";
        return false;
    }
    if (!W25Q128_Read(FLASH_LAYOUT_ROMPACK_OFFSET, header, sizeof(header))) {
        status_text = "ROM pack read failed";
        return false;
    }

    magic = rd32(&header[0]);
    version = rd16(&header[4]);
    header_size = rd16(&header[6]);
    entry_size = rd16(&header[8]);
    count = rd16(&header[10]);
    data_offset = rd32(&header[12]);
    pack_size = rd32(&header[20]);

    if (magic != RPRP_MAGIC) {
        status_text = "ROM pack missing";
        return false;
    }
    if (version != ROMPACK_VERSION || header_size != ROMPACK_HEADER_SIZE || entry_size != ROMPACK_ENTRY_SIZE) {
        status_text = "ROM pack version error";
        return false;
    }
    if (count == 0u || data_offset < ROMPACK_HEADER_SIZE ||
        pack_size < data_offset || pack_size > FLASH_LAYOUT_ROMPACK_SIZE) {
        status_text = "ROM pack header error";
        return false;
    }

    if (count > ROMPACK_MAX_TOTAL) {
        count = ROMPACK_MAX_TOTAL;
        status_text = "ROM pack truncated";
    } else {
        status_text = "ROM pack ready";
    }

    for (uint16_t i = 0; i < count; ++i) {
        uint8_t raw[ROMPACK_ENTRY_SIZE];
        uint32_t entry_addr = FLASH_LAYOUT_ROMPACK_OFFSET + ROMPACK_HEADER_SIZE + (uint32_t)i * ROMPACK_ENTRY_SIZE;
        uint32_t offset;
        uint32_t size;
        uint8_t title_len;
        uint8_t system_id;

        if (!W25Q128_Read(entry_addr, raw, sizeof(raw))) {
            status_text = "ROM entry read failed";
            break;
        }

        offset = rd32(&raw[0]);
        size = rd32(&raw[4]);
        title_len = raw[16];
        system_id = raw[17];
        if (title_len >= ROMPACK_TITLE_SIZE) {
            title_len = ROMPACK_TITLE_SIZE - 1u;
        }

        if (system_id == ROMPACK_SYSTEM_NONE ||
            size == 0u ||
            offset < data_offset ||
            offset > pack_size ||
            size > pack_size - offset) {
            continue;
        }

        entries[loaded].offset = offset;
        entries[loaded].size = size;
        entries[loaded].crc = rd32(&raw[8]);
        entries[loaded].option_flags = rd16(&raw[12]);
        entries[loaded].tickrate = rd16(&raw[14]);
        entries[loaded].system_id = system_id;
        memcpy(entries[loaded].title, &raw[18], title_len);
        entries[loaded].title[title_len] = '\0';
        if (title_len == 0u) {
            strcpy(entries[loaded].title, "Untitled");
        }
        ++loaded;
    }

    entry_count = loaded;
    if (entry_count == 0u) {
        status_text = "ROM pack empty";
        return false;
    }

    return true;
}

uint16_t RomPack_Count(void)
{
    return entry_count;
}

uint16_t RomPack_CountBySystem(uint8_t system_id)
{
    uint16_t count = 0;

    for (uint16_t i = 0; i < entry_count; ++i) {
        if (entries[i].system_id == system_id) {
            ++count;
        }
    }

    return count;
}

bool RomPack_Get(uint16_t index, RomPackEntry *entry)
{
    if (entry == NULL || index >= entry_count) {
        return false;
    }
    *entry = entries[index];
    return true;
}

bool RomPack_GetBySystem(uint8_t system_id, uint16_t system_index, RomPackEntry *entry)
{
    uint16_t current = 0;

    for (uint16_t i = 0; i < entry_count; ++i) {
        if (entries[i].system_id != system_id) {
            continue;
        }
        if (current == system_index) {
            return RomPack_Get(i, entry);
        }
        ++current;
    }

    return false;
}

bool RomPack_Load(uint16_t index, uint8_t *buffer, size_t buffer_size, size_t *rom_size)
{
    uint32_t crc;

    if (index >= entry_count || buffer == NULL || buffer_size < entries[index].size) {
        return false;
    }
    if (!W25Q128_Read(FLASH_LAYOUT_ROMPACK_OFFSET + entries[index].offset, buffer, entries[index].size)) {
        return false;
    }

    crc = crc32_finish(crc32_update(0xFFFFFFFFu, buffer, entries[index].size));
    if (crc != entries[index].crc) {
        return false;
    }

    if (rom_size != NULL) {
        *rom_size = entries[index].size;
    }
    return true;
}

bool RomPack_LoadStream(uint16_t index,
                        uint8_t *scratch,
                        size_t scratch_size,
                        RomPackChunkFn chunk_fn,
                        void *ctx,
                        size_t *rom_size)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t offset = 0u;
    uint32_t size;

    if (index >= entry_count || scratch == NULL || scratch_size == 0u || chunk_fn == NULL) {
        return false;
    }

    size = entries[index].size;
    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > scratch_size) {
            chunk = (uint32_t)scratch_size;
        }

        if (!W25Q128_Read(FLASH_LAYOUT_ROMPACK_OFFSET + entries[index].offset + offset,
                          scratch,
                          chunk)) {
            return false;
        }

        crc = crc32_update(crc, scratch, chunk);
        if (!chunk_fn(scratch, chunk, offset, ctx)) {
            return false;
        }

        offset += chunk;
    }

    if (crc32_finish(crc) != entries[index].crc) {
        return false;
    }

    if (rom_size != NULL) {
        *rom_size = size;
    }
    return true;
}

const char *RomPack_Status(void)
{
    return status_text;
}
