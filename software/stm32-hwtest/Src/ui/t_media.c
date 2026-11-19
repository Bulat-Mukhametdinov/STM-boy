/* Media player: `media write|info` commands and the media.play suite.

   Container (PROTOCOL.md "Media container"), W25Q 0x200000-0xEFFFFF (the only region writable besides the scratch area
   at 0xF00000+, and only through w25q_media_*):
     0x200000  header sector (64 B header, rest 0xFF): magic 'SBMV', version, w, h, fps, frames, audio_rate, audio_bits,
               frame_stride, audio_bytes_per_frame, data_offset (0x1000), total_len, data_crc32, hdr_crc32
     +0x1000   frames: 512 B RGB565 palette (254 colours + 254 black/255 white for the overlay), 20480 B 8bpp pixels in
               native column-major order (fb[x * 128 + y]), audio_bytes_per_frame u8 mono PCM

   media.play: the demo's present pipeline (DMA2 S3 / 48 MHz SPI1, per-frame palette) while the NEXT frame is fetched by
   W25Q DMA (24 MHz SPI3). Audio = engine B (PCM stream voice, 31.25 kHz, 9 bit carrier) fed from a ring one frame
   ahead; the video is paced by the audio clock (DMA position), so A/V stays in sync; frames that are late by more
   than two periods are skipped. Output peak is limited to -9 dBFS (clip guard + normalised data).
   Arena (on top of gamepipe.h): palette 1 at 45568 (512), audio DMA buffer 46080 (2048), PCM ring 49152 (4096),
   audio temp 53248 (2048). `media write`: capture ring 0 (8192), page 8192 (256), verify buffer 12288 (4096). */

#include "suites.h"
#include "audio.h"
#include "bench.h"
#include "clock.h"
#include "cmd.h"
#include "gamepipe.h"
#include "gfx.h"
#include "input.h"
#include "log.h"
#include "timebase.h"
#include "ui.h"
#include "usb.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEDIA_ADDR 0x200000u
#define MEDIA_MAGIC 0x564D4253u   /* "SBMV" */
#define PAL_BYTES 512u
#define PIX_BYTES 20480u

typedef struct {
    uint32_t magic, version, w, h, fps, frames, audio_rate, audio_bits;
    uint32_t frame_stride, apf, data_offset, total_len, data_crc, rsv0, rsv1, hdr_crc;
} mhdr_t;

static uint32_t crc_upd(uint32_t crc, const uint8_t *p, uint32_t n)
{
    static const uint32_t t[16] = {0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu, 0x76dc4190u, 0x6b6b51f4u,
                                   0x4db26158u, 0x5005713cu, 0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
                                   0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu};
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15u];
        crc = (crc >> 4) ^ t[crc & 15u];
    }
    return crc;   /* raw state: start 0xFFFFFFFF, final ^ 0xFFFFFFFF = zlib CRC-32 */
}

static bool hdr_read(mhdr_t *h, const char **why)
{
    if (!w25q_present()) { *why = "no W25Q"; return false; }
    if (w25q_read(MEDIA_ADDR, h, sizeof(*h)) != W25Q_OK) { *why = "read failed"; return false; }
    if (h->magic != MEDIA_MAGIC) { *why = "no media (bad magic)"; return false; }
    if (h->hdr_crc != (crc_upd(0xFFFFFFFFu, (const uint8_t *)h, 60u) ^ 0xFFFFFFFFu)) { *why = "header crc"; return false; }
    if (h->version != 1u || h->w != 160u || h->h != 128u || h->audio_bits != 8u || h->fps == 0u || h->fps > 60u ||
        h->frames == 0u || h->apf == 0u || h->apf > 2048u || h->data_offset != 0x1000u ||
        h->frame_stride != PAL_BYTES + PIX_BYTES + h->apf ||
        h->data_offset + h->frames * h->frame_stride != h->total_len ||
        MEDIA_ADDR + h->total_len > W25Q_MEDIA_END) {
        *why = "unsupported header";
        return false;
    }
    *why = "ok";
    return true;
}

/* ---------------------------------------------------------------- commands */

static void media_info(void)
{
    mhdr_t h;
    const char *why;
    bool ok = hdr_read(&h, &why);
    if (!ok && strcmp(why, "no W25Q") == 0) {
        proto_err("media", why);
        return;
    }
    res_printf("{\"t\":\"media_info\",\"valid\":%s,\"why\":\"%s\",\"magic\":\"%08" PRIX32 "\",\"version\":%" PRIu32
               ",\"w\":%" PRIu32 ",\"h\":%" PRIu32 ",\"fps\":%" PRIu32 ",\"frames\":%" PRIu32 ",\"audio_rate\":%" PRIu32
               ",\"audio_bits\":%" PRIu32 ",\"frame_stride\":%" PRIu32 ",\"audio_bytes_per_frame\":%" PRIu32
               ",\"data_offset\":%" PRIu32 ",\"total_len\":%" PRIu32 ",\"crc\":%" PRIu32 ",\"duration_ms\":%" PRIu32 "}",
               ok ? "true" : "false", why, h.magic, h.version, h.w, h.h, h.fps, h.frames, h.audio_rate, h.audio_bits,
               h.frame_stride, h.apf, h.data_offset, h.total_len, h.data_crc,
               h.fps ? (uint32_t)((uint64_t)h.frames * 1000u / h.fps) : 0u);
    proto_ok("media info");
}

static void media_write(int argc, char **argv)
{
    uint32_t addr = argc >= 4 ? (uint32_t)strtoul(argv[2], NULL, 16) : 0u;
    uint32_t len = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 0) : 0u;
    if (argc < 4 || len == 0u || (addr & 0xFFFFu) != 0u || addr < W25Q_MEDIA_BASE || addr >= W25Q_MEDIA_END ||
        len > W25Q_MEDIA_END - addr) {
        proto_err("media", "usage: media write <addr_hex 64K-aligned in 200000..EFFFFF> <len>");
        return;
    }
    if (!w25q_present()) {
        proto_err("media", "no W25Q");
        return;
    }
    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    const uint32_t hz0 = w25q_spi_hz();
    uint8_t *ring = g_arena, *pg = g_arena + 8192u, *vb = g_arena + 12288u;
    busy_begin("media");
    w25q_set_div((uint16_t)(pclk1 / 24000000u));
    usb_cap_begin(ring, 8192u);
    proto_ok("media ready");
    log_flush(200);
    uint32_t pos = 0, fill = 0, crc = 0xFFFFFFFFu, n_erase = 0, us_erase = 0, us_prog = 0;
    uint32_t t0 = time_us(), t_idle = HAL_GetTick();
    const char *err = NULL;
    while (pos + fill < len) {
        uint32_t want = len - (pos + fill);
        if (want > 256u - fill) want = 256u - fill;
        uint32_t n = usb_cap_pop(pg + fill, want);
        if (n) {
            crc = crc_upd(crc, pg + fill, n);
            fill += n;
            t_idle = HAL_GetTick();
        }
        if (fill == 256u || (pos + fill == len && fill)) {
            uint32_t a = addr + pos;
            uint32_t t1 = time_us();
            if ((a & 0xFFFFu) == 0u) {
                if (w25q_media_erase_block(a) != W25Q_OK) { err = "erase failed"; break; }
                n_erase++;
                us_erase += time_us() - t1;
                t1 = time_us();
            }
            if (w25q_media_program(a, pg, fill) != W25Q_OK) { err = "program failed"; break; }
            us_prog += time_us() - t1;
            pos += fill;
            fill = 0;
            t_idle = HAL_GetTick();
        } else if (!n) {
            log_poll();
            if (HAL_GetTick() - t_idle > 8000u) { err = "rx timeout"; break; }
        }
    }
    uint32_t t_rx = time_us() - t0;
    usb_cap_end();
    uint32_t rx_crc = crc ^ 0xFFFFFFFFu;
    uint32_t vcrc = 0xFFFFFFFFu, t_v = time_us();
    bool verr = false;
    if (!err) {
        for (uint32_t o = 0; o < len; o += 4096u) {
            uint32_t n = (len - o < 4096u) ? len - o : 4096u;
            if (w25q_read_dma(addr + o, vb, n, true) != W25Q_OK) { verr = true; break; }
            vcrc = crc_upd(vcrc, vb, n);
            if ((o & 0xFFFFFu) == 0u) log_poll();
        }
    }
    uint32_t t_vf = time_us() - t_v;
    w25q_set_div((uint16_t)(pclk1 / hz0));
    busy_end();
    vcrc ^= 0xFFFFFFFFu;
    bool ok = !err && !verr && vcrc == rx_crc;
    res_printf("{\"t\":\"media_write\",\"addr\":\"%06" PRIX32 "\",\"len\":%" PRIu32 ",\"got\":%" PRIu32 ",\"crc\":%" PRIu32
               ",\"rx_crc\":%" PRIu32 ",\"ok\":%s,\"err\":\"%s\",\"blocks_erased\":%" PRIu32 ",\"rx_ms\":%" PRIu32
               ",\"erase_ms\":%" PRIu32 ",\"program_ms\":%" PRIu32 ",\"verify_ms\":%" PRIu32 ",\"kBps\":%" PRIu32 "}",
               addr, len, pos + fill, vcrc, rx_crc, ok ? "true" : "false", err ? err : (verr ? "verify read" : ""),
               n_erase, t_rx / 1000u, us_erase / 1000u, us_prog / 1000u, t_vf / 1000u,
               t_rx ? (uint32_t)((uint64_t)(pos + fill) * 1000u / t_rx) : 0u);
    if (ok) proto_ok("media write");
    else proto_err("media", err ? err : "verify mismatch");
}

void media_cmd(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "write") == 0) media_write(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "info") == 0) media_info();
    else proto_err("media", "usage: media info | media write <addr_hex> <len> (then raw bytes after '@OK media ready')");
}

/* ---------------------------------------------------------------- media.play */

#define PAL1 ((uint16_t *)(void *)(g_arena + 45568u))
#define AUDBUF (g_arena + 46080u)
#define RING (g_arena + 49152u)
#define RING_SZ 4096u
#define ATMP (g_arena + 53248u)

static uint32_t s_rd_us, s_rd_bytes, s_rd_err;

static bool rd(uint32_t addr, void *buf, uint32_t n)
{
    uint32_t t0 = time_us();
    bool ok = w25q_dma_start(addr, buf, n, true);
    if (ok) {
        while (!w25q_dma_done() && (uint32_t)(time_us() - t0) < 200000u) {
        }
        ok = w25q_dma_done();
    }
    w25q_dma_finish();
    s_rd_us += time_us() - t0;
    s_rd_bytes += n;
    if (!ok) s_rd_err++;
    return ok;
}

static uint32_t frame_addr(const mhdr_t *h, uint32_t idx) { return MEDIA_ADDR + h->data_offset + idx * h->frame_stride; }

static void fetch_video(const mhdr_t *h, uint32_t idx, uint16_t *pal, uint8_t *fb)
{
    uint32_t a = frame_addr(h, idx);
    rd(a, pal, PAL_BYTES);
    rd(a + PAL_BYTES, fb, PIX_BYTES);
}

static void fetch_audio(const mhdr_t *h, uint32_t idx)
{
    rd(frame_addr(h, idx) + PAL_BYTES + PIX_BYTES, ATMP, h->apf);
    audio_pcm_put(ATMP, h->apf);
}

suite_verdict_t suite_media_play(int argc, char **argv)
{
    bool loop = suite_arg_u32(argc, argv, "loop", 1u) != 0u;
    uint32_t auto_s = suite_arg_u32(argc, argv, "auto", 0u);
    uint32_t lead_us = suite_arg_u32(argc, argv, "lead", 8000u);
    mhdr_t h;
    const char *why;
    if (!hdr_read(&h, &why)) {
        res_printf("{\"t\":\"error\",\"suite\":\"media.play\",\"why\":\"%s (upload with hwtest.py media upload)\"}", why);
        ui_con_line(COLOR_RED, "media: %s", why);
        return SUITE_FAIL;
    }
    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    const uint32_t hz0 = w25q_spi_hz();
    w25q_set_div((uint16_t)(pclk1 / 24000000u));
    s_rd_us = s_rd_bytes = s_rd_err = 0;
    uint16_t *pal[2] = {gp_pal(), PAL1};
    uint32_t period_us = 1000000u / h.fps;

    gp_open();
    audio_set_buffer(AUDBUF);
    audio_init();
    audio_set_max_db(-9.0f);        /* measured: the analog chain saturates at ~-8 dBFS, 0 dBFS heats the speaker */
    audio_set_master_db(0.0f);
    audio_stats_reset();
    int cur = 0;
    fetch_video(&h, 0, pal[0], gp_fb(0));
    float gain_db = (float)(int32_t)h.rsv0 / 100.0f;   /* header audio_gain_cdb; never louder than -9 dB (peak data is full scale) */
    if (gain_db > -9.0f) gain_db = -9.0f;
    audio_pcm_start(RING, RING_SZ, h.audio_rate, gain_db);
    fetch_audio(&h, 0);
    if (h.frames > 1u) fetch_audio(&h, 1);
    audio_pwm_start(2, false);      /* 187.5 kHz / 9 bit carrier -> 31.25 kHz engine rate with RCR 6 */
    audio_amp_on(0);                /* pop-free: PWM to mid with the amp off, 30 ms, then SD# */
    bool eng = audio_engine_start(AUD_ENG_B, 31250u, AUD_DITHER_TPDF);
    const uint32_t fs_mhz = audio_fs_mhz();

    uint32_t F = 0, presented = 0, skipped = 0, late = 0, win_n = 0, fps10 = 0;
    uint32_t win_t0 = HAL_GetTick(), t_begin = HAL_GetTick(), max_late_us = 0;
    bool overlay = true, done = false;
    suite_verdict_t v = SUITE_PASS;
    char l1[32] = "", l2[32] = "", l3[32] = "";
    input_flush();
    while (!done && eng) {
        uint64_t target = (uint64_t)F * 1000000u / h.fps;
        target = target > lead_us ? target - lead_us : 0u;
        uint64_t ta;
        for (;;) {
            ta = (uint64_t)audio_played() * 1000000000u / fs_mhz;
            if (ta >= target) break;
            sys_service();
            if (abort_requested()) { v = SUITE_ABORT; done = true; break; }
        }
        if (done) break;
        uint32_t behind = (uint32_t)(ta - target);
        if (behind > max_late_us) max_late_us = behind;
        gp_wait();
        if (behind > 2u * period_us) {
            skipped++;            /* too late: drop this frame, catch up */
        } else {
            if (behind > period_us / 2u) late++;
            gp_start_pal(gp_fb(cur), pal[cur]);
            presented++;
            win_n++;
        }
        /* next frame (video) + audio two frames ahead, while this one is being sent */
        uint32_t nf = F + 1u;
        if (nf >= h.frames && !loop) { done = true; break; }
        int nxt = cur ^ 1;
        fetch_video(&h, nf % h.frames, pal[nxt], gp_fb(nxt));
        uint32_t af = F + 2u;
        if (af < h.frames || loop) {
            while (audio_pcm_free() < h.apf) {
                sys_service();
                if (abort_requested()) { v = SUITE_ABORT; done = true; break; }
            }
            if (!done) fetch_audio(&h, af % h.frames);
        }
        if (overlay) {
            gp_box(gp_fb(nxt), 0, 0, 6 * 15 + 4, 3 * 8 + 3, GP_IDX_BLACK);
            gp_text(gp_fb(nxt), 2, 2, l1, GP_IDX_WHITE, GP_IDX_BLACK);
            gp_text(gp_fb(nxt), 2, 10, l2, GP_IDX_WHITE, GP_IDX_BLACK);
            gp_text(gp_fb(nxt), 2, 18, l3, GP_IDX_WHITE, GP_IDX_BLACK);
        }
        cur = nxt;
        F = nf;
        uint32_t now = HAL_GetTick();
        if (now - win_t0 >= 1000u) {
            fps10 = win_n * 10000u / (now - win_t0);
            win_n = 0;
            win_t0 = now;
            snprintf(l1, sizeof(l1), "%u.%u/%ufps", (unsigned)(fps10 / 10u), (unsigned)(fps10 % 10u), (unsigned)h.fps);
            snprintf(l2, sizeof(l2), "und %u skip %u", (unsigned)audio_pcm_under(), (unsigned)skipped);
            snprintf(l3, sizeof(l3), "late %u A/B exit", (unsigned)late);
        }
        input_event_t ev;
        while (!auto_s && input_pop(&ev)) {   /* auto mode: keys ignored */
            if (!ev.pressed) continue;
            if (ev.button == BTN_A) overlay = !overlay;
            else if (ev.button == BTN_B) done = true;
        }
        if (auto_s && now - t_begin >= auto_s * 1000u) done = true;
    }
    uint32_t el_ms = HAL_GetTick() - t_begin;
    gp_wait();
    /* quiet exit: fade, engine off, amp off at mid-scale */
    audio_set_master_db(-90.0f);
    for (uint32_t t = time_us(); (uint32_t)(time_us() - t) < 20000u;) sys_service();
    uint32_t under = audio_pcm_under();
    uint32_t uf = g_aud_stats.uf, alate = g_aud_stats.late, aerr = g_aud_stats.dma_err, clipped = g_aud_stats.clipped;
    audio_engine_stop();
    audio_amp_off(0);
    audio_shutdown();
    audio_set_master_db(-24.0f);
    audio_set_max_db(-6.0f);
    audio_set_buffer(NULL);
    uint32_t gp_err = g_gp_dma_err;
    gp_close();
    w25q_set_div((uint16_t)(pclk1 / hz0));

    char a[16], b[16], c[16];
    uint32_t fps100 = el_ms ? presented * 100000u / el_ms : 0u;
    uint32_t mbps100 = s_rd_us ? (uint32_t)((uint64_t)s_rd_bytes * 100u / s_rd_us) : 0u;
    res_printf("{\"t\":\"media_play\",\"suite\":\"media.play\",\"frames_presented\":%" PRIu32 ",\"skipped\":%" PRIu32
               ",\"late\":%" PRIu32 ",\"fps\":%s,\"fps_nominal\":%" PRIu32 ",\"ms\":%" PRIu32 ",\"audio_underruns\":%" PRIu32
               ",\"audio_dma_uf\":%" PRIu32 ",\"audio_late\":%" PRIu32 ",\"audio_dma_err\":%" PRIu32 ",\"audio_clipped\":%"
               PRIu32 ",\"w25q_MBps\":%s,\"w25q_MBps_read\":%s,\"w25q_err\":%" PRIu32 ",\"lcd_dma_err\":%" PRIu32
               ",\"max_late_ms\":%" PRIu32 ",\"audio_rate\":%" PRIu32 ",\"engine_hz\":%" PRIu32 "}",
               presented, skipped, late, fmt2(a, sizeof(a), fps100), h.fps, el_ms, under, uf, alate, aerr, clipped,
               fmt2(b, sizeof(b), el_ms ? (uint32_t)((uint64_t)s_rd_bytes * 100u / (el_ms * 1000u)) : 0u),
               fmt2(c, sizeof(c), mbps100), s_rd_err, gp_err, max_late_us / 1000u, h.audio_rate, fs_mhz / 1000u);
    ui_con_begin("media.play");
    ui_con_line(under || skipped || s_rd_err ? COLOR_YELLOW : COLOR_GREEN, "%s fps und %u skip %u",
                fmt2(a, sizeof(a), fps100), (unsigned)under, (unsigned)skipped);
    if (v == SUITE_ABORT) return v;
    if (!eng) return SUITE_FAIL;
    return (s_rd_err || under > 4u) ? SUITE_FAIL : SUITE_PASS;
}
