# The Khronos CTS runs on the console: KHR-GL30.info, 6/6

**2026-09-25** · firmware 12.40 · the same retail unit as the earlier records · unit 8's first
result

The OpenGL Conformance Test Suite, unmodified from `opengl-cts-4.6.8.1`, executes on Prospero
hardware and reports results. This is unit 8's acceptance beginning to be met: a named subset,
run, recorded as data.

## The run

- **Title** `GCTS00001` (`oops-apps/src/oops-mesa/gl-cts`), packaged with SELFish, deployed with
  `pros probe`
- **Suite** VK-GL-CTS `opengl-cts-4.6.8.1`, peeled commit `067e8832315e`
- **Subset** `--deqp-case=KHR-GL30.info.*`, selected at run time, not compiled in
- **Driver** `amdgpu 3.54.0` through the Gallium DRI frontend and radeonsi
- **Target name** `OOPS` (`DEQP_TARGET_NAME`)

| case | result |
|---|---|
| `KHR-GL30.info.vendor` | Pass |
| `KHR-GL30.info.renderer` | Pass |
| `KHR-GL30.info.version` | Pass |
| `KHR-GL30.info.shading_language_version` | Pass |
| `KHR-GL30.info.extensions` | Pass |
| `KHR-GL30.info.render_target` | Pass |

    Passed:        6/6 (100.0%)
    Failed:        0/6 (0.0%)
    Not supported: 0/6 (0.0%)
    Warnings:      0/6 (0.0%)
    Waived:        0/6 (0.0%)

`dEQP returned 0`. Reproduced on four consecutive runs.

**Each case is a real frame, not a query.** The log shows `direct scanout: 2 buffers, so present
is a flush and a flip` and a present per case - 32 ms for the first, then 7.3 ms
(`flush=7321 read=0 mirror=0 disp=12`). The suite created a context, drew, and presented through
the same path `mesa-cube` uses.

## What this does not say

**Six `info` cases are not conformance.** They ask the driver what it is and check the answers are
well formed. Nothing here exercises a shader, a texture or a blend, and no claim about GL 3.3
conformance follows from it. What is established is that the harness runs: platform, context,
test hierarchy, execution and result reporting all work on hardware, which is what every larger
subset needs first.

**The `.qpa` is empty and the table above comes from the kernel log.** dEQP writes its result log
with `fopen`/`fprintf`/`fclose`, and on this platform the descriptor libc's `open()` returns
accepts writes, reports success at every step and stores nothing - `/data/GCTS00001/TestResults.qpa`
is created and stays 0 bytes. Measured three ways in the same process at the same path:

    stdio     fwrite 24/24 errno 0, fflush 0 errno 0, fclose 0 errno 0   -> read back 0 bytes
    raw       write  24/24 errno 0, fsync  0 errno 0, close  0 errno 0   -> read back 0 bytes
    oops_fs   oops_fs_write_all 0, oops_fs_read_all 0                    -> read back 24/24

`oops_fs_open` uses `SYS_open` directly and its descriptor works; the write call is identical in
all three arms. Filed as `oops-sdk REQ-20260925T1936Z-6c8d`. It does not affect the result - the
cases ran and passed - but a larger subset needs the log file, because thousands of cases will not
come back through klog.

## What it took to get here

Four defects in series, each only visible once the one before it was fixed, and none of them GL:

- **`.eh_frame` was discarded** by the title's link script, so `__eh_frame_start == __eh_frame_end`
  and the unwinder could not leave the frame that threw. dEQP reports everything but success by
  throwing, so every exception was fatal (`oops-apps`, `selfish#D105`).
- **`/app0` was read after the sandbox escape.** `oops_fs_storage_path` raises escape privileges
  to reach `/data`, which repoints the process root; `/app0` then names nothing. The device
  descriptor, the test resources and the argument file were all below it.
- **The device descriptor was claimed lazily**, so it was claimed after that escape. It is taken
  in `.init_array` now.
- **`os_dupfd_cloexec` bailed on the wrong errno.** This platform answers `F_DUPFD_CLOEXEC` with
  EOPNOTSUPP, not EINVAL, so patch 003's fallback to `F_DUPFD` - which works here - never ran, and
  `pipe_loader_drm_probe_fd` returned false without issuing a single ioctl.
  ([worklog 074](../worklog/074-the-errno-was-the-bug-and-worklog-049-had-it-wrong.md))
- **dEQP's GL dispatch is thread-local** and was compiled general-dynamic, so writing it called
  `__tls_get_addr` and faulted in libkernel. `-ftls-model=initial-exec`, which is what oops-mesa
  already uses for Mesa's own dispatch.

## Next

A subset that exercises the driver rather than describing it, chosen at run time through
`/app0/cts-args.txt`, with the log file working so the results come back as a `.qpa` rather than
as scrollback.
