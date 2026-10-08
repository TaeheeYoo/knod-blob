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
- `csrc/xdp.c` — `bpf_xdp_adjust_head()` and `bpf_xdp_adjust_tail()`.
- `csrc/alu.c` — division and remainder, which the GPU has no instruction for.

The JIT calls those directly, by the calling convention `knod_blob.h` sets
out: arguments from v0, nothing it keeps below v64 or s34.

What is left in assembly is what C cannot be:

- `src/gda_engine.S` — the kernel's entry: scratch set up, then the engine's
  functions and the program called in turn.
- `src/gda_program.inc`, `src/gda_prologue.S`, `src/gda_epilogue.S` — a
  program's two ends, which the kernel's JIT splices around it.
- `src/gda_rx.S` — the program that runs when none is attached: pass all.
- `src/default.S` — what a queue runs before anything is installed.
- `src/map_hash.S`, `src/map_array.S`, `src/xdp.S`, `src/alu.S`,
  `src/cfn.inc` — a symbol for each routine on its compiled body.

`tools/cfn.py` turns clang's assembly into those bodies and refuses one that
would touch what the JIT holds or need more stack than a call gets;
`tools/pack.py` packs the result into the container the kernel loads.

## Contract

`include/uapi/linux/` holds the kernel's own headers: `knod_blob.h` (the
container, the register binding, the map descriptor), `knod_persistent.h` (the
engine's control block) and `knod_mlx5.h` (the parts of the NIC's formats the
engine writes, which the mlx5 driver checks against its own).  The kernel
refuses a blob of another `KNOD_BLOB_ABI_VERSION`.

## Requires

`llvm-mc`, `llvm-objcopy`, `clang` with the AMDGPU target, and python3.
