# 032. The import direction is closed at the libdrm level

**2026-09-17** - roadmap unit 6

## What this entry is

Worklog 030 left presentation with three open questions and a proposed design. This closes the
design question, by finding that the design does not work - and the reason is four lines of libdrm
rather than anything about this platform.

D009 carries the decision that replaces it.

## The proposal, and why it fails

Worklog 030 said: "The natural seam is `pipe_screen::resource_from_handle`, which is how a Linux
compositor hands Mesa a buffer; the winsys would answer it from `buffers.c`'s table rather than
from a DMA-BUF."

That is how it works on Linux, and the handle type that would carry it is
`amdgpu_bo_handle_type_kms` - a plain buffer handle, exactly what `buffers.c` hands out. libdrm
refuses it:

```c
case amdgpu_bo_handle_type_kms:
case amdgpu_bo_handle_type_kms_noimport:
        /* Importing a KMS handle in not allowed. */
        r = -EPERM;
        goto unlock;
```

Not "unimplemented". Refused, on purpose, before any ioctl is issued. The reasoning is sound on
Linux - a KMS handle is only meaningful within the process that owns it, so importing one is
either a no-op or a mistake - and it happens to close the one route that would have been
convenient here, where "within the process that owns it" describes everything.

The other two handle types are `gem_flink_name` and `dma_buf_fd`, which reach `GEM_OPEN` and
`drmPrimeFDToHandle`. Both exist to move a buffer between processes. Worklog 027 already recorded
both as refused because nothing on this console shares a buffer, and that is still true.

So Mesa will not accept a buffer this shim already owns, by any route.

## Which leaves the other direction, and it is the better one anyway

D009: the winsys allocates the colour target the way it allocates everything else, and the display
is told about *that* buffer. Registration moves from display open to after the first surface
exists.

Two facts make this natural rather than a consolation:

**The memory is the same kind.** `agc_display.c` uses `sceKernelAllocateDirectMemory` and
`sceKernelBatchMap`; `buffers.c` uses `oops_mem_alloc_direct` and `oops_mem_batch_map`, which are
the SDK's wrappers over those two calls. A radeonsi colour target is the same allocation reached
through a different front door, not a different species of memory.

**The layout is the same**, which is what worklog 031 settled yesterday with a control. Had that
comparison come out the other way, this decision would have had to be the blit, because a buffer
the display cannot interpret is not worth registering. It is worth noticing that the two entries
landed in the right order by luck rather than by plan: the tiling question was chased because it
was an unproven claim, and it turned out to be the load-bearing input to a decision made the next
day.

**The blit stays written down** as the fallback rather than the plan. Because the layouts match it
would be a `memcpy` rather than a retile, which is the cheap version of an expensive idea: 8.3 MB
per frame at 1920x1080, every frame, for nothing.

## The one thing that is unknown, and why it is not filed

Whether `sceVideoOutRegisterBuffers2` constrains the address of a buffer it is handed.
`agc_display.c` maps at `AGC_VM_BASE` = `0x40_0000_0000` and registers those pointers; nothing says
that address is required rather than chosen.

It is bounded. `device_info.c` reports radeonsi a virtual address range of `0x2_0000_0000` to
`0x400_0000_0000`, and `0x40_0000_0000` sits inside it - so if the display does want that
neighbourhood, the shim places the surface there through a range it already controls.

No obSCEne request. The question needs a surface to register before it can be asked, and asking it
now would mean describing a hypothetical rather than requesting a measurement. The first attempt to
register a radeonsi buffer is the measurement. This is the same line worklog 029 drew about sparse:
a request with a reason behind it beats a request filed because a gap was noticed.

## State

No code changed; the platform shim still does not exist. D009 written, decisions index regenerated.
104 host checks, 0 failed. `make check` passes including the new tiling gate. Nothing deployed.
