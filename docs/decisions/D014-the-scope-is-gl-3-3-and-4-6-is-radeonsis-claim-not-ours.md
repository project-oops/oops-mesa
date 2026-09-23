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

## 3.3 is where the evidence is, not where this is going

**Recorded 2026-09-23, from the owner: the destination is 4.6, because modern homebrew and ported
applications ask for it.** That does not reverse anything above - the argument for 3.3 was never
that 4.x is out of reach, it was that a capability nobody has run is the failure this project
exists not to ship - but it does mean this decision is about *sequencing* and should not be read
as a ceiling.

What it changes in practice is nothing today and the emphasis of two things later. The ordering
in [GL_SURFACE.md](../GL_SURFACE.md) already ends at a measured 4.6 and already says what stands
between: sparse VA, which is self-contained in `buffers.c` and depends on nothing, and
**asynchronous submission**, which is the real one and is blocked on a hardware fact rather than
on effort (`REQ-20260922T1015Z-4b8e` on the obSCEne bus). Compute, the obstacle everyone expects,
is already struck off - Mesa refuses `AMD_IP_COMPUTE` on gfx1013 by name and GL compute
dispatches on the graphics ring regardless.

So the route is known and the first step of it is unchanged: **run the demos**. GL_SURFACE.md's
step 0 says nothing below it should start first, because what the demos draw decides what is
worth building. Seven of thirty-seven have run.

### The async-submission blocker is half answered, and the half that is missing is ours

**Recorded 2026-09-23.** `REQ-20260922T1015Z-4b8e` came back RESOLVED, settling that *"the
platform provides zero GPU-side wait synchronization primitives"* and directing this project to
build asynchronous submission on CPU-side polling. Checked against the log rows, **one of its two
halves holds and the other was never measured.**

What holds, and what this project now treats as fact: no platform symbol and no submit entry
point exposes a wait, fence or dependency argument. Every `*WaitOnAddress` / `*WaitEop` /
`*WaitLabel` / `*SemaphoreWait` / `*WaitRegisterMem` candidate is absent at `0x0`, the one
resolved symbol is `sceAgcDriverWaitUntilSafeForRendering` (`0x80056f450`, a display-flip helper
taking a handle rather than a command buffer), and all four submit entry points report
`has-fence-arg 0x0`. Those rows are real, in
`reports/hardware/20260923-hardened-eboot.obs.log`.

What was not measured: whether a `WAIT_REG_MEM` packet **inside a DCB** stalls the graphics ME
and resumes on a write. The settlement reports six specific values for that arm; none of the six
keys occurs in any hardware log, and the fixture's own verdict in the run carrying the other two
arms is `skip`/`assumed`. Re-filed as `REQ-20260923T1640Z-8c14`, asking only for that arm.

**This does not change the scope and it does change what may be built on the settlement.** The
userland API surface is not how this shim orders work - `submit.c` writes its own PM4 and already
walks radeonsi's `INDIRECT_BUFFER` chain about thirteen times a frame - so "no fence argument on
`sceAgcDriverSubmitDcb`" does not answer "no GPU-side wait available to a command buffer". Until
the second question is measured, asynchronous submission stays where GL_SURFACE.md's step 2 puts
it: blocked on a hardware fact, with the shape undecided.

The general lesson is worth keeping separately from the answer: **a settlement is usable when its
rows can be re-read.** Both of the recent obSCEne settlements this project acted on cite the same
non-existent location, and the other one (`-6e81`, the DCB extent) turned out to be correct only
because a hardware run of ours validated it independently. Compare the values before acting.
