# SPDX-License-Identifier: MIT

MCU ?= attiny13a
AVRDUDE_MCU ?= t13
F_CPU ?= 1200000UL
TIMER0_PRESCALER ?= 8UL
TARGET ?= power_fsm
BUILD_DIR ?= build

PROGRAMMER ?= usbasp
BITCLOCK ?= 10
LFUSE ?= 0x6A
HFUSE ?= 0xFF

AVR_GCC ?= avr-gcc
AVR_OBJCOPY ?= avr-objcopy
AVR_SIZE ?= avr-size
AVRDUDE ?= avrdude

-include local.mk

CC = $(AVR_GCC)
OBJCOPY = $(AVR_OBJCOPY)
SIZE = $(AVR_SIZE)

CPPFLAGS += -DF_CPU=$(F_CPU) -DTIMER0_PRESCALER=$(TIMER0_PRESCALER)
CFLAGS += -std=c99 -mmcu=$(MCU) -Os -flto
CFLAGS += -ffunction-sections -fdata-sections -fno-common
CFLAGS += -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wmissing-prototypes
LDFLAGS += -mmcu=$(MCU) -flto -Wl,--gc-sections -Wl,--relax
AVRDUDE_FLAGS ?= -p $(AVRDUDE_MCU) -c $(PROGRAMMER) -B $(BITCLOCK)

SRC = main.c hw.c timebase.c inputs.c power_fsm.c
OBJ = $(addprefix $(BUILD_DIR)/,$(SRC:.c=.o))
DEP = $(OBJ:.o=.d)
TARGET_ELF = $(BUILD_DIR)/$(TARGET).elf
TARGET_HEX = $(BUILD_DIR)/$(TARGET).hex
TARGET_MAP = $(BUILD_DIR)/$(TARGET).map

ifeq ($(OS),Windows_NT)
define CLEAN_CMD
powershell -NoProfile -Command "if (Test-Path -LiteralPath '$(BUILD_DIR)') { Remove-Item -LiteralPath '$(BUILD_DIR)' -Recurse -Force }; exit 0"
endef
define MKDIR_CMD
powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(BUILD_DIR)' | Out-Null"
endef
else
CLEAN_CMD = rm -rf $(BUILD_DIR)
MKDIR_CMD = mkdir -p $(BUILD_DIR)
endif

all: $(TARGET_HEX) size

$(BUILD_DIR):
	$(MKDIR_CMD)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(TARGET_ELF): $(OBJ)
	$(CC) $(LDFLAGS) -Wl,-Map=$(TARGET_MAP) -o $@ $^

$(TARGET_HEX): $(TARGET_ELF)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(TARGET_ELF)
	$(SIZE) --mcu=$(MCU) -C $<

flash: $(TARGET_HEX)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:w:$<:i

fuses:
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U lfuse:w:$(LFUSE):m -U hfuse:w:$(HFUSE):m

read-fuses:
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U lfuse:r:-:h -U hfuse:r:-:h

clean:
	$(CLEAN_CMD)

-include $(DEP)

.PHONY: all clean flash fuses read-fuses size
