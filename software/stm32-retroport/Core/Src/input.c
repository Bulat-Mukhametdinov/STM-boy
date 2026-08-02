#include "input.h"

#include "board_pins.h"
#include "main.h"

#include <stddef.h>

#define INPUT_DEBOUNCE_MS 20u
#define INPUT_REPEAT_DELAY_MS 350u
#define INPUT_REPEAT_PERIOD_MS 90u
#define INPUT_ACTION_QUEUE_CAPACITY 64u
#define INPUT_ACTION_REPEAT_FLAG 0x80u
#define INPUT_ACTION_BUTTON_MASK 0x7Fu

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} InputPin;

static const InputPin input_pins[INPUT_BUTTON_COUNT] = {
    [INPUT_BUTTON_A] = {BTN_A_GPIO_PORT, BTN_A_PIN},
    [INPUT_BUTTON_B] = {BTN_B_GPIO_PORT, BTN_B_PIN},
    [INPUT_BUTTON_X] = {BTN_X_GPIO_PORT, BTN_X_PIN},
    [INPUT_BUTTON_Y] = {BTN_Y_GPIO_PORT, BTN_Y_PIN},
    [INPUT_BUTTON_UP] = {BTN_UP_GPIO_PORT, BTN_UP_PIN},
    [INPUT_BUTTON_LEFT] = {BTN_LEFT_GPIO_PORT, BTN_LEFT_PIN},
    [INPUT_BUTTON_RIGHT] = {BTN_RIGHT_GPIO_PORT, BTN_RIGHT_PIN},
    [INPUT_BUTTON_CENTER] = {BTN_CENTER_GPIO_PORT, BTN_CENTER_PIN},
    [INPUT_BUTTON_DOWN] = {BTN_DOWN_GPIO_PORT, BTN_DOWN_PIN},
};

static volatile InputButtonMask stable_down_mask;
static volatile InputButtonMask pressed_mask;
static volatile InputButtonMask released_mask;
static volatile InputButtonMask pressed_latch_mask;
static volatile InputButtonMask released_latch_mask;

static InputButtonMask last_raw_down_mask;
static uint32_t last_change_ms;
static uint32_t repeat_due_ms[INPUT_BUTTON_COUNT];
static volatile uint8_t input_ready;

static volatile uint8_t action_queue[INPUT_ACTION_QUEUE_CAPACITY];
static volatile uint8_t action_head;
static volatile uint8_t action_tail;
static volatile uint8_t action_count;
static volatile InputButtonMask repeat_queued_mask;

static InputButtonMask snapshot_stable_down_mask;
static InputButtonMask snapshot_pressed_mask;
static InputButtonMask snapshot_released_mask;

static void input_enable_gpio_clock(GPIO_TypeDef *port)
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

static InputButtonMask input_read_raw_down_mask(void)
{
    InputButtonMask mask = 0;

    for (uint32_t i = 0; i < (uint32_t)INPUT_BUTTON_COUNT; ++i) {
        if (HAL_GPIO_ReadPin(input_pins[i].port, input_pins[i].pin) == GPIO_PIN_RESET) {
            mask |= INPUT_BUTTON_MASK(i);
        }
    }

    return mask;
}

static bool input_is_repeatable(InputButton button)
{
    return button == INPUT_BUTTON_UP || button == INPUT_BUTTON_DOWN ||
           button == INPUT_BUTTON_LEFT || button == INPUT_BUTTON_RIGHT;
}

static void input_push_action_from_isr(InputButton button, bool repeat)
{
    InputButtonMask button_mask = INPUT_BUTTON_MASK(button);

    if (repeat && (repeat_queued_mask & button_mask) != 0u) {
        return;
    }

    if (action_count >= INPUT_ACTION_QUEUE_CAPACITY) {
        return;
    }

    action_queue[action_head] = (uint8_t)button | (repeat ? INPUT_ACTION_REPEAT_FLAG : 0u);
    action_head = (uint8_t)((action_head + 1u) % INPUT_ACTION_QUEUE_CAPACITY);
    ++action_count;

    if (repeat) {
        repeat_queued_mask |= button_mask;
    }
}

static void input_queue_pressed_mask(InputButtonMask mask)
{
    for (uint32_t i = 0; i < (uint32_t)INPUT_BUTTON_COUNT; ++i) {
        if ((mask & INPUT_BUTTON_MASK(i)) != 0u) {
            input_push_action_from_isr((InputButton)i, false);
        }
    }
}

static void input_drop_released_repeats_from_isr(InputButtonMask released)
{
    uint8_t original_count = action_count;
    uint8_t kept_count = 0;

    for (uint8_t i = 0; i < original_count; ++i) {
        uint8_t encoded = action_queue[(uint8_t)((action_tail + i) % INPUT_ACTION_QUEUE_CAPACITY)];
        InputButton button = (InputButton)(encoded & INPUT_ACTION_BUTTON_MASK);
        bool repeat = (encoded & INPUT_ACTION_REPEAT_FLAG) != 0u;

        if (repeat && (released & INPUT_BUTTON_MASK(button)) != 0u) {
            continue;
        }

        action_queue[kept_count] = encoded;
        ++kept_count;
    }

    action_tail = 0;
    action_head = (uint8_t)(kept_count % INPUT_ACTION_QUEUE_CAPACITY);
    action_count = kept_count;
    repeat_queued_mask &= (InputButtonMask)~released;
}

void Input_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;

    for (uint32_t i = 0; i < (uint32_t)INPUT_BUTTON_COUNT; ++i) {
        input_enable_gpio_clock(input_pins[i].port);
        gpio_init.Pin = input_pins[i].pin;
        HAL_GPIO_Init(input_pins[i].port, &gpio_init);
    }

    stable_down_mask = input_read_raw_down_mask();
    last_raw_down_mask = stable_down_mask;
    pressed_mask = 0;
    released_mask = 0;
    pressed_latch_mask = 0;
    released_latch_mask = 0;
    last_change_ms = HAL_GetTick();

    for (uint32_t i = 0; i < (uint32_t)INPUT_BUTTON_COUNT; ++i) {
        repeat_due_ms[i] = last_change_ms + INPUT_REPEAT_DELAY_MS;
    }

    action_head = 0;
    action_tail = 0;
    action_count = 0;
    repeat_queued_mask = 0;
    snapshot_stable_down_mask = stable_down_mask;
    snapshot_pressed_mask = 0;
    snapshot_released_mask = 0;
    input_ready = 1u;
}

void Input_Tick1ms(void)
{
    if (input_ready == 0u) {
        return;
    }

    InputButtonMask raw_down_mask = input_read_raw_down_mask();
    uint32_t now_ms = HAL_GetTick();

    pressed_mask = 0;
    released_mask = 0;

    if (raw_down_mask != last_raw_down_mask) {
        last_raw_down_mask = raw_down_mask;
        last_change_ms = now_ms;
        return;
    }

    if ((uint32_t)(now_ms - last_change_ms) < INPUT_DEBOUNCE_MS) {
        return;
    }

    if (raw_down_mask != stable_down_mask) {
        InputButtonMask changed_mask = (InputButtonMask)(raw_down_mask ^ stable_down_mask);

        pressed_mask = (InputButtonMask)(changed_mask & raw_down_mask);
        released_mask = (InputButtonMask)(changed_mask & stable_down_mask);
        pressed_latch_mask |= pressed_mask;
        released_latch_mask |= released_mask;
        stable_down_mask = raw_down_mask;
        input_drop_released_repeats_from_isr(released_mask);
        input_queue_pressed_mask(pressed_mask);

        for (uint32_t i = 0; i < (uint32_t)INPUT_BUTTON_COUNT; ++i) {
            if ((pressed_mask & INPUT_BUTTON_MASK(i)) != 0u) {
                repeat_due_ms[i] = now_ms + INPUT_REPEAT_DELAY_MS;
            }
        }
    }

    for (uint32_t i = 0; i < (uint32_t)INPUT_BUTTON_COUNT; ++i) {
        if ((stable_down_mask & INPUT_BUTTON_MASK(i)) == 0u ||
            !input_is_repeatable((InputButton)i)) {
            continue;
        }

        if ((int32_t)(now_ms - repeat_due_ms[i]) >= 0) {
            input_push_action_from_isr((InputButton)i, true);
            repeat_due_ms[i] = now_ms + INPUT_REPEAT_PERIOD_MS;
        }
    }
}

void Input_Update(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    snapshot_stable_down_mask = stable_down_mask;
    snapshot_pressed_mask = pressed_latch_mask;
    snapshot_released_mask = released_latch_mask;
    pressed_latch_mask = 0;
    released_latch_mask = 0;
    if (primask == 0u) {
        __enable_irq();
    }
}

bool Input_IsDown(InputButton button)
{
    return (snapshot_stable_down_mask & INPUT_BUTTON_MASK(button)) != 0u;
}

bool Input_WasPressed(InputButton button)
{
    return (snapshot_pressed_mask & INPUT_BUTTON_MASK(button)) != 0u;
}

bool Input_WasReleased(InputButton button)
{
    return (snapshot_released_mask & INPUT_BUTTON_MASK(button)) != 0u;
}

InputButtonMask Input_GetDownMask(void)
{
    return snapshot_stable_down_mask;
}

InputButtonMask Input_GetPressedMask(void)
{
    return snapshot_pressed_mask;
}

InputButtonMask Input_GetReleasedMask(void)
{
    return snapshot_released_mask;
}

bool Input_PopAction(InputButton *button)
{
    if (button == NULL) {
        return false;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (action_count == 0u) {
        if (primask == 0u) {
            __enable_irq();
        }
        return false;
    }

    uint8_t encoded = action_queue[action_tail];
    *button = (InputButton)(encoded & INPUT_ACTION_BUTTON_MASK);
    action_tail = (uint8_t)((action_tail + 1u) % INPUT_ACTION_QUEUE_CAPACITY);
    --action_count;

    if ((encoded & INPUT_ACTION_REPEAT_FLAG) != 0u) {
        repeat_queued_mask &= (InputButtonMask)~INPUT_BUTTON_MASK(*button);
    }

    if (primask == 0u) {
        __enable_irq();
    }
    return true;
}
