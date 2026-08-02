#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_SOUND_NAV = 0,
    AUDIO_SOUND_SELECT,
    AUDIO_SOUND_BACK,
    AUDIO_SOUND_RESET,
    AUDIO_SOUND_COUNT
} AudioSoundId;

typedef enum {
    AUDIO_MUSIC_BOOT = 0,
    AUDIO_MUSIC_COUNT
} AudioMusicId;

typedef int8_t (*AudioStreamFn)(void *ctx);

void Audio_Init(void);
void Audio_SetVolume(uint8_t volume);
uint8_t Audio_GetVolume(void);

void Audio_PlaySound(AudioSoundId sound);
void Audio_PlayMusic(AudioMusicId music, bool loop);
void Audio_StopMusic(void);
void Audio_SetStream(AudioStreamFn fn, void *ctx);
void Audio_ClearStream(void);

void Audio_TIM5_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif
