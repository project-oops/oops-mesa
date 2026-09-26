# The GL surface

What the pinned Mesa advertises on this hardware, and where each part of it meets the
shims. Derived from source; the advertised half is measured in
[the advertised surface record](hardware/the-advertised-surface-measured-fw1240.md), with
every extension name in [gl-extensions-fw1240.txt](hardware/gl-extensions-fw1240.txt).
This is not a conformance table: where a CTS result and a row here disagree, the CTS result
holds ([the CTS record](hardware/the-khronos-cts-runs-fw1240.md)).

## Version

`GL_VERSION` reads `4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)`
([the clang 21 record](hardware/the-clang-21-bump-verified-fw1240.md)). The build sets no
clamp: radeonsi derives the version from caps. What the project claims is what has been
measured (D014).

## Headers

A `USE_MESA` title compiles with oops-sdk's include directory ahead of Mesa's
(`oops-apps/common/app.mk`), so `<GL/gl.h>` is oops-sdk's (GL 1.x and 2.0,
oops-sdk#D008) and `<GL/glext.h>` is Mesa's. Mesa's `glext.h` declares prototypes only
under `GL_GLEXT_PROTOTYPES`, so a title using a GL 3.0+ entry point defines it before the
includes. oops-sdk's `PORTING.md` says the same.

## Compute

GL compute runs on the graphics ring, and a compute ring would change nothing for OpenGL:

- A GL context is not `PIPE_CONTEXT_COMPUTE_ONLY`, so `is_gfx_queue` is true
  (`mesa/src/gallium/drivers/radeonsi/gfx/si_gfx_context.c:95`).
- Compute is advertised because ACO is present: `has_gfx_compute = support_aco ||
  support_llvm` (`mesa/src/gallium/drivers/radeonsi/gfx/si_gfx_screen.c:845`, passed to
  `caps->compute` at `:661`).
- Mesa rejects `AMD_IP_COMPUTE` on GFX1013 by name as a broken compute queue
  (`mesa/src/amd/common/ac_gpu_info.c:486`).

The winsys answering only GFX is the one answer Mesa accepts.

## Reachable: no refused command between the API and the ring

These reach the GFX ring through the same `src/winsys/submit.c` path a 3.3 draw takes.

| GL | feature | path |
|---|---|---|
| 3.3 | VAOs, VBOs, EBOs, UBOs, samplers, instancing, MRT, FBOs | the path `mesa-cube` and `gears` run |
| 4.0 | tessellation, `gl_ViewportIndex`, 64-bit vertex attributes | shader and command stream; ACO covers GFX10 |
| 4.1 | separate shader objects, viewport arrays, `glProgramBinary` | API and compiler only |
| 4.2 | atomic counters, image load/store, image atomics | buffer memory and shader; no new command |
| 4.3 | compute shaders, SSBOs, indirect dispatch | dispatches on GFX |
| 4.4 | buffer storage, persistent and coherent mapping | the buffer mapping in `src/winsys/buffers.c` |
| 4.5 | direct state access, `glGetGraphicsResetStatus` | API; reset state reads `CTX`, which is answered |
| 4.6 | SPIR-V ingestion, anisotropic filtering, `glPolygonOffsetClamp` | compiler and sampler state |

## Refused: a named command or VA operation says no

| GL feature | refusal | what the caller sees |
|---|---|---|
| `ARB_sparse_buffer`, `ARB_sparse_texture` | `AMDGPU_VA_OP_REPLACE` and `_CLEAR` in `src/winsys/buffers.c` | advertised (`has_sparse` from `family >= CHIP_POLARIS10`, `mesa/src/amd/common/ac_gpu_info.c:1079`), then the remap is refused |
| `EXT_memory_object`, `EXT_semaphore`, dmabuf import and export | `FENCE_TO_HANDLE`, `GEM_METADATA`, `GEM_FLINK`, `GEM_OPEN` in `src/winsys/drm_device.c` | `-ENOSYS` and a log line |
| `AMD_pinned_memory` | `GEM_USERPTR` | `-ENOSYS` |
| context priority | `SCHED` | `-ENOSYS` |
| user-queue submission | `USERQ`, `USERQ_SIGNAL`, `USERQ_WAIT` | `-ENOSYS`; Mesa uses the classic path |

None of these is core OpenGL.

## Answered by synchronous submission

Submission retires before `oops_winsys_cs` returns (`src/winsys/submit.c`), which shapes
these answers:

| GL feature | answer |
|---|---|
| `ARB_sync` | `WAIT_CS` answers signalled because the work has retired. Correct for one context in one thread; it cannot block on outstanding work because there is none. |
| frame overlap, pipelining | a frame cannot overlap the next; the idle part of each frame is unused |
| dependencies between submissions | dependency chunks are parsed and dropped, since every named dependency has retired |
| `SYNCOBJ_WAIT`, `SYNCOBJ_SIGNAL`, `SYNCOBJ_RESET` | refused; the only caller in the pinned Mesa is the user-queue path (`mesa/src/gallium/winsys/amdgpu/drm/amdgpu_userq.c:245`), which the shim does not take |

Asynchronous submission changes every row in this table ([the roadmap](ROADMAP.md)). The
platform exposes no wait, fence or dependency argument on any submit entry point; whether
a `WAIT_REG_MEM` packet inside a DCB stalls the graphics ME is unmeasured.
