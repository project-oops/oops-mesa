# D001 - A shim under upstream Mesa's radeonsi, not a driver of our own

**Status:** decided
**Date:** 2026-09-26

oops-mesa owns three shims (winsys, platform, runtime) and a patch set, and nothing else.
Upstream Mesa, pinned as a submodule, provides the GL API, the GLSL compiler, the radeonsi
driver, the tiling library and the ACO shader backend.

**Why:** Mesa is built around this seam: the driver asks the platform for buffers, a
submit and a fence, and everything above that is generic. radeonsi is maintained upstream
for this GPU generation, so every register it programs is kept correct by upstream. The
hardware accepts radeonsi's full-state preamble on our queue (`tools/preamble-dump`). Mesa
is MIT, so shims written from our own measurements stay distributable with the collection.

**Rejected:**
- A gallium driver of our own over the vendor graphics library: roughly twice the shim,
  and a second copy of what radeonsi already does.
- Writing GL entry points or a shading-language compiler here: that is Mesa's work, and a
  change that needs it is an upstream contribution.
- oops-gl as the GL for users: it stays in oops-sdk as a measuring instrument, and the two
  never link into the same title.
