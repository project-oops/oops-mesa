# 063. Presentation called the swrast swap, which is null for an image loader

**2026-09-20** - roadmap unit 6

## The fault

With GL up (worklog 062), the title reached `oops_gl_present` and took a SIGSEGV with `rip=0` -
a call through a null function pointer, immediately after `GL_VENDOR`. Contained, as every fault
this unit: the process died and the slot freed.

## Why

`oops_gl_present` called `driSwapBuffers(gl->drawable)`. That is the **swrast/kopper** swap path:

```c
/* dri_util.c:869 */
driSwapBuffers(struct dri_drawable *drawable) {
   assert(drawable->screen->swrast_loader);   /* compiled out in release */
   drawable->swap_buffers(drawable);          /* function pointer */
}
```

`drawable->swap_buffers` is set in exactly two places - `drisw.c:593` (`drisw_swap_buffers`) and
`kopper.c:481` (`kopper_swap_buffers`). This platform is an **image loader**
(`__DRIimageLoaderExtension`) driving radeonsi, neither of those, so `swap_buffers` was never set
and stayed null. The `assert` that would have caught it is a no-op in the release build, so the
call went straight to a null pointer. `driSwapBuffers` was the wrong entry from the start; it only
looked right because nothing had ever created a context to present before this session.

## The fix

`oops_gl_present` now calls `dri_flush_drawable(gl->drawable)` (`dri_drawable.c:559`), the DRI2
flush extension. It takes the current context (`dri_get_current`) and flushes the drawable through
`st_context_flush` - the same finish-the-frame call EGL's swap makes on an image loader, with no
`swap_buffers` dependency. Both `dri_flush_drawable` and `dri_flush` are exported from the DRI
frontend (`T` in `libdri.a`), so the extern declaration in the platform shim resolves at link.

The flip half is still not written, and `oops_gl_present` still returns false: putting the flushed
buffer on screen needs `sceVideoOutRegisterBuffers2` (D009) and is further gated by the
render-target-alignment question on the oops-sdk side (REQ-b52e). A frame that is not on screen is
reported as a failure, never faked (CLAUDE.md principle 4).

The getBuffers -> `dri_create_image` -> `GEM_CREATE` path is already exercised: the run that
faulted had created nine buffers (h1..h9) before reaching present, so the colour-buffer allocation
works; only the swap entry was wrong.

## Verification

Relinked (platform shim only; no Mesa change).

**Confirmed on hardware (run of 2026-09-20).** No fault of any kind. The title ran the whole path
end to end:

```
GL_VENDOR: AMD
[OOPS-GL]   the frame was flushed, but the flip path is not written yet, so it is not on screen
[DRI-PROBE] presentation refused, as expected: the flip half waits on a hardware question
[DRI-PROBE] torn down
[DRI-PROBE] done
[DRI-PROBE] idle and finished - close this title from the host
```

`oops_gl_present` flushed through `dri_flush_drawable` without faulting and returned false as
designed, `oops_gl_destroy` tore the context down, and the title reached `park()` cleanly. With
worklog 062, the dri-probe now exercises the complete bring-up - screen, drawable, context,
make-current, shader execution, three `glGetString` calls, a drawable flush and teardown - with no
crash. Two shim/title bugs stood between a linked title and this; neither was a Mesa change.

The title parks (does not exit), so the console holds an idle DRIP00001 after the run; close it
from the host before re-launching (a relaunch of a running title is a no-op).

## What remains for a frame on screen

Only the flip half: register the flushed colour buffer with the display controller
(`sceVideoOutRegisterBuffers2`, D009) and scan it out. That is gated on the render-target
alignment fix on the oops-sdk side (REQ-b52e) - until it lands, a presented frame would be
misaligned heap memory, so the flip is not worth wiring yet. The winsys diagnostics
(`queue_self_test`, `oops_winsys_dump_bos`, the CREATE/MMAP/VA/CLOSE lifecycle logs, the
"submitting IB" line) have done their job now that the whole path runs clean and are the next
thing to remove.
