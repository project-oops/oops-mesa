# 052. Config enumeration passes on the heap fix; the next wall is an empty high VA range

**2026-09-19** - roadmap unit 6

## The heap fix carried the frontend past worklog 050's crash

`dri-probe` was re-run with the App-Heap-Mode `procparam` (worklog 051, obSCEne `-7b90`).
`driConcatConfigs`, which SIGSEGV'd in worklog 050 when the internal-fallback heap was starved,
now succeeds:

```
[OOPS-GL] 950 configs offered; one is 8888 double-buffered, depth 32
```

The title then ran to a **clean park** - no crash, no fatal signal - which by itself separates
this from 050, where the process died with a page fault at `rip 0x010a0bf0`. Fourteen ioctls
(#40-#53) that no run had ever reached. The heap fix is verified end to end. The capture is
[`docs/hardware/context-fails-va-alloc-fw1240.log`](../hardware/context-fails-va-alloc-fw1240.log).

**A capture lesson worth keeping.** The first re-run missed the klog entirely: the `pros logs`
listener and the `launch` raced, and a title that *parks* emits a tight burst and then goes silent
- unlike 050's crash, which kept emitting into the crash handler and was easy to catch. The fix is
to subscribe the listener, wait a few seconds, then launch. A parked good run is *harder* to
capture than a crash, which is counter-intuitive and cost one wasted launch.

## The new wall: radeonsi asks for a high VA, and the shim answered with none

Past config enumeration the frontend creates a context, and radeonsi asks libdrm for the command
buffer's (IB) GPU VA with `AMDGPU_VA_RANGE_HIGH` set **unconditionally**
(`mesa/src/gallium/winsys/amdgpu/drm/amdgpu_bo.c:690` at the pin). libdrm draws that from
`dev_info.high_va_offset .. high_va_max`, which `device_info.c` had answered `0/0` since worklog
026 - deliberately, on the reasoning that nothing handed out a high virtual address. That premise
expired the instant a context was created. The empty high manager fails the first IB allocation,
and the failure cascades exactly as the code is written to:

```
MESA: error: amdgpu: failed to allocate 2097152 bytes from the 64-bit address space   (x4)
MESA: error: amdgpu: failed to create IB buffer: size=32768
radeonsi: error: can't create gfx_cs
radeonsi: error: Failed to create a context.
[OOPS-GL] the GL context would not be created
```

`oops_gl_create` returns NULL and the title parks. This is a shim gap, not an upstream bug:
the winsys owns the VA ranges it reports.

## The fix, and why the value is what it is

`device_info.c` now populates `high_va`. The value cannot be arbitrary, because the winsys binds
each buffer at **exactly** the VA libdrm picks (`src/winsys/buffers.c:282`, GEM_VA maps the pages
at `arg->va_address`) - so an unmappable range fails the map, it does not merely mislabel it. The
one address measured to map is oops-gl's `0x2_0000_0000`; the extent above it is not measured. So:

- the **high** range is placed just above that base (`0x4_0000_0000 .. 0x24_0000_0000`), because
  radeonsi lands essentially every allocation in HIGH and this gives them an address as close to
  the proven one as an ordered layout allows;
- the **general** range keeps the proven base and is shrunk (`0x2_0000_0000 .. 0x4_0000_0000`), so
  the two are disjoint and numerically ordered the way real amdgpu reports them (low below high).

Both extents are **assumed**, counted in the description's assumed total, and cited to
`REQ-20260919T1708Z-5af3` - filed to obSCEne this session. That request asks for the mappable
window *behaviourally* (map a buffer at candidate fixed addresses, report which take), because
`-7d41` established the device-info topology route is closed to unprivileged userland; a map/no-map
result is a different question that does not hit that wall.

## State / next

Host suite 109/0, runtime suite clean, tree check passes (pin mesa-26.2.2, 3 patches). The title
is rebuilt with the change. The next run tests one thing: whether the platform's GPU mapper binds a
VA near `0x2_0000_0000` for the context's IB.

- If it does, context creation proceeds into `dri_make_current` and `glGetString` - the first GL
  strings ever produced on this platform.
- If GEM_VA cannot map the range, the map is refused **before any submission**, so the failure is
  clean (no GPU ring hang, no wedged compositor) and `-5af3`'s answer chooses the range.

Not yet re-deployed; the hardware run is the user's to trigger.
