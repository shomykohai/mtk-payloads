# Usage in case I forget:
#
#   COMMON_DIR := ../common
#   FEATURES   := uart
#   include $(COMMON_DIR)/common.mk
#
# Available features:
#   uart       - UART driver
#   crypto     - TZCC, SBROM, SHA256, HMAC-SHA256...
#   crypto_ssr - SSR key derivation, PKA, CCC (requires crypto)
#   rpmb       - RPMB
#   mmc        - MMC driver
#   mmc_rpmb   - MMC RPMB driver
#   ufs_rpmb   - UFS RPMB driver using DA-provided UFS helpers

COMMON_DIR ?= ../common

COMMON_SRCS := \
	$(COMMON_DIR)/debug.c \
	$(COMMON_DIR)/libc.c

O3_SRCS ?=

COMMON_INCLUDES := \
	-I$(COMMON_DIR)/include


ifneq ($(filter uart,$(FEATURES)),)
COMMON_SRCS += $(COMMON_DIR)/drivers/uart/uart.c
endif


CRYPTO_SRCS = \
	$(COMMON_DIR)/crypto/xor.c \
	$(COMMON_DIR)/crypto/sbrom/sbrom.c \
	$(COMMON_DIR)/crypto/tzcc.c \
	$(COMMON_DIR)/crypto/key_derive.c \
	$(COMMON_DIR)/crypto/hmac-sha256.c \
	$(COMMON_DIR)/crypto/sha256.c \
	$(COMMON_DIR)/crypto/sej/sej.c \
	$(COMMON_DIR)/crypto/sej/sej_hk.c \
	$(COMMON_DIR)/crypto/sej/sej_sk.c \
	$(COMMON_DIR)/drivers/sgpt/sgpt.c

CRYPTO_SSR_SRCS = \
	$(COMMON_DIR)/crypto/ssr/top.c \
	$(COMMON_DIR)/crypto/ssr/kdf.c \
	$(COMMON_DIR)/crypto/ssr/pka.c \
	$(COMMON_DIR)/crypto/ssr/ccc.c \
	$(COMMON_DIR)/crypto/ssr/ssr.c

RPMB_SRCS = \
	$(COMMON_DIR)/security/rpmb.c

MMC_SRCS = \
	$(COMMON_DIR)/storage/mmc/mmc.c

XML_SRCS = \
    $(COMMON_DIR)/xml.c \
    $(COMMON_DIR)/yxml.c

# Features

ifneq ($(filter crypto,$(FEATURES)),)
COMMON_SRCS   += $(CRYPTO_SRCS)
COMMON_CFLAGS += -Dcrypto
O3_SRCS       += \
    $(COMMON_DIR)/crypto/hmac-sha256.c \
    $(COMMON_DIR)/crypto/sha256.c
endif

ifneq ($(filter crypto_ssr,$(FEATURES)),)
ifeq ($(filter crypto,$(FEATURES)),)
$(error crypto_ssr requires crypto)
endif

COMMON_SRCS   += $(CRYPTO_SSR_SRCS)
COMMON_CFLAGS += -Dcrypto_ssr
endif

ifneq ($(filter mmc,$(FEATURES)),)
COMMON_SRCS   += $(MMC_SRCS)
COMMON_CFLAGS += -Dmmc
O3_SRCS       += $(MMC_SRCS)
endif

ifneq ($(filter rpmb,$(FEATURES)),)
COMMON_SRCS   += $(RPMB_SRCS)
COMMON_CFLAGS += -Drpmb
O3_SRCS       += $(RPMB_SRCS)
endif

ifneq ($(filter mmc_rpmb,$(FEATURES)),)
ifeq ($(filter mmc,$(FEATURES)),)
$(error mmc_rpmb requires mmc)
endif
ifeq ($(filter rpmb,$(FEATURES)),)
$(error mmc_rpmb requires rpmb)
endif

COMMON_SRCS   += $(COMMON_DIR)/storage/mmc/rpmb_mmc.c
COMMON_CFLAGS += -Dmmc_rpmb
O3_SRCS       += $(COMMON_DIR)/storage/mmc/rpmb_mmc.c
endif

ifneq ($(filter ufs_rpmb,$(FEATURES)),)
ifeq ($(filter rpmb,$(FEATURES)),)
$(error ufs_rpmb requires rpmb)
endif

COMMON_SRCS   += $(COMMON_DIR)/storage/ufs/rpmb_ufs.c
COMMON_CFLAGS += -Dufs_rpmb
O3_SRCS       += $(COMMON_DIR)/storage/ufs/rpmb_ufs.c
endif

ifneq ($(filter xml,$(FEATURES)),)
COMMON_SRCS   += $(XML_SRCS)
COMMON_CFLAGS += -Dxml_parser
O3_SRCS       += $(XML_SRCS)
endif

# Flags

COMMON_CFLAGS += \
	-std=gnu99 -O2 -Wall -Wextra \
	-fno-strict-aliasing \
	-fno-builtin \
	-flto \
	-fno-omit-frame-pointer \
	-fno-pic -fno-pie \
	-I$(COMMON_DIR)/include

COMMON_LDFLAGS ?= \
	-nodefaultlibs -nostdlib \
	-Wl,--build-id=none

ifneq ($(shell $(CC) -Wl,--help 2>/dev/null | grep -q -- --no-warn-rwx-segments && echo yes),)
COMMON_LDFLAGS += -Wl,--no-warn-rwx-segments
endif


# For some files we want to use -O3 for faster performance (mainly storage drivers
# and SW crypto functions)
$(foreach src,$(O3_SRCS),$(eval %$(subst ../,,$(src:.c=.o)): EXTRA_CFLAGS := -O3))

define common_build_rule
$(1)/common/%.o: $(COMMON_DIR)/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(3) $$(EXTRA_CFLAGS) -c $$< -o $$@
endef
