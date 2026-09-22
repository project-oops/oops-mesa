# D014 - The scope is OpenGL 3.3, and the 4.6 the driver reports is radeonsi's claim rather than ours

**decided** · 2026-09-22 (found by noticing that two hardware records print `4.6` while the README
and the roadmap both promise `3.3`, and that nothing in the build sets either number)

## The inconsistency

`GL_VERSION` on hardware reads **`4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)`** -
twice, in [the clang 21 record](../hardware/the-clang-21-bump-verified-fw1240.md) and in
[the GLSL record](../hardware/glsl-pipeline-and-frame-hash-fw1240.md). `README.md` and
[the roadmap](../ROADMAP.md) both say *"OpenGL 3.3 Core and GLSL 3.30"*.

Neither number was chosen. `toolchain/build-mesa.sh` contains no version clamp; radeonsi derives
its version from caps, and this part answers 4.6. The 3.3 in the prose is the scope somebody
wrote down; the 4.6 on the panel is what the driver concluded. They have never agreed and nothing
made them.

## The decision

**The scope stays OpenGL 3.3 and GLSL 3.30. The 4.6 string is recorded as radeonsi's claim about
the hardware, and is not a promise this repository makes.**

The prose changes to say that explicitly rather than quietly reading as though 4.6 were unknown.

## Why that way round

**Because a capability nobody has run is exactly the failure this project exists not to ship.**
[CONVENTIONS §3](../../../docs/CONVENTIONS.md) puts it as *"an explicit 'not handled yet' is worth
more than a wrong answer and costs the same to write"*, and an unqualified 4.6 in a README is a
wrong answer that a porter would act on.

What has been **run** is a GLSL 330 triangle ([worklog 066](../worklog/066-the-glsl-pipeline-runs-and-the-stack-was-the-wall.md)),
`mesa-cube`'s texture, depth test, element buffer and present loop
([068](../worklog/068-the-gpu-draws-in-four-milliseconds-the-cpu-spends-thirty-two.md),
[071](../worklog/071-two-buffers-and-the-pacing-that-had-to-come-with-them.md)) and upstream's
`gears` through GLUT. That is inside 3.3 with room to spare. `mesa-cube`'s own header already
says coverage and not conformance; this entry makes the repository say the same thing at the top
level.

**Raising the roadmap to 4.6 was the other option and it fails on evidence, not on ambition.**
[The surface analysis](../GL_SURFACE.md) works out from the source which parts of 4.6 are
reachable, and the answer is *most of them* - core 4.0 through 4.6 is command-stream and compiler
work with no refused ioctl between the API and the ring. So 4.6 is not obviously false. It is
**unmeasured**, and the distance between "we found no structural obstacle" and "it draws the
right pixels" is the entire distance this project cares about. Promising 4.6 on a reading of the
code would be the same mistake as promising it on the version string, with more words.

## What this is not

**It is not a claim that 4.x is broken.** Three findings from the same analysis, all of which
would support a later move to 4.x rather than against it:

- **The compute limit is not one.** GL compute dispatches on the graphics ring
  ([`si_gfx_context.c:95`](../../mesa/src/gallium/drivers/radeonsi/gfx/si_gfx_context.c:95)), is
  advertised because ACO exists rather than because a ring does
  ([`si_gfx_screen.c:845`](../../mesa/src/gallium/drivers/radeonsi/gfx/si_gfx_screen.c:845)), and
  Mesa refuses `AMD_IP_COMPUTE` on gfx1013 by name anyway
  ([`ac_gpu_info.c:486`](../../mesa/src/amd/common/ac_gpu_info.c:486)). The winsys answering only
  GFX is the only answer Mesa would accept. A compute ring is **closed, not deferred**.
- **The refused ioctls cost extensions, not core.** `GEM_USERPTR`, `FENCE_TO_HANDLE`,
  `GEM_METADATA`, `SCHED`, the `USERQ` family - interop and scheduling, none of it core GL.
- **One core-adjacent refusal exists**: sparse buffers and textures are advertised and land on
  `AMDGPU_VA_OP_REPLACE` being refused at `src/winsys/buffers.c:383`.

**And one advertised feature is answered hollowly**, which is worth more attention than any of
the refusals. `ARB_sync` works - `glClientWaitSync` returns signalled - because submission is
synchronous and the work retired before the fence was asked
([`src/winsys/submit.c:404`](../../src/winsys/submit.c:404)). That is the right answer for the wrong
reason, and the reason stops holding the moment submission goes asynchronous. It is the one place
in the shim where a refusal has been replaced by a plausible success.

## What changes because of this

- `README.md` and `ROADMAP.md` say 3.3 **and** say what the driver reports and why they differ.
- [`docs/GL_SURFACE.md`](../GL_SURFACE.md) carries the per-feature analysis, labelled as derived
  from source and explicitly not a conformance table.
- Unit 8 remains the only thing that can change this entry's answer. When a CTS subset runs, the
  scope moves to what it measured - which may be above 3.3 or, on some rows, below it.

## What would reverse it

A measured pass table above 3.3. Not a version string, not a reading of the code, and not a demo
that draws - a table with a firmware and a build against tests this repository did not write.
