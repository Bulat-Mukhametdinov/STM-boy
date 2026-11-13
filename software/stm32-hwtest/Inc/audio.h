#ifndef AUDIO_H
#define AUDIO_H

/* P4 audio test driver: PA8 = TIM1_CH1 PWM (AF1) -> RC LPF -> PAM8302A (SD# = PB2, active high).

   Carrier: TIM1 up-counting, PSC 0, ARR = 2^bits - 1 (750 kHz/7 bit ... 93.75 kHz/10 bit).
   Engine A: TIM5 update ISR per sample writes TIM1->CCR1 (any sample rate).
   Engine B: TIM1 repetition counter (update rate = carrier/(RCR+1)); TIM1_UP -> DMA2 S5 Ch6
             circular double buffer -> TIM1->CCR1; DMA HT/TC IRQ renders the other half.
   Samples come from a small synth mixer (4 voices) + master gain/slew + hard clip guard +
   quantiser to the carrier resolution with optional TPDF dither / 1st-order noise shaping.
   Everything is amp-safe by default: master -24 dBFS, output clip guard -6 dBFS until
   audio_set_max_db() unlocks it. Thread context except where noted. */

#include <stdbool.h>
#include <stdint.h>

#define AUD_NCARR   4u
#define AUD_NVOICE  4u
#define AUD_HALF    512u      /* samples per DMA half-buffer (engine B) */

typedef struct {
    uint16_t arr;             /* TIM1 ARR (period = arr+1 ticks) */
    uint8_t bits;
    uint32_t carrier_hz;
    const char *name;         /* "750k/7b" */
} aud_carrier_t;

extern const aud_carrier_t g_aud_carriers[AUD_NCARR];

typedef enum { AUD_DITHER_OFF = 0, AUD_DITHER_TPDF, AUD_DITHER_SHAPED } aud_dither_t;
typedef enum { AUD_ENG_NONE = 0, AUD_ENG_A, AUD_ENG_B } aud_engine_t;
typedef enum { AUD_PA8_LOW = 0, AUD_PA8_HIGH, AUD_PA8_HIZ } aud_pa8_t;
typedef enum {
    AUD_GEN_OFF = 0, AUD_GEN_SINE, AUD_GEN_SQUARE, AUD_GEN_WHITE, AUD_GEN_PINK, AUD_GEN_CHIRP
} aud_gen_t;

typedef struct {
    volatile uint32_t isr_count;   /* engine A samples */
    volatile uint32_t dma_ht;
    volatile uint32_t dma_tc;
    volatile uint32_t late;        /* engine B: HT+TC both pending at ISR entry; A: sample overrun */
    volatile uint32_t dma_err;
    volatile uint32_t cyc_sum;     /* DWT cycles inside the render body */
    volatile uint32_t cyc_min;
    volatile uint32_t cyc_max;
    volatile uint32_t calls;       /* number of timed bodies (A: samples, B: blocks) */
    volatile uint32_t clipped;     /* samples hit by the clip guard */
    volatile uint32_t last_us;     /* TIM2 time_us() at the last A sample / B block ISR entry */
} aud_stats_t;

extern aud_stats_t g_aud_stats;

void audio_init(void);                              /* LUT + defaults; idempotent */
void audio_shutdown(void);                          /* engine off, PWM off, PA8 input, amp off */

/* Carrier / pins */
void audio_pwm_start(unsigned carrier, bool at_mid); /* TIM1 PWM on PA8 at 0 % or 50 % (may be re-called) */
void audio_pwm_stop(void);                          /* TIM1 off, PA8 back to input (reset state) */
void audio_pa8_mode(aud_pa8_t m);                   /* PWM off, PA8 static low/high/hi-Z */
void audio_amp_raw(bool on);                        /* SD# pin only */
bool audio_amp_is_on(void);
bool audio_amp_on(uint32_t ramp_ms);                /* 0 = naive (50 % then SD#=1); else 0 % -> SD#=1 -> ramp */
bool audio_amp_off(uint32_t ramp_ms);               /* 0 = naive (SD#=0 at 50 %); else ramp to 0 % -> SD#=0; false = aborted */
unsigned audio_carrier(void);
uint16_t audio_mid(void);                           /* CCR for 50 % */

/* Engines */
bool audio_engine_start(aud_engine_t e, uint32_t fs_hz, aud_dither_t d);
void audio_engine_stop(void);
aud_engine_t audio_engine(void);
uint32_t audio_fs_mhz(void);                        /* actual sample rate x1000 */
uint32_t audio_rcr_div(void);                       /* engine B: RCR+1 */
void audio_dither_set(aud_dither_t d);

/* Mixer */
void audio_set_master_db(float db);                 /* <= 0, slewed ~3 ms */
float audio_master_db(void);
void audio_set_max_db(float db);                    /* output clip guard (peak), default -6 */
float audio_max_db(void);
void audio_voice_off(unsigned v);
void audio_voice_tone(unsigned v, aud_gen_t type, float f_hz, float db); /* SINE / SQUARE */
void audio_voice_noise(unsigned v, bool pink, float rms_db);             /* db = RMS dBFS */
void audio_voice_chirp(unsigned v, float f0, float f1, float dur_s, float db);
void audio_silence(void);                           /* all voices off */

/* Stats */
void audio_stats_reset(void);

/* Timing helper that keeps USB serviced; false when abort was requested. */
bool audio_wait_ms(uint32_t ms);

#endif
