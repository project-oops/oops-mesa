# The programmable pipeline runs, and the frame hashes

**2026-09-20** · firmware 12.40 · the same retail unit as the earlier records · roadmap unit 6

Two runs of `dri-probe` (`DRIP00001`) on the same evening. The first found a fault and the second,
with the fault fixed, ran the whole probe to a presented frame. Both were deployed with
`pros restore` to `/data/homebrew/DRIP00001` and started with `pros launch`, with `pros logs`
subscribed **before** the launch - a parked title emits one burst and goes silent, so the
subscription has to be listening first.

This is the first record in this repository to carry a pixel hash. The earlier ones stop before a
frame is drawn.

## Run 1, build `v2026-09-20 23:26`: the GL thread ran out of stack

The probe reached hardware, reported GL 4.6 through radeonsi, and passed the fixed-function render.
It then took a `SIGSEGV` on thread `dri-probe:gl0` before the GLSL step printed anything.

```text
# signal: 11 (SIGSEGV)
# thread name: dri-probe:gl0
# reason: page fault (user write data, page not present)
# fault address: 00000007eed80fa8
# rbp: 00000007eed92960  rsp: 00000007eed80fb0
# rip: 00000000009fb279
```

**The fault address is `rsp - 8`.** That is a `call` pushing its return address onto a page that is
not there - a stack that ended, not a bad pointer. The backtrace, resolved against
`build/dri-probe.map` with the module base of `0x400000` subtracted:

| address | symbol |
|---|---|
| `0x9fb279` | `nir_opt_varyings` +0x49 |
| `0xa01aeb` | `nir_opt_varyings_bulk` +0x1eb |
| `0x9ee75e` | `gl_nir_lower_optimize_varyings` +0x21e |
| `0xa2597a` | `gl_nir_link_varyings` +0x1ada |
| `0x9f185e` | `gl_nir_link_glsl` +0x21ee |
| `0x88847d` | `st_link_shader` +0x19d |
| `0x6ab31b` | `link_program_error` +0x17b |
| `0xafd663` | `_mesa_unmarshal_LinkProgram` +0x13 |
| `0xac2763` | `glthread_unmarshal_batch` +0x143 |
| `0x424a57` | `util_queue_thread_func` +0x1a7 |
| `0x4195a9` | `impl_thrd_routine` +0x19 |

`glLinkProgram` was marshalled onto Mesa's GL thread and NIR's varying optimiser ran off the end of
it. Ten frames had consumed about 72 KB (`rbp - rsp` = `0x119b0`), and the pass that died allocates
its own large structure on the heap (`MALLOC_STRUCT(linkage_info)`), so this is ordinary compiler
frame depth against a stack far too small for it rather than one runaway frame.

**Cause.** Mesa's C11 threads layer calls `pthread_create(thr, NULL, ...)` - a null attribute,
every time (`mesa/src/c11/impl/threads_posix.c:255`). On a Linux host that means glibc's 8 MB
default. Here the runtime shim passed the null straight through to `scePthreadCreate`, so every
Mesa thread took the vendor default.

**Fix**, in `src/runtime/threads.c`: when the caller passes no attribute, the shim now builds one
with an 8 MB stack, using `scePthreadAttrInit` / `scePthreadAttrSetstacksize` /
`scePthreadAttrDestroy`. Those three needed no new measurement - `oops-sdk/src/thread/thread.c`
already calls all three on this hardware, and the build's own import manifest
(`build/mesa-imports.txt`) attributes them to `libkernel`. 8 MB matches what upstream Mesa is
written and tested against; sizing to the 72 KB that was seen would leave the next larger shader to
find a new edge on the console.

It is not a Mesa patch, and deliberately so (CLAUDE.md principle 1): Mesa asking for a default
stack is correct, and what a default stack *is* on this platform is what the runtime shim exists
to answer.

## Run 2, build `v2026-09-20 23:34`: the whole probe

Captured verbatim from `pros logs`; only the surrounding system-log noise is removed.

```text
[DRIP00001:DRI-PROBE] bringing GL up through the DRI frontend (v2026-09-20 23:34)
[DRIP00001:DRI-PROBE] oops_gl_create returned a handle: this is the first time that has happened
[DRIP00001:DRI-PROBE] the drawable reports the extent that was asked for
[DRIP00001:DRI-PROBE] GL_VERSION:
[DRIP00001:DRI-PROBE] 4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)
[DRIP00001:DRI-PROBE] GL_RENDERER:
[DRIP00001:DRI-PROBE] AMD Radeon Graphics (radeonsi, gfx1013, ACO, DRM 3.54)
[DRIP00001:DRI-PROBE] GL_VENDOR:
[DRIP00001:DRI-PROBE] AMD
[DRIP00001:DRI-PROBE] first render: draw err=0x0 read err=0x0 | centre R=255 G=0 B=0 (expect ~255 0 0) | corner R=64 G=128 B=191 (expect ~64 128 191)
[DRIP00001:DRI-PROBE] glsl render: compile vs=1 fs=1 link=1 draw err=0x0 | centre R=64 G=64 B=128 (a blend of the three vertex colours)
[DRIP00001:DRI-PROBE] frame-hash 0x5188ddb7 (FNV-1a/32, full-frame, 1920 x 1080 words, BGRA bottom-up) | mod-pixels 373248 | centre-pixel 0xff404080 | corner-pixel 0xff0d0d14 (the clear, expect 0xff0d0d14) | read err=0x0
[DRIP00001:AGC] flip 1: tile=8077 submit=64 total=8141
[DRIP00001:DRI-PROBE] presentation succeeded: the frame is on the display
[DRIP00001:DRI-PROBE] holding it on screen - close this title from the host
[DRIP00001:DRI-PROBE] idle and finished - close this title from the host
```

## The frame

| | |
|---|---|
| pixel hash | `0x5188ddb7` |
| definition | FNV-1a 32-bit, basis `0x811c9dc5`, prime `0x01000193`, stepped **one 32-bit pixel word at a time** over all 1920 x 1080 words of `glReadPixels(GL_BGRA, GL_UNSIGNED_BYTE)` on the finished drawable, in the readback's own bottom-up order, after `glFinish` and before the present |
| pixels outside the clear | 373248 |
| centre pixel (`0xAARRGGBB`) | `0xff404080` |
| clear (corner) pixel | `0xff0d0d14` |
| the frame | `glClearColor(0.05, 0.05, 0.08, 1.0)` and one GLSL 330 triangle, vertices `(-0.6,-0.6)` red, `(0.6,-0.6)` green, `(0.0, 0.6)` blue, `glDrawArrays(GL_TRIANGLES, 0, 3)` |

The algorithm and the two sibling measurements are gl1-cube's, deliberately, so the *method* is the
collection's one method (`oops-apps/src/oops-gl/gl1-cube/gl1_cube_main.c:594`, and Prosperous D008
on why this stays six copied lines rather than a shared API). The **value** is a new oracle for
this title's own frame and is not comparable with gl1-cube's `0x9dbfe189` / `0xc51cec32`, which are
tied to that title's clear colour and its cube.

## What it establishes

- **Unmodified upstream Mesa compiles, links and runs GLSL on this hardware.** `compile vs=1 fs=1
  link=1 draw err=0x0`: the GLSL front end, program linking, a VBO, a VAO, two vertex attributes
  through `glVertexAttribPointer`, and ACO's compilation of the result all work. This is the half
  of the pipeline the fixed-function triangle does not reach.
- **The interpolation is right to the bit, not merely present.** At the screen centre the
  barycentric weights of that triangle are 0.25 red, 0.25 green, 0.5 blue, so the colour predicted
  from first principles is `(0.25, 0.25, 0.5)` x 255 = `(63.75, 63.75, 127.5)`, which rounds to
  **(64, 64, 128)**. The hardware returned exactly R=64 G=64 B=128. A draw that merely put
  *something* there could not land on that value.
- **The rasterised coverage matches the analytic area exactly.** The triangle spans 1.2 NDC in each
  axis, so at 1920 x 1080 it is 1152 x 648 pixels and its area is `0.5 x 1152 x 648 = 373248`.
  `mod-pixels` came back **373248**, to the pixel. Nothing is antialiased here, so exact agreement
  is the right answer and any half-covered edge handling would have shown as a discrepancy.
- **Two readback paths agree.** The centre pixel read as `GL_RGBA` bytes (64, 64, 128) and as a
  `GL_BGRA` word (`0xff404080`) are the same pixel, so neither format's path is transposing.
- **The clear constant was right.** `corner-pixel 0xff0d0d14` is what `glClearColor(0.05, 0.05,
  0.08, 1.0)` should quantise to, measured rather than assumed.
- **The 8 MB stack took.** The shim logs one line if it cannot set the stack and falls back; that
  line is absent from this run, and there is no fault. Mesa's thread pool came up at the larger
  size without complaint.
- **The frame reached the panel.** `presentation succeeded`, one `AGC flip`, and the title parked
  holding it.

## What it does not establish

- **The hash is not yet a gate.** One run produces a value; a gate needs it to be *stable*. The
  frame is deterministic by construction - fixed clear, fixed vertices, fixed colours - so a second
  run of the same binary must return `0x5188ddb7`, but that second run has not happened. Until it
  has, this is an oracle proposed, not confirmed.
- **The performance half of unit 6's gate is untouched.** The roadmap asks for a swap costing
  milliseconds. `flip 1: tile=8077 submit=64 total=8141` is microseconds - 8.14 ms - but that is
  the **display half only**: the CPU tile plus the flip submit. The `glReadPixels` detile that
  feeds it is not in that number and is not measured here. `REQ-7e21` (render straight into a
  scanout buffer) is what removes both, and it remains open.
- **Nothing about this probe's own cost is production cost.** It takes two full-frame readbacks,
  one for the hash and one inside the present. The hash's readback is a measurement, not something
  a title would pay.
- **Nothing beyond one triangle.** No textures, no depth, no blending, no multiple draws, and no
  shader more complex than a passthrough. Unit 8's CTS subset is what puts a floor under the rest.
