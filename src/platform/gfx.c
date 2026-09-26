/*
 * The oops-mesa backend of <oops/gfx.h>.
 *
 * The same renderer API as oops-sdk's `src/gl/gfx.c`, so a title builds on Mesa or on
 * oops-gl by a build switch. It is a thin layer over `oops_gl_create` and friends. The
 * handle is file-static: a title has one display and one context, and this must not
 * pull in an allocator.
 */

#include "oops/gfx.h"
#include "oops/display.h"

#include "oops_platform.h"
#include "oops/system.h" /* oops_klog */

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
     * A size the display cannot scan out falls back to its default size, and
     * `oops_gfx_extent` reports what was opened. The display is probed before GL exists
     * because `oops_gl_create` returns a usable handle when its display fails to open,
     * and building a second Mesa device in one process reuses GPU addresses the first
     * still holds. The probe's handle is closed, so the adopting open gets a fresh one.
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
