# 066. The GLSL pipeline runs, and the wall in front of it was a thread stack

**2026-09-20** - roadmap unit 6

## What happened

`dri-probe` gained a GLSL step - a 330 vertex and fragment shader drawing a colour-interpolated
triangle from a VBO - and a frame hash, and both ran on hardware. The record is
[glsl-pipeline-and-frame-hash-fw1240.md](../hardware/glsl-pipeline-and-frame-hash-fw1240.md).

It took two runs. The first one crashed, and what it crashed on is the interesting part.

## The surprise: Mesa never asks for a stack, and this platform's default is tiny

The first run faulted with `SIGSEGV` on thread `dri-probe:gl0`, after the fixed-function render and
before the GLSL step printed anything. The fault address was `0x7eed80fa8` against `rsp
0x7eed80fb0` - **exactly `rsp - 8`**, which is a `call` pushing its return address onto a page that
is not mapped. That is a stack that ended, not a bad pointer, and the distinction is the whole
diagnosis: a wild write would have landed somewhere unrelated to `rsp`.

Resolved against the link map, the chain was `impl_thrd_routine` -> `util_queue_thread_func` ->
`glthread_unmarshal_batch` -> `_mesa_unmarshal_LinkProgram` -> `st_link_shader` ->
`gl_nir_link_glsl` -> `gl_nir_link_varyings` -> `nir_opt_varyings_bulk` -> `nir_opt_varyings`,
which died 0x49 bytes into itself. So `glLinkProgram` had been marshalled onto Mesa's GL thread and
NIR's varying optimiser overflowed it, ten frames and about 72 KB down.

The cause is a single argument. Mesa's C11 threads layer calls `pthread_create(thr, NULL, ...)` -
**a null attribute, every time**, from `mesa/src/c11/impl/threads_posix.c:255`. On a Linux host
that is glibc's 8 MB. Our `pthread_create` passed the null straight through to `scePthreadCreate`,
so every Mesa thread got the vendor's default, and the vendor's default is nowhere near what a
shader compiler needs.

What made this easy to miss is that it is invisible in every earlier run. Mesa's threads existed
and worked from worklog 011 onwards; they just never did anything deep. `mesa-probe` creates a
screen, `dri-probe` cleared and drew a fixed-function triangle - none of that recurses. The first
GLSL link in this project's life was also the first thing to need more than a few frames of stack,
so the bug and the feature arrived in the same run.

Fixed in `src/runtime/threads.c`: with no caller attribute, the shim builds one with an 8 MB stack
through `scePthreadAttrInit` / `scePthreadAttrSetstacksize` / `scePthreadAttrDestroy`. No
measurement was needed for those - `oops-sdk/src/thread/thread.c` already calls all three on this
hardware and `build/mesa-imports.txt` attributes them to `libkernel`, so this is principle 3
satisfied from an oops-sdk record rather than from a guess. 8 MB because that is the environment
upstream Mesa is written against; sizing it to the 72 KB that was observed would only move the wall
to the next larger shader. A caller that brings its own attribute keeps it, and if the stack cannot
be set the shim says so once rather than failing silently.

It is **not** a Mesa patch, and that is the point of principle 1: Mesa asking for a default stack
is correct behaviour, and what a default stack *means* here is exactly what the runtime shim is for.

## What the second run establishes

Everything the probe checks, and two of the checks are exact rather than approximate:

- `compile vs=1 fs=1 link=1 draw err=0x0` - the GLSL compiler, the linker, a VBO, a VAO and two
  vertex attributes all work through unmodified Mesa on this hardware.
- The centre pixel came back **R=64 G=64 B=128**. The barycentric weights at the screen centre of
  that triangle are 0.25/0.25/0.5, so first principles predict `(63.75, 63.75, 127.5)` and
  therefore (64, 64, 128). The interpolation is right to the bit, not merely present.
- `mod-pixels 373248`. The triangle is 1152 x 648 pixels, so its area is `0.5 x 1152 x 648 =
  373248` - the rasterised coverage matches the analytic area to the pixel.
- `frame-hash 0x5188ddb7`, the first pixel hash recorded in this repository.
- `presentation succeeded`, one `AGC flip`, frame on the panel, title parked holding it.

The hash uses gl1-cube's algorithm and its two sibling measurements deliberately, so the method is
the collection's one method. The value is a new oracle for this frame and is **not** comparable
with gl1-cube's, which is tied to a different picture - worth saying plainly, because a reader who
compared them would conclude something had broken.

## What is next

- ~~A second run of the same binary~~ - **done on 2026-09-21, and the hash held.** A fresh process
  (`pid 966` against run 2's `952`), an overnight gap and a close through the shell UI in between,
  the same eboot neither rebuilt nor redeployed: `frame-hash 0x5188ddb7`, `mod-pixels 373248`,
  `centre-pixel 0xff404080`, `corner-pixel 0xff0d0d14` - every one identical. The flip timing moved
  (8131 us against 8141 us) and the hash did not, which is what makes the comparison worth
  something: a varying measurement beside a fixed one says the runs were independent rather than
  the capture stale. **Unit 6's "draws and hashes a known frame" is met.**
- **D012 step 3, the performance half of that same gate** *(written here as `REQ-7e21`, which is
  oops-gl's identifier - see D012)*. `flip 1: tile=8077 submit=64 total=8141`
  is 8.14 ms, and that is only the display half; the `glReadPixels` detile feeding it is not in the
  number and is not yet measured. Rendering straight into a scanout buffer removes both.
- **Unit 8.** One triangle is one triangle: no textures, no depth, no blending. The bounded CTS
  subset is what puts a floor under the rest, and a failure there is an upstream Mesa bug to
  report, not something to patch around here.
