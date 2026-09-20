# 053. The high VA unblocked the allocation, and uncovered a GEM_VA encoding the dispatch missed

**2026-09-19** - roadmap unit 6

## The high-VA fix worked

The run with worklog 052's populated `high_va` no longer fails the VA allocation. The four
`failed to allocate 2097152 bytes from the 64-bit address space` lines are **gone** - radeonsi's
userspace VA allocator now has a non-empty high range to draw from, so it proceeds from *allocating*
a VA to *mapping* it, which it does through the `GEM_VA` ioctl. New ground again: the frontend now
issues `GEM_VA` for the first time. Capture:
[`docs/hardware/context-fails-va-alloc-fw1240.log`](../hardware/context-fails-va-alloc-fw1240.log)
is the pre-fix run; the post-fix run is in the session's scratch and is summarised here.

## The next wall: GEM_VA arrives in an encoding the dispatch did not match

```
ioctl 0xc0406448 is not one this shim knows   ->  answered -22   (x4)
```

`0xc0406448` is `GEM_VA` (nr `0x48` = `DRM_COMMAND_BASE + 0x08`) with a **64-byte** arg and the
`_IOWR` direction. The shim's dispatch matched only `DRM_IOCTL_AMDGPU_GEM_VA`, which the header
defines with `DRM_IOW` (`0x40406448`). The gap is the direction bit:

- libdrm issues `GEM_VA` through `drmCommandWriteRead`
  (`mesa/subprojects/libdrm-2.4.133/amdgpu/amdgpu_bo.c:794,830` at the pin), which builds the
  request as `_IOWR` from the struct size, regardless of the header macro.
- `GEM_VA` is the one call in this family whose header macro alone says write-only, because the
  struct grew a returned field (the vm-timeline syncobj out) but the macro was never moved to
  `_IOWR` to match. Every other amdgpu ioctl in the log matched because its macro's direction
  happened to agree with what libdrm sends.

This is the "one ioctl, two encodings" shape worklog 038 named, biting at a new call. Because the
shim does an exact `switch` on the request (stricter than a real kernel, which dispatches by `nr`),
the mismatch drops every real `GEM_VA` to the unknown-command default and `-22` (EINVAL).

## The fix

`drm_device.c` now matches both encodings for `GEM_VA` - the header's `DRM_IOW` form and the
`DRM_IOWR(..., struct drm_amdgpu_gem_va)` form `drmCommandWriteRead` actually sends. The handler
(`buffers.c` `oops_winsys_gem_va`) is unchanged; it already reads the extended struct's fields,
all of which sit in the unchanged leading bytes. A regression check was added to
`test_a_buffer_through_its_whole_life`: it undoes the map through the `_IOWR` encoding over a real
fd and asserts it routes to `gem_va` rather than being refused. Host suite now 111/0.

## State / next

Title rebuilt (v2026-09-19 18:35), imports clean, tree check passes. With the dispatch fixed, the
next run reaches the part that actually tests worklog 052's premise: `oops_winsys_gem_va` calling
`oops_mem_batch_map` at the chosen high VA. Two outcomes, both informative:

- the platform **maps** it -> context creation continues, and for the first time a run can reach GL
  make-current, `glGetString`, and then the frame flush in `oops_gl_present` - which is the **first
  GPU submission** this project has ever issued;
- the platform **refuses** the address -> `GEM_VA` returns `-ENOMEM`, the BO alloc fails, the
  context fails cleanly with no submission and no GPU-ring hang, and the shim logs the exact VA it
  tried (`buffers.c:284`) - which, with `-5af3`, pins where the mappable window is.

The second is the more likely first result, because the chosen high range sits just above the one
address (`0x2_0000_0000`) measured to map and nothing has yet confirmed the extent. Either way the
run is safe up to the point a map succeeds; the first *successful* submission is the new frontier,
and its own risks (a frame that does not retire) belong to the run after a context exists.
