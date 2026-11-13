#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#include "w25q128.h"

/*
 * W25Q128 (16 MiB) layout:
 *   0x000000 - 0x0FFFFF  state/settings (1 MiB)
 *   0x100000 - 0xEFFFFF  ROM pack (14 MiB)
 *   0xF00000 - 0xFFFFFF  reserved for stm32-hwtest (1 MiB): scratch / battery log
 *                        0xF00000-0xFDFFFF, overclock journal 0xFE0000-0xFEFFFF,
 *                        HWCAL calibration record at 0xFFF000.
 *                        See software/stm32-hwtest/README.md ("External flash
 *                        layout") and docs/hardware-reference-v0.1.md.
 * Nothing in this firmware may write to the reserved region: the ROM-pack size
 * below bounds both the upload eraser and the pack reader.
 */
#define FLASH_LAYOUT_STATE_OFFSET 0u
#define FLASH_LAYOUT_STATE_SIZE (1024u * 1024u)
#define FLASH_LAYOUT_ROMPACK_OFFSET (FLASH_LAYOUT_STATE_OFFSET + FLASH_LAYOUT_STATE_SIZE)
#define FLASH_LAYOUT_HWTEST_SIZE (1024u * 1024u)
#define FLASH_LAYOUT_HWTEST_OFFSET (W25Q128_SIZE_BYTES - FLASH_LAYOUT_HWTEST_SIZE)
#define FLASH_LAYOUT_ROMPACK_SIZE (FLASH_LAYOUT_HWTEST_OFFSET - FLASH_LAYOUT_ROMPACK_OFFSET)

#endif
