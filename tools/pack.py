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




def symbols(obj):
    """Return {name: (offset, size)} for every knod_ routine in obj."""
    out = subprocess.run(["llvm-nm", "--print-size", "--defined-only", obj],
                         capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        f = line.split()
        # "<addr> <size> T <name>" - a symbol without a size has three fields.
        if len(f) == 4 and f[3].startswith("knod_"):
            syms[f[3]] = (int(f[0], 16), int(f[1], 16))
        elif len(f) == 3 and f[2].startswith("knod_"):
            syms[f[2]] = (int(f[0], 16), 0)
    return syms


def parse_name(name, kinds):
    """knod_lookup_hash_k3 -> (kind, key_chunks). k<N> is optional."""
    body = name[len("knod_"):]
    chunks = 0
    if "_k" in body:
        body, _, n = body.rpartition("_k")
        chunks = int(n)
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
    args = ap.parse_args()

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
        kind, chunks = parse_name(name, kinds)
        # exec_save_pairs is not derivable from the object; the routines
        # declare it through a knod_<name>_xsave absolute symbol.
        pairs = syms.get(name + "_xsave", (0, 0))[0]
        entries.append((kind, chunks, off, size, pairs))

    code_off = HDR_SIZE + ENTRY_SIZE * len(entries)
    blob = struct.pack(HDR, magic, abi, args.isa, LINK_SPLICE,
                       args.wave, len(entries), HDR_SIZE, 0)
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
