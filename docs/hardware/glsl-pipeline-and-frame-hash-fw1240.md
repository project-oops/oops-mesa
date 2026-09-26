# GLSL pipeline and frame hash

Firmware 12.40, retail unit. Title `dri-probe` (`DRIP00001`), deployed with `pros restore` to
`/data/homebrew/DRIP00001`, started with `pros launch`, log captured with `pros logs` subscribed
before the launch. Runs on 2026-09-20 and 2026-09-21.

## GL thread stack, build `v2026-09-20 23:26`

With the vendor default thread stack, `glLinkProgram` faults on Mesa's GL thread:

```text
# signal: 11 (SIGSEGV)
# thread name: dri-probe:gl0
# reason: page fault (user write data, page not present)
# fault address: 00000007eed80fa8
# rbp: 00000007eed92960  rsp: 00000007eed80fb0
# rip: 00000000009fb279
```

The fault address is `rsp - 8`: a `call` pushing its return address onto an unmapped page. The
backtrace, resolved against `build/dri-probe.map` with the module base `0x400000` subtracted:

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

Ten frames use about 72 KB (`rbp - rsp` = `0x119b0`). Mesa's C11 threads layer calls
`pthread_create(thr, NULL, ...)` (`mesa/src/c11/impl/threads_posix.c`, line 255), which on Linux
means glibc's 8 MB default. `src/runtime/threads.c` gives a null attribute an 8 MB stack through
`scePthreadAttrInit` / `scePthreadAttrSetstacksize` / `scePthreadAttrDestroy`, which
`oops-sdk/src/thread/thread.c` calls on this hardware and `build/mesa-imports.txt` attributes to
`libkernel`.

## Probe log, build `v2026-09-20 23:34`

Verbatim from `pros logs`, with the surrounding system-log noise removed.

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

## Reproducibility

The same eboot, not redeployed, run again on 2026-09-21 as a separate process (pid 966 against
952) after closing the parked instance from the shell UI's Close:

| | 2026-09-20 | 2026-09-21 |
|---|---|---|
| `frame-hash` | `0x5188ddb7` | `0x5188ddb7` |
| `mod-pixels` | 373248 | 373248 |
| `centre-pixel` | `0xff404080` | `0xff404080` |
| `corner-pixel` | `0xff0d0d14` | `0xff0d0d14` |
| `flip 1` total | 8141 us | 8131 us |

The flip time varies and the hash does not.

## The frame

| | |
|---|---|
| pixel hash | `0x5188ddb7` |
| definition | FNV-1a 32-bit, basis `0x811c9dc5`, prime `0x01000193`, stepped one 32-bit pixel word at a time over all 1920 x 1080 words of `glReadPixels(GL_BGRA, GL_UNSIGNED_BYTE)` on the finished drawable, in the readback's bottom-up order, after `glFinish` and before the present |
| pixels outside the clear | 373248 |
| centre pixel (`0xAARRGGBB`) | `0xff404080` |
| clear (corner) pixel | `0xff0d0d14` |
| the frame | `glClearColor(0.05, 0.05, 0.08, 1.0)` and one GLSL 330 triangle, vertices `(-0.6,-0.6)` red, `(0.6,-0.6)` green, `(0.0, 0.6)` blue, `glDrawArrays(GL_TRIANGLES, 0, 3)` |

The method is gl1-cube's (`oops-apps/src/oops-gl/gl1-cube/gl1_cube_main.c`, line 594); the value
is specific to this frame and not comparable with gl1-cube's hashes.

## Facts from the frame

- GLSL compiles, links and draws: `compile vs=1 fs=1 link=1 draw err=0x0`, with a VBO, a VAO and
  two vertex attributes through `glVertexAttribPointer`, compiled by ACO.
- At the screen centre the barycentric weights are 0.25, 0.25, 0.5, predicting
  `(63.75, 63.75, 127.5)`, which rounds to (64, 64, 128). The hardware returns R=64 G=64 B=128.
- The triangle is 1152 x 648 pixels at 1920 x 1080, area `0.5 x 1152 x 648 = 373248`;
  `mod-pixels` is 373248.
- The centre pixel read as `GL_RGBA` bytes (64, 64, 128) and as a `GL_BGRA` word (`0xff404080`)
  agree.
- `corner-pixel 0xff0d0d14` is the quantised `glClearColor(0.05, 0.05, 0.08, 1.0)`.
- The shim's stack fallback line is absent and there is no fault.
- `flip 1: tile=8077 submit=64 total=8141` is the display half only (CPU tile plus flip submit);
  the `glReadPixels` detile that feeds it is not in that number. The probe takes two full-frame
  readbacks, one for the hash and one inside the present.
