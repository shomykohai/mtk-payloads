name      := da_xml
arches    := arm arm64
binsuffix := _$(ARCH)
opt       := -O2
ldscript  := da_xml/src/arch/$(ARCH)/generic.ld

ldflags-y += -lgcc

ifeq ($(ARCH),arm)
archflags := -marm -mcpu=cortex-a53 -mno-unaligned-access
endif

cflags-da_xml/src/main.c                += -O3
cflags-da_xml/src/commands.c            += -O3
cflags-da_xml/src/protocol_functions.c  += -O3

srcs-y := \
	da_xml/src/main.c \
	da_xml/src/commands.c \
	da_xml/src/protocol_functions.c \
	$(srcs-y)
