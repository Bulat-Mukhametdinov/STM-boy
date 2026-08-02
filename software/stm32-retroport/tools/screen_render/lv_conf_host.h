/* Host LVGL config wrapper.
 *
 * Uses the firmware's real lv_conf.h verbatim (so colours, fonts, draw format
 * and widget set are identical to the device) but swaps the memory backend.
 *
 * On the 32-bit MCU, lv_obj_t / spec_attr / style structs are packed with
 * 4-byte pointers and fit inside the firmware's 24 KiB builtin pool. On a 64-bit
 * PC every pointer doubles, so the same UI needs roughly twice the memory and
 * overflows that pool (NULL spec_attr -> crash) on the busier screens. Switching
 * to the C library allocator for the render harness sidesteps the pool size
 * entirely; it has no effect on the rendered pixels.
 */
#ifndef LV_CONF_HOST_H
#define LV_CONF_HOST_H

#include "../../Core/Inc/lv_conf.h"

#undef LV_USE_STDLIB_MALLOC
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

#undef LV_MEM_SIZE
#define LV_MEM_SIZE (4U * 1024U * 1024U)

#endif /* LV_CONF_HOST_H */
