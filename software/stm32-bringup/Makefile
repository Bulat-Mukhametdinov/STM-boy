# Toolchain. By default the arm-none-eabi tools are taken from PATH.
# Override TOOLROOT if they live somewhere else, for example:
#   make TOOLROOT=/c/ST/STM32CubeCLT_1.20.0/GNU-tools-for-STM32/bin
TOOLROOT ?=
PREFIX   := $(if $(TOOLROOT),$(TOOLROOT)/,)
CC      = $(PREFIX)arm-none-eabi-gcc
OBJCOPY = $(PREFIX)arm-none-eabi-objcopy
SIZE    = $(PREFIX)arm-none-eabi-size

TARGET  = stm-boy-bringup
BUILD   = build

CPU     = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

DEFS    = -DSTM32F411xE -DUSE_HAL_DRIVER -DUSE_USB_FS

INCLUDES = \
  -IInc \
  -ICMSIS/Device/Include \
  -ICMSIS/Include \
  -IHAL/Inc \
  -IUSB_Device/Core/Inc \
  -IUSB_Device/Class/CDC/Inc

CFLAGS  = $(CPU) $(DEFS) $(INCLUDES) -Wall -O2 -g -ffunction-sections -fdata-sections -std=c11
LDFLAGS = $(CPU) -TSTM32F411CEUX_FLASH.ld -Wl,--gc-sections -specs=nosys.specs -specs=nano.specs -Wl,-Map=$(BUILD)/$(TARGET).map

SRC_C = \
  Src/main.c \
  Src/stm32f4xx_it.c \
  Src/system_stm32f4xx.c \
  Src/usbd_conf.c \
  Src/usbd_desc.c \
  Src/usbd_cdc_if.c \
  Src/st7735.c \
  Src/font5x7.c \
  Src/buttons.c \
  Src/buzzer.c \
  Src/power.c \
  Src/powerctl.c \
  HAL/Src/stm32f4xx_hal.c \
  HAL/Src/stm32f4xx_hal_flash.c \
  HAL/Src/stm32f4xx_hal_flash_ex.c \
  HAL/Src/stm32f4xx_hal_flash_ramfunc.c \
  HAL/Src/stm32f4xx_hal_rcc.c \
  HAL/Src/stm32f4xx_hal_rcc_ex.c \
  HAL/Src/stm32f4xx_hal_gpio.c \
  HAL/Src/stm32f4xx_hal_cortex.c \
  HAL/Src/stm32f4xx_hal_pwr.c \
  HAL/Src/stm32f4xx_hal_pwr_ex.c \
  HAL/Src/stm32f4xx_hal_dma.c \
  HAL/Src/stm32f4xx_hal_dma_ex.c \
  HAL/Src/stm32f4xx_hal_exti.c \
  HAL/Src/stm32f4xx_hal_spi.c \
  HAL/Src/stm32f4xx_hal_adc.c \
  HAL/Src/stm32f4xx_hal_adc_ex.c \
  HAL/Src/stm32f4xx_hal_tim.c \
  HAL/Src/stm32f4xx_hal_tim_ex.c \
  HAL/Src/stm32f4xx_hal_pcd.c \
  HAL/Src/stm32f4xx_hal_pcd_ex.c \
  HAL/Src/stm32f4xx_ll_usb.c \
  USB_Device/Core/Src/usbd_core.c \
  USB_Device/Core/Src/usbd_ctlreq.c \
  USB_Device/Core/Src/usbd_ioreq.c \
  USB_Device/Class/CDC/Src/usbd_cdc.c

SRC_S = Startup/startup_stm32f411xe.s

OBJ = $(SRC_C:%.c=$(BUILD)/%.o) $(SRC_S:%.s=$(BUILD)/%.o)

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
