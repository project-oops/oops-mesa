# D005 - The winsys shims the kernel interface, and libdrm is carried

**Status:** decided
**Date:** 2026-09-14

libdrm is a dependency, pinned through Mesa's own `subprojects/libdrm.wrap` and built with
Mesa. The winsys sits underneath it: a synthetic device descriptor, an `ioctl` that
dispatches the AMD command set onto the vendor graphics driver oops-sdk binds, and `mmap`.
libdrm's bookkeeping (VA ranges, buffer objects, command-stream assembly) stays upstream's.

**Why:** the kernel interface is one `ioctl` with 22 command cases plus `open`, `close`
and `mmap`, against 91 public functions for libdrm_amdgpu's API. Everything libdrm's AMD
layer does reaches the kernel through `drmCommandWriteRead`, `drmCommandWrite` and
`drmIoctl`, so an unimplemented command fails at one place. libdrm already supports
FreeBSD, and Mesa bundles the interface definitions at `include/drm-uapi`.

**Rejected:**
- Implementing libdrm_amdgpu's API against the vendor driver: four times the surface, and
  a hand transcription that can silently omit a call.
