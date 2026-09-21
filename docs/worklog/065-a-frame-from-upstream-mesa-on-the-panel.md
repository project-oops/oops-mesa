# 065. A frame from upstream Mesa on the panel: the flip works

**2026-09-20** - roadmap unit 6

## What is on screen

DRIP00001 rendered a blue clear (0.25, 0.50, 0.75) with a red triangle over the centre and put it
on the console's display. Photographed off the panel over JetKVM: the blue field with the red
triangle, apex pointing **down** - inverted, because `glReadPixels` reads bottom-up while the
scanout is top-down (the orientation fix is a trivial follow-up, a vertical flip of the readback).

This is upstream Mesa/radeonsi driving OpenGL to a frame on Prospero hardware, end to end:

- radeonsi renders the clear and the triangle on the GPU (worklog 064: pixel-exact by readback)
- `oops_gl_present` flushes the drawable (`dri_flush_drawable`), reads the finished frame back with
  `glReadPixels(GL_BGRA, GL_UNSIGNED_BYTE)` - the one path that detiles radeonsi's colour buffer
  into a linear `0xAARRGGBB` image - and hands it to `oops_display_present`, which tiles it onto the
  next scanout buffer and flips it
- radeonsi's GPU use and oops-sdk's sceVideoOut display coexist in one title: the display runs its
  **CPU tiler**, off the GPU queue radeonsi drives, so the two do not contend

`AGC flip 1` retired, `presentation succeeded`, no fault. The title then idles with the display
still open (it does not tear down on success - closing the display releases the scanout and blanks
the screen, which is why the first attempt showed the frame for one flip then black).

## What made it work

- **The display foundation**: REQ-b52e's fix (the render target back on GPU-mapped Onion) confirmed
  first with gl-cube, which drew its shaded cube on the panel - so the flip built on a known-good
  display.
- **The flip, in the platform shim** (`dri_loader.c`): open the display at `oops_gl_create`,
  `glReadPixels` + `oops_display_present` at `oops_gl_present`, close at destroy. The drawable is
  1920x1080 to match the display's scanout size so the whole frame fills it.
- **A build fix** (`oops-apps/common/app.mk`): a parallel oops-sdk change had put the freestanding
  `include/libc` and `libc.c`/`math.c` on every title's target build. A hosted (USE_MESA) title
  takes its C library from the Mesa sysroot, whose `clock_t` collides with that libc's, so those are
  now guarded to freestanding titles only.

## What is left

- The vertical flip for correct orientation (a row-reverse of the readback, or a flipped render).
- Unit 6's own gate is a **hashed** known frame; the pixels are on screen now, the hash is the last
  step. Then the CPU readback + CPU tile per present is the obvious performance follow-up (render
  straight into a scanout buffer), not a correctness one. *(Written as "per REQ-7e21"; that
  identifier is `oops-sdk#REQ-20260919T1927Z-7e21`, oops-gl's, and this work is D012 step 3.)*
