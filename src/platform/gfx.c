/*
 * gfx.c - the oops-mesa backend of <oops/gfx.h>.
 *
 * The mirror of oops-sdk's `src/gl/gfx.c`: the same six-function renderer API, so a
 * title written against `oops/gfx.h` builds on Mesa or on oops-gl by a build switch and
 * nothing else. Here the native call underneath is `oops_gl_create` and friends, which
 * already open the display, bring up the DRI screen and make a context current - so
 * this is a thin rename, not new machinery.
 *
 * A single file-static handle rather than a heap allocation, for the same reasons as
 * the oops-gl backend: a title has one display and one context, and this must not drag
 * an allocator into a build that does not already have one.
 */

#include "oops/gfx.h"
#include "oops/display.h"

#include "oops_platform.h"
#include "oops/system.h" /* oops_klog, for the fallback below */

#include <stdio.h> /* snprintf */

#include <stddef.h>

struct oops_gfx {
    struct oops_gl *gl;
    bool in_use;
};

static struct oops_gfx s_gfx;

oops_gfx_t *oops_gfx_create(const oops_gfx_desc_t *desc) {
    if (s_gfx.in_use) {
        return NULL;
    }
    const uint32_t w =
        (desc != NULL && desc->width != 0u) ? desc->width : OOPS_DISPLAY_DEFAULT_WIDTH;
    const uint32_t h = (desc != NULL && desc->height != 0u)
                           ? desc->height
                           : OOPS_DISPLAY_DEFAULT_HEIGHT;

    /*
     * A requested size this output cannot scan out falls back to the one it can.
     *
     * A console has one display and no window manager, so a size a title names is a
     * preference, not a demand - and the caller most likely to name one is a ported
     * program that has been asking for a window since 1996. `gears` from mesa-demos
     * calls `glutInitWindowSize(300, 300)` at `gears.c:394`, and oops-sdk's GLUT passes
     * that straight through because on the oops-gl backend, and on the host, a small
     * surface is perfectly serviceable and honouring it is the right answer. Here it is
     * not: the scanout would not open at 300x300, and the failure was silent in the
     * worst way - the context came up, Mesa drew every frame correctly, the flush
     * succeeded, and the panel stayed black while the log said "the frame was flushed,
     * but the scanout output is not open" two thousand times.
     *
     * So the fallback belongs here and not in GLUT. GLUT is right to pass the request
     * on; this backend is the only code that knows the request cannot be met.
     * `oops_gfx_extent` then reports what was actually opened, which is how the caller
     * finds out - for a GLUT program that is `glutGet(GLUT_WINDOW_WIDTH)` and the
     * reshape callback, exactly as on a desktop whose window manager gave it a
     * different size than it asked for.
     *
     * **The test is not `gl == NULL`, and the first version of this got that wrong.**
     * `oops_gl_create` treats a failed display open as non-fatal by design: it logs
     * "the scanout output would not open; present will flush but not flip" and returns
     * a perfectly usable handle, because a title that only reads pixels back does not
     * need a display. So the case that actually matters - GL came up, the display did
     * not - returns non-NULL and sailed straight past a `gl == NULL` check.
     *
     * `glinfo` proved it on hardware on 2026-09-22: `oops_gl_create(1280, 720)`
     * succeeded, the scanout refused, no fallback fired, and the run produced a context
     * nothing could show. `gears` had fallen back correctly only because *its* 300x300
     * create failed outright, which made the broken test look like a working one.
     *
     * # Why the display is asked first, rather than GL built twice
     *
     * The first version of this fallback created GL at the requested size, found the
     * display had refused, destroyed the whole thing and created it again. That works
     * and it is how `fbotexture` reached a first frame - but it builds **two Mesa
     * devices in one process**, and the second one is not a clean slate.
     * `amdgpu_device_initialize` runs again with a GPU address allocator that starts
     * from the same base, while the first device's buffers are only mostly gone: the
     * teardown on 2026-09-22 leaked `gem 5` at 0x400600000 and `gem 7` at 0x400700000,
     * and the replacement device handed 0x400600000 straight back out for an 8.8 MB
     * scanout buffer. The buffer flipped to the screen every frame shared an address
     * with a live mapping nobody had released. `src/winsys/buffers.c` now names that
     * collision when it happens; this removes the occasion for it.
     *
     * **A probe is possible because the output can be re-opened.** VideoOut buffer
     * registration is single-shot per handle (oops-sdk `agc_display.c:346`, obSCEne
     * `REQ-20260921T1202Z-9a4c`), so asking twice on one handle would be refused - but
     * `agc_display_close` calls `sceVideoOutClose`, which releases the handle, and the
     * next open gets a fresh slot. That is why the destroy-and-retry worked at all, and
     * it is what lets the question be asked before Mesa exists rather than after.
     *
     * The test is `oops_display_is_ready`, which is the same test `dri_loader.c:716`
     * applies to its own adopting open - so a probe that says yes and a GL create that
     * then says no would be a disagreement between two calls to one function, not a
     * difference of opinion.
     */
    uint32_t use_w = w;
    uint32_t use_h = h;

    if (w != OOPS_DISPLAY_DEFAULT_WIDTH || h != OOPS_DISPLAY_DEFAULT_HEIGHT) {
        oops_display_t *probe = oops_display_open(OOPS_DISPLAY_BACKEND_AUTO, w, h);
        const int scannable = (probe != NULL) && (oops_display_is_ready(probe) != 0);
        if (probe != NULL) {
            oops_display_close(probe);
        }
        if (!scannable) {
            /* `oops_klog` and not `oops_winsys_log`: the first attempt of this run said
             * nothing through the winsys logger, and a fallback nobody can see in the
             * log is the same class of defect as the one it is fixing. This is the call
             * the lines either side of it use. */
            char msg[160];
            (void)snprintf(
                msg, sizeof msg,
                "%ux%u would not scan out; opening the display's own %ux%u instead", w,
                h, (unsigned)OOPS_DISPLAY_DEFAULT_WIDTH,
                (unsigned)OOPS_DISPLAY_DEFAULT_HEIGHT);
            oops_klog("OOPS-GL", msg);
            use_w = OOPS_DISPLAY_DEFAULT_WIDTH;
            use_h = OOPS_DISPLAY_DEFAULT_HEIGHT;
        }
    }

    struct oops_gl *gl = oops_gl_create(use_w, use_h);

    if (gl == NULL) {
        /* oops_gl_create already logged the step it stopped on. */
        return NULL;
    }
    s_gfx.gl = gl;
    s_gfx.in_use = true;
    return &s_gfx;
}

bool oops_gfx_present(oops_gfx_t *gfx) {
    if (gfx == NULL || gfx->gl == NULL) {
        return false;
    }
    return oops_gl_present(gfx->gl);
}

void oops_gfx_extent(const oops_gfx_t *gfx, uint32_t *width, uint32_t *height) {
    if (gfx != NULL && gfx->gl != NULL) {
        oops_gl_extent(gfx->gl, width, height);
    }
}

oops_display_t *oops_gfx_display(oops_gfx_t *gfx) {
    return (gfx != NULL && gfx->gl != NULL) ? oops_gl_display(gfx->gl) : NULL;
}

const char *oops_gfx_backend_name(void) {
    return "oops-mesa";
}

void oops_gfx_destroy(oops_gfx_t *gfx) {
    if (gfx == NULL) {
        return;
    }
    if (gfx->gl != NULL) {
        oops_gl_destroy(gfx->gl);
    }
    gfx->gl = NULL;
    gfx->in_use = false;
}
