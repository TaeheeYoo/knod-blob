#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Turn clang's assembly for csrc/ into macros a routine can call through.

For each cfn_<name> function, three macros:

  CFN_BODY_<name>	its instructions under a local label, .Lcfn_<name>,
			with no symbol or metadata, to be placed inside the
			routine that calls it so that the JIT splices both
  CFN_SAVE_<name>	keeps every register the function writes that the
  CFN_RESTORE_<name>	JIT may still need, and puts them back

and CFN_TMP_<name>, a scalar pair the function writes and the save has
already kept, to hold the call target.

The function is compiled to the AMDGPU calling convention, which lets it
write registers knod keeps state in.  Rather than move knod's state, the
caller keeps exactly what the function writes: scalars in the splice
routine's free scalars where they are not the function's, in lanes of a
window register past that; vectors other than the result in the window.

Refuses a function that uses a stack, since nothing sets one up, one that
uses the window, and one that uses a scalar past s49.
"""
import re
import sys

WIN_VGPR = 22			# v22-v57 is the routine's
WIN_VGPR_END = 57
FREE_SGPR = range(34, 50)	# what a splice routine may destroy
RET_ADDR = (30, 31)
SPILL_VGPR = 22			# lanes of it hold scalars past the free ones
VSAVE = list(range(42, 58)) + list(range(23, 28))
REG = re.compile(r"\b([sv])(?:\[(\d+):(\d+)\]|(\d+)\b)")


def regs(lines):
    s, v = set(), set()
    for line in lines:
        if re.match(r"^\s*(\.|[\w.$]+:)", line):
            continue
        for kind, lo, hi, one in REG.findall(line):
            r = range(int(lo), int(hi) + 1) if lo else [int(one)]
            (s if kind == "s" else v).update(r)
    return s, v


def emit(name, body, out):
    sgprs, vgprs = regs(body)
    if max(vgprs, default=0) >= WIN_VGPR:
        sys.exit(f"{name}: uses v{max(vgprs)}, which is the routine's window")
    if max(sgprs, default=0) >= FREE_SGPR[-1] + 1:
        sys.exit(f"{name}: uses s{max(sgprs)}, past the routine's scalars")

    keep = sorted(sgprs | set(RET_ADDR))
    free = [r for r in FREE_SGPR if r not in sgprs]
    save, restore = [], []
    lane = 0
    for r in keep:
        if free:
            d = free.pop(0)
            save.append(f"\ts_mov_b32 s{d}, s{r}")
            restore.append(f"\ts_mov_b32 s{r}, s{d}")
        else:
            save.append(f"\tv_writelane_b32 v{SPILL_VGPR}, s{r}, {lane}")
            restore.append(f"\tv_readlane_b32 s{r}, v{SPILL_VGPR}, {lane}")
            lane += 1
    if lane:
        # A scalar a VALU wrote is not safe to address memory with for
        # five more instructions.
        restore.append("\ts_nop 4")
    vkeep = sorted(vgprs - {0, 1})
    if len(vkeep) > len(VSAVE):
        sys.exit(f"{name}: writes {len(vkeep)} vector registers")
    for r, d in zip(vkeep, VSAVE):
        save.append(f"\tv_mov_b32 v{d}, v{r}")
        restore.append(f"\tv_mov_b32 v{r}, v{d}")

    tmp = next(r for r in keep if r % 2 == 0 and r + 1 in keep and
               r not in RET_ADDR)
    short = name[len("cfn_"):]
    out.append(f".set CFN_TMP_{short}, {tmp}")
    out.append(f".macro CFN_SAVE_{short}")
    out += save
    out.append(".endm")
    out.append(f".macro CFN_RESTORE_{short}")
    out += restore
    out.append(".endm")
    out.append(f".macro CFN_BODY_{short}")
    out.append(f".Lcfn_{short}:")
    out += body
    out.append(".endm")


def main():
    src, out_path = sys.argv[1], sys.argv[2]
    text = open(src).read()
    out, name, body = [], None, []
    for line in text.splitlines():
        code = line.split(";", 1)[0].rstrip()
        m = re.match(r"^(cfn_\w+):", code)
        if m:
            name, body = m.group(1), []
            continue
        if name is None:
            continue
        if re.match(r"^\.Lfunc_end\d+:", code):
            emit(name, body, out)
            name = None
            continue
        if code.strip():
            body.append(code)
    for m in re.finditer(r"; ScratchSize: (\d+)", text):
        if int(m.group(1)):
            sys.exit(f"{src}: a function uses {m.group(1)} bytes of stack")
    open(out_path, "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
