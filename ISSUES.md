# Issues

Open defects, gaps and unmeasured facts, one line each. Delete a line when it is fixed.

- Sparse `VA_OP_REPLACE`/`CLEAR` is refused though Mesa advertises sparse.
- `ARB_sync` works only because submission is synchronous.
- Asynchronous submission is blocked on whether a `WAIT_REG_MEM` packet inside a DCB stalls the GPU.
- `transform_feedback.draw_xfb_stream_test` hangs the GPU. GFX10 streamout uses no GDS; the CP waits in its sequence are the suspects: `WAIT_REG_MEM` on `CP_STRMOUT_CNTL` (`mesa/src/amd/common/ac_cmdbuf.c:1042`) and `STRMOUT_BUFFER_UPDATE` from memory (`si_state_streamout.c:322`).
- The CTS `.qpa` file is empty because libc `open()` writes store nothing.
- `KHR-GL46` has not been run.
- Compute has not been run. `launch_grid` submits on the graphics ring, so the refused COMPUTE `HW_IP_INFO` does not block it; the first run reaching it names what does.
- Tessellation has not been run. radeonsi allocates its own factor ring and writes `VGT_TF_RING_SIZE`/`VGT_TF_MEMORY_BASE` from the command buffer (`ac_cmdbuf_cp.c:359-367`); whether that holds against the driver-global ring `sceAgcDriverSetTFRing` sets is unmeasured.
- A registered VideoOut buffer set cannot be extended (0x80290010); replacing it through `sceVideoOutUnregisterBuffers` is unmeasured, and oops-sdk does not bind that call.
- RADV builds and links but no title runs it: `vulkaninfo` needs a Vulkan loader, or a title that calls RADV's ICD entry points from the archives.
- RADV maps buffers with libc `mmap()` (`radv_amdgpu_bo.c:744`), not the `drm_mmap` patch 001 redirects, so on the device descriptor it maps the title's eboot; `munmap` likewise escapes patch 004.
- RADV's render node is `/app0/eboot.bin`, so physical-device creation fails its `stat` after a title escapes to `/data`.
- RADV's fences need `SYNCOBJ_WAIT`/`RESET`/`SIGNAL`, which the winsys refuses, and its secondary command buffers need IB2, which `submit.c` takes for a tail chain.
- Patch 005 gives radeonsi's loader a PCI identity (vendor 0x1002, device 0) it lacked; GL has not been run on hardware since.
- `pros close` cannot end a parked title.
- `preamble_dump.c` may use the pre-GFX10 `pbb_max_alloc_count` (128 versus 341).
- `runtime_test` FAIL lines are swallowed by its own `write` override.
- The winsys host test does not link (oops-sdk's `memory.c` needs `oops_kprintf_level`).
- Device-info values are assumed: `CONFORMANT_TRUNC_COORD`, the single memory pool, INFO `0x21`/`0x22`.
- `src/winsys/{buffers,log,submit}.c` and `oops_winsys.h` need the formatter and comment pass; `oops_winsys.h` cites the deleted D012, and `log.c`/`submit.c` cite an orbistoun worklog.
