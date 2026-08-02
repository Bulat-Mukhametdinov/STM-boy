#ifndef W25Q128_H
#define W25Q128_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define W25Q128_SIZE_BYTES (16u * 1024u * 1024u)
#define W25Q128_SECTOR_SIZE 4096u
#define W25Q128_BLOCK64_SIZE (64u * 1024u)
#define W25Q128_PAGE_SIZE 256u

bool W25Q128_Init(void);
uint32_t W25Q128_ReadJedecId(void);
bool W25Q128_Read(uint32_t address, uint8_t *data, size_t size);
bool W25Q128_EraseSector(uint32_t address);
bool W25Q128_EraseBlock64(uint32_t address);
bool W25Q128_Write(uint32_t address, const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
