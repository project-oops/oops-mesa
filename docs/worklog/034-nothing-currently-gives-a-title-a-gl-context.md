# 034. Nothing currently gives a title a GL context

**2026-09-17** - roadmap unit 6

## What this entry is

The plan was to trace what surfaceless EGL calls at swap, so the platform shim's entry point was
known rather than guessed. The trace stopped early, on something that had been sitting in plain
sight in a file this repository generates.

**EGL is not in the link order.** A title links 45 archives and none of them is EGL, so as things
stand there is no way for a title to obtain a GL context at all. Unit 6's acceptance criterion
names EGL and the build cannot deliver it.

D010 settles what to do instead.

## Why it is missing

Not an oversight in `build-mesa.sh`, which passes `-Degl=enabled` and sets
`default_library=static` precisely because a title links archives and this platform has no loader
for a `.so`. Every other component honours that. EGL does not:

```meson
libegl = shared_library(          # src/egl/meson.build:207
```

`shared_library()` overrides `default_library`, so the build produces a `libEGL.so` that nothing
here can load, and `tools/generate-imports.sh` correctly leaves it out of `build/link-order.txt`.

Worth noting how this stayed invisible: the build *succeeds*, `libEGL.so` *exists* in
`build/mesa/src/egl/`, and the roadmap says EGL, so every previous look at this had three pieces of
evidence agreeing that EGL was fine. What settled it was reading `link-order.txt` - the file that
records what a title actually links - rather than what the build produced.

## What is underneath, and that it is sufficient

`libdri.a` **is** in the link order, and the Gallium DRI frontend's interface is public C:

```
driCreateNewScreen3(scrn, fd, loader_extensions, type, &configs, inferred, multibuffer, data)
dri_create_drawable(screen, config, is_pixmap, loaderPrivate)
driCreateNewContext(screen, config, shared, data)
dri_make_current(ctx, draw, read)
driSwapBuffers(drawable)
```

Every one of those is reachable from a title today. That is a complete route from no context to a
swapped frame, with no EGL anywhere in it.

`loader_extensions` is the seam. EGL's surfaceless platform registers a
`__DRIimageLoaderExtension` whose `getBuffers` hands the frontend a drawable's colour buffer, and
allocates that buffer with `dri_create_image` - **Mesa allocates, the loader holds**.

That is D009 reached from the opposite direction. D009 argued the display must adopt Mesa's buffer
because libdrm refuses KMS-handle import; the DRI loader interface turns out to be built on exactly
that assumption. Two independent routes to the same arrangement is a better position than one
argument, and it was not planned - D009 was decided yesterday from libdrm's `-EPERM`, and this is
Mesa's own loader contract agreeing.

## The decision, briefly

D010: the shim implements `__DRIimageLoaderExtension` and drives the DRI frontend directly. No
EGL, no patch to upstream's build system.

The short of the reasoning: it is the same work either way, because `platform_surfaceless.c` is
three hundred lines of callbacks over exactly these calls; there is no portable title to protect,
since a title here already reaches `oops_display` and `scePad` before it draws anything; and a
build-system patch could never go upstream, so it would be re-applied on every pin bump forever
for a layer that adds no capability.

What it costs is stated in the decision rather than implied: a title calls an oops-mesa entry point
instead of `eglGetDisplay` / `eglCreateContext` / `eglSwapBuffers`. Three calls at startup and one
per frame.

Reversing it is one word in `src/egl/meson.build` and a numbered patch, and the loader written
under D010 is what EGL's surfaceless platform would sit on - so the order is additive rather than
exclusive.

## State

Roadmap unit 6 amended: DRI rather than EGL, with the reason and D010 named, and its status line
now says "designed, not written" with the four documents that design it. D010 written, decisions
index regenerated to ten entries.

No code changed; the platform shim still does not exist, but it now has a contract to be written
against rather than a guess. 104 host checks, `make check` passes. Nothing deployed.
