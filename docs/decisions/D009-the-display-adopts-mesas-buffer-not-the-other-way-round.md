# D009 - The display adopts Mesa's buffer, not the other way round

**decided** · 2026-09-17

Presentation has to end with a buffer that `sceVideoOutRegisterBuffers2` has been told about and
that radeonsi has drawn into. There are two ways to arrange that, and worklog 030 proposed the one
that turns out not to be available.

## The seam that is closed

Worklog 030 suggested `pipe_screen::resource_from_handle` - how a Linux compositor hands Mesa a
buffer it did not allocate. It cannot work here, and not because of anything this shim does.
libdrm refuses before the shim is reached:

```c
case amdgpu_bo_handle_type_kms:
case amdgpu_bo_handle_type_kms_noimport:
        /* Importing a KMS handle in not allowed. */
        r = -EPERM;
        goto unlock;
```

The other two handle types are `gem_flink_name`, which needs `GEM_OPEN`, and `dma_buf_fd`, which
needs `drmPrimeFDToHandle`. Both are mechanisms for passing a buffer *between processes*, and this
platform has neither. Worklog 027 already recorded both as refused for that reason.

So there is no route by which Mesa accepts a buffer this shim already owns. The import direction
is closed.

## The decision

**The winsys allocates the colour target the way it allocates everything else, and the display is
told about that buffer.** Registration moves from "at display open, before any GL exists" to "once
the surface radeonsi will draw into exists".

Three things make this the natural direction rather than the leftover one.

**The memory is already the same kind.** `agc_display.c` allocates its scanout buffers with
`sceKernelAllocateDirectMemory` and maps them with `sceKernelBatchMap`. `buffers.c` allocates with
`oops_mem_alloc_direct` and maps with `oops_mem_batch_map`, which are the SDK's wrappers over the
same two calls. A radeonsi colour target is not a different species of memory from a scanout
buffer; it is the same allocation reached through a different front door.

**The layout is already the same.** This is what worklog 031 settled, and it is the fact the
decision rests on. A radeonsi `64KB_R_X` colour target is byte-for-byte in the layout
`agc_tile_surface` produces, across blocks and not merely within one, with a control proving the
comparison can see a difference. Had that come out the other way this decision would be the
opposite one, because a buffer the display cannot interpret is not worth registering.

**The alternative costs a frame-sized copy, every frame.** Blitting from radeonsi's target into a
pre-registered buffer works and needs no new plumbing - and because the layouts match it is a
`memcpy` rather than a retile. At 1920x1080 that is 8.3 MB per frame of pure bandwidth for
nothing. It stays written down as the fallback if registration turns out to be constrained, not as
the plan.

## What is not established

**Whether `sceVideoOutRegisterBuffers2` constrains the address of a buffer it is given.**
`agc_display.c` maps its buffers at `AGC_VM_BASE` = `0x40_0000_0000` and registers the mapped
pointers. Nothing in that file says the address is required rather than chosen, and nothing in the
collection has tried another one.

This is bounded rather than open-ended, which is why it does not block the decision. `device_info.c`
reports radeonsi a virtual address range of `0x2_0000_0000` to `0x400_0000_0000`, and
`0x40_0000_0000` is inside it - so if the display does turn out to want that neighbourhood, the
shim can place the surface there through the range it already controls, rather than needing a new
mechanism.

No request is filed. The question only becomes answerable when there is a surface to register, and
asking it before then would mean describing a hypothetical to obSCEne instead of a measurement.
The first attempt to register a radeonsi buffer *is* the measurement.

## What this leaves for the platform shim

- Registering after the first surface exists, rather than at display open, and re-registering if
  the surface is recreated.
- Which of the two buffers is being drawn into, and the flip. oops-sdk owns this already -
  `sceVideoOutSubmitFlip` and a flip queue measured 26 deep (`REQ-20260909T1020Z-a51e`) - so the
  shim schedules rather than implements.
- The format word. The display is handed `0x8000000000000000` with 32-bit pixels and nothing in
  the collection says which Gallium format that is. Worklog 030 lists it; it is still open.

## What would reverse this

A measurement showing the display will only scan out of memory allocated in some way
`oops_mem_alloc_direct` cannot produce - a particular pool, a particular alignment, a reserved
aperture. Then the import direction becomes necessary despite being closed at the libdrm level,
and the answer is the blit, not a patch to libdrm.
