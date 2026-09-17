# D010 - The platform shim is a DRI loader, and EGL waits

**decided** · 2026-09-17

The roadmap's unit 6 says "a triangle through EGL and OpenGL 3.3". EGL is not available to a title
here, and the thing underneath it is - so the shim is written against that instead, and the
roadmap changes rather than the build.

## EGL is built, and cannot be linked

`build-mesa.sh` passes `-Degl=enabled` and `default_library=static`, because a title links archives
and this platform has no loader for a `.so`. Every other component honours that. EGL does not:

```meson
libegl = shared_library(          # src/egl/meson.build:207
```

`shared_library()` ignores `default_library`, so the build produces `libEGL.so` and nothing else.
It is absent from `build/link-order.txt` for that reason, and a title that wanted it would have
nothing to link.

Making it static is a one-word patch to upstream's build system. That is the option this decision
declines.

## What is underneath it, and that it is enough

EGL's surfaceless platform is a loader over the Gallium DRI frontend, which *is* in the link order
as `libdri.a`. Its whole interface is public C:

```
driCreateNewScreen3(scrn, fd, loader_extensions, type, &configs, inferred, multibuffer, data)
dri_create_drawable(screen, config, is_pixmap, loaderPrivate)
driCreateNewContext(screen, config, shared, data)
dri_make_current(ctx, draw, read)
driSwapBuffers(drawable)
```

`loader_extensions` is where the shim lives. `platform_surfaceless.c` registers a
`__DRIimageLoaderExtension` whose `getBuffers` hands the frontend the colour buffer for a drawable,
and allocates that buffer with:

```c
return dri_create_image(dri2_dpy->dri_screen_render_gpu, width, height, visual, NULL, 0, 0, NULL);
```

**Mesa allocates; the loader holds.** Which is D009 arrived at from the other end: the buffer comes
out of Gallium, through radeonsi, through this repository's `GEM_CREATE`, and the shim is the
thing that knows its handle and can hand it to `sceVideoOutRegisterBuffers2`.

## The decision

The platform shim implements `__DRIimageLoaderExtension` and drives the DRI frontend directly. No
EGL, and no patch to upstream's build.

**It is the same work either way.** `platform_surfaceless.c` is about three hundred lines of loader
callbacks over exactly these calls. Writing the loader is writing what EGL would have wrapped, not
an alternative to it - so nothing here is thrown away if EGL arrives later.

**There is no portable title to protect.** EGL's value is that title code moves between platforms.
A title here already opens its display through `oops_display` and reads its pad through `scePad`;
it is oops-sdk-specific before it draws anything. Paying a patch and a second API layer to make one
seam portable, in a program that is portable nowhere else, buys nothing.

**A build-system patch is the kind worth avoiding.** It could never go upstream - upstream is right
that EGL is a shared library on the platforms it supports - so it would be re-applied on every pin
bump forever, for a layer that adds no capability. Principle 1 asks that a patch be something that
could not be a shim. This one could be nothing at all.

## What this costs, stated plainly

A title uses an oops-mesa entry point rather than `eglGetDisplay` / `eglCreateContext` /
`eglSwapBuffers`. Anyone porting GL code written against EGL adapts three calls at startup and one
per frame. That is the whole of it, and it is worth saying rather than implying it is free.

## Reversing this

One word in `src/egl/meson.build` and a numbered patch. The loader written under this decision is
what EGL's surfaceless platform would sit on, so the order is additive: DRI loader first, EGL over
it if a title ever needs the API. The reverse order would have been the expensive one.

The roadmap's unit 6 is amended to say DRI rather than EGL, with this decision named.
