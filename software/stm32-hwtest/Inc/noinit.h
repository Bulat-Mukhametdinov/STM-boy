#ifndef NOINIT_H
#define NOINIT_H

/* Reset-surviving control block, linked into RAM_NOINIT (0x2001FF00, 256 B,
   top of SRAM; see STM32F411CEUX_HWTEST.ld). Never zeroed by the startup code.
   On a cold power-up its content is random, so every field is only trusted
   when `valid == NOINIT_VALID_MAGIC` (early_boot_check() initialises the block
   otherwise). */

#include <stdint.h>

#define NOINIT_VALID_MAGIC   0xA5A5C3C3u
#define NOINIT_DFU_MAGIC     0xDF11B007u

/* boot_flags (written by early_boot_check, consumed by main) */
#define NOINIT_BOOT_COLD          (1u << 0)  /* block was invalid -> initialised */
#define NOINIT_BOOT_CLEAN_RESET   (1u << 1)  /* this boot follows our own clean reset after bootloader exit */

typedef struct {
    uint32_t valid;              /* NOINIT_VALID_MAGIC */
    uint32_t dfu_magic;          /* NOINIT_DFU_MAGIC => jump to system bootloader */
    uint32_t clean_reset_count;  /* bootloader-exit clean resets in a row (max 1) */
    uint32_t boot_flags;         /* NOINIT_BOOT_* for the current boot */
    uint32_t boot_count;         /* incremented on every early_boot_check() */
    /* Fault breadcrumbs (HardFault/MemManage/BusFault/UsageFault) */
    uint32_t fault_count;
    uint32_t fault_pc;
    uint32_t fault_lr;
    uint32_t fault_cfsr;
    uint32_t fault_hfsr;
    uint32_t fault_addr;         /* MMFAR or BFAR if valid, else 0 */
    uint32_t fault_ipsr;
    uint32_t boot_first_pixel_ms; /* HAL tick (ms since HAL_Init) when the first frame + backlight were up */
    union {
        uint32_t reserved[51];       /* t_power.c uses [44..47]; indices must not move */
        struct {
            uint32_t pad[38];
            /* Overclock attempt copy (oc.c): oc_state 1 = ATTEMPT written, RESULT pending */
            uint32_t oc_state;
            uint32_t oc_seq;
            uint32_t oc_mhz;
            uint32_t oc_cfg;         /* ws | apb1_div<<8 | apb2_div<<16 | step<<24 */
            uint32_t oc_faults;      /* fault_count when the attempt started */
            uint32_t oc_ctr;         /* last record sequence number */
        };
    };
} noinit_t;

_Static_assert(sizeof(noinit_t) <= 256u, "noinit block must fit RAM_NOINIT");

extern volatile noinit_t g_noinit;

#endif
