#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Turn clang's assembly for csrc/ into macros a routine calls through.

For each function cfn_<name>:

  CFN_BODY_<name>	its instructions under the label .Lcfn_<name>, with no
			symbol or metadata, to be placed inside the routine that
			calls it so that the JIT splices both
  CFN_SAVE_<name>	keep what the call destroys that the JIT still needs,
  CFN_RESTORE_<name>	and put it back
  CFN_TMP_<name>	a scalar pair the save has kept, free to hold the
			call's target

The callee keeps s34 and up itself, as the calling convention has it.  Below
that the save keeps every scalar the callee touches, and s32, which the routine
points at the call's stack: in s34-s49, which a routine may destroy, then in
lanes of the first call-save register.  Of the vectors it keeps the BPF
registers the callee touches, other than r0, in the call-save registers after
it; the window is the routine's to lose.

Refuses a function that needs more stack than the routine gives it, or that
touches a vector register past the window.

The engine's functions, cfn_gda_*, run between programs with nothing live
but their arguments, so they get only the body, and may use any register the
wave has.
"""
import re
import sys

HEADER = "include/uapi/linux/knod_blob.h"


def contract():
    text = open(HEADER).read()

    def val(name):
        return int(re.search(rf"^#define {name}\s+(\S+)", text, re.M)[1], 0)

    return {n: val("KNOD_BLOB_" + n) for n in
            ("CALL_STACK_BYTES", "CALL_SAVE_VREG", "CALL_SAVE_VREGS",
             "SPLICE_TMP_VREG", "SPLICE_TMP_VREG_END", "EXEC_SAVE_SREG",
             "SPLICE_TMP_SREG_END")}


REG = re.compile(r"\b([sv])(?:\[(\d+):(\d+)\]|(\d+)\b)")


def regs(lines):
    s, v = set(), set()
    for line in lines:
        for kind, lo, hi, one in REG.findall(line):
            r = range(int(lo), int(hi) + 1) if lo else [int(one)]
            (s if kind == "s" else v).update(r)
    return s, v


def emit(c, name, body, stack, out):
    if stack > c["CALL_STACK_BYTES"]:
        sys.exit(f"{name}: needs {stack} bytes of stack, the routine gives "
                 f"{c['CALL_STACK_BYTES']}")
    sgprs, vgprs = regs(body)
    short = name[len("cfn_"):]
    body = [re.sub(r"\.L([\w$.]+)", rf".Lcfn_{short}_\1", line)
            for line in body]
    if short.startswith("gda_"):
        # The engine's: called between programs, when nothing but what it
        # is handed is live, so it keeps nothing.  It has the wave's VGPRs.
        if max(vgprs, default=0) >= c["CALL_SAVE_VREG"] + c["CALL_SAVE_VREGS"]:
            sys.exit(f"{name}: touches v{max(vgprs)}, past the wave's")
        out.append(f".macro CFN_BODY_{short}")
        out.append(f".Lcfn_{short}:")
        out += body
        out.append(".endm")
        return
    if max(vgprs, default=0) > c["SPLICE_TMP_VREG_END"]:
        sys.exit(f"{name}: touches v{max(vgprs)}, past the window")

    keep = (sgprs & set(range(c["EXEC_SAVE_SREG"]))) | {30, 31, 32}
    tmp = next((r for r in sorted(keep) if r % 2 == 0 and r + 1 in keep and
                r not in (30, 32)), 4)
    keep |= {tmp, tmp + 1}

    free = list(range(c["EXEC_SAVE_SREG"], c["SPLICE_TMP_SREG_END"] + 1))
    lanes = c["CALL_SAVE_VREG"]
    save, restore = [], []
    lane = 0
    for r in sorted(keep):
        if free:
            d = free.pop(0)
            save.append(f"\ts_mov_b32 s{d}, s{r}")
            restore.append(f"\ts_mov_b32 s{r}, s{d}")
        else:
            save.append(f"\tv_writelane_b32 v{lanes}, s{r}, {lane}")
            restore.append(f"\tv_readlane_b32 s{r}, v{lanes}, {lane}")
            lane += 1
    if lane:
        # A scalar a VALU wrote is not safe to address memory with for
        # five more instructions.
        restore.append("\ts_nop 4")

    vkeep = sorted(vgprs & set(range(2, c["SPLICE_TMP_VREG"])))
    dest = range(lanes + 1, lanes + c["CALL_SAVE_VREGS"])
    for r, d in zip(vkeep, dest):
        save.append(f"\tv_mov_b32 v{d}, v{r}")
        restore.append(f"\tv_mov_b32 v{r}, v{d}")

    out.append(f".set CFN_TMP_{short}, {tmp}")
    for macro, lines in (("SAVE", save), ("RESTORE", restore),
                         ("BODY", [f".Lcfn_{short}:"] + body)):
        out.append(f".macro CFN_{macro}_{short}")
        out += lines
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
