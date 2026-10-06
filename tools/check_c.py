#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check what clang made of the C in a blob before it goes in.

The C runs inside a shader built around a register contract it knows nothing
of, so the compiler's output is held to it here rather than trusted:

  - no scratch: the shader has none (flat_scratch_init is off);
  - no more VGPRs than lie below KNOD_BLOB_PRO_GDA_VREG, where the engine's
    state lives across the program;
  - each entry point reads its arguments before anything else touches a
    register (the empty asm at the top of each, see src/gda/gda.c);
  - nothing it calls or reads outside its own text.
"""

import argparse
import re
import sys


def define(header, name):
    got = re.search(rf"^#define\s+{name}\s+(\d+)", open(header).read(), re.M)
    if not got:
        sys.exit(f"{header}: no {name}")
    return int(got.group(1))


# The glue leaves an entry point's arguments in v0 upwards; none takes more.
ARG_VGPRS = 16


def clobbers_args(operand):
    got = re.fullmatch(r"v(\d+)|v\[(\d+):(\d+)\]", operand)
    if not got:
        return False
    lo = int(got.group(1) or got.group(2))
    return lo < ARG_VGPRS


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--abi", required=True, help="include/uapi/linux/knod_blob.h")
    ap.add_argument("asm", help="clang -S output")
    args = ap.parse_args()

    max_vgpr = define(args.abi, "KNOD_BLOB_PRO_GDA_VREG")
    text = open(args.asm).read()
    bad = []

    funcs = re.findall(r"^(\w+):\s+; @\1\n(.*?)^; (?:Function|Kernel) info:\n"
                       r"(.*?)^; MemoryBound", text, re.M | re.S)
    if not funcs:
        sys.exit(f"{args.asm}: no functions")
    for name, body, info in funcs:
        vgprs = int(re.search(r"NumVgprs: (\d+)", info).group(1))
        scratch = int(re.search(r"ScratchSize: (\d+)", info).group(1))
        lds = re.search(r"^; LDSByteSize: (\d+)", text[text.index(info):], re.M)
        if scratch:
            bad.append(f"{name}: {scratch} bytes of scratch")
        if lds and int(lds.group(1)):
            bad.append(f"{name}: {lds.group(1)} bytes of LDS of its own")
        if vgprs > max_vgpr:
            bad.append(f"{name}: {vgprs} VGPRs, more than the {max_vgpr} "
                       f"below v[KNOD_BLOB_PRO_GDA_VREG]")
        code = [l.strip() for l in body.splitlines()
                if l.strip() and (not l.strip().startswith(";")
                                  or l.strip() == ";;#ASMSTART")]
        if ";;#ASMSTART" not in code:
            bad.append(f"{name}: never takes its arguments")
        else:
            # The compiler may move work that depends on nothing ahead of
            # the asm that takes the arguments, and as far as it knows the
            # registers they are in are free until then.
            # Hold anything ahead of it to touching no VGPR an argument
            # could be in and not exec, whatever the operand's role.
            for insn in code[:code.index(";;#ASMSTART")]:
                ops = re.split(r"[\s,]+", insn)[1:]
                if (any(clobbers_args(o) for o in ops) or "exec" in insn
                        or "cmpx" in insn or "saveexec" in insn):
                    bad.append(f"{name}: '{insn}' comes before it takes "
                               f"its arguments, and may overwrite one")
        for reg in re.findall(r"\bv\[?(\d+)", body):
            if int(reg) >= max_vgpr:
                bad.append(f"{name}: names v{reg}, the engine's own")
                break
        for insn in ("s_swappc", "s_call", "scratch_", "buffer_store",
                     "buffer_load", "flat_"):
            if re.search(rf"^\s+{insn}", body, re.M):
                bad.append(f"{name}: uses {insn}")
        if re.search(r"@(abs32|abs64|gotpcrel|rel32)", body):
            bad.append(f"{name}: refers to a symbol")

    for b in bad:
        print(f"{args.asm}: {b}", file=sys.stderr)
    if bad:
        return 1
    for name, _, info in funcs:
        vgprs = re.search(r"NumVgprs: (\d+)", info).group(1)
        sgprs = re.search(r"NumSgprs: (\d+)", info).group(1)
        size = re.search(r"codeLenInByte = (\d+)", info).group(1)
        print(f"  {name}: {size} bytes, {vgprs} VGPRs, {sgprs} SGPRs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
