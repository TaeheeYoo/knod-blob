# knod-blob

The GPU code knod runs around and inside a BPF program, built once per GPU
generation and installed as firmware.

    make
    sudo make install          # /lib/firmware/knod/knod-{core,bpf-persistent}-gfx{10,11}.bin

## What is here

Most of it is C, compiled by clang for the GPU:

- `csrc/gda_engine.h` — the GDA engine: one persistent workgroup per queue
  runs the NIC's rings, every round taking packets off the CQ, calling the
  program, and sending, passing or dropping what it decided.  What a NIC's
  rings look like is its `csrc/nic_<vendor>.h`'s, and `csrc/gda_<format>.c`
  puts the two together into the engine for one NIC format
  (`KNOD_PERSIST_FMT_*`); every format's engine is in every build, and the
  kernel runs the NIC's: mlx5 (ConnectX-5 on), bnxt (P5, P7) and ice (E810).
  `csrc/gda.h` is what they share, `csrc/gda_order.c` the ordered engine's
  gate.
- `csrc/map_hash.c`, `csrc/map_array.c` — the map lookups, updates and
  deletes a program calls in place of the BPF helpers.
- `csrc/xdp.c` — `bpf_xdp_adjust_head()` and `bpf_xdp_adjust_tail()`.
- `csrc/alu.c` — division and remainder, which the GPU has no instruction for.

The JIT calls those directly, by the calling convention `knod_blob.h` sets
out: arguments from v0, nothing it keeps below v64 or s34.

What is left in assembly is what C cannot be:

- `src/gda_engine.inc` — the kernel's entry: scratch set up, then the
  engine's functions and the program called in turn; `src/gda_engine.S` and
  `src/gda_engine_ordered.S` make one per NIC format.
- `src/gda_rx.S` — the program that runs when none is attached: pass all.

`tools/cfn.py` turns clang's assembly into bodies, and every C function but
the engine's own into a routine `knod_<fn>` the JIT calls; it refuses one that
would touch what the JIT holds or need more stack than a call gets;
`tools/pack.py` packs the result into the container the kernel loads.

## Contract

`include/uapi/linux/` holds the kernel's own headers: `knod_blob.h` (the
container, the register binding, the map descriptor) and `knod_persistent.h`
(the engine's control block).  The kernel refuses a blob of another
`KNOD_BLOB_ABI_VERSION`.

The NICs' ring formats the engine reads and writes are the hardware's, not
the kernel's, and live with the engine: `csrc/mlx5.h` and `csrc/bnxt.h`.

## Requires

`llvm-mc`, `llvm-objcopy`, `clang` with the AMDGPU target, and python3.
