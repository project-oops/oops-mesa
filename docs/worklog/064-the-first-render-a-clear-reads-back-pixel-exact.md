# 064. The first render: a clear to a known colour reads back pixel-exact

**2026-09-20** - roadmap unit 6

## What ran

dri-probe (DRIP00001) now does the smallest real render after bring-up: `glClearColor(0.25,
0.50, 0.75, 1.0)`, `glClear(GL_COLOR_BUFFER_BIT)`, `glFinish()`, then `glReadPixels(0,0,1,1,
GL_RGBA, GL_UNSIGNED_BYTE)`. On hardware:

```
first render: clear err=0x0 readback err=0x0 -> R=64 G=128 B=191 A=255 (expect ~64 128 191 255)
```

`0.25*255=63.75`, `0.50*255=127.5`, `0.75*255=191.25`, `1.0*255=255` -> 64/128/191/255, exactly
what came back. No GL error on either call, no fault, and the title ran through present (flush
half), teardown and park as before.

## Why it matters

This is the whole render path proven, not just bring-up: making the context current, framebuffer
validation pulling the drawable's colour buffer through the shim's `getBuffers` ->
`dri_create_image` -> `GEM_CREATE`, radeonsi's GPU clear, the end-of-pipe fence, and a detiled
readback. `glReadPixels` reads the drawable's own colour buffer and detiles through Mesa, so the
value is verifiable regardless of the surface's tiling and does not depend on the flip half - which
is still gated on `sceVideoOutRegisterBuffers2` (D009) and the render-target alignment fix
(REQ-b52e). A clear is a draw; the pixel is exact; the driver renders correctly on gfx1013.

The same run linked the cleaned winsys (worklog 063's follow-up: `queue_self_test`,
`oops_winsys_dump_bos` and the CREATE/MMAP/VA/CLOSE lifecycle logs removed, the functional
`oops_winsys_flush_cpu_writes`/`sfence` kept), so the cleanup is confirmed on hardware too.

## The triangle: the full pipeline, also pixel-exact

The next step landed the same day. dri-probe now clears blue, draws a red triangle over the centre
in NDC (compat-profile immediate mode: `glBegin(GL_TRIANGLES)` / `glColor4f` / `glVertex2f`), and
reads back two pixels - the centre and a corner. On hardware:

```
first render: draw err=0x0 read err=0x0 | centre R=255 G=0 B=0 (expect ~255 0 0) | corner R=64 G=128 B=191 (expect ~64 128 191)
```

Centre is the triangle's red, corner is still the clear's blue - so it is a real triangle, not a
whole-surface fill, and neither a missing draw nor a stale buffer could pass. This exercises the
rest of the 3D pipeline past the clear: the fixed-function vertex path radeonsi lowers to an
ACO-compiled shader, rasterisation, and the fragment path. No fault; the title parked clean. (The
run's one signal line is proc 699, the previous instance being closed to free the busy `eboot.bin`,
not this run.)

## Where unit 6 stands

Rendering is proven end to end on gfx1013 - clear and a rasterised triangle, both pixel-exact via
readback. Unit 6's own gate is a hashed frame **on screen**, and the only piece left for that is
the flip half: `sceVideoOutRegisterBuffers2` (D009; its attribute-block layout is known from
obSCEne `-83df`, the address-constraint question now answerable since a surface exists) and the
display render-target alignment fix (REQ-b52e). Both are the flip; the draw is done.
