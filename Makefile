# SPDX-License-Identifier: GPL-2.0-or-later
#
# Assembles the map routines once per ISA and packs each into the blob the
# knod BPF JIT loads.  Install the results as /lib/firmware/knod/<name>.

LLVM_MC		?= llvm-mc
LLVM_OBJCOPY	?= llvm-objcopy
# Not CPP: make defines that as $(CC) -E, which cannot preprocess assembly.
ASM_CPP		?= clang -x assembler-with-cpp -E

SRC		:= $(wildcard src/*.S)
ABI_HDR		:= include/uapi/linux/knod_blob.h
DEPS		:= $(SRC) src/common.inc $(ABI_HDR)
BUILD		:= build
FIRMWARE_DIR	?= /lib/firmware/knod

# gfx10 and later default to wave32 and the JIT runs wave64, so they have to
# be told; gfx9 has no such switch.
ISAS		:= 9 10 11
CPU_9		:= gfx900
CPU_10		:= gfx1030
CPU_11		:= gfx1100
ATTR_9		:=
ATTR_10		:= --mattr=+wavefrontsize64
ATTR_11		:= --mattr=+wavefrontsize64

BLOBS		:= $(foreach i,$(ISAS),$(BUILD)/knod-bpf-gfx$(i).bin)

all: $(BLOBS)

# One set of rules per ISA.  A pattern rule cannot express this because the
# cpu and attributes are looked up by the ISA number, not the stem.
define isa_rules
$(BUILD)/all.$(1).s: $(DEPS) | $(BUILD)
	cat $(SRC) > $(BUILD)/all.$(1).cat.S
	$(ASM_CPP) -Isrc -Iinclude/uapi -DKNOD_BLOB_LINK=0 -D__ASSEMBLY__ \
		-DKNOD_ISA=$(1) $(BUILD)/all.$(1).cat.S -o $$@

$(BUILD)/all.$(1).o: $(BUILD)/all.$(1).s
	$(LLVM_MC) -triple=amdgcn-amd-amdhsa -mcpu=$(CPU_$(1)) $(ATTR_$(1)) \
		-filetype=obj $$< -o $$@

$(BUILD)/all.$(1).text: $(BUILD)/all.$(1).o
	$(LLVM_OBJCOPY) -O binary --only-section=.text $$< $$@

$(BUILD)/knod-bpf-gfx$(1).bin: $(BUILD)/all.$(1).text $(BUILD)/all.$(1).o \
			       tools/pack.py $(ABI_HDR)
	python3 tools/pack.py --isa $(1) --wave 64 \
		--text $$< --obj $(BUILD)/all.$(1).o -o $$@

dis-$(1): $(BUILD)/all.$(1).o
	llvm-objdump -d --mcpu=$(CPU_$(1)) $(ATTR_$(1)) $$<
.PHONY: dis-$(1)
endef

$(foreach i,$(ISAS),$(eval $(call isa_rules,$(i))))

$(BUILD):
	mkdir -p $@

install: $(BLOBS)
	install -d $(DESTDIR)$(FIRMWARE_DIR)
	install -m 0644 $(BLOBS) $(DESTDIR)$(FIRMWARE_DIR)

clean:
	rm -rf $(BUILD)

.PHONY: all install clean
