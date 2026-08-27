CROSS_COMPILE    ?= arm-none-eabi-
CROSS_COMPILE_64 ?= aarch64-none-linux-gnu-

CC_ARM      := $(CROSS_COMPILE)gcc
LD_ARM      := $(CROSS_COMPILE)gcc
OBJCOPY_ARM := $(CROSS_COMPILE)objcopy

CC_ARM64      := $(CROSS_COMPILE_64)gcc
LD_ARM64      := $(CROSS_COMPILE_64)gcc
OBJCOPY_ARM64 := $(CROSS_COMPILE_64)objcopy

FEATURES   := uart xml
COMMON_DIR := ../common
include $(COMMON_DIR)/common.mk

SRC_DIR   := src
BUILD_DIR := build
OUT_ARM   := ../payloads/hakujoudai_arm
OUT_ARM64 := ../payloads/hakujoudai_arm64

CFLAGS_COMMON := $(COMMON_CFLAGS) -Os -fno-builtin -fno-strict-aliasing
CFLAGS_COMMON += -fvisibility=hidden
CFLAGS_COMMON += -fno-stack-protector
CFLAGS_COMMON += -Isrc/include -Wno-main
CFLAGS_COMMON += -DXML_STACK_SIZE=512

ifeq ($(DEBUG),1)
CFLAGS_COMMON += -DDEBUG
endif

CFLAGS_ARM := $(CFLAGS_COMMON)
CFLAGS_ARM += -march=armv7-a -marm -fpic -mpic-data-is-text-relative -fno-lto

CFLAGS_ARM64 := $(CFLAGS_COMMON)
CFLAGS_ARM64 += -march=armv8-a -mcmodel=tiny -fno-pic -fno-pie -fno-plt

LDFLAGS_COMMON := $(COMMON_LDFLAGS) -nostdlib

LDFLAGS_ARM   := $(LDFLAGS_COMMON) -T src/linker32.x
LDFLAGS_ARM64 := $(LDFLAGS_COMMON) -no-pie -T src/linker64.x

LIBS_ARM   := -lgcc
LIBS_ARM64 :=

C_SOURCES := $(shell find $(SRC_DIR) -name '*.c')

map_arm_obj   = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/arm/%.o,$(patsubst $(COMMON_DIR)/%.c,$(BUILD_DIR)/arm/common/%.o,$(1)))
map_arm64_obj = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/arm64/%.o,$(patsubst $(COMMON_DIR)/%.c,$(BUILD_DIR)/arm64/common/%.o,$(1)))

ARM_C_OBJ   := $(call map_arm_obj,$(C_SOURCES) $(COMMON_SRCS))
ARM_ASM_OBJ := $(BUILD_DIR)/arm/entry.o
ARM_OBJ     := $(ARM_ASM_OBJ) $(ARM_C_OBJ)

ARM64_OBJ   := $(call map_arm64_obj,$(C_SOURCES) $(COMMON_SRCS))

ARM_ELF   := $(BUILD_DIR)/arm/hakujoudai.elf
ARM_BIN   := $(OUT_ARM).bin

ARM64_ELF := $(BUILD_DIR)/arm64/hakujoudai.elf
ARM64_BIN := $(OUT_ARM64).bin

.PHONY: all arm arm64 clean disasm

all: arm arm64

arm: $(ARM_BIN)

$(ARM_BIN): $(ARM_ELF)
	$(OBJCOPY_ARM) -O binary $< $@

$(ARM_ELF): $(ARM_OBJ)
	$(LD_ARM) $(LDFLAGS_ARM) -o $@ $^ $(LIBS_ARM)

$(BUILD_DIR)/arm/entry.o: $(SRC_DIR)/entry.S
	mkdir -p $(@D)
	$(CC_ARM) $(CFLAGS_ARM) -c $< -o $@

$(BUILD_DIR)/arm/%.o: $(SRC_DIR)/%.c
	mkdir -p $(@D)
	$(CC_ARM) $(CFLAGS_ARM) -c $< -o $@

$(BUILD_DIR)/arm/common/%.o: $(COMMON_DIR)/%.c
	mkdir -p $(@D)
	$(CC_ARM) $(CFLAGS_ARM) -c $< -o $@

arm64: $(ARM64_BIN)

$(ARM64_BIN): $(ARM64_ELF)
	$(OBJCOPY_ARM64) -O binary $< $@

$(ARM64_ELF): $(ARM64_OBJ)
	$(LD_ARM64) $(LDFLAGS_ARM64) -o $@ $^ $(LIBS_ARM64)

$(BUILD_DIR)/arm64/%.o: $(SRC_DIR)/%.c
	mkdir -p $(@D)
	$(CC_ARM64) $(CFLAGS_ARM64) -c $< -o $@

$(BUILD_DIR)/arm64/common/%.o: $(COMMON_DIR)/%.c
	mkdir -p $(@D)
	$(CC_ARM64) $(CFLAGS_ARM64) -c $< -o $@

disasm: $(ARM_ELF) $(ARM64_ELF)
	$(CROSS_COMPILE)objdump -d $(ARM_ELF) > $(BUILD_DIR)/arm/disasm.txt
	$(CROSS_COMPILE_64)objdump -d $(ARM64_ELF) > $(BUILD_DIR)/arm64/disasm.txt
	@echo "Disassembly written to build/arm/disasm.txt and build/arm64/disasm.txt"

clean:
	rm -rf $(BUILD_DIR)
