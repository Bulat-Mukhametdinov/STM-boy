#ifndef RUNTIME_WORKSPACE_H
#define RUNTIME_WORKSPACE_H

#include "chip8.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIME_WORKSPACE_UPLOAD_RX_RING_SIZE 8192u
#define RUNTIME_WORKSPACE_UPLOAD_MAX_PAYLOAD 4096u
#define RUNTIME_WORKSPACE_UPLOAD_VERIFY_CHUNK 512u

#define RUNTIME_WORKSPACE_CHIP8_ROM_SIZE (CHIP8_MEMORY_SIZE - CHIP8_ROM_START)
#define RUNTIME_WORKSPACE_CHIP8_LINE_BYTES (CHIP8_DISPLAY_WIDTH * 2u * 2u * 2u)

#define RUNTIME_WORKSPACE_ZX48_RAM_SIZE 49152u
#define RUNTIME_WORKSPACE_ZX48_SNA_SIZE 49179u
#define RUNTIME_WORKSPACE_ZX48_SNA_HEADER_SIZE 27u
#define RUNTIME_WORKSPACE_ZX48_SCRATCH_SIZE 512u
#define RUNTIME_WORKSPACE_ZX48_STRIP_LINES 8u
#define RUNTIME_WORKSPACE_ZX48_LINE_BYTES (160u * 2u)
#define RUNTIME_WORKSPACE_ZX48_STRIP_BYTES \
    (RUNTIME_WORKSPACE_ZX48_LINE_BYTES * RUNTIME_WORKSPACE_ZX48_STRIP_LINES)

typedef struct {
    volatile uint8_t rx_ring[RUNTIME_WORKSPACE_UPLOAD_RX_RING_SIZE];
    uint8_t payload[RUNTIME_WORKSPACE_UPLOAD_MAX_PAYLOAD];
    uint8_t verify_buf[RUNTIME_WORKSPACE_UPLOAD_VERIFY_CHUNK];
} RuntimeWorkspaceUpload;

typedef struct {
    Chip8 chip8;
    uint8_t rom_buffer[RUNTIME_WORKSPACE_CHIP8_ROM_SIZE];
    uint8_t previous_display[CHIP8_DISPLAY_SIZE];
    uint8_t line[RUNTIME_WORKSPACE_CHIP8_LINE_BYTES];
} RuntimeWorkspaceChip8;

typedef struct {
    uint8_t ram[RUNTIME_WORKSPACE_ZX48_RAM_SIZE];
    uint8_t scratch[RUNTIME_WORKSPACE_ZX48_SCRATCH_SIZE];
    uint8_t line[RUNTIME_WORKSPACE_ZX48_STRIP_BYTES];
} RuntimeWorkspaceZx48;

typedef union {
    RuntimeWorkspaceUpload upload;
    RuntimeWorkspaceChip8 chip8;
    RuntimeWorkspaceZx48 zx48;
    uint32_t align;
} RuntimeWorkspace;

extern RuntimeWorkspace g_runtime_workspace;

#ifdef __cplusplus
}
#endif

#endif
