#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_DEFAULT_VOLUME 50u
#define SETTINGS_DEFAULT_THEME 0u

bool Settings_Init(void);
void Settings_Task(void);
uint8_t Settings_GetVolume(void);
void Settings_SetVolume(uint8_t volume);
uint8_t Settings_GetTheme(void);
void Settings_SetTheme(uint8_t theme);
bool Settings_Flush(void);

#ifdef __cplusplus
}
#endif

#endif
