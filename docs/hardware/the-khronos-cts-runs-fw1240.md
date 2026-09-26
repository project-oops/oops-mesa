# Khronos CTS KHR-GL30.info on hardware

Firmware 12.40, retail unit, 2026-09-25.

- Title `GCTS00001` (`oops-apps/src/oops-mesa/gl-cts`), packaged with SELFish, deployed with
  `pros probe`
- Suite VK-GL-CTS `opengl-cts-4.6.8.1`, peeled commit `067e8832315e`, unmodified
- Subset `--deqp-case=KHR-GL30.info.*`, selected at run time
- Driver `amdgpu 3.54.0` through the Gallium DRI frontend and radeonsi
- Target name `OOPS` (`DEQP_TARGET_NAME`)

## Results

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

`dEQP returned 0`, on four consecutive runs. The log shows `direct scanout: 2 buffers, so present
is a flush and a flip` and one present per case: 32 ms for the first, then 7.3 ms
(`flush=7321 read=0 mirror=0 disp=12`). The `info` cases query and check the driver's
self-description; they exercise no shader, texture or blend.

## Result file

The table comes from the kernel log. dEQP writes its result log with `fopen`/`fprintf`/`fclose`;
`/data/GCTS00001/TestResults.qpa` is created and stays 0 bytes. Three arms in the same process at
the same path:

    stdio     fwrite 24/24 errno 0, fflush 0 errno 0, fclose 0 errno 0   -> read back 0 bytes
    raw       write  24/24 errno 0, fsync  0 errno 0, close  0 errno 0   -> read back 0 bytes
    oops_fs   oops_fs_write_all 0, oops_fs_read_all 0                    -> read back 24/24

`oops_fs_open` uses `SYS_open` directly; the write call is identical in all three arms.

## Platform requirements

- The title's link script keeps `.eh_frame`; dEQP reports every non-success by throwing.
- `/app0` is read before `oops_fs_storage_path` raises privileges to reach `/data`, which
  repoints the process root. The device descriptor is claimed in `.init_array`.
- This platform answers `F_DUPFD_CLOEXEC` with EOPNOTSUPP; patch 003 falls back to `F_DUPFD` on
  it. Without that, `pipe_loader_drm_probe_fd` returns false before any ioctl.
- dEQP's thread-local GL dispatch is compiled `-ftls-model=initial-exec`; general-dynamic calls
  `__tls_get_addr` and faults in libkernel.
