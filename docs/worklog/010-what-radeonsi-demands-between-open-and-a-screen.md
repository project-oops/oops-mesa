# 010. What radeonsi demands between opening a device and having a screen, and the one thing that blocks it

**2026-09-14** - roadmap unit 5, the startup path traced

## What changed

The question "which of the refusing commands actually block a screen" was answered by reading
the startup path rather than by guessing, and the answer is short. `AMDGPU_INFO_ACCEL_WORKING`
is implemented. One step in the sequence is a hard blocker and now has a request against it.

## The sequence, in order

From `radeonsi_screen_create` through `amdgpu_device_initialize` and
`amdgpu_query_gpu_info_init` at the pinned versions:

| step | what it needs | state |
|---|---|---|
| `drmGetVersion` | major version 3 and the name `amdgpu` | done |
| `fcntl(F_DUPFD_CLOEXEC)` | a descriptor that can be duplicated | **open question** |
| `pthread_mutex_init` | the thread surface | unit 4, already mapped |
| `AMDGPU_INFO_ACCEL_WORKING` | non-zero, or it refuses the device | done this entry |
| `AMDGPU_INFO_DEV_INFO` | the device description | done, six groups assumed |
| `READ_MMR_REG 0x263e` | `GB_ADDR_CONFIG` | **blocked** |
| `amdgpu_va_manager_init2` | the address range from `DEV_INFO` | done |

Most of the register reads around it are skipped because they sit behind
`family_id < AMDGPU_FAMILY_AI` and this part reports a newer family. One is not:

```c
r = amdgpu_read_mm_registers(dev, 0x263e, 1, 0xffffffff, 0, &dev->info.gb_addr_cfg);
if (r) return r;
```

## Why that register is refused rather than filled in

`GB_ADDR_CONFIG` describes how memory is banked, interleaved and pipe-mapped, and the tiling
library computes every surface layout from it. Nothing in this collection has read it: oops-sdk,
obSCEne's census and orbistoun were all searched and none carries it.

A guess here would not fail. It would produce surfaces laid out incorrectly and read back as
corruption in some later frame, with nothing pointing at the register that caused it. That is the
worst shape of error this project can produce, and refusing is the only honest option.
`REQ-20260914T1712Z-5b60` is filed with three routes to a real value, one of which oops-gl could
run itself if the vendor library has no register read.

## What `ACCEL_WORKING` is allowed to claim

It means "is the graphics engine usable", and answering yes is not an assumption here: D003's
measurement put a command stream of ours on this queue next to the compositor and watched it
retire, for a minute at a time, twice (worklog 002). That is the one claim in the whole device
description that rests on a hardware run.

## Surprises

- **The blocker is one register, not a pile of missing commands.** Twelve commands were still
  refusing and the reasonable expectation was that several of them stood between here and a
  screen. Reading the actual path shows only one does, and it is not a command this shim can
  write its way out of.
- **`fcntl` on a descriptor that is not one.** libdrm duplicates the device descriptor with
  `F_DUPFD_CLOEXEC` and keeps the copy. The shim's descriptor is a token, not a kernel object, so
  there is nothing to duplicate. It has not been decided whether that becomes a fourth patched
  call site or whether the token should be a real descriptor from the platform. Naming it now so
  it is not discovered as a crash later.

## Next

Nothing in the startup path can move until `GB_ADDR_CONFIG` is answered, so the useful work
meanwhile is elsewhere: the `fcntl` question above, and unit 4's runtime shim, which the same
sequence needs for `pthread_mutex_init` and which is already fully mapped in
`docs/hardware/mesa-thread-surface-fw1240.md`.
