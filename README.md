# knod-blob

Routines the knod BPF JIT loads rather than emits, assembled once per GPU
generation and installed as firmware.

    make
    sudo make install          # /lib/firmware/knod/knod-bpf-persistent-gfx{10,11}.bin

BPF has one execution format: a GFX10/GFX11 Wave64 persistent workgroup polls
the versioned mailbox until the host requests a terminal stop. Core keeps its
existing per-generation firmware and execution model.

Register bindings and structure offsets come from `include/uapi/linux/knod_blob.h`,
a copy of the kernel's own header, so the two sides cannot drift silently. The
kernel checks the offsets it publishes against its structures at build time.

## What is here

- `src/prologue.S` — works out which packet a lane is for and hands the program
  its context, the packet bounds, and the buffer descriptor to write back to.
- `src/epilogue.S` — tells the host what the program decided and where the
  packet ended up, publishes mailbox completion after the workgroup barrier,
  and returns to the persistent-shader poll loop.

## Checking it

The prologue has to come out the same as what the kernel's own JIT emits.
`knod-blob-check`, in knod-tools, compares the two:

    knod-blob-check /sys/kernel/debug/dri/128/knod/bpf/insn build/knod-bpf-persistent-gfx10.bin

The runtime passes the actual batch and workgroup sizes, allowing the RDNA
firmware to serve the supported WG256, WG512, and WG768 geometries. Only gfx10
has been checked against real hardware. The gfx11 image assembles, but nothing
has confirmed it matches that generation's JIT output on hardware.

## Requires

`llvm-mc`, `llvm-objcopy`, `clang` (as a preprocessor) and python3.
