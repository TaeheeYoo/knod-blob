#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pack assembled routines into one blob per ISA.

Reads the object files the Makefile produced, takes each routine's offset and
size from the symbol table, and writes the header and entry table the kernel
loader expects.  The layout is described in include/knod_blob_abi.h.
"""

import argparse
import re
import struct
import subprocess
import sys

HEADER = "include/uapi/linux/knod_blob.h"

LINK_SPLICE = 0

HDR = "<8I"          # magic .. reserved
ENTRY = "<6I"        # kind .. reserved
HDR_SIZE = struct.calcsize(HDR)
ENTRY_SIZE = struct.calcsize(ENTRY)

def contract(path):
    """MAGIC, the ABI version and the routine kinds, as the header spells them.

    Copying them here instead would leave four numbers to keep in step by hand,
    and getting one wrong is not a build error on either side: the kernel takes
    a blob whose magic and version still match and splices whatever the entry
    says, so a kind that has shifted by one names a different routine.
    """
    text = open(path).read()

    def define(name):
        got = re.search(rf"^#define\s+{name}\s+(\S+)", text, re.M)
        if not got:
            sys.exit(f"{path}: no {name}")
        return int(got.group(1), 0)

    body = re.search(r"enum knod_blob_kind \{(.*?)\n\};", text, re.S)
    if not body:
        sys.exit(f"{path}: no enum knod_blob_kind")

    kinds, nxt = {}, 0
    for name, val in re.findall(r"^\s*(KNOD_BLOB_\w+)\s*(?:=\s*(\d+))?\s*,",
                                body.group(1), re.M):
        nxt = int(val) if val else nxt
        if not name.endswith("_MAX"):
            kinds[name[len("KNOD_BLOB_"):].lower()] = nxt
        nxt += 1

    return define("KNOD_BLOB_MAGIC"), define("KNOD_BLOB_ABI_VERSION"), kinds




END_PREFIX = "__end_"


def text_base(obj):
    """Where .text starts in obj: 0 in an object, wherever the linker put it."""
    out = subprocess.run(["llvm-objdump", "-h", obj],
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 4 and f[1] == ".text":
            return int(f[3], 16)
    sys.exit(f"{obj}: no .text")


def symbols(obj):
    """Return {name: (offset, size)} for every knod_ routine in obj.

    A routine with C in it (src/gda.inc) runs past its own code into the C
    the linker put after it, and marks where it really ends with
    __end_<routine>: that, not its .size, is what it covers.
    """
    out = subprocess.run(["llvm-nm", "--print-size", "--defined-only", obj],
                         capture_output=True, text=True, check=True).stdout
    # A symbol's offset in the code is its address less where .text is.
    base = text_base(obj)
    syms, ends = {}, {}
    for line in out.splitlines():
        f = line.split()
        # "<addr> <size> T <name>" - a symbol without a size has three fields.
        name = f[-1] if len(f) in (3, 4) else ""
        # An absolute symbol (knod_<routine>_xsave) is a number, not a place.
        absolute = name and f[-2] in "aA"
        addr = int(f[0], 16) - (0 if absolute else base) if name else 0
        size = int(f[1], 16) if len(f) == 4 else 0
        if name.startswith(END_PREFIX + "knod_"):
            ends[name[len(END_PREFIX):]] = addr
        elif name.startswith("knod_"):
            syms[name] = (addr, size)
    for name, end in ends.items():
        if name not in syms:
            sys.exit(f"{END_PREFIX}{name}: no routine {name}")
        start = syms[name][0]
        if end <= start:
            sys.exit(f"{name}: ends at {end:#x}, before it starts at {start:#x}")
        syms[name] = (start, end - start)
    return syms


def parse_name(name, kinds):
    """knod_lookup_hash_k3 -> (kind, key_chunks). k<N> is optional."""
    body = name[len("knod_"):]
    chunks = 0
    m = re.search(r"_k(\d+)$", body)
    if m:
        body, chunks = body[:m.start()], int(m.group(1))
    if body not in kinds:
        raise SystemExit(f"{name}: unknown routine kind '{body}'")
    return kinds[body], chunks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--isa", type=int, required=True)
    ap.add_argument("--wave", type=int, default=64)
    ap.add_argument("--text", required=True, help="flat .text of all routines")
    ap.add_argument("--obj", required=True, help="object to read symbols from")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--persistent-shader", action="store_true")
    args = ap.parse_args()
    protocol = 0
    if args.persistent_shader:
        if args.isa not in (10, 11) or args.wave != 64:
            sys.exit("persistent shader requires gfx10/gfx11 Wave64")
        with open("include/uapi/linux/knod_persistent.h") as source:
            protocol = int(re.search(r"^#define KNOD_PERSIST_VERSION (\S+)", source.read(), re.M)[1], 0)

    magic, abi, kinds = contract(HEADER)

    code = open(args.text, "rb").read()
    syms = symbols(args.obj)
    if not syms:
        raise SystemExit(f"{args.obj}: no knod_ routines found")

    entries = []
    for name, (off, size) in sorted(syms.items()):
        # knod_<routine>_xsave carries the EXEC-save count, not code.
        if name.endswith("_xsave"):
            continue
        if size == 0:
            raise SystemExit(f"{name}: zero size, is .size missing?")
        if size % 4:
            raise SystemExit(f"{name}: size {size} is not a multiple of 4")
        if off + size > len(code):
            raise SystemExit(f"{name}: {off:#x}+{size:#x} is past the "
                             f"{len(code):#x} bytes of code")
        kind, chunks = parse_name(name, kinds)
        # exec_save_pairs is not derivable from the object; the routines
        # declare it through a knod_<name>_xsave absolute symbol.
        pairs = syms.get(name + "_xsave", (0, 0))[0]
        entries.append((kind, chunks, off, size, pairs))

    code_off = HDR_SIZE + ENTRY_SIZE * len(entries)
    blob = struct.pack(HDR, magic, abi, args.isa, LINK_SPLICE,
                       args.wave, len(entries), HDR_SIZE, protocol)
    for kind, chunks, off, size, pairs in entries:
        blob += struct.pack(ENTRY, kind, chunks, code_off + off, size, pairs, 0)
    blob += code

    open(args.output, "wb").write(blob)
    print(f"{args.output}: isa gfx{args.isa}, {len(entries)} entries, "
          f"{len(blob)} bytes")
    for kind, chunks, off, size, pairs in entries:
        name = next(k for k, v in kinds.items() if v == kind)
        suffix = f" k{chunks}" if chunks else ""
        print(f"  {name}{suffix:<4} off={code_off + off:<6} size={size:<5} "
              f"xsave={pairs}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
