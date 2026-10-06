.PHONY: all arm arm64 clean disasm
all:
	+$(MAKE) -C .. hakujoudai
arm:
	+$(MAKE) -C .. hakujoudai@arm
arm64:
	+$(MAKE) -C .. hakujoudai@arm64
clean:
	+$(MAKE) -C .. hakujoudai-clean

OBJDUMP_arm   ?= $(or $(CROSS_COMPILE_arm),arm-none-eabi-)objdump
OBJDUMP_arm64 ?= $(or $(CROSS_COMPILE_arm64),aarch64-none-linux-gnu-)objdump

disasm: all
	$(OBJDUMP_arm) -d ../out/hakujoudai/arm/hakujoudai_arm.elf > ../out/hakujoudai/arm/disasm.txt
	$(OBJDUMP_arm64) -d ../out/hakujoudai/arm64/hakujoudai_arm64.elf > ../out/hakujoudai/arm64/disasm.txt
	@echo "Disassembly written to out/hakujoudai/{arm,arm64}/disasm.txt"
