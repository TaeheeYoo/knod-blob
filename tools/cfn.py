#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Turn clang's assembly for csrc/ into bodies the routines are made of.

For each function cfn_<name>, CFN_BODY_<name>: its instructions under the
label .Lcfn_<name>, with its local labels its own and no symbol or metadata,
for src/ to put where it wants it.

The functions are called by the AMDGPU calling convention, which keeps s34 and
up for the caller, and knod_blob.h has a function keep off the VGPRs the JIT
holds from KNOD_BLOB_JIT_VREG up.  Refuses one that touches those, or that
needs more stack than a call gets.
"""
import re
import sys

HEADER = "include/uapi/linux/knod_blob.h"


def contract():
    text = open(HEADER).read()

    def val(name):
        return int(re.search(rf"^#define {name}\s+(\S+)", text, re.M)[1], 0)

    return {n: val("KNOD_BLOB_" + n) for n in ("CALL_STACK_BYTES", "JIT_VREG")}


REG = re.compile(r"\b([sv])(?:\[(\d+):(\d+)\]|(\d+)\b)")


def vgprs(lines):
    v = set()
    for line in lines:
        for kind, lo, hi, one in REG.findall(line):
            if kind == "v":
                v.update(range(int(lo), int(hi) + 1) if lo else [int(one)])
    return v


def emit(c, name, body, stack, out):
    if stack > c["CALL_STACK_BYTES"]:
        sys.exit(f"{name}: needs {stack} bytes of stack, a call gets "
                 f"{c['CALL_STACK_BYTES']}")
    top = max(vgprs(body), default=0)
    if top >= c["JIT_VREG"]:
        sys.exit(f"{name}: touches v{top}, which the JIT holds")
    short = name[len("cfn_"):]
    # Local labels are numbered per file; every function gets its own.
    body = [re.sub(r"\.L([\w$.]+)", rf".Lcfn_{short}_\1", line)
            for line in body]
    out.append(f".macro CFN_BODY_{short}")
    out.append(f".Lcfn_{short}:")
    out += body
    out.append(".endm")


def main():
    out_path, srcs = sys.argv[1], sys.argv[2:]
    c = contract()
    text = "\n".join(open(src).read() for src in srcs)
    funcs, name, body, last = [], None, [], None
    for line in text.splitlines():
        # clang reports a function's stack in a comment after its end.
        m = re.match(r"^; ScratchSize: (\d+)", line)
        if m and last:
            funcs.append((last[0], last[1], int(m.group(1))))
            last = None
        code = line.split(";", 1)[0].rstrip()
        m = re.match(r"^(cfn_\w+):", code)
        if m:
            name, body = m.group(1), []
            continue
        if name is None:
            continue
        if re.match(r"^\.Lfunc_end\d+:", code):
            last, name = (name, body), None
            continue
        if code.strip():
            body.append(code)
    out = []
    for name, body, stack in funcs:
        emit(c, name, body, stack, out)
    open(out_path, "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
