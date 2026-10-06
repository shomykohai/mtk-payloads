name      := hakujoudai
arches    := arm arm64
binsuffix := _$(ARCH)
opt       := -Os
ldscript  := hakujoudai/src/linker$(if $(filter arm64,$(ARCH)),64,32).x

incdirs := -Ihakujoudai/src/include

cflags-y += -fvisibility=hidden -Wno-main -DXML_STACK_SIZE=512

ifeq ($(DEBUG),1)
cflags-y += -DDEBUG
endif

ldcflags := n

asflags_full := y

ifeq ($(ARCH),arm)
archflags := -march=armv7-a -marm -fpic -mpic-data-is-text-relative
endif
ifeq ($(ARCH),arm64)
archflags := -march=armv8-a -mcmodel=tiny -fno-pic -fno-pie -fno-plt
ldflags-y := $(filter-out -lgcc,$(ldflags-y))
endif

cflags-common/xml.c  :=
cflags-common/yxml.c :=

srcs-y := \
	$(if $(filter arm,$(ARCH)),hakujoudai/src/entry.S) \
	hakujoudai/src/heap.c \
	hakujoudai/src/main.c \
	hakujoudai/src/commands.c \
	$(srcs-y)
