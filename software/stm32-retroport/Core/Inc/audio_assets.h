#ifndef AUDIO_ASSETS_H
#define AUDIO_ASSETS_H

#include "audio.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *samples;
    uint32_t length;
} AudioPcmClip;

const AudioPcmClip *AudioAssets_GetSound(AudioSoundId sound);

#ifdef __cplusplus
}
#endif

#endif
