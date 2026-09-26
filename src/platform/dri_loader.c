/*
 * The platform shim: a DRI loader over the Gallium frontend (D010).
 *
 * It does what EGL does on Linux - creates the screen, holds the drawable's buffers and
 * turns a present into a flip - because upstream builds EGL only as a shared library.
 *
 * Mesa allocates and this loader holds (D009): `getBuffers` answers the frontend with
 * an image made by `dri_create_image`, and the display is then told about that buffer.
 * The present scans that buffer out directly when its size is a plain `64KB_R_X`
 * surface, and otherwise reads the frame back and copies it.
 */

#include <errno.h>
#include <fcntl.h> /* the dup probe below, which replicates os_dupfd_cloexec */
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Restated from `mesa/subprojects/libdrm-2.4.133/xf86drm.h` at the pin: `xf86drm.h` is
 * not on this shim's include path, and two calls do not justify putting it there. */
typedef struct _drmVersion {
    int version_major;
    int version_minor;
    int version_patchlevel;
    int name_len;
    char *name;
    int date_len;
    char *date;
    int desc_len;
    char *desc;
} drmVersion, *drmVersionPtr;

extern drmVersionPtr drmGetVersion(int fd);
extern void drmFreeVersion(drmVersionPtr);

/* The loader ABI, and the one Mesa header safe here: it includes only `<stdbool.h>` and
 * `<stdint.h>` and compiles at the title's `-Wconversion -Werror`. */
#include "mesa_interface.h"
#include "oops_platform.h"
#include "oops/display.h"
#include "oops/time.h"

/* For the readback copy's `glReadPixels`. The suppression matches the title's includes:
 * upstream's header does not compile clean at `-Wconversion -Wsign-conversion`. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <GL/gl.h>
#pragma clang diagnostic pop

extern void oops_klog(const char *tag, const char *msg);

static void say(const char *msg) {
    oops_klog("OOPS-GL", msg);
}

/*
 * Restated from `mesa/src/gallium/frontends/dri/dri_util.h` at the pin, because that
 * header reaches Mesa internals and generated headers a title cannot compile. A wrong
 * prototype here is a silent ABI mismatch, so each one is copied, not recalled.
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
/* Bumps the drawable's stamp and clears its `texture_mask` (`dri2.c:97-103`), so the
 * frontend calls `getBuffers` again instead of keeping the image it holds. */
extern void dri_invalidate_drawable(struct dri_drawable *drawable);
extern void driDestroyContext(struct dri_context *ctx);
extern void driDestroyDrawable(struct dri_drawable *drawable);
extern void driDestroyScreen(struct dri_screen *screen);
extern struct dri_image *dri_create_image(struct dri_screen *screen, int width,
                                          int height, int format,
                                          const uint64_t *modifiers,
                                          const unsigned count, unsigned int use,
                                          void *loaderPrivate);
extern void dri2_destroy_image(struct dri_image *image);
extern int dri2_query_image(struct dri_image *image, int attrib, int *value);

/*
 * The colour buffer's use flags, from
 * `mesa/src/gallium/include/mesa_interface.h:540,550`. `SCANOUT` because the display
 * scans this buffer. `FRONT_RENDERING` because radeonsi then disables DCC
 * (`mesa/src/gallium/drivers/radeonsi/si_texture.c:236-242`, with no modifier given),
 * and the display is registered with `dcc_control = 0`.
 */
#define OOPS_DRI_IMAGE_USE_SCANOUT 0x0002
#define OOPS_DRI_IMAGE_USE_FRONT_RENDERING 0x0080

/* From `mesa/include/GL/internal/dri_interface.h:1333` at the pin. */
#define OOPS_DRI_IMAGE_ATTRIB_HANDLE 0x2001
extern int driGetConfigAttrib(const struct dri_config *config, unsigned int attrib,
                              unsigned int *value);

extern void *oops_winsys_bo_cpu_range(uint32_t handle, uint64_t offset, uint64_t bytes);
/* Declared here rather than by including `oops_winsys.h`. A title links with
 * `--unresolved-symbols=ignore-all` and `-fvisibility=hidden`, so check these names in
 * the build's `.map` file: a missing one faults on first call, and `nm` sees none. */
extern uint64_t oops_winsys_bo_size(uint32_t handle);
extern uint64_t oops_winsys_bo_gpu_va(uint32_t handle);

extern int oops_winsys_open(void);

struct oops_gl {
    struct dri_screen *screen;
    struct dri_drawable *drawable;
    struct dri_context *context;
    /* Two colour buffers, alternated at every present, so the display never scans the
     * one radeonsi is drawing into. With one, direct scanout works and tears. */
    struct dri_image *back[2];
    unsigned draw;     /* which of the two the frontend is drawing into */
    unsigned n_images; /* how many exist: 1 if the second could not be made */

    uint32_t width;
    uint32_t height;
    /* The colour format, an `enum pipe_format`, carried from the chosen `dri_config`.
     */
    int format;
    int fd;

    /* Each image's buffer handle as Mesa reports it, and what the winsys allocated for
     * it. `registered` means present flips the image and copies nothing;
     * `direct_tried` stops a failed registration being retried every frame. */
    int gem[2];
    uint64_t gem_bytes[2];
    int scanout_index[2]; /* the flip index the display gave each image, once adopted */
    bool registered;      /* the display has been told about `back` */
    bool direct_tried;

    /* The scanout output and a linear frame for the readback copy. Both are null when
     * the display did not open, and present then flushes without flipping. */
    oops_display_t *display;
    uint32_t *scanbuf;
};

/*
 * The frontend asking for the drawable's buffers, on the first draw and after every
 * invalidate. Each image is created once and kept, because the display registration
 * describes that allocation. Only a back buffer exists: present is a real flip.
 */
static int oops_get_buffers(struct dri_drawable *driDrawable, unsigned int format,
                            uint32_t *stamp, void *loaderPrivate, uint32_t buffer_mask,
                            struct __DRIimageList *buffers);

/*
 * Create colour buffer `which` now rather than on the frontend's first request, because
 * the display takes the buffers it scans only when it opens. It goes through
 * `oops_get_buffers`, which builds whichever image `gl->draw` names, so `draw` is
 * restored afterwards.
 */
static bool ensure_back_image(struct oops_gl *gl, unsigned which) {
    struct __DRIimageList list;
    const unsigned saved = gl->draw;
    bool made;

    if (which > 1u) {
        return false;
    }
    if (gl->back[which] != NULL) {
        return true;
    }

    gl->draw = which;
    made = oops_get_buffers(gl->drawable, 0, NULL, gl, __DRI_IMAGE_BUFFER_BACK,
                            &list) != 0 &&
           gl->back[which] != NULL;
    gl->draw = saved;

    return made;
}

static int oops_get_buffers(struct dri_drawable *driDrawable, unsigned int format,
                            uint32_t *stamp, void *loaderPrivate, uint32_t buffer_mask,
                            struct __DRIimageList *buffers) {
    struct oops_gl *gl = (struct oops_gl *)loaderPrivate;

    (void)driDrawable;
    (void)format;
    (void)stamp;

    buffers->image_mask = 0;
    buffers->front = NULL;
    buffers->back = NULL;

    if (!(buffer_mask & __DRI_IMAGE_BUFFER_BACK)) {
        say("the frontend asked for a buffer that is not the back buffer");
        return 0;
    }

    const unsigned which = (gl->draw > 1u) ? 0u : gl->draw;

    if (gl->back[which] == NULL) {
        /* `gl->format` is an `enum pipe_format`, which is generated and cannot be named
         * here; it comes from the chosen config, as EGL does (`egl_dri2.h:390`). */
        gl->back[which] = dri_create_image(
            gl->screen, (int)gl->width, (int)gl->height, gl->format, NULL, 0,
            OOPS_DRI_IMAGE_USE_SCANOUT | OOPS_DRI_IMAGE_USE_FRONT_RENDERING, gl);
        if (gl->back[which] == NULL) {
            say("Mesa could not allocate the drawable's colour buffer");
            return 0;
        }

        char what[128];
        int gem = 0;
        struct dri_image *const img = gl->back[which];
        const int have_gem = dri2_query_image(img, OOPS_DRI_IMAGE_ATTRIB_HANDLE, &gem);

        /* The size comes from the winsys's record of the `GEM_CREATE`, not from the
         * extent, so it measures what Mesa allocated. addrlib pads a tiled surface's
         * height to a multiple of 128. */
        if (have_gem) {
            const uint64_t bytes = oops_winsys_bo_size((uint32_t)gem);

            gl->gem[which] = gem;
            gl->gem_bytes[which] = bytes;

            const uint64_t linear = (uint64_t)gl->width * (uint64_t)gl->height * 4u;
            const uint64_t tiled = (uint64_t)gl->width *
                                   (((uint64_t)gl->height + 127u) & ~(uint64_t)127u) *
                                   4u;

            (void)snprintf(
                what, sizeof what,
                "colour buffer gem %d: %llu bytes (linear %llu, 64KB_R_X %llu) -> %s",
                gem, (unsigned long long)bytes, (unsigned long long)linear,
                (unsigned long long)tiled,
                (bytes == 0u)       ? "handle not live in the winsys"
                : (bytes >= tiled)  ? "tiled"
                : (bytes >= linear) ? "linear"
                                    : "neither - smaller than a linear frame");
            say(what);
        } else {
            say("colour buffer: Mesa would not report a GEM handle, so its size is not "
                "askable");
        }
    }

    buffers->back = gl->back[which];
    buffers->image_mask = __DRI_IMAGE_BUFFER_BACK;
    return 1;
}

static void oops_flush_front_buffer(struct dri_drawable *driDrawable,
                                    void *loaderPrivate) {
    (void)driDrawable;
    (void)loaderPrivate;
    say("the frontend tried to flush a front buffer, which this loader does not "
        "provide");
}

static unsigned oops_get_capability(void *loaderPrivate, enum dri_loader_cap cap) {
    (void)loaderPrivate;
    switch (cap) {
    case DRI_LOADER_CAP_FP16:
        /* The display is 32-bit B8G8R8A8; there is no half-float scanout. */
        return 0;
    case DRI_LOADER_CAP_RGBA_ORDERING:
        /* Either ordering is accepted; answering no would filter Mesa's configs down to
         * the historical ordering for no reason. */
        return 1;
    default:
        return 0;
    }
}

static const __DRIimageLoaderExtension s_image_loader = {
    .base = {.name = __DRI_IMAGE_LOADER, .version = 3},
    .getBuffers = oops_get_buffers,
    .flushFrontBuffer = oops_flush_front_buffer,
    .getCapability = oops_get_capability,
};

static const __DRIextension *s_loader_extensions[] = {
    &s_image_loader.base,
    NULL,
};

/*
 * The config's colour format, carried to `dri_create_image` without being interpreted.
 * EGL reads it the same way (`egl_dri2.c:283`): `color_format` is the first member of
 * `struct gl_config` (`mesa/src/mesa/main/glconfig.h:12`), and the pin keeps it there.
 */
struct dri_config_head {
    /* `enum pipe_format` in Mesa; the same width as `int`, which is how it is passed
     * on.
     */
    int color_format;
};

static int config_format(const struct dri_config *config) {
    return ((const struct dri_config_head *)config)->color_format;
}

static void say_config_count(unsigned seen, bool found, unsigned depth) {
    char msg[96];

    (void)snprintf(
        msg, sizeof(msg), "%u configs offered; %s, depth %u", seen,
        found ? "one is 8888 double-buffered" : "none are 8888 double-buffered", depth);
    say(msg);
}

/*
 * Pick the config to draw into: 8 bits per channel with alpha, double buffered, the
 * deepest depth buffer offered. The display scans out `B8G8R8A8_UNORM` (D009). Chosen
 * by attributes, because naming a `pipe_format` needs the generated enum.
 */
static const struct dri_config *choose_config(const struct dri_config **configs) {
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

        /* The first match wins a tie, so the choice is stable across runs. */
        if (best == NULL || depth > best_depth) {
            best = configs[i];
            best_depth = depth;
        }
    }

    say_config_count(i, best != NULL, best_depth);
    return best;
}

struct oops_gl *oops_gl_create(uint32_t width, uint32_t height) {
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
     * Ask the descriptor what the frontend will ask it first, because
     * `driCreateNewScreen3` returns a bare NULL and Mesa's own reasons go to a log
     * level this platform cannot enable. These lines separate a bad descriptor from a
     * refusal further in.
     */
    {
        drmVersionPtr v = drmGetVersion(gl->fd);
        char msg[128];
        struct stat sb;

        /* libdrm's `amdgpu_device_initialize` keys its device table on `fstat`, which
         * is not an ioctl and so never reaches the shim. */
        if (fstat(gl->fd, &sb) != 0) {
            (void)snprintf(
                msg, sizeof(msg),
                "device fd %d cannot be fstat'ed (errno %d) - libdrm keys its device "
                "table on that and will stop before its first ioctl",
                gl->fd, errno);
        } else {
            (void)snprintf(msg, sizeof(msg), "device fd %d fstat: mode 0%o rdev %lu",
                           gl->fd, (unsigned)sb.st_mode, (unsigned long)sb.st_rdev);
        }
        say(msg);

        /*
         * The dup `os_dupfd_cloexec` does, as `pipe_loader_drm_probe_fd` calls it
         * first. Patch 003 passes the fd through only when `fcntl` fails with EINVAL,
         * so any other errno ends screen creation with no ioctl issued.
         */
        {
            char dmsg[160];
            int a, ea, b, eb;

            errno = 0;
            a = fcntl(gl->fd, F_DUPFD_CLOEXEC, 3);
            ea = errno;
            errno = 0;
            b = fcntl(gl->fd, F_DUPFD, 3);
            eb = errno;

            (void)snprintf(
                dmsg, sizeof(dmsg),
                "dup probe on fd %d: F_DUPFD_CLOEXEC=%d errno %d, F_DUPFD=%d errno %d "
                "(patch 003 only passes the fd through when that errno is EINVAL=%d)",
                gl->fd, a, ea, b, eb, EINVAL);
            say(dmsg);
        }

        if (v != NULL) {
            (void)snprintf(msg, sizeof(msg),
                           "device fd %d answers drmGetVersion: %s %d.%d.%d", gl->fd,
                           v->name ? v->name : "(unnamed)", v->version_major,
                           v->version_minor, v->version_patchlevel);
            drmFreeVersion(v);
        } else {
            (void)snprintf(
                msg, sizeof(msg),
                "device fd %d does NOT answer drmGetVersion (errno %d) - the frontend "
                "cannot name a driver for it",
                gl->fd, errno);
        }
        say(msg);
    }

    /* `DRI3` selects `dri2_init_drawable`, the image-loader path
     * (`dri_drawable.c:184`); `SWRAST` would select the software rasteriser. */
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
     * The colour buffers are made before the display opens, because VideoOut takes the
     * buffers it scans once, at registration (D009). A buffer is offered only when its
     * size is exactly a plain `64KB_R_X` surface; any extra bytes are displayable DCC
     * metadata the display is not told about. Otherwise present copies.
     */
    void *adopt[2];
    int adopt_count = 0;

    const uint64_t plain =
        (uint64_t)gl->width * (((uint64_t)gl->height + 127u) & ~(uint64_t)127u) * 4u;

    for (unsigned i = 0; i < 2u; i++) {
        if (!ensure_back_image(gl, i)) {
            break;
        }
        gl->n_images = i + 1u;
    }

    for (unsigned i = 0; i < gl->n_images; i++) {
        const uint64_t va =
            (gl->gem[i] != 0) ? oops_winsys_bo_gpu_va((uint32_t)gl->gem[i]) : 0;
        char msg[128];

        if (va != 0 && gl->gem_bytes[i] == plain) {
            adopt[adopt_count] = (void *)(uintptr_t)va;
            adopt_count++;
        }

        (void)snprintf(
            msg, sizeof msg, "colour buffer %u: %llu bytes, plain is %llu - %s", i,
            (unsigned long long)gl->gem_bytes[i], (unsigned long long)plain,
            (va != 0 && gl->gem_bytes[i] == plain)
                ? "no compression, offering it for scanout"
            : (gl->gem_bytes[i] > plain) ? "carries metadata, so present copies"
                                         : "unexpected size, so present copies");
        say(msg);
    }

    /* All or nothing: a pair that alternates a flip with a copy would stutter. */
    if (adopt_count != (int)gl->n_images) {
        adopt_count = 0;
        say("not every colour buffer is scannable, so present copies for all of them");
    }

    /* The display tiles on the CPU (`oops_display_try_gpu_tiler` is not called), so it
     * never contends for the GPU queue radeonsi drives. A failure to open is not fatal:
     * present then flushes and refuses the flip. */
    gl->display = oops_display_open_adopting(OOPS_DISPLAY_BACKEND_AUTO, gl->width,
                                             gl->height, adopt, adopt_count);
    if (gl->display != NULL && oops_display_is_ready(gl->display)) {
        gl->scanbuf = (uint32_t *)malloc((size_t)gl->width * (size_t)gl->height *
                                         sizeof(uint32_t));
        if (gl->scanbuf == NULL) {
            oops_display_close(gl->display);
            gl->display = NULL;
            say("the scanout frame buffer would not allocate; present will flush but "
                "not flip");
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

/*
 * Log a present's cost in four disjoint parts that sum to the total: flush (the GPU
 * finishing the frame), read (`glReadPixels`), mirror (row swap) and disp
 * (`oops_display_present`). Logged for the first ten frames and every sixtieth after,
 * and kept under the ~128 bytes past which a klog line is dropped.
 */
static void say_present_cost(uint64_t t_start, uint64_t t_flush, uint64_t t_read,
                             uint64_t t_mirror, uint64_t t_done) {
    static uint32_t s_presents;
    char msg[112];

    s_presents++;
    if (s_presents > 10u && (s_presents % 60u) != 0u) {
        return;
    }

    (void)snprintf(msg, sizeof(msg),
                   "present us: flush=%u read=%u mirror=%u disp=%u total=%u",
                   (unsigned)(t_flush - t_start), (unsigned)(t_read - t_flush),
                   (unsigned)(t_mirror - t_read), (unsigned)(t_done - t_mirror),
                   (unsigned)(t_done - t_start));
    say(msg);
}

/*
 * Read back the flip index the display gave each adopted colour buffer, so present can
 * flip radeonsi's buffer instead of copying it (D009). Registration itself happens in
 * `oops_display_open_adopting`. All buffers get an index or none are used.
 */
static bool try_direct_scanout(struct oops_gl *gl) {
    char msg[128];

    gl->direct_tried = true;

    if (gl->display == NULL || gl->n_images == 0u) {
        say("direct scanout: no display, or Mesa made no colour buffer for the "
            "drawable");
        return false;
    }

    for (unsigned i = 0; i < gl->n_images; i++) {
        const int index = oops_display_adopted_index(gl->display, (int)i);
        const uint64_t va =
            (gl->gem[i] != 0) ? oops_winsys_bo_gpu_va((uint32_t)gl->gem[i]) : 0;

        (void)snprintf(
            msg, sizeof msg,
            "direct scanout: buffer %u, VA 0x%llx (%llu bytes) has flip index %d", i,
            (unsigned long long)va, (unsigned long long)gl->gem_bytes[i], index);
        say(msg);

        if (index < 0) {
            /* The display folds a refused registration into its last error as
             * `0xE4000000 | (rc & 0xFFFFFF)`; without it, -1 names no cause. */
            const int err = oops_display_get_last_error(gl->display);

            (void)snprintf(
                msg, sizeof msg,
                "direct scanout: no index for buffer %u (display last error 0x%x) - "
                "present reads back and tiles instead",
                i, (unsigned)err);
            say(msg);
            return false;
        }

        gl->scanout_index[i] = index;
    }

    (void)snprintf(msg, sizeof msg,
                   "direct scanout: %u buffer%s, so present is a flush and a flip",
                   gl->n_images, (gl->n_images == 1u) ? " (it will tear)" : "s");
    say(msg);

    gl->registered = true;
    return true;
}

bool oops_gl_present(struct oops_gl *gl) {
    if (gl == NULL || gl->drawable == NULL) {
        return false;
    }

    /*
     * `dri_flush_drawable` finishes the frame through `st_context_flush`, as EGL's swap
     * does on an image loader. `driSwapBuffers` is not used: it calls
     * `drawable->swap_buffers`, which only the swrast and kopper paths set.
     */
    const uint64_t t_start = oops_time_get_us();
    dri_flush_drawable(gl->drawable);
    const uint64_t t_flush = oops_time_get_us();

    if (!gl->direct_tried) {
        (void)try_direct_scanout(gl);
    }

    if (gl->registered) {
        const unsigned shown = (gl->draw < gl->n_images) ? gl->draw : 0u;
        const int frc = oops_display_flip_index(gl->display, gl->scanout_index[shown]);
        const uint64_t t_done = oops_time_get_us();

        if (frc == 0) {
            /* Give the frontend the other buffer for the next frame; the invalidate
             * makes it call `getBuffers` again. */
            if (gl->n_images > 1u) {
                /* Wait for the buffer about to be drawn into to leave the screen, or
                 * the title outruns the display and fills the flip queue. The wait is
                 * bounded (about 100 ms) and follows the flip that frees it. */
                (void)oops_display_wait_scanout(gl->display);

                gl->draw = (gl->draw + 1u) % gl->n_images;
                dri_invalidate_drawable(gl->drawable);
            }

            /* No read, mirror or display copy happened; the line keeps its shape so the
             * two paths compare column by column. */
            say_present_cost(t_start, t_flush, t_flush, t_flush, t_done);
            return true;
        }

        /* `oops_display_flip_index` returns -1 for every failure; the platform's code
         * is in the display's last error. */
        char msg[128];
        const int err = oops_display_get_last_error(gl->display);

        (void)snprintf(
            msg, sizeof msg,
            "direct scanout: flip refused (display last error 0x%x) - falling back to "
            "readback",
            (unsigned)err);
        say(msg);
        gl->registered = false;
    }

    if (gl->display == NULL || gl->scanbuf == NULL) {
        say("the frame was flushed, but the scanout output is not open, so it is not "
            "on screen");
        return false;
    }

    /* `glReadPixels` detiles radeonsi's colour buffer. `GL_BGRA` / `GL_UNSIGNED_BYTE`
     * gives 0xAARRGGBB words, the display's B8G8R8A8 order. */
    glReadPixels(0, 0, (GLsizei)gl->width, (GLsizei)gl->height, GL_BGRA,
                 GL_UNSIGNED_BYTE, gl->scanbuf);
    const uint64_t t_read = oops_time_get_us();

    /* GL's origin is the lower-left and the scanout is top-down, so mirror the frame in
     * place. */
    for (uint32_t y = 0; y < gl->height / 2u; y++) {
        uint32_t *top = gl->scanbuf + (size_t)y * gl->width;
        uint32_t *bot = gl->scanbuf + (size_t)(gl->height - 1u - y) * gl->width;
        for (uint32_t x = 0; x < gl->width; x++) {
            uint32_t tmp = top[x];
            top[x] = bot[x];
            bot[x] = tmp;
        }
    }
    const uint64_t t_mirror = oops_time_get_us();

    oops_display_present(gl->display, gl->scanbuf);
    const uint64_t t_done = oops_time_get_us();

    /* Once, not per frame, so a continuous title does not bury the log. */
    static bool said_first;
    if (!said_first) {
        said_first = true;
        say("the frame was read back, flipped upright and presented to the display");
    }

    say_present_cost(t_start, t_flush, t_read, t_mirror, t_done);
    return true;
}

void oops_gl_destroy(struct oops_gl *gl) {
    if (gl == NULL) {
        return;
    }
    /* Reverse creation order; each step tolerates the object never having been made. */
    if (gl->scanbuf != NULL) {
        free(gl->scanbuf);
    }
    if (gl->display != NULL) {
        oops_display_close(gl->display);
    }
    for (unsigned i = 0; i < 2u; i++) {
        if (gl->back[i] != NULL) {
            dri2_destroy_image(gl->back[i]);
        }
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

void oops_gl_extent(const struct oops_gl *gl, uint32_t *width, uint32_t *height) {
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

struct oops_display *oops_gl_display(struct oops_gl *gl) {
    return (gl != NULL) ? gl->display : NULL;
}
