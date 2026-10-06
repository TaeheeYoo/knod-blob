# knod-blob

The GPU code knod runs around and inside a BPF program, built once per GPU
generation and installed as firmware.

    make
    sudo make install          # /lib/firmware/knod/knod-{core,bpf-persistent}-gfx{10,11}.bin

## What is here

Most of it is C, compiled by clang for the GPU:

- `csrc/gda.c` — the GDA engine: one persistent workgroup per queue runs the
  NIC's rings, every round taking packets off the CQ, calling the program,
  and sending, passing or dropping what it decided.
- `csrc/map_hash.c`, `csrc/map_array.c` — the map lookups, updates and
  deletes a program calls in place of the BPF helpers.

What is left in assembly is the glue the C cannot be:

- `src/gda_engine.S` — the kernel's entry: scratch set up, then the engine's
  functions and the program called in turn.
- `src/gda_program.inc`, `src/gda_prologue.S`, `src/gda_epilogue.S` — a
  program's two ends, which the kernel's JIT splices around it.
- `src/gda_rx.S` — the program that runs when none is attached: pass all.
- `src/map_hash.S`, `src/map_array.S`, `src/cfn.inc` — a routine per map
  operation that moves the JIT's registers into the calling convention and
  calls the C.

`tools/cfn.py` turns clang's assembly into what those include, and keeps
exactly the registers each function touches; `tools/pack.py` packs the result
into the container the kernel loads.

## Contract

`include/uapi/linux/` holds the kernel's own headers: `knod_blob.h` (the
container, the register binding, the map descriptor), `knod_persistent.h` (the
engine's control block) and `knod_mlx5.h` (the parts of the NIC's formats the
engine writes, which the mlx5 driver checks against its own).  The kernel
refuses a blob of another `KNOD_BLOB_ABI_VERSION`.

## Requires

`llvm-mc`, `llvm-objcopy`, `clang` with the AMDGPU target, and python3.
