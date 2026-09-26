# D010 - The platform shim is a DRI loader, not EGL

**Status:** decided
**Date:** 2026-09-17

The platform shim implements `__DRIimageLoaderExtension` and drives the Gallium DRI
frontend (`libdri.a`) directly. A title uses oops-mesa's entry points to create a context
and present, not `eglGetDisplay`, `eglCreateContext` and `eglSwapBuffers`.

**Why:** Mesa builds EGL only as a shared library (`shared_library()` in
`mesa/src/egl/meson.build:207` ignores `default_library`), and a title links archives. EGL's
surfaceless platform is itself a loader over the same DRI calls, so the loader here is
what EGL would wrap and nothing is lost if EGL arrives later. A title is oops-sdk-specific
before it draws, so EGL's portability buys nothing.

**Rejected:**
- A patch making EGL static: it could never go upstream and would be re-applied on every
  pin bump for a layer that adds no capability.
