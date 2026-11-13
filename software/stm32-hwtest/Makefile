# STM-boy hardware test firmware (stm32-hwtest)
#
# Vendor code (CMSIS, STM32F4 HAL, ST USB device library) is NOT copied into
# this project: it is compiled straight out of HALDIR (read-only), objects go
# to build/vendor/...
#
#   make                      build build/hwtest.{elf,bin,hex} (+ build/fw_id.json)
#   make OPT=-Os              other optimisation level (reported by `info`)
#   make flash                program over SWD (ST-Link)
#   make dfu                  program over USB DFU via tools/hwtest.py
#
# Toolchain: arm-none-eabi-* from PATH, or set TOOLROOT, e.g.
#   make TOOLROOT=/c/ST/STM32CubeCLT_1.20.0/GNU-tools-for-STM32/bin

TOOLROOT ?=
PREFIX   := $(if $(TOOLROOT),$(TOOLROOT)/,)
CC       := $(PREFIX)arm-none-eabi-gcc
OBJCOPY  := $(PREFIX)arm-none-eabi-objcopy
SIZE     := $(PREFIX)arm-none-eabi-size

PYTHON   ?= py -3.14
PROGRAMMER ?= STM32_Programmer_CLI

HALDIR   ?= ../stm32-bringup
OPT      ?= -O2

TARGET   := hwtest
BUILD    := build
LDSCRIPT := STM32F411CEUX_HWTEST.ld

# ---------------------------------------------------------------- build identity
# Evaluated once per make invocation (simple expansion) so the compiled-in id
# and build/fw_id.json always agree.
GIT_HASH   := $(shell git rev-parse --short HEAD 2>/dev/null || echo nogit)
GIT_DIRTY  := $(shell git status --porcelain 2>/dev/null | head -n 1)
FW_HASH    := $(GIT_HASH)$(if $(strip $(GIT_DIRTY)),+dirty,)
BUILD_TIME := $(shell date +%s)

# ---------------------------------------------------------------- flags
CPU  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS := -DSTM32F411xE -DUSE_HAL_DRIVER -DUSE_USB_FS -DHSE_VALUE=8000000U

INCLUDES := \
  -IInc \
  -I$(HALDIR)/CMSIS/Device/Include \
  -I$(HALDIR)/CMSIS/Include \
  -I$(HALDIR)/HAL/Inc \
  -I$(HALDIR)/USB_Device/Core/Inc \
  -I$(HALDIR)/USB_Device/Class/CDC/Inc

CFLAGS_BASE   := $(CPU) $(DEFS) $(INCLUDES) $(OPT) -g -std=c11 \
                 -ffunction-sections -fdata-sections -MMD -MP
CFLAGS_OWN    := $(CFLAGS_BASE) -Wall -Wextra
CFLAGS_VENDOR := $(CFLAGS_BASE) -Wall

LDFLAGS := $(CPU) -T$(LDSCRIPT) -Wl,--gc-sections -specs=nosys.specs -specs=nano.specs \
           -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments

# ---------------------------------------------------------------- sources
SRC_OWN := \
  Src/main.c \
  Src/early_boot.c \
  Src/system_stm32f4xx.c \
  Src/stm32f4xx_it.c \
  Src/board.c \
  Src/clock.c \
  Src/timebase.c \
  Src/dfu.c \
  Src/log.c \
  Src/cmd.c \
  Src/suites.c \
  Src/crc.c \
  Src/hwcal.c \
  Src/fw_id.c \
  Src/syscalls.c \
  Src/drv/lcd.c \
  Src/drv/gfx.c \
  Src/drv/font5x7_full.c \
  Src/drv/input.c \
  Src/drv/power.c \
  Src/drv/w25q.c \
  Src/ui/menu.c \
  Src/ui/prompt.c \
  Src/usb/usb_device.c \
  Src/usb/usbd_conf.c \
  Src/usb/usbd_desc.c \
  Src/usb/usbd_cdc_if.c

SRC_ASM := Startup/startup_stm32f411xe.s

# Paths relative to HALDIR. Only the HAL modules enabled in Inc/stm32f4xx_hal_conf.h.
SRC_VENDOR := \
  HAL/Src/stm32f4xx_hal.c \
  HAL/Src/stm32f4xx_hal_cortex.c \
  HAL/Src/stm32f4xx_hal_rcc.c \
  HAL/Src/stm32f4xx_hal_rcc_ex.c \
  HAL/Src/stm32f4xx_hal_gpio.c \
  HAL/Src/stm32f4xx_hal_pwr.c \
  HAL/Src/stm32f4xx_hal_pwr_ex.c \
  HAL/Src/stm32f4xx_hal_flash.c \
  HAL/Src/stm32f4xx_hal_flash_ex.c \
  HAL/Src/stm32f4xx_hal_dma.c \
  HAL/Src/stm32f4xx_hal_dma_ex.c \
  HAL/Src/stm32f4xx_hal_pcd.c \
  HAL/Src/stm32f4xx_hal_pcd_ex.c \
  HAL/Src/stm32f4xx_ll_usb.c \
  USB_Device/Core/Src/usbd_core.c \
  USB_Device/Core/Src/usbd_ctlreq.c \
  USB_Device/Core/Src/usbd_ioreq.c \
  USB_Device/Class/CDC/Src/usbd_cdc.c

OBJ_OWN    := $(SRC_OWN:%.c=$(BUILD)/%.o)
OBJ_ASM    := $(SRC_ASM:%.s=$(BUILD)/%.o)
OBJ_VENDOR := $(SRC_VENDOR:%.c=$(BUILD)/vendor/%.o)
OBJ        := $(OBJ_ASM) $(OBJ_OWN) $(OBJ_VENDOR)

ELF := $(BUILD)/$(TARGET).elf
BIN := $(BUILD)/$(TARGET).bin
HEX := $(BUILD)/$(TARGET).hex

# ---------------------------------------------------------------- rules
all: $(ELF) $(BIN) $(HEX) $(BUILD)/fw_id.json

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_OWN) -c $< -o $@

$(BUILD)/vendor/%.o: $(HALDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_VENDOR) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -g -c $< -o $@

# The build identity lives only in fw_id.o; it is rebuilt on every make so the
# hash/time are always current, without forcing anything else to recompile.
$(BUILD)/Src/fw_id.o: CFLAGS_OWN += -DFW_GIT_HASH=\"$(FW_HASH)\" -DFW_BUILD_TIME=$(BUILD_TIME)UL -DFW_OPT=\"$(OPT)\"
$(BUILD)/Src/fw_id.o: FORCE

$(ELF): $(OBJ) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJ) -o $@
	$(SIZE) $@

# Canonical image: raw bin with the CRC trailer patched in.
$(BIN): $(ELF) tools/append_crc.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/append_crc.py $@

# HEX is generated from the patched bin so both carry the same trailer.
# Only the data section is relocated (same bytes as --change-addresses), so
# no bogus "start address = 0x08000000" (type 05) record is emitted.
$(HEX): $(BIN)
	$(OBJCOPY) -I binary -O ihex --change-section-address .data=0x08000000 $< $@

$(BUILD)/fw_id.json: $(ELF)
	@printf '{"fw":"%s","build":%s}\n' '$(FW_HASH)' '$(BUILD_TIME)' > $@
	@cat $@

flash: all
	$(PROGRAMMER) -c port=SWD -w $(BIN) 0x08000000 -v -rst

dfu: all
	$(PYTHON) tools/hwtest.py flash $(BIN)

clean:
	rm -rf $(BUILD)

FORCE:

.PHONY: all clean flash dfu FORCE

-include $(OBJ:.o=.d)
