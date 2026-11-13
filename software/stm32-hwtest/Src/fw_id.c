#include "fw_id.h"

#ifndef FW_GIT_HASH
#define FW_GIT_HASH "unknown"
#endif
#ifndef FW_BUILD_TIME
#define FW_BUILD_TIME 0UL
#endif
#ifndef FW_OPT
#define FW_OPT "?"
#endif

const char g_fw_hash[] = FW_GIT_HASH;
const uint32_t g_fw_build = FW_BUILD_TIME;
const char g_fw_opt[] = FW_OPT;
