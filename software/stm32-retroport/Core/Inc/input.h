#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INPUT_BUTTON_A = 0,
    INPUT_BUTTON_B,
    INPUT_BUTTON_X,
    INPUT_BUTTON_Y,
    INPUT_BUTTON_UP,
    INPUT_BUTTON_LEFT,
    INPUT_BUTTON_RIGHT,
    INPUT_BUTTON_CENTER,
    INPUT_BUTTON_DOWN,
    INPUT_BUTTON_COUNT
} InputButton;

typedef uint16_t InputButtonMask;

#define INPUT_BUTTON_MASK(button) ((InputButtonMask)(1u << (button)))

void Input_Init(void);
void Input_Update(void);
void Input_Tick1ms(void);

bool Input_IsDown(InputButton button);
bool Input_WasPressed(InputButton button);
bool Input_WasReleased(InputButton button);
bool Input_PopAction(InputButton *button);

InputButtonMask Input_GetDownMask(void);
InputButtonMask Input_GetPressedMask(void);
InputButtonMask Input_GetReleasedMask(void);

#ifdef __cplusplus
}
#endif

#endif
