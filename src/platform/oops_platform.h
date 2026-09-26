/*
 * The platform shim: presentation to the display oops-sdk opens.
 *
 * This is the third of the three shims CLAUDE.md names, and the last one written. The
 * winsys is what Mesa asks a kernel for; the runtime is what Mesa asks a C library for;
 * this is what a *title* asks oops-mesa for, and what Mesa asks a window system for. On
 * Linux the second half of that sentence is EGL's job. Here it is this file's, because
 * EGL is not linkable (D010).
 *
 * # The shape, and where each piece comes from
 *
 * A title gets a GL context by calling into the Gallium DRI frontend, which is in
 * `libdri.a` and therefore in a title's link:
 *
 *     driCreateNewScreen3()   the screen, handed this shim's loader extension table
 *     dri_create_drawable()   the thing that has a colour buffer
 *     driCreateNewContext()   the GL context
 *     dri_make_current()      ... GL calls, through libglapi ...
 *     driSwapBuffers()        flush, then this shim flips
 *
 * The loader extension table is the seam. The frontend calls *back* into it when it
 * needs a drawable's buffers, through `__DRIimageLoaderExtension::getBuffers` - and the
 * buffer handed back is one Mesa allocated, with `dri_create_image`, not one this shim
 * found somewhere. That is D009 arriving from Mesa's side: Mesa allocates, the loader
 * holds, and the display is told about the result.
 *
 * # Why a title does not see any of that
 *
 * The five calls above are Mesa's, and their headers are not safe to put in front of a
 * title: `dri_util.h` reaches `main/formats.h`, `main/glconfig.h` and
 * `pipe/p_defines.h`, which is most of Mesa's internals and a good deal of generated
 * code. `mesa_interface.h` - the loader ABI this shim implements against - is the
 * exception, including only `<stdbool.h>` and `<stdint.h>`, and it is exposed to the
 * build for that reason (worklog 035).
 *
 * So a title sees the four functions below and an opaque handle. Everything above is
 * inside.
 *
 * # What this costs a title, said plainly
 *
 * Four calls instead of `eglGetDisplay` / `eglInitialize` / `eglCreateContext` /
 * `eglMakeCurrent` / `eglSwapBuffers`. Code written against EGL adapts its startup and
 * its swap and nothing else; the GL in between is unchanged, because the GL is Mesa's
 * and this shim does not touch it.
 */
#ifndef OOPS_PLATFORM_H
#define OOPS_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque. It owns the DRI screen, drawable and context, the buffers Mesa allocated for
 * the drawable, and whatever the display needs to know about them. A title never looks
 * inside. */
struct oops_gl;

/*
 * Bring up GL at `width` x `height` and make it current on the calling thread.
 *
 * The extent is the title's to choose and is not taken from the display: a title may
 * render smaller than the screen and let the compositor scale, which is the cheaper
 * path at 4K. What the display is told about is the buffer this creates, not the other
 * way round (D009).
 *
 * The colour format is `B8G8R8A8_UNORM`, which is what the display controller reports
 * for its own scanout (`REQ-20260909T1315Z-71dc`, sweep `20260909-144348`: pixel format
 * `0x80000000`, stride exactly width * 4). It is not a parameter, because there is no
 * second format this has any evidence for.
 *
 * Returns NULL and logs the step it stopped on. Every failure here is a failure to
 * start, never a silent fall back to software: a frame that did not come from the GPU
 * is a failure and says so (CLAUDE.md, principle 4).
 */
struct oops_gl *oops_gl_create(uint32_t width, uint32_t height);

/*
 * Present what has been drawn, and return once the frame is on its way.
 *
 * Flushes the context, then hands the buffer to the display. Returns false if the frame
 * did not retire or the flip was refused - and false means the frame is *not* on
 * screen, not that it might not be.
 */
bool oops_gl_present(struct oops_gl *gl);

/* The drawable's extent, which is what was asked for rather than what the display is
 * running at. */
void oops_gl_extent(const struct oops_gl *gl, uint32_t *width, uint32_t *height);

/* Tear down the context, the drawable, the screen and the buffers, in that order. Safe
 * on NULL. */
void oops_gl_destroy(struct oops_gl *gl);

/*
 * The display this opened. It belongs to the `oops_gl` and is closed by
 * `oops_gl_destroy`; a caller uses it to reach the display directly - the input pump, a
 * CPU overlay surface - and does not close it. Returns NULL on NULL. Declared with the
 * bare struct so this header need not pull in <oops/display.h>; a caller that uses the
 * result includes it anyway.
 */
struct oops_display *oops_gl_display(struct oops_gl *gl);

#ifdef __cplusplus
}
#endif

#endif /* OOPS_PLATFORM_H */
