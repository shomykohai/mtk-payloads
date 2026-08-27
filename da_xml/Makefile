CROSS_COMPILE    ?= arm-none-eabi-
CROSS_COMPILE_64 ?= aarch64-none-linux-gnu-

CC_ARM      := $(CROSS_COMPILE)gcc
OBJCOPY_ARM := $(CROSS_COMPILE)objcopy
SIZE_ARM    := $(CROSS_COMPILE)size

CC_ARM64      := $(CROSS_COMPILE_64)gcc
OBJCOPY_ARM64 := $(CROSS_COMPILE_64)objcopy
SIZE_ARM64    := $(CROSS_COMPILE_64)size

FEATURES   := uart xml crypto rpmb mmc mmc_rpmb ufs_rpmb crypto_ssr
COMMON_DIR := ../common


OUTPUT_DIR := ../payloads
BUILD_DIR  := build

DA_XML_SRCS := \
	src/main.c \
	src/commands.c \
	src/protocol_functions.c

O3_SRCS := $(DA_XML_SRCS)

include $(COMMON_DIR)/common.mk

ALL_SRCS := $(DA_XML_SRCS) $(COMMON_SRCS)

CFLAGS := $(COMMON_CFLAGS) \
	-Iinclude \
	-Ilib \
	$(COMMON_INCLUDES)

LDFLAGS := $(COMMON_LDFLAGS) -lgcc

CFLAGS_ARM   := $(CFLAGS) -DARM -marm -mcpu=cortex-a9 -mno-unaligned-access
CFLAGS_ARM64 := $(CFLAGS) -DAARCH64 -march=armv8-a -mgeneral-regs-only -mno-outline-atomics -mstrict-align -fno-stack-protector -fno-lto -ffreestanding -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0

LDFLAGS_ARM   := $(LDFLAGS) -Tsrc/arch/arm/generic.ld
LDFLAGS_ARM64 := $(LDFLAGS) -no-pie -Tsrc/arch/arm64/generic.ld

arm_obj   = $(patsubst %.c,$(BUILD_DIR)/arm/%.o,$(subst ../,,$(1)))
arm64_obj = $(patsubst %.c,$(BUILD_DIR)/aarch64/%.o,$(subst ../,,$(1)))

ARM_OBJS   := $(call arm_obj,$(filter-out $(CRYPTO_SSR_SRCS),$(ALL_SRCS)))
ARM64_OBJS := $(call arm64_obj,$(ALL_SRCS))

ARM_ELF   := $(BUILD_DIR)/arm/da_xml_arm.elf
ARM_BIN   := $(OUTPUT_DIR)/da_xml_arm.bin

ARM64_ELF := $(BUILD_DIR)/aarch64/da_xml_arm64.elf
ARM64_BIN := $(OUTPUT_DIR)/da_xml_arm64.bin

.PHONY: all arm aarch64 clean

all: arm aarch64

arm: $(ARM_BIN)
aarch64: $(ARM64_BIN)

$(ARM_BIN): $(ARM_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY_ARM) -O binary $< $@
	$(SIZE_ARM) $<
	@echo "Built: $@"

$(ARM_ELF): $(ARM_OBJS)
	@mkdir -p $(dir $@)
	$(CC_ARM) $(CFLAGS_ARM) $^ -o $@ $(LDFLAGS_ARM)

$(BUILD_DIR)/arm/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC_ARM) $(CFLAGS_ARM) $(EXTRA_CFLAGS) -c $< -o $@

$(ARM64_BIN): $(ARM64_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY_ARM64) -O binary $< $@
	$(SIZE_ARM64) $<
	@echo "Built: $@"

$(ARM64_ELF): $(ARM64_OBJS)
	@mkdir -p $(dir $@)
	$(CC_ARM64) $(CFLAGS_ARM64) $^ -o $@ $(LDFLAGS_ARM64)

$(BUILD_DIR)/aarch64/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC_ARM64) $(CFLAGS_ARM64) $(EXTRA_CFLAGS) -c $< -o $@


$(eval $(call common_build_rule,$(BUILD_DIR)/arm,$(CC_ARM),$(CFLAGS_ARM)))
$(eval $(call common_build_rule,$(BUILD_DIR)/aarch64,$(CC_ARM64),$(CFLAGS_ARM64)))


clean:
	rm -rf $(BUILD_DIR)
	rm -f $(ARM_BIN) $(ARM64_BIN)
