# SPDX-License-Identifier: GPL-2.0-or-later
#
# Assembles the map routines once per ISA and packs each into the blob the
# knod BPF JIT loads.  Install the results as /lib/firmware/knod/<name>.

LLVM_MC		?= llvm-mc
LLVM_OBJCOPY	?= llvm-objcopy
LD_LLD		?= ld.lld
CLANG		?= clang
# Not CPP: make defines that as $(CC) -E, which cannot preprocess assembly.
ASM_CPP		?= clang -x assembler-with-cpp -E

# One container per feature, because the core has to bring up a queue before
# any feature module is loaded and so cannot read the BPF JIT's blob.
FEATURES	:= core
# The core's: what a queue comes up with, and the GDA engine's receive kernel,
# which runs the NIC's rings with no feature's code in them.
SRC_core	:= src/default.S src/gda_rx.S
SRC_bpf-persistent := $(filter-out $(SRC_core),$(wildcard src/*.S))

ABI_HDR		:= include/uapi/linux/knod_blob.h
BUILD		:= build
# The preprocessor flags decide what goes in - a probe, or no probe - and they
# are in no file, so nothing about the sources says a build made with one is
# not the build being asked for with another.  Keep them in a stamp every
# object depends on, and changing them becomes a reason to build again.
FLAGS_STAMP	:= $(BUILD)/.cppflags
# What the last build used, when nothing is given.  A configuration set once
# then survives the `make install` that follows, which would otherwise build
# with no flags and install the opposite of what was asked for.  Give EXTRA_CPPFLAGS on the command line to change it, empty to clear.
EXTRA_CPPFLAGS	?= $(shell cat $(FLAGS_STAMP) 2>/dev/null)
# Every source, not just the ones that name themselves .S: the bodies live in
# .inc files that the .S files include, and leaving them out meant editing a
# prologue or an epilogue built nothing.
DEPS		:= $(wildcard src/*.S) $(wildcard src/*.inc) src/gda/gda_lanes.h \
		   $(ABI_HDR) $(FLAGS_STAMP) include/uapi/linux/knod_persistent.h

# The GDA engine is C (src/gda), compiled once for each routine that carries
# it, into that routine's own section; see src/gda.inc.  Which routines each
# feature has, and which of the engine's entry points each one jumps to:
GDA_C		:= src/gda/gda.c
GDA_C_DEPS	:= $(wildcard src/gda/*.c src/gda/*.h) src/gda/shim/linux/types.h \
		   $(ABI_HDR) include/uapi/linux/knod_persistent.h \
		   tools/check_c.py $(FLAGS_STAMP)
C_ROUTINES_core			:= knod_gda_rx_kernel
C_ROUTINES_bpf-persistent	:= knod_gda_prologue knod_gda_epilogue
C_ENTRIES_knod_gda_rx_kernel	:= START ROUND FINISH
C_ENTRIES_knod_gda_prologue	:= START ROUND
C_ENTRIES_knod_gda_epilogue	:= FINISH
# Wave64 and CU mode as the shader runs; nothing from a GPU library or the
# compiler's own runtime, since there is nothing to link them from.
GDA_CFLAGS	:= -O2 -mwavefrontsize64 -mcumode -nogpulib -ffreestanding \
		   -fno-builtin -fno-stack-protector -Wall -Wextra -Werror \
		   -Isrc/gda/shim -Iinclude/uapi
FIRMWARE_DIR	?= /lib/firmware/knod

# Persistent-shader KNOD supports RDNA generations in Wave64 mode.
ISAS		:= 10 11
CPU_10		:= gfx1030
CPU_11		:= gfx1100
ATTR_10		:= --mattr=+wavefrontsize64
ATTR_11		:= --mattr=+wavefrontsize64

BLOBS		:= $(foreach f,$(FEATURES),\
		     $(foreach i,$(ISAS),$(BUILD)/knod-$(f)-gfx$(i).bin)) \
		     $(foreach i,$(ISAS),$(BUILD)/knod-bpf-persistent-gfx$(i).bin)

all: $(BLOBS)

# Rewritten only when it would change, so an unchanged flag set is not itself
# a reason to rebuild.
.PHONY: FORCE
$(FLAGS_STAMP): FORCE | $(BUILD)
	@printf '%s' "$(EXTRA_CPPFLAGS)" > $@.new
	@cmp -s $@.new $@ || { mv $@.new $@; echo "flags: [$(EXTRA_CPPFLAGS)]"; }
	@rm -f $@.new

# One set of rules per feature and ISA.  A pattern rule cannot express this
# because the cpu and attributes are looked up by the ISA number, not the stem.
define isa_rules
$(BUILD)/$(1).$(2).s: $(DEPS) | $(BUILD)
	cat $(SRC_$(1)) > $(BUILD)/$(1).$(2).cat.S
	$(ASM_CPP) -Isrc -Iinclude/uapi -Werror=undef -Werror=macro-redefined \
		$(EXTRA_CPPFLAGS) \
		-DKNOD_BLOB_LINK=KNOD_BLOB_LINK_SPLICE -D__ASSEMBLY__ \
		-DKNOD_ISA=$(2) $(BUILD)/$(1).$(2).cat.S -o $$@

$(BUILD)/$(1).$(2).o: $(BUILD)/$(1).$(2).s
	$(LLVM_MC) -triple=amdgcn-amd-amdhsa -mcpu=$(CPU_$(2)) $(ATTR_$(2)) \
		-filetype=obj $$< -o $$@

# The routines and the C they carry, as one .text in which every routine is
# one piece: tools/knod.ld puts each routine's C between its glue.  Nothing may
# be left for a loader to fix up, since the JIT copies the bytes as they are.
$(BUILD)/$(1).$(2).elf: $(BUILD)/$(1).$(2).o \
		$(foreach r,$(C_ROUTINES_$(1)),$(BUILD)/$(r).$(2).c.o) tools/knod.ld
	$(LD_LLD) -shared --no-undefined -T tools/knod.ld -o $$@ \
		$(BUILD)/$(1).$(2).o \
		$(foreach r,$(C_ROUTINES_$(1)),$(BUILD)/$(r).$(2).c.o)
	@if llvm-readelf -r $$@ | grep -q "Relocation section"; then \
		echo "$$@: relocations left for a loader" >&2; \
		llvm-readelf -r $$@ >&2; rm -f $$@; exit 1; fi

$(BUILD)/$(1).$(2).text: $(BUILD)/$(1).$(2).elf
	$(LLVM_OBJCOPY) -O binary --only-section=.text $$< $$@

$(BUILD)/knod-$(1)-gfx$(2).bin: $(BUILD)/$(1).$(2).text $(BUILD)/$(1).$(2).elf \
				tools/pack.py $(ABI_HDR)
	python3 tools/pack.py --isa $(2) --wave 64 --persistent-shader \
		--text $$< --obj $(BUILD)/$(1).$(2).elf -o $$@

dis-$(1)-$(2): $(BUILD)/$(1).$(2).elf
	llvm-objdump -d --mcpu=$(CPU_$(2)) $(ATTR_$(2)) $$<
.PHONY: dis-$(1)-$(2)
endef

# The GDA engine, built for one routine: checked as clang wrote it, then
# assembled.
define c_rules
$(BUILD)/$(1).$(2).s: $(GDA_C_DEPS) | $(BUILD)
	$(CLANG) --target=amdgcn-amd-amdhsa -mcpu=$(CPU_$(2)) $(GDA_CFLAGS) \
		$(EXTRA_CPPFLAGS) -DKNOD_ISA=$(2) -DKNOD_GDA_ROUTINE=$(1) \
		$(foreach e,$(C_ENTRIES_$(1)),-DKNOD_GDA_WANT_$(e)) \
		-S $(GDA_C) -o $$@.new
	python3 tools/check_c.py --abi $(ABI_HDR) $$@.new
	mv $$@.new $$@

$(BUILD)/$(1).$(2).c.o: $(BUILD)/$(1).$(2).s
	$(CLANG) --target=amdgcn-amd-amdhsa -mcpu=$(CPU_$(2)) -mwavefrontsize64 \
		-c $$< -o $$@
endef

$(foreach f,$(FEATURES),\
  $(foreach i,$(ISAS),$(eval $(call isa_rules,$(f),$(i)))))
$(foreach r,$(C_ROUTINES_core) $(C_ROUTINES_bpf-persistent),\
  $(foreach i,$(ISAS),$(eval $(call c_rules,$(r),$(i)))))
$(foreach i,$(ISAS),$(eval $(call isa_rules,bpf-persistent,$(i))))

$(BUILD):
	mkdir -p $@

install: $(BLOBS)
	@echo "install: flags [$(EXTRA_CPPFLAGS)]"
	install -d $(DESTDIR)$(FIRMWARE_DIR)
	install -m 0644 $(BLOBS) $(DESTDIR)$(FIRMWARE_DIR)

uninstall:
	rm -f $(addprefix $(DESTDIR)$(FIRMWARE_DIR)/,$(notdir $(BLOBS)))

clean:
	rm -rf $(BUILD)

# Back to a blob with nothing extra in it.
plain:
	$(MAKE) EXTRA_CPPFLAGS=

.PHONY: all install uninstall clean plain
