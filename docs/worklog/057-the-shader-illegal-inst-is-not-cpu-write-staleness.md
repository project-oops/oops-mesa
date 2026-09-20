# 057. The shader ILLEGAL_INST is not CPU-write staleness

**2026-09-19** - roadmap unit 6

## What was tried

Worklog 056 got the GPU executing radeonsi's command stream and dispatching shader wavefronts, which
then faulted `ILLEGAL_INST` at shader `PC=0x400A00014`. The first hypothesis, backed by a read of the
Mesa upload path, was CPU-write visibility: radeonsi uploads shader code into a **VRAM write-combined**
buffer, `memcpy`s it and unmaps, and performs **no flush of its own** - it assumes the winsys/kernel
drains those writes before the GPU executes (`mesa si_buffer.c:65-67`; the upload ends in a bare
`buffer_unmap`, `si_shader_binary.c:217`). Our winsys never drained radeonsi's buffers.

The fix (kept, because it is correct regardless): before every submission the winsys now drains
radeonsi's CPU writes - `oops_winsys_flush_cpu_writes()` clflushes every live CPU-mapped buffer
(write-back / Onion), and `oops_winsys_cs` follows with one `sfence` (write-combined / Garlic, where
shaders live). This is what oops-gl does for its own buffers (`gl_context.c`, `agc_draw.c`); radeonsi
delegates it to the winsys and we were not doing it.

## Result: it did not fix the shader

The wavefronts still fault `ILLEGAL_INST` at the same `PC=0x400A00014`, same `TRAPSTS=0x40000800`,
`GPU_FAULT_WAVEFRONT_ERROR_ASYNC`. So the shader bytes the GPU runs are wrong for a reason that
flushing the CPU's writes does not cure. Three candidates remain:

1. **A mapping the flush does not cover** - if radeonsi's shader bo does not carry a `cpu_ptr` in our
   table (a different map path), the clflush loop skips it, and the bytes at the shader VA are stale or
   never written.
2. **ISA / encoding mismatch** - ACO emits code for GFX1013 that this silicon does not decode. Weakened
   by obSCEne's hand-written RDNA2 shaders executing on this part, but not ruled out for ACO output.
3. **Instruction-cache** - radeonsi relies on RDNA2 auto-invalidating I$ at IB start and emits no
   explicit `INV_ICACHE` on GFX10+ (`si_gfx_cs.c:366-381`). If our DCB submission does not trigger that
   auto-invalidate, the wavefronts fetch stale/garbage I$ lines.

## The next step is to read the bytes, not guess

`PC=0x400A00014` has been identical across the last two runs, so the shader sits reliably at
~`0x4_00A0_0000`. The definitive fork is to dump what is actually there. The winsys can do it safely
from the buffer's `cpu_ptr` (a valid CPU mapping of the same physical pages the GPU reads): a diagnostic
that logs each live buffer's `gpu_va` and first dwords. Then:

- bytes are a repeating pattern / zeros / garbage -> a mapping or write the flush did not cover
  (candidate 1);
- bytes look like RDNA2 (`0xBF81_0000` `s_endpgm`, `0xBE..`/`0xBF..` scalar ops, the `s_sendmsg`
  `0xBF90_0009` and `s_mov m0` `0xBEFC_0380` obSCEne recorded for NGG) -> valid code the GPU rejects or
  cannot see through I$ (candidates 2/3).

The flush stays in either way. Console stayed healthy through both faults (SceShellUI kept rendering,
slot freed) - a wavefront fault is contained, not a ring wedge.
