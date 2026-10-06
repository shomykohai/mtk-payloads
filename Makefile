OUTPUT_DIR ?= $(CURDIR)/payloads

Q     := $(if $(V),,@)
build := -f scripts/Makefile.build --no-print-directory OUTPUT_DIR=$(OUTPUT_DIR)

PAYLOADS := $(patsubst %/payload.mk,%,$(wildcard */payload.mk))

arches_of = $(shell $(MAKE) -s --no-print-directory -f scripts/Makefile.arch PAYLOAD=$(1))
TARGETS := $(foreach p,$(PAYLOADS),$(foreach a,$(call arches_of,$(p)),$(p)@$(a)))

unknown_targets := $(filter-out $(TARGETS),$(filter $(addsuffix @%,$(PAYLOADS)),$(MAKECMDGOALS)))
ifneq ($(unknown_targets),)
$(error no such target: $(unknown_targets) -- available: $(TARGETS))
endif

.PHONY: all clean list cdb $(PAYLOADS) $(TARGETS) $(PAYLOADS:%=%-clean) $(TARGETS:%=cdb-%)

all:
	+$(Q)$(MAKE) $(TARGETS)
	+$(Q)$(MAKE) compile_commands.json

$(foreach p,$(PAYLOADS),$(eval $(p): $(filter $(p)@%,$(TARGETS))))

$(TARGETS):
	@echo "=> $@"
	+$(Q)$(MAKE) $(build) PAYLOAD=$(firstword $(subst @, ,$@)) ARCH=$(lastword $(subst @, ,$@))

$(PAYLOADS:%=%-clean):
	rm -rf out/$(@:%-clean=%)

list:
	@printf '%s\n' $(TARGETS)

compile_commands.json: $(TARGETS:%=cdb-%)
	@{ printf '[\n'; cat $(foreach u,$(TARGETS),out/$(subst @,/,$(u))/cdb.frag) | sed '$$ s/,$$//'; printf ']\n'; } > $@.tmp
	@cmp -s $@.tmp $@ || { mv -f $@.tmp $@; echo "  CDB     $@"; }
	@rm -f $@.tmp

$(TARGETS:%=cdb-%):
	+$(Q)$(MAKE) $(build) PAYLOAD=$(firstword $(subst @, ,$(@:cdb-%=%))) ARCH=$(lastword $(subst @, ,$(@:cdb-%=%))) cdb

cdb: compile_commands.json

clean:
	rm -rf out compile_commands.json
