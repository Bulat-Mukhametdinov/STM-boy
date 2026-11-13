/* W25Q read-only host commands (P1): read (binary @B dump), crc, scan.
   Nothing here erases or programs the external flash. */

#include "cmd.h"
#include "crc.h"
#include "log.h"
#include "power.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t g_arena[]; /* main.c, 72 KB scratch */

#define READ_MAX   (64u * 1024u)
#define CHUNK      4096u
#define MAX_RANGES 64

static bool parse_hex(const char *s, uint32_t *out)
{
    char *end;
    if (!s || !*s) return false;
    unsigned long v = strtoul(s, &end, 16);
    if (*end) return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_num(const char *s, uint32_t *out)
{
    char *end;
    if (!s || !*s) return false;
    unsigned long v = strtoul(s, &end, 0);
    if (*end) return false;
    *out = (uint32_t)v;
    return true;
}

static bool range_ok(uint32_t addr, uint32_t len)
{
    return len > 0u && addr < W25Q_SIZE && len <= W25Q_SIZE - addr;
}

/* w25q read <addr_hex> <len>  ->  "@B <len> <crc32>" + raw bytes, then @OK */
static void w25q_cmd_read(int argc, char **argv)
{
    uint32_t addr, len;
    if (argc < 4 || !parse_hex(argv[2], &addr) || !parse_num(argv[3], &len)) {
        proto_err("w25q", "usage: w25q read <addr_hex> <len<=65536>");
        return;
    }
    if (!range_ok(addr, len) || len > READ_MAX) {
        proto_err("w25q", "range");
        return;
    }
    if (w25q_read(addr, g_arena, len) != W25Q_OK) {
        proto_err("w25q", "read failed");
        return;
    }
    uint32_t crc = crc32_zlib(0, g_arena, len);
    char hdr[32];
    int n = snprintf(hdr, sizeof(hdr), "@B %" PRIu32 " %08" PRIX32 "\r\n", len, crc);
    if (!log_write_blocking(hdr, (size_t)n, 2000u) || !log_write_blocking(g_arena, len, 2000u)) {
        /* the frame is broken; the host sees a CRC/length error */
        log_flush(100);
        proto_err("w25q", "usb stalled");
        return;
    }
    proto_ok("w25q read");
}

/* Periodic housekeeping inside long loops: USB, OFF_REQ, restricted host
   commands (abort/info/ping). Returns true when an abort was requested. */
static bool service(void)
{
    sys_service();
    return abort_requested();
}

/* w25q crc <addr_hex> <len> -> @R {"t":"w25q_crc",...} (zlib CRC-32) */
static void w25q_cmd_crc(int argc, char **argv)
{
    uint32_t addr, len;
    if (argc < 4 || !parse_hex(argv[2], &addr) || !parse_num(argv[3], &len) || !range_ok(addr, len)) {
        proto_err("w25q", "usage: w25q crc <addr_hex> <len> (within 16 MB)");
        return;
    }
    busy_begin("w25q crc");
    uint32_t t0 = HAL_GetTick();
    uint32_t crc = 0, done = 0;
    bool aborted = false;
    while (done < len) {
        uint32_t k = (len - done < CHUNK) ? (len - done) : CHUNK;
        if (w25q_read(addr + done, g_arena, k) != W25Q_OK) {
            break;
        }
        crc = crc32_zlib(crc, g_arena, k);
        done += k;
        if ((done & 0xFFFFu) == 0u && service()) {
            aborted = true;
            break;
        }
    }
    busy_end();
    if (aborted) {
        proto_err("w25q", "aborted");
        return;
    }
    if (done != len) {
        proto_err("w25q", "read failed");
        return;
    }
    res_printf("{\"t\":\"w25q_crc\",\"addr\":\"%06" PRIX32 "\",\"len\":%" PRIu32 ",\"crc32\":\"%08" PRIX32
               "\",\"ms\":%" PRIu32 ",\"spi_hz\":%" PRIu32 "}",
               addr, len, crc, HAL_GetTick() - t0, w25q_spi_hz());
    proto_ok("w25q crc");
}

/* w25q scan <addr_hex> <len> [step] -> @R {"t":"w25q_scan",...}
   Every byte is read. A granule of `step` bytes (power of two, 256 B..1 MB,
   default 4 KB) is "used" if any byte != 0xFF. Consecutive used granules are
   merged into [start,end) ranges (hex strings). */
static void w25q_cmd_scan(int argc, char **argv)
{
    static char out[2560];
    static uint32_t rs[MAX_RANGES], re[MAX_RANGES];
    uint32_t addr, len, step = 4096u;
    if (argc < 4 || !parse_hex(argv[2], &addr) || !parse_num(argv[3], &len) || !range_ok(addr, len) ||
        (argc >= 5 && !parse_num(argv[4], &step))) {
        proto_err("w25q", "usage: w25q scan <addr_hex> <len> [step]");
        return;
    }
    if (step < 256u || step > 0x100000u || (step & (step - 1u)) || (addr % step) || (len % step)) {
        proto_err("w25q", "step must be a power of two 256..1M; addr/len multiples of step");
        return;
    }
    busy_begin("w25q scan");
    uint32_t t0 = HAL_GetTick();
    uint32_t chunk = step < CHUNK ? step : CHUNK;
    uint32_t granules = len / step, blank = 0, nr = 0, nonff = 0;
    uint32_t first = 0xFFFFFFFFu, last = 0xFFFFFFFFu;
    bool truncated = false, aborted = false, rd_err = false, prev_used = false;

    for (uint32_t g = 0; g < granules && !aborted && !rd_err; g++) {
        uint32_t gbase = addr + g * step;
        bool used = false;
        for (uint32_t off = 0; off < step; off += chunk) {
            if (w25q_read(gbase + off, g_arena, chunk) != W25Q_OK) {
                rd_err = true;
                break;
            }
            const uint32_t *w = (const uint32_t *)(void *)g_arena;
            for (uint32_t i = 0; i < chunk / 4u; i++) {
                if (w[i] != 0xFFFFFFFFu) {
                    for (uint32_t b = 0; b < 4u; b++) {
                        if (g_arena[i * 4u + b] != 0xFFu) {
                            uint32_t a = gbase + off + i * 4u + b;
                            nonff++;
                            if (first == 0xFFFFFFFFu) first = a;
                            last = a;
                        }
                    }
                    used = true;
                }
            }
            if (((gbase + off + chunk) & 0xFFFFu) == 0u && service()) {
                aborted = true;
                break;
            }
        }
        if (aborted || rd_err) break;
        if (!used) {
            blank++;
        } else if (prev_used && nr > 0u && re[nr - 1u] == gbase) {
            re[nr - 1u] = gbase + step;
        } else if (nr < MAX_RANGES) {
            rs[nr] = gbase;
            re[nr] = gbase + step;
            nr++;
        } else {
            truncated = true;
        }
        prev_used = used;
    }
    busy_end();
    if (aborted) {
        proto_err("w25q", "aborted");
        return;
    }
    if (rd_err) {
        proto_err("w25q", "read failed");
        return;
    }
    size_t n = 0;
    n += (size_t)snprintf(&out[n], sizeof(out) - n,
                          "@R {\"t\":\"w25q_scan\",\"addr\":\"%06" PRIX32 "\",\"len\":%" PRIu32
                          ",\"step\":%" PRIu32 ",\"granules\":%" PRIu32 ",\"blank\":%" PRIu32
                          ",\"used\":%" PRIu32 ",\"nonff_bytes\":%" PRIu32 ",",
                          addr, len, step, granules, blank, granules - blank, nonff);
    if (first == 0xFFFFFFFFu) {
        n += (size_t)snprintf(&out[n], sizeof(out) - n, "\"first_nonff\":null,\"last_nonff\":null,");
    } else {
        n += (size_t)snprintf(&out[n], sizeof(out) - n,
                              "\"first_nonff\":\"%06" PRIX32 "\",\"last_nonff\":\"%06" PRIX32 "\",", first, last);
    }
    n += (size_t)snprintf(&out[n], sizeof(out) - n, "\"ranges\":[");
    for (uint32_t i = 0; i < nr && n < sizeof(out) - 64u; i++) {
        n += (size_t)snprintf(&out[n], sizeof(out) - n, "%s[\"%06" PRIX32 "\",\"%06" PRIX32 "\"]",
                              i ? "," : "", rs[i], re[i]);
    }
    n += (size_t)snprintf(&out[n], sizeof(out) - n, "],\"truncated\":%s,\"ms\":%" PRIu32 "}\r\n",
                          truncated ? "true" : "false", HAL_GetTick() - t0);
    log_write(out, n);
    proto_ok("w25q scan");
}

/* Returns true if the sub-command was handled here. */
bool w25q_cmd_ext(int argc, char **argv)
{
    if (argc < 2) return false;
    if (strcmp(argv[1], "read") == 0) { w25q_cmd_read(argc, argv); return true; }
    if (strcmp(argv[1], "crc") == 0) { w25q_cmd_crc(argc, argv); return true; }
    if (strcmp(argv[1], "scan") == 0) { w25q_cmd_scan(argc, argv); return true; }
    return false;
}
