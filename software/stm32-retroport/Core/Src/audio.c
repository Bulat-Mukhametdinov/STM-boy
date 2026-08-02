#include "audio.h"

#include "audio_assets.h"
#include "board_pins.h"
#include "main.h"

#define AUDIO_SAMPLE_RATE_HZ 8000u
#define AUDIO_PWM_TOP 255u
#define AUDIO_PWM_CENTER 128u
#define AUDIO_DEFAULT_VOLUME 50u
#define AUDIO_SINE_TABLE_SIZE 64u
#define AUDIO_PHASE_INDEX_SHIFT 10u
#define AUDIO_MUSIC_ATTACK_SAMPLES 32u
#define AUDIO_MUSIC_RELEASE_SAMPLES 48u
/* Keep the amp enabled for this long after the last active sample (100ms @ 8kHz)
 * so short gaps don't click, then power it down to kill idle carrier hiss. */
#define AUDIO_AMP_HOLD_SAMPLES 800u
#define NOTE(freq_hz) ((uint16_t)(((uint32_t)(freq_hz) * 65536u) / AUDIO_SAMPLE_RATE_HZ))

typedef struct {
    uint16_t phase_step;
    uint16_t duration_samples;
} AudioMusicNote;

static const int8_t audio_sine[AUDIO_SINE_TABLE_SIZE] = {
       0,   12,   25,   37,   49,   60,   71,   81,   90,   98,  106,  112,  117,  122,  125,  126,
     127,  126,  125,  122,  117,  112,  106,   98,   90,   81,   71,   60,   49,   37,   25,   12,
       0,  -12,  -25,  -37,  -49,  -60,  -71,  -81,  -90,  -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106,  -98,  -90,  -81,  -71,  -60,  -49,  -37,  -25,  -12
};

static const AudioMusicNote audio_boot_theme[] = {
    {NOTE(262), 1200u},
    {NOTE(330), 1200u},
    {NOTE(392), 1200u},
    {NOTE(523), 1600u},
    {0u, 320u},
    {NOTE(392), 1200u},
    {NOTE(330), 1200u},
    {NOTE(294), 1200u},
    {NOTE(349), 1600u},
    {0u, 480u},
};

static const AudioPcmClip *volatile sfx_clip;
static volatile uint32_t sfx_position;
static volatile uint16_t music_phase;
static volatile uint16_t music_note_index;
static volatile uint16_t music_note_position;
static volatile uint8_t music_playing;
static volatile uint8_t music_loop;
static volatile uint8_t audio_volume = AUDIO_DEFAULT_VOLUME;
static volatile uint8_t audio_ready;
static volatile uint16_t amp_hold_samples;
static AudioStreamFn stream_fn;
static void *stream_ctx;

static void audio_enter_critical(uint32_t *primask)
{
    *primask = __get_PRIMASK();
    __disable_irq();
}

static void audio_leave_critical(uint32_t primask)
{
    if (primask == 0u) {
        __enable_irq();
    }
}

static uint32_t audio_apb1_timer_clock_hz(void)
{
    uint32_t clock_hz = HAL_RCC_GetPCLK1Freq();

    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0u) {
        clock_hz *= 2u;
    }

    return clock_hz;
}

static void audio_enable_gpio_clock(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else if (port == GPIOE) {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
}

static void audio_write_pwm(uint8_t value)
{
    TIM1->CCR1 = value;
}

static void audio_init_gpio(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    audio_enable_gpio_clock(AUDIO_PWM_GPIO_PORT);
    audio_enable_gpio_clock(AUDIO_AMP_SD_GPIO_PORT);

    /* Amp stays enabled the whole time so it can react instantly to a new sound
     * (no turn-on latency clipping the attack). Idle hiss is avoided instead by
     * stopping the PWM carrier when nothing is playing; the amp input coupling
     * cap blocks the resulting static DC level, so a still pin is silent. */
    HAL_GPIO_WritePin(AUDIO_AMP_SD_GPIO_PORT, AUDIO_AMP_SD_PIN, GPIO_PIN_SET);

    gpio_init.Pin = AUDIO_PWM_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = AUDIO_PWM_AF;
    HAL_GPIO_Init(AUDIO_PWM_GPIO_PORT, &gpio_init);

    gpio_init.Pin = AUDIO_AMP_SD_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(AUDIO_AMP_SD_GPIO_PORT, &gpio_init);
    HAL_GPIO_WritePin(AUDIO_AMP_SD_GPIO_PORT, AUDIO_AMP_SD_PIN, GPIO_PIN_SET);
}

static void audio_init_pwm_timer(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();

    TIM1->CR1 = 0u;
    TIM1->CR2 = 0u;
    TIM1->SMCR = 0u;
    TIM1->DIER = 0u;
    TIM1->CCER = 0u;
    TIM1->PSC = 0u;
    TIM1->ARR = AUDIO_PWM_TOP;
    TIM1->CCR1 = AUDIO_PWM_CENTER;
    TIM1->CCMR1 &= ~(TIM_CCMR1_OC1M | TIM_CCMR1_CC1S);
    TIM1->CCMR1 |= TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->CCER = TIM_CCER_CC1E;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0u;
    TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

static void audio_init_sample_timer(void)
{
    uint32_t timer_clock_hz = audio_apb1_timer_clock_hz();
    uint32_t period = (timer_clock_hz / AUDIO_SAMPLE_RATE_HZ);

    if (period == 0u) {
        period = 1u;
    }

    __HAL_RCC_TIM5_CLK_ENABLE();

    TIM5->CR1 = 0u;
    TIM5->PSC = 0u;
    TIM5->ARR = period - 1u;
    TIM5->EGR = TIM_EGR_UG;
    TIM5->SR = 0u;
    TIM5->DIER = TIM_DIER_UIE;

    HAL_NVIC_SetPriority(TIM5_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(TIM5_IRQn);

    TIM5->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

static int32_t audio_next_sample(const AudioPcmClip *clip,
                                 volatile uint32_t *position,
                                 uint8_t loop,
                                 uint8_t attenuation_shift)
{
    uint32_t pos = *position;
    int32_t sample;

    if (clip == NULL || clip->samples == NULL || clip->length == 0u) {
        return 0;
    }

    sample = (int32_t)clip->samples[pos] - (int32_t)AUDIO_PWM_CENTER;
    if (attenuation_shift != 0u) {
        sample /= 2;
    }

    ++pos;
    if (pos >= clip->length) {
        if (loop != 0u) {
            pos = 0u;
        } else {
            pos = clip->length;
        }
    }
    *position = pos;

    return sample;
}

static int32_t audio_next_music_sample(void)
{
    const AudioMusicNote *note;
    uint16_t position;
    int32_t sample;
    int32_t envelope = 255;

    if (music_playing == 0u) {
        return 0;
    }

    if ((uint32_t)music_note_index >= (sizeof(audio_boot_theme) / sizeof(audio_boot_theme[0]))) {
        if (music_loop != 0u) {
            music_note_index = 0u;
        } else {
            music_playing = 0u;
            return 0;
        }
    }

    note = &audio_boot_theme[music_note_index];
    position = music_note_position;

    if (note->phase_step == 0u) {
        sample = 0;
    } else {
        uint8_t sine_index;

        if (position < AUDIO_MUSIC_ATTACK_SAMPLES) {
            envelope = ((int32_t)position * 255) / AUDIO_MUSIC_ATTACK_SAMPLES;
        } else if ((uint32_t)position + AUDIO_MUSIC_RELEASE_SAMPLES > note->duration_samples) {
            envelope = ((int32_t)(note->duration_samples - position) * 255) /
                       AUDIO_MUSIC_RELEASE_SAMPLES;
        }

        if (envelope < 0) {
            envelope = 0;
        } else if (envelope > 255) {
            envelope = 255;
        }

        sine_index = (uint8_t)(music_phase >> AUDIO_PHASE_INDEX_SHIFT);
        sample = ((int32_t)audio_sine[sine_index] * 28 * envelope) / (127 * 255);
        music_phase = (uint16_t)(music_phase + note->phase_step);
    }

    ++position;
    if (position >= note->duration_samples) {
        position = 0u;
        music_phase = 0u;
        ++music_note_index;
    }
    music_note_position = position;

    return sample;
}

static uint8_t audio_scale_to_volume(int32_t centered_sample)
{
    int32_t scaled;

    if (centered_sample < -128) {
        centered_sample = -128;
    } else if (centered_sample > 127) {
        centered_sample = 127;
    }

    scaled = ((centered_sample * (int32_t)audio_volume) / 100) + (int32_t)AUDIO_PWM_CENTER;

    if (scaled < 0) {
        return 0u;
    }
    if (scaled > (int32_t)AUDIO_PWM_TOP) {
        return AUDIO_PWM_TOP;
    }
    return (uint8_t)scaled;
}

void Audio_Init(void)
{
    uint32_t primask;

    audio_init_gpio();
    audio_init_pwm_timer();

    audio_enter_critical(&primask);
    sfx_clip = NULL;
    sfx_position = 0u;
    music_phase = 0u;
    music_note_index = 0u;
    music_note_position = 0u;
    music_playing = 0u;
    music_loop = 0u;
    audio_ready = 1u;
    audio_leave_critical(primask);

    audio_init_sample_timer();
}

void Audio_SetVolume(uint8_t volume)
{
    if (volume > 100u) {
        volume = 100u;
    }

    audio_volume = volume;
}

uint8_t Audio_GetVolume(void)
{
    return audio_volume;
}

void Audio_PlaySound(AudioSoundId sound)
{
    const AudioPcmClip *clip = AudioAssets_GetSound(sound);
    uint32_t primask;

    if (clip == NULL) {
        return;
    }

    audio_enter_critical(&primask);
    sfx_clip = clip;
    sfx_position = 0u;
    audio_leave_critical(primask);
}

void Audio_PlayMusic(AudioMusicId music, bool loop)
{
    uint32_t primask;

    if (music != AUDIO_MUSIC_BOOT) {
        return;
    }

    audio_enter_critical(&primask);
    music_phase = 0u;
    music_note_index = 0u;
    music_note_position = 0u;
    music_playing = 1u;
    music_loop = loop ? 1u : 0u;
    audio_leave_critical(primask);
}

void Audio_StopMusic(void)
{
    uint32_t primask;

    audio_enter_critical(&primask);
    music_playing = 0u;
    music_phase = 0u;
    music_note_index = 0u;
    music_note_position = 0u;
    music_loop = 0u;
    audio_leave_critical(primask);
}

void Audio_SetStream(AudioStreamFn fn, void *ctx)
{
    uint32_t primask;

    audio_enter_critical(&primask);
    stream_fn = fn;
    stream_ctx = ctx;
    audio_leave_critical(primask);
}

void Audio_ClearStream(void)
{
    Audio_SetStream(NULL, NULL);
}

void Audio_TIM5_IRQHandler(void)
{
    int32_t mixed = 0;
    const AudioPcmClip *clip;
    AudioStreamFn current_stream_fn;

    if ((TIM5->SR & TIM_SR_UIF) == 0u) {
        return;
    }

    TIM5->SR &= (uint32_t)~TIM_SR_UIF;

    if (audio_ready == 0u) {
        audio_write_pwm(AUDIO_PWM_CENTER);
        return;
    }

    mixed += audio_next_music_sample();

    current_stream_fn = stream_fn;
    if (current_stream_fn != NULL) {
        mixed += current_stream_fn(stream_ctx);
    }

    clip = sfx_clip;
    mixed += audio_next_sample(clip, &sfx_position, 0u, 0u);
    if (clip != NULL && sfx_position >= clip->length) {
        sfx_clip = NULL;
        sfx_position = 0u;
    }

    /* Carrier gating: while a source is (recently) active, emit the real PWM
     * sample. Otherwise stop toggling the pin (CCR1 = 0 -> constant low) so the
     * 62.5kHz carrier no longer reaches the always-on amp. The hold keeps the
     * carrier running across short gaps (e.g. rapid scroll beeps) so onsets are
     * not clipped and it only settles after a real pause. */
    if (music_playing != 0u || sfx_clip != NULL || stream_fn != NULL) {
        amp_hold_samples = AUDIO_AMP_HOLD_SAMPLES;
    }

    if (amp_hold_samples != 0u) {
        --amp_hold_samples;
        audio_write_pwm(audio_scale_to_volume(mixed));
    } else {
        TIM1->CCR1 = 0u;
    }
}
