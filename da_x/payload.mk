name     := da_x
arches   := arm
opt      := -O2
ldscript := da_x/src/generic.ld

cflags-y += -fno-stack-protector -fPIE

asflags :=

srcs-y := \
	da_x/src/commands.c \
	da_x/src/main.c \
	da_x/src/protocol.c \
	$(srcs-y) \
	da_x/src/start.S
