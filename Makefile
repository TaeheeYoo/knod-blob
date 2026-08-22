# SPDX-License-Identifier: GPL-2.0-or-later
#
# Assembles the map routines once per ISA and packs each into the blob the
# knod BPF JIT loads.  Install the results as /lib/firmware/knod/<name>.

LLVM_MC		?= llvm-mc
LLVM_OBJCOPY	?= llvm-objcopy
# Not CPP: make defines that as $(CC) -E, which cannot preprocess assembly.
ASM_CPP		?= clang -x assembler-with-cpp -E

# One container per feature, because the core has to bring up a queue before
# any feature module is loaded and so cannot read the BPF JIT's blob.
FEATURES	:= core bpf ipsec
SRC_core	:= src/default.S
SRC_ipsec	:= src/ipsec.S
SRC_bpf		:= $(filter-out $(SRC_core) $(SRC_ipsec),$(wildcard src/*.S))

ABI_HDR		:= include/uapi/linux/knod_blob.h
DEPS		:= $(wildcard src/*.S) src/common.inc $(ABI_HDR)
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

BLOBS		:= $(foreach f,$(FEATURES),\
		     $(foreach i,$(ISAS),$(BUILD)/knod-$(f)-gfx$(i).bin))

all: $(BLOBS)

# One set of rules per feature and ISA.  A pattern rule cannot express this
# because the cpu and attributes are looked up by the ISA number, not the stem.
define isa_rules
$(BUILD)/$(1).$(2).s: $(DEPS) | $(BUILD)
	cat $(SRC_$(1)) > $(BUILD)/$(1).$(2).cat.S
	$(ASM_CPP) -Isrc -Iinclude/uapi -Werror=undef \
		-DKNOD_BLOB_LINK=KNOD_BLOB_LINK_SPLICE -D__ASSEMBLY__ \
		-DKNOD_ISA=$(2) $(BUILD)/$(1).$(2).cat.S -o $$@

$(BUILD)/$(1).$(2).o: $(BUILD)/$(1).$(2).s
	$(LLVM_MC) -triple=amdgcn-amd-amdhsa -mcpu=$(CPU_$(2)) $(ATTR_$(2)) \
		-filetype=obj $$< -o $$@

$(BUILD)/$(1).$(2).text: $(BUILD)/$(1).$(2).o
	$(LLVM_OBJCOPY) -O binary --only-section=.text $$< $$@

$(BUILD)/knod-$(1)-gfx$(2).bin: $(BUILD)/$(1).$(2).text $(BUILD)/$(1).$(2).o \
				tools/pack.py $(ABI_HDR)
	python3 tools/pack.py --isa $(2) --wave 64 \
		--text $$< --obj $(BUILD)/$(1).$(2).o -o $$@

dis-$(1)-$(2): $(BUILD)/$(1).$(2).o
	llvm-objdump -d --mcpu=$(CPU_$(2)) $(ATTR_$(2)) $$<
.PHONY: dis-$(1)-$(2)
endef

$(foreach f,$(FEATURES),\
  $(foreach i,$(ISAS),$(eval $(call isa_rules,$(f),$(i)))))

$(BUILD):
	mkdir -p $@

install: $(BLOBS)
	install -d $(DESTDIR)$(FIRMWARE_DIR)
	install -m 0644 $(BLOBS) $(DESTDIR)$(FIRMWARE_DIR)

clean:
	rm -rf $(BUILD)

.PHONY: all install clean
