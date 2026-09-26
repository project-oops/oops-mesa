/*
 * The platform shim: presentation to the display oops-sdk opens.
 *
 * It stands where EGL stands on Linux (D010). A title gets a GL context through the
 * functions below and an opaque handle; behind them the Gallium DRI frontend creates
 * the screen, drawable and context, and calls back into this shim's image loader for
 * the drawable's buffers. Those buffers are allocated by Mesa and the display is told
 * about them (D009). Mesa's DRI headers are not safe for a title to include, so none of
 * that is visible here.
 */
#ifndef OOPS_PLATFORM_H
#define OOPS_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque. It owns the DRI screen, drawable and context, the buffers Mesa allocated for
 * the drawable, and the display they are shown on. */
struct oops_gl;

/*
 * Bring up GL at `width` x `height` and make it current on the calling thread.
 *
 * The extent is the title's to choose, not the display's. The colour format is
 * `B8G8R8A8_UNORM`, the display's own scanout format (D009).
 *
 * Returns NULL and logs the step it stopped on; there is no software fallback. A
 * display that fails to open is not a failure here: the handle is returned and present
 * flushes without flipping.
 */
struct oops_gl *oops_gl_create(uint32_t width, uint32_t height);

/*
 * Flush the frame and hand it to the display, flipping radeonsi's buffer directly or
 * copying it when the flip is refused. Returns false when the frame is not on screen
 * because no display is open.
 */
bool oops_gl_present(struct oops_gl *gl);

/* The drawable's extent, as requested rather than the display's mode. */
void oops_gl_extent(const struct oops_gl *gl, uint32_t *width, uint32_t *height);

/* Tear down the display, the buffers, the context, the drawable and the screen. Safe on
 * NULL. */
void oops_gl_destroy(struct oops_gl *gl);

/*
 * The display this opened, for reaching it directly (input, a CPU overlay). It belongs
 * to the `oops_gl` and is closed by `oops_gl_destroy`. Returns NULL on NULL.
 */
struct oops_display *oops_gl_display(struct oops_gl *gl);

#ifdef __cplusplus
}
#endif

#endif /* OOPS_PLATFORM_H */
