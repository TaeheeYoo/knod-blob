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
BUILD		:= build
# The preprocessor flags decide what goes in - a probe, or no probe - and they
# are in no file, so nothing about the sources says a build made with one is
# not the build being asked for with another.  Keep them in a stamp every
# object depends on, and changing them becomes a reason to build again.
FLAGS_STAMP	:= $(BUILD)/.cppflags
DEPS		:= $(wildcard src/*.S) src/common.inc $(ABI_HDR) $(FLAGS_STAMP)
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

# Rebuild with the cycle probe, which has every wave record what the prologue,
# the program and the epilogue each took, in shader clocks, in the half of its
# ring slot spsc_bd leaves free.  Not the default: it is four register reads
# and two stores on the path it exists to measure.  From scratch, because the
# flag changes no file the build depends on, and checked, because a stale
# object left a probe out once and the missing field read as a real answer.
cycles:
	$(MAKE) EXTRA_CPPFLAGS='-DKNOD_CYCLE_PROBE'
	@grep -q s_getreg $(BUILD)/bpf.11.s || { \
		echo "cycles: probe missing from the build" >&2; exit 1; }
	@echo "cycles: probe present"

# Rebuild with one of the two stores every lane makes left out, to price a
# store on its own.  Wrong for any program that moves the packet - the host
# then reads the bounds the producer wrote - so this is for measuring and
# nothing else.
onestore:
	$(MAKE) EXTRA_CPPFLAGS='-DKNOD_NO_OFFLEN_STORE'
	@! grep -q 'offset:16 glc slc' $(BUILD)/bpf.11.s || { \
		echo "onestore: the store is still there" >&2; exit 1; }
	@echo "onestore: one store a lane"

.PHONY: all install clean cycles onestore
