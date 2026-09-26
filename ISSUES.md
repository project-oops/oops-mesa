# Issues

Open defects, gaps and unmeasured facts, one line each. Delete a line when it is fixed.

- Sparse `VA_OP_REPLACE`/`CLEAR` is refused though Mesa advertises sparse.
- `ARB_sync` works only because submission is synchronous.
- Asynchronous submission is blocked on whether a `WAIT_REG_MEM` packet inside a DCB stalls the GPU.
- `transform_feedback.draw_xfb_stream_test` hangs the GPU.
- The CTS `.qpa` file is empty because libc `open()` writes store nothing.
- `KHR-GL46` has not been run.
- A registered VideoOut buffer set cannot be extended (0x80290010).
- `pros close` cannot end a parked title.
- `preamble_dump.c` may use the pre-GFX10 `pbb_max_alloc_count` (128 versus 341).
- `runtime_test` FAIL lines are swallowed by its own `write` override.
- The winsys host test does not link (oops-sdk's `memory.c` needs `oops_kprintf_level`).
- Device-info values are assumed: `CONFORMANT_TRUNC_COORD`, the single memory pool, INFO `0x21`/`0x22`.
- `src/winsys/{buffers,log,submit}.c` and `oops_winsys.h` need the formatter and comment pass; `oops_winsys.h` cites the deleted D012, and `log.c`/`submit.c` cite an orbistoun worklog.
