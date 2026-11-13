#include "board.h"

#define UID_BASE_ADDR       0x1FFF7A10u
#define FLASH_SIZE_ADDR     0x1FFF7A22u

void gpio_config(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t pull,
                 uint32_t speed, uint32_t af)
{
    uint32_t shift2 = pin * 2u;

    if (mode == GPIO_MODE_AF_) {
        volatile uint32_t *afr = &port->AFR[pin >> 3];
        uint32_t shift4 = (pin & 7u) * 4u;
        *afr = (*afr & ~(0xFu << shift4)) | ((af & 0xFu) << shift4);
    }
    port->OSPEEDR = (port->OSPEEDR & ~(3u << shift2)) | ((speed & 3u) << shift2);
    port->OTYPER &= ~(1u << pin); /* push-pull */
    port->PUPDR = (port->PUPDR & ~(3u << shift2)) | ((pull & 3u) << shift2);
    port->MODER = (port->MODER & ~(3u << shift2)) | ((mode & 3u) << shift2);
}

void board_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;

    /* Amplifier OFF (PB2 low) — preset ODR before switching to output. */
    gpio_clr(AMP_SD_PORT, AMP_SD_PIN);
    gpio_config(AMP_SD_PORT, AMP_SD_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);

    /* OFF_ACK deasserted (low) from boot so the ATtiny never sees a floating ACK. */
    gpio_clr(OFF_ACK_PORT, OFF_ACK_PIN);
    gpio_config(OFF_ACK_PORT, OFF_ACK_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);

    /* OFF_REQ: input, external divider/pull-up. */
    gpio_config(OFF_REQ_PORT, OFF_REQ_PIN, GPIO_MODE_IN_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);

    /* Charger CHG (open-drain, active-low): pull-up. VBUS sense: pull-down. */
    gpio_config(CHG_PORT, CHG_PIN, GPIO_MODE_IN_, GPIO_PULL_UP_, GPIO_SPEED_LOW_, 0);
    gpio_config(VBUS_PORT, VBUS_PIN, GPIO_MODE_IN_, GPIO_PULL_DOWN_, GPIO_SPEED_LOW_, 0);

    /* Both SPI chip selects high (deselected). */
    gpio_set(LCD_CS_PORT, LCD_CS_PIN);
    gpio_config(LCD_CS_PORT, LCD_CS_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 0);
    gpio_set(FLASH_CS_PORT, FLASH_CS_PIN);
    gpio_config(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 0);

    /* PA8 (AUDIO_PWM) and PA1 (BAT_SNS) stay in their reset state in P0. */
}

static void hex32(char *out, uint32_t v, unsigned digits)
{
    static const char hx[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < digits; i++) {
        out[i] = hx[(v >> (4u * (digits - 1u - i))) & 0xFu];
    }
}

void board_uid_hex(char out[25])
{
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE_ADDR;
    hex32(&out[0], uid[0], 8);
    hex32(&out[8], uid[1], 8);
    hex32(&out[16], uid[2], 8);
    out[24] = '\0';
}

/* Same algorithm as usbd_desc.c Get_SerialNum() in stm32-bringup (and ST's
   CubeMX template): 8 hex digits of (UID0 + UID2) followed by the top 4 hex
   digits of UID1. This matches the serial the system bootloader reports. */
void board_usb_serial(char out[13])
{
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE_ADDR;
    uint32_t s0 = uid[0] + uid[2];
    uint32_t s1 = uid[1];
    hex32(&out[0], s0, 8);
    hex32(&out[8], s1 >> 16, 4);
    out[12] = '\0';
}

uint32_t board_flash_kb(void)
{
    return *(const volatile uint16_t *)FLASH_SIZE_ADDR;
}
