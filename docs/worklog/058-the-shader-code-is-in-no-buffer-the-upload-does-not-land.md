# 058. The shader code is in no buffer: the upload does not land

**2026-09-19** - roadmap unit 6

## Ruling out the memory domain

Worklog 057 left the shader faulting `ILLEGAL_INST` and hypothesised the VRAM/write-combined
("Garlic") domain. `buffers.c` was changed to allocate every buffer from write-back Onion instead
(matching oops-gl, which puts shaders in Onion and executes them; and obSCEne's open `-a6c2` suspects
Garlic). The shader still faulted at the same PC, so **the domain was not the cause**. The Onion
change is kept - it is the safer, oops-gl-aligned choice and the pre-submit flush already accounts
for the CPU cache - but it is not the fix.

## What the buffer dump shows

A diagnostic that logs every CPU-mapped buffer's handle, GPU address, physical offset and first
dwords (read from `cpu_ptr`, a valid mapping of the pages the GPU also reads) gave, at the crashing
submission:

```
bo h2 va 0x400000000 phys 0x400000: c0012800 80000000 80000000 c0004600 0000000e c0001200 ... (the IB - real PM4)
bo h7 va 0x400a00000 phys 0xe00000: 00000000 00000000 00000000 31016fac 00000000 00000000 00000000 31016fac
bo h8 va 0x400c00000 phys 0x1000000: 00000000 00000000 00000000 31016fac ...
(h1, h3, h6: all zeros)
```

The wavefronts dispatch to `PC=0x400A00014` - inside h7. **h7 holds no shader code**: zeros with a
`0x31016fac`-every-16-bytes poison fill. No buffer in the process holds recognisable RDNA2 code
(`0xbf81` s_endpgm, `0xbe../0xbf..` scalar ops) at all. The IB (h2) is written correctly; the shader
buffer is not written at all. So radeonsi programs `SPI_SHADER_PGM_LO` at h7 and dispatches, but the
shader machine code never reaches h7's memory.

## Why this is not what it looks like

The map path traces as correct: libdrm's `amdgpu_bo_cpu_map` issues GEM_MMAP for the bo's handle,
gets our `handle * PAGE` token, and `drm_mmap`s it (patch 001 -> `oops_winsys_mmap` -> the bo's phys).
So a `memcpy` through that pointer should land in the same phys the GPU reads. It does for the IB. It
does not for the shader. The difference is in how radeonsi maps the shader vs the IB:

- the shader upload uses `buffer_map(..., PIPE_MAP_UNSYNCHRONIZED | RADEON_MAP_TEMPORARY)`
  (`si_shader_binary.c`), and the shader bo is a small `si_aligned_buffer_create` - **likely a slab
  sub-allocation**, so the real bo mapped is a 2 MiB slab and the shader sits at a sub-offset;
- h7 and h8 are 2 MiB, poison-filled, and adjacent - the shape of freshly-reclaimed slabs.

So the leading hypothesis is now a **slab / buffer-lifecycle** mismatch: radeonsi writes the shader
into a slab sub-allocation whose real-bo CPU mapping our winsys handles differently from the GPU-VA
mapping, or the slab is reclaimed/re-poisoned between upload and dispatch, leaving h7 empty at the VA
the dispatch targets. The other open possibility is a second CPU-map path (persistent vs temporary)
that does not route through `oops_winsys_mmap`.

## Next

Trace radeonsi's actual shader `memcpy` target - the real bo and offset behind the slab
sub-allocation - and how our winsys maps and VA-binds that real bo, to find where the write goes vs
where the GPU reads. This is a careful winsys/slab trace, not another parameter guess; the last two
guesses (Garlic, then domain-agnostic flush) did not hold, and the buffer dump has now turned the
question from "why are the bytes wrong" into the sharper "why is the shader buffer empty".

Everything up to here stands: the queue executes, radeonsi's IB runs, shaders dispatch. Console stays
healthy across the fault (SceShellUI rendering, slot freed). The diagnostics (`queue_self_test`,
`oops_winsys_dump_bos`) stay in until a shader executes, then come out with the bug.
