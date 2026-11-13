#ifndef FW_ID_H
#define FW_ID_H

#include <stdint.h>

/* Build identity, compiled into Src/fw_id.c (rebuilt on every make). */
extern const char g_fw_hash[];   /* git short hash, "+dirty" if the tree was dirty */
extern const uint32_t g_fw_build; /* unix time of the build */
extern const char g_fw_opt[];    /* optimisation flag, e.g. "-O2" */

#endif
