# The advertised GL surface, and where each part of it first meets a wall

**2026-09-22** · derived from source, not from hardware · companion to
[D014](decisions/D014-the-scope-is-gl-3-3-and-4-6-is-radeonsis-claim-not-ours.md)

`GL_VERSION` on this console reads **`4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)`**
([the clang 21 record](hardware/the-clang-21-bump-verified-fw1240.md),
[the GLSL record](hardware/glsl-pipeline-and-frame-hash-fw1240.md)). Nothing in
`toolchain/build-mesa.sh` clamps it; radeonsi derives it from caps and this part answers 4.6.

What has actually been **run** is a GLSL 330 triangle ([worklog 066](worklog/066-the-glsl-pipeline-runs-and-the-stack-was-the-wall.md))
and `mesa-cube`'s texture, depth test, element buffer and present loop
([068](worklog/068-the-gpu-draws-in-four-milliseconds-the-cpu-spends-thirty-two.md),
[071](worklog/071-two-buffers-and-the-pacing-that-had-to-come-with-them.md)), plus upstream's
`gears` through GLUT. `mesa-cube`'s own header calls that coverage and not conformance.

This file is what sits between those two statements: read out of the code, every row sourced, no
hardware involved. **It is not a conformance table and must never be cited as one.** It says
where a feature would *first* be refused if something exercised it - which is a different and
weaker claim than saying it works.

## The headline, which is not what it looks like from the outside

**Most of core 4.6 is reachable. Almost nothing above 3.3 has been run.** Those are separate
problems and the second is the real one.

The four structural limits in the winsys are real, but three of them cost *extensions,
sharing and performance* rather than core features, and the fourth turns out not to be a limit at
all. The gap between 3.3 and 4.6 is therefore mostly **untested**, not mostly blocked - which is
worse for a consumer, because a blocked feature announces itself.

## The compute limit is not a limit

Listed as: only the GFX ring is answered, `COMPUTE` refuses, so GL 4.3 compute has no queue.

**GL compute does not use the compute ring here, and could not even if the shim offered one.**

- A GL context is not `PIPE_CONTEXT_COMPUTE_ONLY`, and
  [`si_gfx_context.c:95`](../mesa/src/gallium/drivers/radeonsi/gfx/si_gfx_context.c:95) sets
  `sctx->is_gfx_queue` true when that flag is absent *or* when `ip[AMD_IP_COMPUTE].num_queues` is
  zero. Both hold. Dispatches go on the graphics ring.
- Compute is advertised because ACO is present:
  [`si_gfx_screen.c:845`](../mesa/src/gallium/drivers/radeonsi/gfx/si_gfx_screen.c:845) sets
  `has_gfx_compute = support_aco || support_llvm`, and
  [`:661`](../mesa/src/gallium/drivers/radeonsi/gfx/si_gfx_screen.c:661) hands that straight to
  `caps->compute`. Nothing about a ring enters that decision.
- And Mesa **discards a `COMPUTE` answer on this exact part regardless**:
  [`ac_gpu_info.c:486`](../mesa/src/amd/common/ac_gpu_info.c:486) - *"GFX1013 is known to have
  broken compute queue"* - returns false for `AMD_IP_COMPUTE` on `FAMILY_NV` / `GFX1013` before
  it ever reads `num_queues`.

So `drm_device.c`'s GFX-only answer is not a restriction this shim imposes; it is the only answer
Mesa would accept. **Implementing a compute ring would change nothing for OpenGL.** That row
should come off the list of things a real 4.6 needs.

## The table

Sourced to `file:line`. "First refusal" means the first place an exercising program would be
told no - not the only place it might fail.

### Reachable: no refused path between the API and the ring

These are command-stream and memory features. They reach the GFX ring through the same
`submit.c` path a 3.3 draw takes, and nothing in `drm_device.c` refuses on the way.

| GL | feature | why it is reachable |
|---|---|---|
| 3.3 | VAOs, VBOs, EBOs, UBOs, samplers, instancing, MRT, FBOs | the path `mesa-cube` and `gears` already run |
| 4.0 | tessellation, `gl_ViewportIndex`, 64-bit vertex attributes | shader + command stream; ACO covers GFX10 |
| 4.1 | separate shader objects, viewport arrays, `glProgramBinary` | API-level and compiler-level only |
| 4.2 | atomic counters, image load/store, shader image atomics | buffer memory plus shader; no new ioctl |
| **4.3** | **compute shaders, SSBOs, indirect dispatch** | dispatches on GFX ([`si_gfx_context.c:95`](../mesa/src/gallium/drivers/radeonsi/gfx/si_gfx_context.c:95)) |
| 4.4 | buffer storage, persistent and coherent mapping | the BO mapping `buffers.c` already provides |
| 4.5 | direct state access, `glGetGraphicsResetStatus` | API-level; reset state reads `CTX`, which is answered |
| 4.6 | SPIR-V ingestion, anisotropic filtering, `glPolygonOffsetClamp` | compiler and sampler state |

**And there is a wall in front of the table that this document missed on its first writing: the
headers.**

A `USE_MESA` title compiles with `-I<oops-sdk>/include` **ahead of** `-I<oops-mesa>/mesa/include`
(`oops-apps/common/app.mk`), so `<GL/gl.h>` resolves to **oops-sdk's**, which covers GL 1.x and
2.0 (oops-sdk#D008). `<GL/glext.h>` has no oops-sdk counterpart, so that one *is* Mesa's - and
Mesa's hides every prototype behind `GL_GLEXT_PROTOTYPES`, which nothing defines by default. The
practical result, found by building mesa-demos on 2026-09-22:

```
GL_NUM_EXTENSIONS   defined      (an enum, from Mesa's glext.h)
glGetStringi        undeclared   (a prototype, behind GL_GLEXT_PROTOTYPES)
```

So a title reaching for a GL 3.0+ entry point gets a compile error from a driver that reports
4.6, and the error reads like a header bug. It is not one; it is two header sets from two
projects in front of one driver, describing different versions of OpenGL.

**This does not move anything from "reachable" to "refused"** - `#define GL_GLEXT_PROTOTYPES 1`
before the includes is the whole fix, and nothing in the winsys or the driver is involved. But it
does mean **no row above 2.0 in the table below has ever been compiled, let alone run**, and the
step between them is a line a porter has to know to write. It is in oops-sdk's `PORTING.md` now.

**Reachable is not working.** Every row here is an argument that nothing *structurally* refuses
it, and none of them is evidence that it draws the right pixels. That evidence is unit 8's job,
and `mesa-demos` is the cheap approximation until then.

### Refused: a named ioctl or VA op says no

| GL feature | first refusal | what the caller sees |
|---|---|---|
| `ARB_sparse_buffer`, `ARB_sparse_texture` | [`src/winsys/buffers.c:383`](../src/winsys/buffers.c:383) - `AMDGPU_VA_OP_REPLACE` and `_CLEAR` | advertised (`si_gfx_screen.c:543` enables sparse from `has_sparse`, which [`ac_gpu_info.c:1079`](../mesa/src/amd/common/ac_gpu_info.c:1079) derives as `family >= CHIP_POLARIS10` - never asked of the device), then the remap is refused |
| `EXT_memory_object`, `EXT_semaphore`, dmabuf import/export | `src/winsys/drm_device.c` - `FENCE_TO_HANDLE`, `GEM_METADATA`, `GEM_FLINK`, `GEM_OPEN` all `unimplemented()` | `-ENOSYS` and a log line |
| `AMD_pinned_memory` | `src/winsys/drm_device.c` - `GEM_USERPTR` | `-ENOSYS` |
| context priority / scheduling | `src/winsys/drm_device.c` - `SCHED` | `-ENOSYS` |
| the user-queue submission path | `src/winsys/drm_device.c` - `USERQ`, `USERQ_SIGNAL`, `USERQ_WAIT` | `-ENOSYS`; Mesa falls back to the classic path, which is the one in use |

None of these is core OpenGL. They are extensions and interop, and every one of them is refused
loudly rather than answered wrongly - which is [CONVENTIONS §3](../../docs/CONVENTIONS.md)
working as intended, *"an explicit 'not handled yet' is worth more than a wrong answer"*.

### Answered, but degenerately - the rows that deserve the most suspicion

| GL feature | mechanism | why the answer is hollow |
|---|---|---|
| `ARB_sync` - `glFenceSync`, `glClientWaitSync`, `glWaitSync` | `src/winsys/submit.c` | submission is synchronous ([`submit.c:9-23`](../src/winsys/submit.c:9)), so `WAIT_CS` answers "signalled" because the work retired before it was asked ([`:404`](../src/winsys/submit.c:404)). A program gets the **right answer for the wrong reason**: the fence cannot block, because there is never anything outstanding to block on. Correct for one context in one thread; silently meaningless the moment that stops being true |
| `glFlush` / `glFinish` overlap, pipelining, any frame-over-frame parallelism | `src/winsys/submit.c` | a frame cannot overlap the next. `mesa-cube` draws in 1.7-4.2 ms inside a 16.7 ms frame; the rest is idle, and none of it can be spent on the next frame |
| dependencies between submissions | [`submit.c:271`](../src/winsys/submit.c:271) | dependency chunks are parsed and then deliberately dropped, because a named dependency has already retired. The parse is kept so that making submission asynchronous starts by deleting the drop |
| `SYNCOBJ_WAIT`, `SYNCOBJ_SIGNAL`, `SYNCOBJ_RESET` | `src/winsys/drm_device.c` | refused, and **it costs nothing today**: the only caller in the pinned Mesa is [`amdgpu_userq.c:245`](../mesa/src/gallium/winsys/amdgpu/drm/amdgpu_userq.c:245), on the user-queue path this shim does not take. This row is here because that is a fact about Mesa 26.2.2 and not a property of the shim, and a pin bump could change it |

**The `ARB_sync` row is the one to watch.** It is the only place where a refusal has been replaced
by a plausible success, and it is plausible for a reason that stops being true the instant
submission goes asynchronous. Anything built on it between now and then is built on an accident.

## What a measured 4.6 would actually need, in order

Estimates are omitted where they can be measured instead; each row says what it depends on.

**0. Run the demos first.** `mesa-demos` is already in the tree
(`oops-apps/src/oops-titles/mesa-demos`) and each of its 56 programs exercises one feature.
Every "reachable" row above is a guess until one of them draws. This costs no winsys work at all
and would turn most of this document into evidence or into bug reports. **Nothing below should
start before this, because it decides what is worth building.**

**1. Sparse VA (`AMDGPU_VA_OP_REPLACE` / `_CLEAR`).** Self-contained in `buffers.c`, depends on
nothing, and closes the only *core-adjacent* refusal in the table. Unblocks `ARB_sparse_buffer`
and `ARB_sparse_texture`, which Mesa advertises today and cannot deliver.

**2. Asynchronous submission.** The big one, and it is blocked on a hardware fact rather than on
effort: it needs somewhere the shim can read radeonsi's fence from, and something that makes one
submit wait on another's. [D007](decisions/D007-a-syncobj-is-a-local-handle-onto-a-polled-64-bit-fence.md) recorded the
Eq/wait-rendering symbols as absent from one sweep, which is not the same as the platform having
no such primitive under any spelling - so it is now filed as
`REQ-20260922T1015Z-4b8e` on the obSCEne bus rather than assumed.
- **Unblocks:** frame overlap (the 12 ms of idle in every `mesa-cube` frame), honest `ARB_sync`,
  and every multi-context and multi-threaded GL path.
- **Depends on:** the answer to that request. A negative answer does not stop asynchronous
  submission, but it decides its shape - CPU-side ordering around a readable fence rather than
  GPU-side waits - and building it before the answer arrives means building it twice.

**3. The refused extension ioctls, individually, when something asks.** `GEM_USERPTR`,
`FENCE_TO_HANDLE`, `GEM_METADATA`, `SCHED`. Each is one extension, none is core, and a refusal
that nobody has hit is not a defect. They are listed so the set is complete, not so the set gets
built.

**~~4. A compute ring.~~ Closed, not deferred.** See above: Mesa refuses `AMD_IP_COMPUTE` on
gfx1013 by name, GL compute dispatches on GFX, and implementing one would change nothing.

## What this document is not

It is derived entirely from reading code. No part of it has been on hardware. A row in the
"reachable" table that turns out to draw nothing is not a contradiction of this file - it is
exactly the thing this file says it cannot tell you.
