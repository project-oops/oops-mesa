# 059. Two buffers, one CPU address: the shader upload was overwritten

**2026-09-19** - roadmap unit 6

## The smoking gun

Worklog 058 turned the shader `ILLEGAL_INST` into "why is the shader buffer empty". Logging every
`oops_winsys_mmap` (handle, phys, returned CPU pointer) answered it in one line:

```
MMAP h7 phys 0xe00000  cpu 0x200428000
MMAP h8 phys 0x1000000 cpu 0x200428000     <- same CPU address, different physical memory
```

Two distinct buffers were handed the **same CPU virtual address**. The mapping addresses had been
perfectly sequential (h1..h7, each at the previous one plus its size); h8 then *reused h7's address*.
radeonsi writes the shader through h7's pointer (which then backs phys `0xe00000`); h8 is later mapped
onto the same CPU address (now backing phys `0x1000000`); the shader's dirty cache lines flush to
h8's pages; h7's phys stays at its allocation-time poison; the GPU reads that at `SPI_SHADER_PGM_LO`
and its wavefronts run zeros -> `ILLEGAL_INST`. The empty-buffer and identical-poison symptoms of
worklog 058 are exactly this collision seen from the other side.

## Root cause: drm_munmap was never redirected

Patch 001 redirected libdrm's `drm_mmap` to the shim but left `drm_munmap` calling the real
`munmap`. So radeonsi's shader upload - `buffer_map(..., RADEON_MAP_TEMPORARY)`, memcpy,
`buffer_unmap` - actually **frees the CPU virtual address** on unmap. The platform re-hands that freed
address to the next buffer's map, and the two collide. The shim's own `oops_winsys_munmap` is already
a deliberate no-op ("an unmap here would leave a live handle pointing at nothing... the mapping is
released when the buffer is closed") - but libdrm bypassed it by calling `munmap` directly through
the un-redirected macro. The design was right; one macro escaped it.

## The fix

`drm_munmap` is now a no-op under `OOPS_MESA_WINSYS`
(`mesa/subprojects/libdrm-2.4.133/libdrm_macros.h`), the other half of the `drm_mmap` redirect: a
buffer's CPU mapping belongs to the bo for its lifetime and is released only at GEM_CLOSE
(`oops_winsys_gem_close` -> `oops_mem_unmap`). Each bo therefore keeps its own unique CPU address for
as long as it is alive, and no two buffers can share one. Recorded in `patches/001` so a clean
re-apply carries it.

## Where this sits

This is the shader's actual killer. The earlier fixes on the way to it were all necessary and are all
kept - `sceAgcInit` (the queue would not execute at all without it, worklog 056), the pre-submit
cache flush and the SET_CONTEXT_REG fence preamble (worklog 055/057), the Onion domain (worklog 058)
- but none of them could put the shader bytes where the GPU reads while two buffers aliased one CPU
address. Verification (Mesa rebuilt with the libdrm change, then a hardware run) is the next step; the
expectation is that h7 holds real RDNA2 code and the wavefronts execute, carrying the frontend past
shader dispatch toward make-current and `glGetString`.

The `queue_self_test`, `oops_winsys_dump_bos` and the CREATE/MMAP/VA/CLOSE lifecycle logs stay in
until a shader executes cleanly, then come out together - they earned their keep finding this.
