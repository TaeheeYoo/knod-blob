# knod-blob

Routines the knod BPF JIT loads rather than emits, assembled once per GPU
generation and installed as firmware.

    make
    sudo make install          # /lib/firmware/knod/knod-bpf-gfx<n>.bin

One source is assembled for every generation; what differs between them lives
in `src/common.inc`, and today that is two mnemonics that were renamed and two
scalars the JIT places differently. Everything else - opcode numbers, field
layouts, the pitfalls each generation brought - is the assembler's problem
rather than ours.

Register bindings and structure offsets come from `include/uapi/linux/knod_blob.h`,
a copy of the kernel's own header, so the two sides cannot drift silently. The
kernel checks the offsets it publishes against its structures at build time.

## What is here

- `src/prologue.S` — works out which packet a lane is for and hands the program
  its context, the packet bounds, and the buffer descriptor to write back to.

## Checking it

The prologue has to come out the same as what the kernel's own JIT emits.
`knod-blob-check`, in knod-tools, compares the two:

    knod-blob-check /sys/kernel/debug/dri/128/knod/bpf/insn build/knod-bpf-gfx10.bin

Only gfx10 has been checked against real hardware. The other two assemble, but
nothing has confirmed they match what those generations' JIT would emit.

## Requires

`llvm-mc`, `llvm-objcopy`, `clang` (as a preprocessor) and python3.
