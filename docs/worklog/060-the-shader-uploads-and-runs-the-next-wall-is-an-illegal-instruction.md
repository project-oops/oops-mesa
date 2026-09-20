# 060. The shader uploads and runs; the next wall is an illegal instruction

**2026-09-20** - roadmap unit 6

## Worklog 059's fix is confirmed on hardware

libdrm rebuilt with `drm_munmap` as a no-op (patch 004), the title relinked against it, and the run
confirms both halves of the diagnosis:

- **The CPU-address collision is gone.** `MMAP h7 ... cpu 0x200428000` and `MMAP h8 ... cpu
  0x200628000` - distinct now, where before both were `0x200428000`.
- **The shader buffer holds real code.** h7 (`va 0x400a00000`, the dispatch target) now reads
  `be800086 be810083 d4000002 02000880 00000001 e0001000 80020200 bf800000` - RDNA2 scalar and
  vector ops and an `s_nop`, not the poison it held before. radeonsi's shader upload now lands where
  the GPU reads.

So the whole memory path is finally correct end to end: allocate, CPU-map (unique, stable), GPU-map,
write, submit, execute. Patch 004 makes the fix durable; `check` passes with four patches.

## The new wall: ILLEGAL_INST inside a valid-looking shader

The wavefronts still fault, but the meaning has changed completely - it is no longer "the buffer is
empty", it is "the GPU decoded the shader and rejected an instruction":

```
PC=0x0000000400A00014 {TRAPSTS=0x40000800} ILLEGAL_INST   (all waves, same PC)
GPU_FAULT_WAVEFRONT_ERROR_ASYNC
```

`0x400A00014` is h7 base + 0x14 = dword 5 = `0xe0001000`, a MUBUF-class (buffer) encoding. The waves
get several instructions in and fault there. This is a genuine ISA/decode question, not a memory or
mapping one: either ACO emitted an encoding for GFX1013 that this exact silicon does not accept, the
wave was launched in a mode (wave32/64) that makes the decoder read it wrong, or the entry/alignment
is off by the header. Distinguishing them needs RDNA2-level decoding of the uploaded bytes against
what ACO intended - a different kind of investigation from the plumbing cleared so far.

## Where the project stands

This session cleared, in order: the libc heap (config enumeration), the high-VA range (VA allocation
and mapping), the GEM_VA ioctl encoding, `sceAgcInit` (the queue executes and shaders dispatch), and
the shader-upload CPU-address collision (the shader now runs). The GPU now executes radeonsi's
command stream and decodes its shaders. What remains on the path to a frame:

1. **The shader ILLEGAL_INST** - the current wall, an ISA/decode question.
2. Then make-current, `glGetString`, and the present/flush path (`-83df` has the attribute-block
   layout for `sceVideoOutRegisterBuffers2` when a surface exists).

Console healthy throughout (SceShellUI rendering, slot freed on each fault). The diagnostics
(`queue_self_test`, `oops_winsys_dump_bos`, CREATE/MMAP/VA/CLOSE lifecycle logs) stay in until a
shader executes cleanly, then come out together.
