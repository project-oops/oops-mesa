/*
 * The platform shim: a DRI loader over the Gallium frontend (D010).
 *
 * # What this file is
 *
 * On Linux, EGL is the thing that sits between a title and Mesa's DRI frontend: it creates the
 * screen, holds the drawable's buffers, and turns `eglSwapBuffers` into a flip. EGL is not
 * linkable here - upstream builds it `shared_library()` regardless of `default_library=static`,
 * so a title has no archive to link (D010, worklog 034). This file is that layer instead.
 *
 * # The declarations below, and why they are here rather than included
 *
 * The five DRI entry points live in `dri_util.h`, which reaches `main/formats.h`,
 * `main/glconfig.h` and `pipe/p_defines.h` - most of Mesa's internals plus generated headers, and
 * none of it survives the warning flags a title compiles at. So they are restated, from
 * `dri_util.h` at the pinned revision, with the same care `mesa-probe` restates
 * `pipe_screen_config`: exact types, exact order, and the pin is what keeps them true.
 *
 * `mesa_interface.h` is the exception and *is* included. It is the loader ABI - deliberately
 * dependency-free, `<stdbool.h>` and `<stdint.h>` only - and it carries every type this file
 * implements against. Checked to compile at the title's full `-Wconversion -Werror` setting
 * (worklog 035).
 *
 * # The direction buffers flow, which is the whole design
 *
 * Mesa allocates; this loader holds. `getBuffers` below answers the frontend's request for a
 * drawable's colour buffer with one made by `dri_create_image`, which goes through Gallium, through
 * radeonsi, and out through this repository's own `GEM_CREATE`. The display is then told about
 * that buffer.
 *
 * The other direction - handing Mesa a buffer the display already owns - is closed: libdrm refuses
 * `amdgpu_bo_handle_type_kms` imports with `-EPERM`, and the remaining handle types are
 * cross-process mechanisms this platform does not have (D009, worklog 032).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesa_interface.h"
#include "oops_platform.h"
#include "oops/display.h"

/*
 * GL's own header, for the readback the flip half does. Same suppression as the title's includes:
 * this file compiles at `-Wconversion -Wsign-conversion -Werror` and upstream's header is not ours
 * to make clean. The flip reads the drawable back with `glReadPixels` because that is the one path
 * that detiles radeonsi's colour buffer into the linear image the display wants.
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <GL/gl.h>
#pragma clang diagnostic pop

/* oops-sdk's log sink, and the winsys's own logger for anything below this layer. */
extern void oops_klog(const char *tag, const char *msg);

static void say(const char *msg)
{
    oops_klog("OOPS-GL", msg);
}

/*
 * Restated from `mesa/src/gallium/frontends/dri/dri_util.h` at the pin. A wrong prototype here is
 * a silent ABI mismatch rather than a compile error, so each one is copied rather than recalled.
 */
enum dri_screen_type {
    DRI_SCREEN_DRI3,
    DRI_SCREEN_KOPPER,
    DRI_SCREEN_SWRAST,
    DRI_SCREEN_KMS_SWRAST,
};

struct dri_screen;
struct dri_drawable;
struct dri_context;
struct dri_image;
struct dri_config;

extern struct dri_screen *driCreateNewScreen3(int scrn, int fd,
                                              const __DRIextension **loader_extensions,
                                              enum dri_screen_type type,
                                              const struct dri_config ***driver_configs,
                                              bool driver_name_is_inferred,
                                              bool has_multibuffer, void *data);
extern struct dri_drawable *dri_create_drawable(struct dri_screen *screen,
                                                const struct dri_config *config,
                                                bool isPixmap, void *loaderPrivate);
extern struct dri_context *driCreateNewContext(struct dri_screen *screen,
                                               const struct dri_config *config,
                                               struct dri_context *shared, void *data);
extern bool dri_make_current(struct dri_context *ctx, struct dri_drawable *draw,
                             struct dri_drawable *read);
extern void dri_flush_drawable(struct dri_drawable *drawable);
extern void driDestroyContext(struct dri_context *ctx);
extern void driDestroyDrawable(struct dri_drawable *drawable);
extern void driDestroyScreen(struct dri_screen *screen);
extern struct dri_image *dri_create_image(struct dri_screen *screen, int width, int height,
                                          int format, const uint64_t *modifiers,
                                          const unsigned count, unsigned int use,
                                          void *loaderPrivate);
extern void dri2_destroy_image(struct dri_image *image);
extern int dri2_query_image(struct dri_image *image, int attrib, int *value);
extern int driGetConfigAttrib(const struct dri_config *config, unsigned int attrib,
                              unsigned int *value);

/* The winsys side: this is how the loader learns which buffer Mesa actually allocated. */
extern void *oops_winsys_bo_cpu_range(uint32_t handle, uint64_t offset, uint64_t bytes);
extern int oops_winsys_open(void);

struct oops_gl {
    struct dri_screen *screen;
    struct dri_drawable *drawable;
    struct dri_context *context;
    struct dri_image *back;

    uint32_t width;
    uint32_t height;
    /* The colour format, as an `enum pipe_format`, read from the chosen `dri_config` rather than
     * named by a constant. See the note in `oops_get_buffers`. */
    int format;
    int fd;
    bool registered;   /* the display has been told about `back` */

    /* The flip half: the scanout output, and a linear frame to read the drawable back into. Both
     * are null when the display did not open, and present then flushes without flipping. */
    oops_display_t *display;
    uint32_t *scanbuf;
};

/*
 * The frontend asking for the drawable's buffers.
 *
 * Called on the first draw and whenever the drawable is invalidated. The image is created once and
 * kept: a new one each time would mean a new allocation, a new address, and a display registration
 * that no longer describes the buffer being drawn into.
 *
 * Front and back: the frontend is told there is only a back buffer. Single-buffered rendering with
 * a front buffer is what EGL's surfaceless platform does for pbuffers, for compatibility with
 * behaviour this platform has no equivalent of. Here there is a real flip, so a back buffer is the
 * honest description of what exists.
 */
static int oops_get_buffers(struct dri_drawable *driDrawable, unsigned int format,
                            uint32_t *stamp, void *loaderPrivate, uint32_t buffer_mask,
                            struct __DRIimageList *buffers)
{
    struct oops_gl *gl = (struct oops_gl *)loaderPrivate;

    (void)driDrawable;
    (void)format;
    (void)stamp;

    buffers->image_mask = 0;
    buffers->front = NULL;
    buffers->back = NULL;

    if (!(buffer_mask & __DRI_IMAGE_BUFFER_BACK)) {
        /* The frontend wants something other than a back buffer. Nothing here provides one, and
         * saying so beats handing back an empty list that reads as an allocation failure. */
        say("the frontend asked for a buffer that is not the back buffer");
        return 0;
    }

    if (gl->back == NULL) {
        /*
         * `use` is zero: the sharing flags all describe cross-process lifetimes this platform does
         * not have.
         *
         * `gl->format` is where this file is unfinished, and the reason is worth stating rather
         * than papering over. `dri_create_image` used to take a `__DRI_IMAGE_FORMAT_*` token, and
         * `mesa_interface.h:537` now says in as many words that those "are no longer exported".
         * The parameter is an `enum pipe_format`, which EGL supplies from its config
         * (`egl_dri2.h:390`, `enum pipe_format visual`).
         *
         * That enum is *generated*, and it lives in the part of the gallium tree a title cannot
         * include. Hardcoding a number out of it would be restating a generated value with nothing
         * to keep it true across a pin bump - the mistake worklog 024 removed from the chip test,
         * and worse here because a wrong format is a wrong surface rather than a failed check.
         *
         * So the format is taken from the `dri_config` the screen hands back, which is what EGL
         * actually does and needs no constant. `oops_gl_create` fills it in; until it does, this
         * file is not in `OOPS_MESA_SRCS` (worklog 037).
         */
        gl->back = dri_create_image(gl->screen, (int)gl->width, (int)gl->height,
                                    gl->format, NULL, 0, 0, gl);
        if (gl->back == NULL) {
            say("Mesa could not allocate the drawable's colour buffer");
            return 0;
        }
    }

    buffers->back = gl->back;
    buffers->image_mask = __DRI_IMAGE_BUFFER_BACK;
    return 1;
}

static void oops_flush_front_buffer(struct dri_drawable *driDrawable, void *loaderPrivate)
{
    /* There is no front buffer to flush to, and the frontend only calls this when it believes
     * there is. Saying so is more useful than returning quietly. */
    (void)driDrawable;
    (void)loaderPrivate;
    say("the frontend tried to flush a front buffer, which this loader does not provide");
}

static unsigned oops_get_capability(void *loaderPrivate, enum dri_loader_cap cap)
{
    (void)loaderPrivate;
    switch (cap) {
    case DRI_LOADER_CAP_FP16:
        /* No: the display is 32-bit B8G8R8A8 and nothing here has a half-float scanout path. */
        return 0;
    case DRI_LOADER_CAP_RGBA_ORDERING:
        /* Yes: this loader can accept either channel ordering, and the format it asks for above
         * says which one it wants. Answering no would make Mesa filter its configs down to the
         * historical ordering for no reason. */
        return 1;
    default:
        return 0;
    }
}

static const __DRIimageLoaderExtension s_image_loader = {
    .base = { .name = __DRI_IMAGE_LOADER, .version = 3 },
    .getBuffers = oops_get_buffers,
    .flushFrontBuffer = oops_flush_front_buffer,
    .getCapability = oops_get_capability,
};

static const __DRIextension *s_loader_extensions[] = {
    &s_image_loader.base,
    NULL,
};

/*
 * The colour format of a config, carried rather than named.
 *
 * This is what worklog 037 left open, and the answer turned out to dissolve the dilemma rather
 * than pick a side of it. `dri_create_image` wants an `enum pipe_format`, that enum is generated,
 * and a title cannot include it - so the choice looked like "hardcode a number out of a generated
 * enum, or find another source".
 *
 * Neither is needed, because **this shim never has to know what the value means**. Mesa produces
 * it and Mesa consumes it; the only job here is to carry it from the config to `dri_create_image`
 * unexamined. An opaque word cannot go stale across a pin bump, because nothing here depends on
 * what it stands for.
 *
 * Reading it needs one fact about layout, which is the same fact EGL relies on:
 * `dri2_image_format_for_pbuffer_config` (`egl_dri2.c:283`) casts a `dri_config` to
 * `struct gl_config` and reads `color_format`, and that member is *first* in the struct
 * (`mesa/src/mesa/main/glconfig.h:12`, with the comment above it explaining why it leads). So the
 * restatement below is one member long, which is as small as a layout dependency gets - the same
 * arrangement `mesa-probe` uses for `pipe_screen_config`, and the pin is what keeps it true.
 */
struct dri_config_head {
    /* `enum pipe_format` in Mesa. Read as `int` because that is the type it is handed back to
     * `dri_create_image` as, and the two are the same width. */
    int color_format;
};

static int config_format(const struct dri_config *config)
{
    return ((const struct dri_config_head *)config)->color_format;
}

/* Split out so the selection loop stays readable; `say` takes a finished string. */
static void say_config_count(unsigned seen, bool found, unsigned depth)
{
    char msg[96];

    (void)snprintf(msg, sizeof(msg), "%u configs offered; %s, depth %u", seen,
                   found ? "one is 8888 double-buffered" : "none are 8888 double-buffered",
                   depth);
    say(msg);
}

/*
 * Pick the config to draw into.
 *
 * Eight bits per channel with alpha, double buffered, and a depth buffer if one is offered. The
 * display scans out `B8G8R8A8_UNORM` (`REQ-20260909T1315Z-71dc`), so a config with those channel
 * widths is the one whose buffer can be shown without a conversion pass.
 *
 * Chosen by the attributes rather than by comparing formats, deliberately: the attributes are in
 * `mesa_interface.h`, which a title may include, while naming a `pipe_format` would drag in the
 * generated enum this file exists to avoid. The format then comes out of whichever config the
 * attributes selected, still unexamined.
 */
static const struct dri_config *choose_config(const struct dri_config **configs)
{
    const struct dri_config *best = NULL;
    unsigned best_depth = 0;
    unsigned i;

    if (configs == NULL) {
        return NULL;
    }

    for (i = 0; configs[i] != NULL; i++) {
        unsigned r = 0, g = 0, b = 0, a = 0, dbl = 0, depth = 0;

        if (!driGetConfigAttrib(configs[i], __DRI_ATTRIB_RED_SIZE, &r) ||
            !driGetConfigAttrib(configs[i], __DRI_ATTRIB_GREEN_SIZE, &g) ||
            !driGetConfigAttrib(configs[i], __DRI_ATTRIB_BLUE_SIZE, &b) ||
            !driGetConfigAttrib(configs[i], __DRI_ATTRIB_ALPHA_SIZE, &a) ||
            !driGetConfigAttrib(configs[i], __DRI_ATTRIB_DOUBLE_BUFFER, &dbl)) {
            continue;
        }
        if (r != 8u || g != 8u || b != 8u || a != 8u || dbl == 0u) {
            continue;
        }
        (void)driGetConfigAttrib(configs[i], __DRI_ATTRIB_DEPTH_SIZE, &depth);

        /* Deeper is better, and the first match wins a tie so the choice is stable across runs. */
        if (best == NULL || depth > best_depth) {
            best = configs[i];
            best_depth = depth;
        }
    }

    say_config_count(i, best != NULL, best_depth);
    return best;
}

struct oops_gl *oops_gl_create(uint32_t width, uint32_t height)
{
    const struct dri_config **configs = NULL;
    const struct dri_config *config;
    struct oops_gl *gl;

    gl = (struct oops_gl *)calloc(1, sizeof(*gl));
    if (gl == NULL) {
        say("no memory for the GL state");
        return NULL;
    }
    gl->width = width;
    gl->height = height;

    gl->fd = oops_winsys_open();
    if (gl->fd < 0) {
        say("the winsys would not open; there is no device to bring GL up on");
        free(gl);
        return NULL;
    }

    /*
     * `DRI_SCREEN_DRI3` is the screen type, and the name is about which interface is in use rather
     * than about X11: `dri_drawable.c:184` switches on it and `DRI3` selects `dri2_init_drawable`,
     * the image path this shim's extension table serves. `SWRAST` would silently select the
     * software rasteriser, which is the failure this project refuses to fall back to.
     */
    gl->screen = driCreateNewScreen3(0, gl->fd, s_loader_extensions, DRI_SCREEN_DRI3,
                                     &configs, false, false, gl);
    if (gl->screen == NULL) {
        say("the DRI frontend would not create a screen");
        free(gl);
        return NULL;
    }

    config = choose_config(configs);
    if (config == NULL) {
        say("the screen offered no config this display can scan out");
        driDestroyScreen(gl->screen);
        free(gl);
        return NULL;
    }
    gl->format = config_format(config);

    gl->drawable = dri_create_drawable(gl->screen, config, false, gl);
    if (gl->drawable == NULL) {
        say("the drawable would not be created");
        driDestroyScreen(gl->screen);
        free(gl);
        return NULL;
    }

    gl->context = driCreateNewContext(gl->screen, config, NULL, gl);
    if (gl->context == NULL) {
        say("the GL context would not be created");
        driDestroyDrawable(gl->drawable);
        driDestroyScreen(gl->screen);
        free(gl);
        return NULL;
    }

    if (!dri_make_current(gl->context, gl->drawable, gl->drawable)) {
        say("the context would not become current on this thread");
        driDestroyContext(gl->context);
        driDestroyDrawable(gl->drawable);
        driDestroyScreen(gl->screen);
        free(gl);
        return NULL;
    }

    say("GL is current: screen, drawable and context are up");

    /*
     * Open the scanout output for the flip half of present, now that the GL stack is up and known
     * good. CPU tiling is left in place - `oops_display_try_gpu_tiler` is deliberately not called -
     * so the display converts frames on the CPU and never contends for the GPU queue radeonsi
     * drives; that is what lets the two run in one title. A failure here is not fatal: present
     * flushes and refuses the flip, exactly as it did before this existed.
     */
    gl->display = oops_display_open(OOPS_DISPLAY_BACKEND_AUTO, gl->width, gl->height);
    if (gl->display != NULL && oops_display_is_ready(gl->display)) {
        gl->scanbuf = (uint32_t *)malloc((size_t)gl->width * (size_t)gl->height * sizeof(uint32_t));
        if (gl->scanbuf == NULL) {
            oops_display_close(gl->display);
            gl->display = NULL;
            say("the scanout frame buffer would not allocate; present will flush but not flip");
        }
    } else {
        if (gl->display != NULL) {
            oops_display_close(gl->display);
            gl->display = NULL;
        }
        say("the scanout output would not open; present will flush but not flip");
    }

    return gl;
}

bool oops_gl_present(struct oops_gl *gl)
{
    if (gl == NULL || gl->drawable == NULL) {
        return false;
    }

    /*
     * The flush half, then the flip half.
     *
     * `dri_flush_drawable` is the DRI2 flush extension: it finishes the current context's frame for
     * this drawable through `st_context_flush`, the same call EGL's swap makes on an image loader.
     * It is *not* `driSwapBuffers` - that is the swrast/kopper swap path and calls
     * `drawable->swap_buffers`, a pointer only `drisw.c`/`kopper.c` ever set, null for an image
     * loader (oops-mesa worklog 063).
     *
     * The flip half reads the finished frame back with `glReadPixels` - the one path that detiles
     * radeonsi's colour buffer into a linear image - as 0xAARRGGBB words (`GL_BGRA` /
     * `GL_UNSIGNED_BYTE` gives exactly that byte order, which is what the display and `CB_COLOR0`'s
     * B8G8R8A8 scanout want), then hands it to `oops_display_present`, which tiles it onto the next
     * scanout buffer and flips it. The display runs its CPU tiler, off the GPU queue radeonsi
     * drives, so this does not contend with the driver.
     *
     * The readback is bottom-up (GL's origin is the lower-left) while the scanout is top-down, so
     * the frame is mirrored vertically before it is presented.
     */
    dri_flush_drawable(gl->drawable);

    if (gl->display == NULL || gl->scanbuf == NULL) {
        say("the frame was flushed, but the scanout output is not open, so it is not on screen");
        return false;
    }

    glReadPixels(0, 0, (GLsizei)gl->width, (GLsizei)gl->height, GL_BGRA, GL_UNSIGNED_BYTE,
                 gl->scanbuf);

    /* glReadPixels reads bottom-up (GL's origin is the lower-left) and the scanout is top-down, so
     * mirror the frame vertically before presenting - swap row y with row (height-1-y), pixel by
     * pixel so no full-row scratch buffer is needed. */
    for (uint32_t y = 0; y < gl->height / 2u; y++) {
        uint32_t *top = gl->scanbuf + (size_t)y * gl->width;
        uint32_t *bot = gl->scanbuf + (size_t)(gl->height - 1u - y) * gl->width;
        for (uint32_t x = 0; x < gl->width; x++) {
            uint32_t tmp = top[x];
            top[x] = bot[x];
            bot[x] = tmp;
        }
    }

    oops_display_present(gl->display, gl->scanbuf);
    say("the frame was read back, flipped upright and presented to the display");
    return true;
}

void oops_gl_destroy(struct oops_gl *gl)
{
    if (gl == NULL) {
        return;
    }
    /* Reverse of the order they were made in, and each one tolerates never having been made. */
    if (gl->scanbuf != NULL) {
        free(gl->scanbuf);
    }
    if (gl->display != NULL) {
        oops_display_close(gl->display);
    }
    if (gl->back != NULL) {
        dri2_destroy_image(gl->back);
    }
    if (gl->context != NULL) {
        driDestroyContext(gl->context);
    }
    if (gl->drawable != NULL) {
        driDestroyDrawable(gl->drawable);
    }
    if (gl->screen != NULL) {
        driDestroyScreen(gl->screen);
    }
    free(gl);
}

void oops_gl_extent(const struct oops_gl *gl, uint32_t *width, uint32_t *height)
{
    if (gl == NULL) {
        return;
    }
    if (width != NULL) {
        *width = gl->width;
    }
    if (height != NULL) {
        *height = gl->height;
    }
}
