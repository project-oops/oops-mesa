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
#include "oops/time.h"

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
/* Bumps the drawable's stamp and clears its `texture_mask` (`dri2.c:97-103`), which is what makes
 * the frontend ask `getBuffers` again instead of keeping the image it already holds. Double
 * buffering needs it: without it the frontend draws into the same buffer for ever. */
extern void dri_invalidate_drawable(struct dri_drawable *drawable);
extern void driDestroyContext(struct dri_context *ctx);
extern void driDestroyDrawable(struct dri_drawable *drawable);
extern void driDestroyScreen(struct dri_screen *screen);
extern struct dri_image *dri_create_image(struct dri_screen *screen, int width, int height,
                                          int format, const uint64_t *modifiers,
                                          const unsigned count, unsigned int use,
                                          void *loaderPrivate);
extern void dri2_destroy_image(struct dri_image *image);
extern int dri2_query_image(struct dri_image *image, int attrib, int *value);

/*
 * What the drawable's colour buffer is for, restated from
 * `mesa/src/gallium/include/mesa_interface.h:540,550` at the pin.
 *
 * `SCANOUT` says the surface will be shown, which is now literally true - the display scans this
 * buffer rather than a copy of it.
 *
 * `FRONT_RENDERING` is there to **turn DCC off**, and it is the honest flag for it rather than a
 * lever that happens to work. radeonsi disables delta colour compression for a surface with
 * `PIPE_BIND_USE_FRONT_RENDERING` (`si_texture.c:236-242`, which needs
 * `modifier == DRM_FORMAT_MOD_INVALID` - true here, the image is created with none), and the
 * reason it does is that a compressed surface cannot be scanned out while it is also being drawn
 * into. That is exactly this arrangement.
 *
 * Why it matters, measured: with `use = 0` Mesa allocated **8896512** bytes, 49152 more than the
 * surface needs, and that excess is `surf->u.gfx9.color.display_dcc_size` - displayable DCC
 * metadata. oops-sdk registers the buffer with `dcc_control = 0`, so the display read
 * DCC-compressed colour as though it were raw pixels and put a sparse lattice on the panel
 * (obSCEne `REQ-20260921T1349Z-8b52`). The swizzle was never wrong: that request measured Mesa's
 * `64KB_R_X` as **bit-for-bit identical to the display tiler across all 135 blocks of a 1080p
 * frame**, zero mismatches.
 */
#define OOPS_DRI_IMAGE_USE_SCANOUT           0x0002
#define OOPS_DRI_IMAGE_USE_FRONT_RENDERING   0x0080

/* Restated from `mesa/include/GL/internal/dri_interface.h:1332,1333,1346,1347` at the pin, the way
 * every other value taken from a Mesa header in this file is: copied with its line, not recalled.
 * The modifier is a 64-bit DRM format modifier delivered as two halves. */
#define OOPS_DRI_IMAGE_ATTRIB_STRIDE         0x2000
#define OOPS_DRI_IMAGE_ATTRIB_HANDLE         0x2001
#define OOPS_DRI_IMAGE_ATTRIB_MODIFIER_LOWER 0x200B
#define OOPS_DRI_IMAGE_ATTRIB_MODIFIER_UPPER 0x200C
extern int driGetConfigAttrib(const struct dri_config *config, unsigned int attrib,
                              unsigned int *value);

/* The winsys side: this is how the loader learns which buffer Mesa actually allocated. */
extern void *oops_winsys_bo_cpu_range(uint32_t handle, uint64_t offset, uint64_t bytes);
/* Declared here rather than by including `oops_winsys.h`, which is how this file already reaches
 * the winsys. A title links with `--unresolved-symbols=ignore-all`, so a name that does not
 * resolve is not a link error - it is a fault on the first call.
 *
 * **Check it in the build's `.map` file, not with `nm`.** A packaged title is `-fvisibility=hidden`,
 * so an internal name is in neither the dynamic symbol table nor anything `nm` prints by default
 * - `nm` reports *zero* symbols for the whole ELF and `readelf --dyn-syms` shows two, so both
 * answer "absent" for every name in the module, including ones that are certainly there. The link
 * map lists it with an address and a size (worklog 050 used the same instrument to place a fault),
 * and a control name checked the same way is what proves the check can see anything at all. */
extern uint64_t oops_winsys_bo_size(uint32_t handle);
extern uint64_t oops_winsys_bo_gpu_va(uint32_t handle);

/*
 * The video-out calls are deliberately **not** declared here any more.
 *
 * An earlier version of D012 step 3 called `sceVideoOutRegisterBuffers2` from this file, reaching
 * the output through `oops_display_get_video_handle`. It worked as an experiment and it was the
 * wrong home: registering a buffer means re-registering the display's whole set, in registration
 * order, and that order is only knowable inside the display. `oops_display_adopt_buffer` in
 * oops-sdk does it there. What is left here is naming the buffer and flipping the index it gives
 * back, both through `<oops/display.h>`.
 */
extern int oops_winsys_open(void);

struct oops_gl {
    struct dri_screen *screen;
    struct dri_drawable *drawable;
    struct dri_context *context;
    /*
     * Two colour buffers, alternated, so the display never scans the one radeonsi is drawing.
     *
     * With one, direct scanout is correct and tears: the frame on the panel is the frame being
     * overwritten. With two, the frontend draws into `back[draw]` while the display shows the
     * other, and `draw` swaps at every present.
     *
     * The frontend picks up the swap because `dri_invalidate_drawable` bumps the drawable's stamp
     * and clears its `texture_mask` (`dri2.c:97-103`), which is what makes it ask `getBuffers`
     * again rather than keep the image it already has. That is the same mechanism a DRI3 loader
     * uses on a present-complete event.
     */
    struct dri_image *back[2];
    unsigned draw;      /* which of the two the frontend is drawing into */
    unsigned n_images;  /* how many exist: 1 if the second could not be made */

    uint32_t width;
    uint32_t height;
    /* The colour format, as an `enum pipe_format`, read from the chosen `dri_config` rather than
     * named by a constant. See the note in `oops_get_buffers`. */
    int format;
    int fd;

    /* D012 step 3. `gem` is the drawable image's buffer handle as Mesa reports it, `gem_bytes`
     * what the winsys allocated against it, and `direct` says the display has been told about
     * that buffer and will scan it - so present flips it and copies nothing. `direct_tried`
     * stops a failed registration being retried every frame. */
    int  gem[2];
    uint64_t gem_bytes[2];
    int  scanout_index[2]; /* the flip index the display gave each image, once adopted */
    bool registered;   /* the display has been told about `back` */
    bool direct_tried;

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
                            struct __DRIimageList *buffers);

/*
 * Make the drawable's colour buffer exist now, rather than when the frontend first asks.
 *
 * `oops_gl_create` needs it before it opens the display, because VideoOut takes the buffers it
 * will scan **once** and never again (obSCEne `-9a4c`), so a buffer that does not exist yet can
 * never be shown without a copy.
 *
 * It asks through `oops_get_buffers` rather than duplicating the creation, which matters for more
 * than tidiness: that function also reports the surface's stride, modifier and allocated size, and
 * a second creation path would either lose that or print it twice. The frontend's own later call
 * finds the image already set and simply hands it back.
 *
 * `which` selects one of the pair. `oops_get_buffers` builds whichever `gl->draw` names, so this
 * points `draw` at the one being asked for and puts it back afterwards - the frontend's own idea
 * of which buffer it is drawing into must not be moved by a call it did not make.
 */
static bool ensure_back_image(struct oops_gl *gl, unsigned which)
{
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
    made = oops_get_buffers(gl->drawable, 0, NULL, gl, __DRI_IMAGE_BUFFER_BACK, &list) != 0 &&
           gl->back[which] != NULL;
    gl->draw = saved;

    return made;
}

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

    const unsigned which = (gl->draw > 1u) ? 0u : gl->draw;

    if (gl->back[which] == NULL) {
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
        gl->back[which] = dri_create_image(gl->screen, (int)gl->width, (int)gl->height,
                                           gl->format, NULL, 0,
                                           OOPS_DRI_IMAGE_USE_SCANOUT |
                                               OOPS_DRI_IMAGE_USE_FRONT_RENDERING,
                                           gl);
        if (gl->back[which] == NULL) {
            say("Mesa could not allocate the drawable's colour buffer");
            return 0;
        }

        /*
         * What Mesa actually allocated, said once, because D012 rests on it.
         *
         * Both of D012's proposed fixes - copy the colour target into the scanout buffer, or
         * register it and copy nothing - assume this buffer is in the GPU's 64KB_R_X swizzle, the
         * same one `agc_display.c` scans out. That assumption is reasonable (addrlib's render
         * target choice at this size and format, and `tools/tiling-compare` gates the general
         * equivalence) and it has never been read back off this particular allocation. D012 says
         * so in as many words under "what is not established".
         *
         * The raw modifier is logged rather than decoded here. Decoding it would mean restating
         * `AMD_FMT_MOD`'s bit layout in this file, which is a second place for it to be wrong;
         * the value is small, it goes in the log beside everything else, and it is decoded against
         * Mesa's own headers afterwards with a citation. Measure here, conclude elsewhere.
         */
        int stride = 0;
        int mod_lo = 0;
        int mod_hi = 0;
        char what[128];

        /*
         * **Every one of these return values is checked, and the first version of this code did
         * not check them - which made its own answer unreadable.**
         *
         * `dri2_query_image` returns false and leaves `*value` untouched when the modifier is
         * `DRM_FORMAT_MOD_INVALID` (`dri2.c:1144-1153` at the pin). With the locals initialised to
         * zero and the result discarded, a failed query and a genuine `DRM_FORMAT_MOD_LINEAR`
         * (which *is* zero) produce identical output. The 2026-09-21 run reported
         * `modifier 0x0000000000000000` and it could not be told which of the two it meant.
         *
         * So the query's own answer is reported beside the value. `ok` means the modifier is real;
         * `unavailable` means Mesa declined, which is itself the useful reading - it says this
         * surface has no modifier to report rather than that it is linear.
         */
        int gem = 0;
        struct dri_image *const img = gl->back[which];
        const int have_stride = dri2_query_image(img, OOPS_DRI_IMAGE_ATTRIB_STRIDE, &stride);
        const int have_gem = dri2_query_image(img, OOPS_DRI_IMAGE_ATTRIB_HANDLE, &gem);
        const int have_lo =
            dri2_query_image(img, OOPS_DRI_IMAGE_ATTRIB_MODIFIER_LOWER, &mod_lo);
        const int have_hi =
            dri2_query_image(img, OOPS_DRI_IMAGE_ATTRIB_MODIFIER_UPPER, &mod_hi);

        (void)snprintf(what, sizeof what,
                       "colour buffer %u of 2, %ux%u: stride %d (%s, linear would be %u), modifier "
                       "0x%08x%08x (%s)",
                       which, (unsigned)gl->width, (unsigned)gl->height, stride,
                       have_stride ? "ok" : "unavailable", (unsigned)(gl->width * 4u),
                       (unsigned)mod_hi, (unsigned)mod_lo,
                       (have_lo && have_hi) ? "ok" : "unavailable");
        say(what);

        /*
         * The size, which is the one thing that does answer it.
         *
         * Neither half above discriminates: the modifier is `unavailable` because an image created
         * with `modifiers = NULL` has none, and 1920 is already a multiple of 128 so a `64KB_R_X`
         * surface's padded pitch equals the linear one. The **allocation** differs, because
         * addrlib pads a tiled surface's height to a multiple of 128:
         *
         *   linear     1920 x 1080 x 4 = 8294400
         *   64KB_R_X   1920 x 1152 x 4 = 8847360
         *
         * The GEM handle comes from Mesa and the size from the winsys's own record of what it was
         * asked for at `GEM_CREATE` - so neither number is derived from the width and height here,
         * which would answer the question with the assumption it is meant to test. Both expected
         * values are printed beside the measurement so the log decides it rather than a reader
         * doing arithmetic.
         *
         * `oops-sdk#REQ-20260920T0745Z-2d7f` predicts the answer: it measured the RDNA2 colour
         * block **dropping pixels** on a 1920x1080 target with a linear swizzle, and this probe's
         * `mod-pixels` matches the analytic triangle area exactly across four runs, so the target
         * is almost certainly already tiled. This measures it rather than resting on that.
         */
        if (have_gem) {
            const uint64_t bytes = oops_winsys_bo_size((uint32_t)gem);

            /* Kept for D012 step 3: this is a buffer the display will be asked to scan. */
            gl->gem[which] = gem;
            gl->gem_bytes[which] = bytes;

            const uint64_t linear = (uint64_t)gl->width * (uint64_t)gl->height * 4u;
            const uint64_t tiled = (uint64_t)gl->width *
                                   (((uint64_t)gl->height + 127u) & ~(uint64_t)127u) * 4u;

            (void)snprintf(what, sizeof what,
                           "colour buffer gem %d: %llu bytes (linear %llu, 64KB_R_X %llu) -> %s",
                           gem, (unsigned long long)bytes, (unsigned long long)linear,
                           (unsigned long long)tiled,
                           (bytes == 0u)      ? "handle not live in the winsys"
                           : (bytes >= tiled) ? "tiled"
                           : (bytes >= linear) ? "linear"
                                               : "neither - smaller than a linear frame");
            say(what);
        } else {
            say("colour buffer: Mesa would not report a GEM handle, so its size is not askable");
        }
    }

    buffers->back = gl->back[which];
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
     * The colour buffer is made **now**, before the display opens, and the order is the whole
     * point rather than tidiness.
     *
     * VideoOut buffer registration is single-shot: obSCEne `REQ-20260921T1202Z-9a4c` measured that
     * a second `sceVideoOutRegisterBuffers2` on a handle that already has buffers returns
     * `SCE_VIDEO_OUT_ERROR_SLOT_OCCUPIED` however it is shaped, that unregistration is not
     * exported at all, and that a concurrent handle is refused. There is exactly one moment at
     * which this drawable's buffer can be named to the display, and it is before the display
     * opens.
     *
     * Until today this happened the other way round - the display opened here and Mesa allocated
     * the colour buffer lazily, on the frontend's first `getBuffers`, by which point the one
     * registration was already spent. D009 called this correctly a week before it could be
     * measured: "Registration moves from 'at display open, before any GL exists' to 'once the
     * surface radeonsi will draw into exists'."
     *
     * Creating it here rather than waiting costs nothing: `oops_get_buffers` already caches
     * `gl->back` and hands back whatever is there, so the frontend gets this same image when it
     * asks. A failure is not fatal either - the display simply opens without it and present falls
     * back to reading the frame out, which is what it did before any of this existed.
     */
    void *adopt[2];
    int adopt_count = 0;

    /*
     * **Two buffers, and each has to prove it carries no compression before it is offered.**
     *
     * `FRONT_RENDERING` asks radeonsi to disable DCC and it does - but "it should" is exactly how
     * the first attempt put a sparse lattice on the panel. That surface was offered on the
     * strength of being tiled, while its size said, in a number written down and not chased, that
     * it carried 49152 bytes of something else: displayable DCC metadata, which
     * `oops_display_open_adopting` registers with `dcc_control = 0` and the display therefore
     * reads as raw pixels (obSCEne `-8b52`, oops-sdk D011, worklog 070).
     *
     * So the gate is the allocated size, exact rather than a tolerance. A plain `64KB_R_X` target
     * is the height padded to a multiple of 128 and nothing more - 1920 x 1152 x 4 = 8847360 at
     * 1080p. Anything larger is carrying metadata the display was not told about.
     *
     * Two images rather than one so the display never scans the buffer radeonsi is drawing into.
     * If only the first can be made the path still works and tears; if neither can, or either
     * fails the size gate, present falls back to reading the frame out, which is what it did
     * before any of this existed.
     */
    const uint64_t plain = (uint64_t)gl->width *
                           (((uint64_t)gl->height + 127u) & ~(uint64_t)127u) * 4u;

    for (unsigned i = 0; i < 2u; i++) {
        if (!ensure_back_image(gl, i)) {
            break;
        }
        gl->n_images = i + 1u;
    }

    for (unsigned i = 0; i < gl->n_images; i++) {
        const uint64_t va = (gl->gem[i] != 0) ? oops_winsys_bo_gpu_va((uint32_t)gl->gem[i]) : 0;
        char msg[128];

        if (va != 0 && gl->gem_bytes[i] == plain) {
            adopt[adopt_count] = (void *)(uintptr_t)va;
            adopt_count++;
        }

        (void)snprintf(msg, sizeof msg, "colour buffer %u: %llu bytes, plain is %llu - %s",
                       i, (unsigned long long)gl->gem_bytes[i], (unsigned long long)plain,
                       (va != 0 && gl->gem_bytes[i] == plain)
                           ? "no compression, offering it for scanout"
                       : (gl->gem_bytes[i] > plain) ? "carries metadata, so present copies"
                                                    : "unexpected size, so present copies");
        say(msg);
    }

    /* All or nothing: a pair where only one buffer is scannable would alternate between a flip
     * and a copy, which is worse to reason about than either on its own. */
    if (adopt_count != (int)gl->n_images) {
        adopt_count = 0;
        say("not every colour buffer is scannable, so present copies for all of them");
    }

    /*
     * Open the scanout output for the flip half of present, now that the GL stack is up and known
     * good. CPU tiling is left in place - `oops_display_try_gpu_tiler` is deliberately not called -
     * so the display converts frames on the CPU and never contends for the GPU queue radeonsi
     * drives; that is what lets the two run in one title. A failure here is not fatal: present
     * flushes and refuses the flip, exactly as it did before this existed.
     */
    gl->display = oops_display_open_adopting(OOPS_DISPLAY_BACKEND_AUTO, gl->width, gl->height,
                                             adopt, adopt_count);
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

/*
 * What a present actually costs, in four parts.
 *
 * Roadmap unit 6's gate has two halves and only one of them is met: the frame hashes
 * (`0x5188ddb7`, worklog 066), but "the swap costs milliseconds, not the 508 ms the CPU path costs
 * today" is still unmeasured *on this path*. The display's own line reports the back of it
 * (`AGC flip 1: tile=8066 submit=65`), so the tile and the flip submit are known - but the
 * `glReadPixels` detile and the vertical mirror in front of them have never been timed, and those
 * two are exactly what **D012 step 3** removes by rendering straight into a scanout buffer.
 * Optimising them before measuring them would be guessing at which one matters, and the 508 ms is
 * an inherited figure that has never been checked against this code.
 *
 * The four parts are disjoint and sum to the total, so the line says where the time goes rather
 * than only how much of it there is:
 *
 *   flush   `dri_flush_drawable` - finishing the frame on the GPU. This is the *render*, not the
 *           present; a large number here is radeonsi still working and is not what D012 step 3 is
 *           about, which is worth separating before anyone optimises the wrong half.
 *   read    `glReadPixels` - the detile, on the CPU, 1920x1080 words.
 *   mirror  the row swap, on the CPU, in place.
 *   disp    `oops_display_present` - the CPU tile plus the flip submit, which the AGC line
 *           already breaks down further.
 *
 * Logged for the first ten frames and every sixtieth after, which is `agc_display.c`'s own rule: a
 * probe presents once and sees it, and a title that presents continuously does not pay for a klog
 * line per frame. The formatted line stays well inside the ~128 bytes past which a klog line is
 * dropped in silence (orbistoun worklog 539, and the note on `oops_winsys_log`).
 */
static void say_present_cost(uint64_t t_start, uint64_t t_flush, uint64_t t_read,
                             uint64_t t_mirror, uint64_t t_done)
{
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
 * D012 step 3: tell the display to scan radeonsi's own colour buffer.
 *
 * # What this replaces, and what it is worth
 *
 * The present path built in worklog 065 reads the finished frame back with `glReadPixels`, mirrors
 * it, and hands the linear image to `oops_display_present`, which tiles it into a scanout buffer.
 * Measured on hardware (worklog 068): `read` 20442 us, `mirror` 3761 us, `disp` 8270 us against a
 * `flush` of 4195 us. **The GPU draws the frame in 4.2 ms and the CPU spends 32.5 ms delivering
 * it.** All three CPU parts exist only to move pixels out of one buffer and into another.
 *
 * They do not have to exist. The drawable's colour buffer is **tiled** - measured, 8896512 bytes
 * against a linear frame's 8294400 - and `agc_display.c` sets `tiling_mode = 0`, which its own
 * comment calls "tiled mode required by libSceVideoOut on this port". Both ends already agree.
 * So the buffer radeonsi renders into can be the buffer the display scans, and the present becomes
 * a flush and a flip.
 *
 * # Why this is registration and not a copy
 *
 * D009 settled the direction: Mesa cannot import a buffer this shim allocated, because libdrm
 * refuses a KMS handle import outright and the two remaining handle types are cross-process
 * mechanisms this platform does not have. The buffer therefore has to start on Mesa's side, and
 * the display has to be told about it. `sceVideoOutRegisterBuffers2` takes `data` as a plain
 * pointer, so there is no import to refuse.
 *
 * D009 also named the one thing it could not answer: "whether `sceVideoOutRegisterBuffers2`
 * constrains the address of a buffer it is given", and said the first attempt to register a
 * radeonsi buffer *is* the measurement. This function is that attempt.
 *
 * # What it does not do yet
 *
 * **One buffer, so it tears.** The image loader hands the frontend a single back image and this
 * registers that one, so the display scans the same memory radeonsi is drawing the next frame
 * into. Correct pixels, no copy, and a tear line - the honest first version. A second drawable
 * image alternated frame to frame is the fix, and it is a separate change with its own failure
 * modes.
 *
 * **It registers at index 2.** oops-sdk's display already registered its own two buffers at 0 and
 * 1 when it opened, and those stay: if this fails, or if the flip is refused, present falls back
 * to the readback path and the title keeps working rather than losing its display.
 */
static bool try_direct_scanout(struct oops_gl *gl)
{
    char msg[128];

    gl->direct_tried = true;

    if (gl->display == NULL || gl->n_images == 0u) {
        say("direct scanout: no display, or Mesa made no colour buffer for the drawable");
        return false;
    }

    /*
     * Nothing is registered here - it already happened, at `oops_display_open_adopting`. This only
     * asks where each buffer landed.
     *
     * Getting to that took three hardware runs and they are worth keeping straight, because two of
     * them looked like the same failure and were not. Registering a buffer at slot index 2 gave
     * `0x80290001` (`SCE_VIDEO_OUT_ERROR_INVALID_VALUE`, obscene D214/D301), which names no
     * argument; re-issuing the identical call with the display's *own* live address gave the same
     * code, which proved the **address** was never the objection - D009's long-open question,
     * answered no. Re-registering the whole set from inside the display then gave a different
     * code, `0x80290010`, and obSCEne `-9a4c` named it: `SCE_VIDEO_OUT_ERROR_SLOT_OCCUPIED`.
     * Registration is single-shot and immutable - no extending, no unregistering (those symbols do
     * not exist), no second handle.
     *
     * So there is exactly one moment to name a buffer, and it is before the display opens. That is
     * why `oops_gl_create` builds the colour buffers first and hands them to
     * `oops_display_open_adopting`, and why this function has nothing left to do but read the
     * indices back.
     *
     * **All of them or none.** A pair where only one buffer got an index would alternate between a
     * flip and a copy, which is harder to reason about than either alone and would show as a
     * stutter rather than a fault.
     */
    for (unsigned i = 0; i < gl->n_images; i++) {
        const int index = oops_display_adopted_index(gl->display, (int)i);
        const uint64_t va = (gl->gem[i] != 0) ? oops_winsys_bo_gpu_va((uint32_t)gl->gem[i]) : 0;

        (void)snprintf(msg, sizeof msg,
                       "direct scanout: buffer %u, VA 0x%llx (%llu bytes) has flip index %d",
                       i, (unsigned long long)va, (unsigned long long)gl->gem_bytes[i], index);
        say(msg);

        if (index < 0) {
            /*
             * The display did not take it. The reason is inside oops-sdk and does not reach this
             * log - `agc_log` there only emits through a logger callback this title does not
             * install - but it is not lost: a failed registration is folded into the display's
             * last error as `0xE4000000 | (rc & 0xFFFFFF)`, which is public. Without this line,
             * "-1" is indistinguishable between a refused registration, a display that never
             * became ready, and a buffer that was not offered at all.
             */
            const int err = oops_display_get_last_error(gl->display);

            (void)snprintf(msg, sizeof msg,
                           "direct scanout: no index for buffer %u (display last error 0x%x) - "
                           "present reads back and tiles instead",
                           i, (unsigned)err);
            say(msg);
            return false;
        }

        gl->scanout_index[i] = index;
    }

    (void)snprintf(msg, sizeof msg, "direct scanout: %u buffer%s, so present is a flush and a flip",
                   gl->n_images, (gl->n_images == 1u) ? " (it will tear)" : "s");
    say(msg);

    gl->registered = true;
    return true;
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
    const uint64_t t_start = oops_time_get_us();
    dri_flush_drawable(gl->drawable);
    const uint64_t t_flush = oops_time_get_us();

    /*
     * D012 step 3, tried once. If the display will scan radeonsi's buffer, the frame is already
     * where it needs to be and present is a flip: no readback, no mirror, no tile. If it will not,
     * everything below still runs and the title keeps its display.
     */
    if (!gl->direct_tried) {
        (void)try_direct_scanout(gl);
    }

    if (gl->registered) {
        const unsigned shown = (gl->draw < gl->n_images) ? gl->draw : 0u;
        const int frc = oops_display_flip_index(gl->display, gl->scanout_index[shown]);
        const uint64_t t_done = oops_time_get_us();

        if (frc == 0) {
            /*
             * Hand the frontend the *other* buffer for the next frame, so the display is never
             * scanning the one radeonsi is drawing into.
             *
             * `dri_invalidate_drawable` is what makes that stick. The frontend keeps the image it
             * was last given until the drawable's stamp moves; bumping it and clearing
             * `texture_mask` (`dri2.c:97-103`) is what makes it ask `getBuffers` again, and
             * `getBuffers` answers with whichever image `gl->draw` now names. It is the same
             * mechanism a DRI3 loader uses on a present-complete event.
             *
             * With one image this is a no-op that costs an invalidate, and the frame tears, which
             * is said once when the indices are read.
             */
            if (gl->n_images > 1u) {
                /*
                 * **Wait for the buffer we are about to draw into to leave the screen.**
                 *
                 * Without this the title outruns the display and the flip queue fills. Measured:
                 * a present costs about 4.76 ms here, almost all of it the GPU drawing, which is
                 * roughly 210 frames a second submitted into a queue obSCEne measured at 26 deep
                 * while the display consumes 60 a second. `mesa-cube` filled it in well under a
                 * second, `sceVideoOutSubmitFlip` refused, and this path fell back to reading the
                 * frame out for the rest of the run.
                 *
                 * `oops_display_wait_scanout` is the call for it - "the buffer that was on screen
                 * before the last one is free to draw into" - and it is the same pacing oops-sdk's
                 * own flip path uses. It bounds the wait at about 100 ms and reports a timeout
                 * rather than hanging, which is why a slow or stopped display degrades the frame
                 * rate instead of the title.
                 *
                 * It belongs after the flip rather than before it: the flip is what frees the
                 * other buffer, so waiting first would wait for something that has not been asked
                 * for yet.
                 */
                (void)oops_display_wait_scanout(gl->display);

                gl->draw = (gl->draw + 1u) % gl->n_images;
                dri_invalidate_drawable(gl->drawable);
            }

            /* `read`, `mirror` and `disp` are zero because they did not happen - the line keeps
             * its shape so a direct present and a copied one can be compared column by column. */
            say_present_cost(t_start, t_flush, t_flush, t_flush, t_done);
            return true;
        }

        /*
         * Flipped nothing. Say so once, with the *platform's* code rather than this shim's.
         *
         * `oops_display_flip_index` answers -1 for every failure and puts the real return in the
         * display's last error as `0xE4000000 | (rc & 0xFFFFFF)`. Logging `frc` alone printed
         * `0xffffffff`, which is this file's own sentinel and says nothing - the same mistake
         * `try_direct_scanout` had and had already fixed, repeated here because the two paths were
         * written hours apart.
         */
        char msg[128];
        const int err = oops_display_get_last_error(gl->display);

        (void)snprintf(msg, sizeof msg,
                       "direct scanout: flip refused (display last error 0x%x) - falling back to "
                       "readback",
                       (unsigned)err);
        say(msg);
        gl->registered = false;
    }

    if (gl->display == NULL || gl->scanbuf == NULL) {
        say("the frame was flushed, but the scanout output is not open, so it is not on screen");
        return false;
    }

    glReadPixels(0, 0, (GLsizei)gl->width, (GLsizei)gl->height, GL_BGRA, GL_UNSIGNED_BYTE,
                 gl->scanbuf);
    const uint64_t t_read = oops_time_get_us();

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
    const uint64_t t_mirror = oops_time_get_us();

    oops_display_present(gl->display, gl->scanbuf);
    const uint64_t t_done = oops_time_get_us();

    /* Said once, not per frame. This line was written when the only caller presented a single
     * frame and stopped, so an unconditional `say` cost one line. A title that presents
     * continuously turns the same line into one system-log write per frame, which buries every
     * other line in the log - including the cost report below, which is the one worth reading.
     * The first present is the interesting one: it is the one that might not have worked. */
    static bool said_first;
    if (!said_first) {
        said_first = true;
        say("the frame was read back, flipped upright and presented to the display");
    }

    say_present_cost(t_start, t_flush, t_read, t_mirror, t_done);
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
