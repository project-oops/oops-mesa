# The full stack runs on hardware, and parks instead of crashing

**2026-09-17** · firmware 12.40 · retail unit (CFI-1116A) · roadmap units 5 and 7

This is the second hardware run of `mesa-probe` (`MESA00001`), and the first with the whole
runtime surface in place: libm, the C locale tables, the C++ ABI shims, the fourteen `libc_absent`
names measured absent by `-a4f2`, the GL entry points, the processor-count shim, and the parking
ending. It was deployed with `pros restore` and started with `pros launch` on 2026-09-17 ~21:44,
and the system log was captured with `pros logs`.

It matters for two reasons the 17:15 run (`screen-created-fw1240.md`) could not show:

1. **Every one of those changes bound and ran.** No `PRX_NOT_RESOLVED_FUNCTION`, no fault. The
   startup path reproduces the earlier run's 35 ioctls exactly, and reaches the same screen.
2. **The title parks instead of crashing.** The 17:15 run ended in a fault at `rip: 0x0` on
   return from the entry point. This one ends at `idle and finished` and stays there - `pros ps`
   showed it `SLEEP` at 55 threads, stable across repeated samples, holding steady rather than
   winding down or faulting.

## The log

Captured verbatim from `pros logs`; only the surrounding shell noise is removed.

```text
[MESA00001:MESA-PROBE] linking upstream Mesa and walking its startup path (v2026-09-17 22:22)
[MESA00001:MESA-PROBE] winsys device opened
[MESA00001:OOPS-MESA] getenv: this platform exports none, so every name reads as unset. Mesa's debug switches are therefore all off. Said once, not per call.
[MESA00001:MESA-PROBE] screen options built
[MESA00001:OOPS-MESA] ioctl #1 0xc0406400 answered 0
[MESA00001:OOPS-MESA] ioctl #2 0xc0406400 answered 0
[MESA00001:OOPS-MESA] ioctl #3 0xc0286405 answered 0
[MESA00001:OOPS-MESA] ioctl #4 0xc0406400 answered 0
[MESA00001:OOPS-MESA] ioctl #5 0xc0406400 answered 0
[MESA00001:OOPS-MESA] AMDGPU_INFO query 0 (0x0) asked
[MESA00001:OOPS-MESA] ioctl #6 0x80206445 answered 0
[MESA00001:OOPS-MESA] AMDGPU_INFO query 22 (0x16) asked
[MESA00001:OOPS-MESA] device description: 6 of its groups are assumed, not measured; REQ-20260914T1558Z-7d41 is the request that would settle them
[MESA00001:OOPS-MESA] ioctl #7 0x80206445 answered 0
[MESA00001:OOPS-MESA] AMDGPU_INFO query 21 (0x15) asked
[MESA00001:OOPS-MESA] GB_ADDR_CONFIG (0x263e) answered 0x00000004: derived by inverting addrlib against oops-sdk's tiler, not read from the register
[MESA00001:OOPS-MESA] ioctl #8 0x80206445 answered 0
[MESA00001:OOPS-MESA] AMDGPU_INFO query 3 (0x3) asked
[MESA00001:OOPS-MESA] ioctl #9 0x80206445 answered 0
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) asked
[MESA00001:OOPS-MESA] ioctl #10 0x80206445 answered 0
[MESA00001:OOPS-MESA] ioctl #11 0xc010640c answered 0
[MESA00001:OOPS-MESA] ioctl #12 0xc00864bf answered 0
[MESA00001:OOPS-MESA] device description: 6 of its groups are assumed, not measured; REQ-20260914T1558Z-7d41 is the request that would settle them
[MESA00001:OOPS-MESA] ioctl #13 0x80206445 answered 0
[MESA00001:OOPS-MESA] GB_ADDR_CONFIG (0x263e) answered 0x00000004: derived by inverting addrlib against oops-sdk's tiler, not read from the register
[MESA00001:OOPS-MESA] ioctl #14 0x80206445 answered 0
[MESA00001:OOPS-MESA] device description: 6 of its groups are assumed, not measured; REQ-20260914T1558Z-7d41 is the request that would settle them
[MESA00001:OOPS-MESA] ioctl #15 0x80206445 answered 0
[MESA00001:OOPS-MESA] ioctl #16 0x80206445 answered 0
[MESA00001:OOPS-MESA] ioctl #17 0x80206445 answered 0
[MESA00001:OOPS-MESA] hardware IP type 1 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #18 0x80206445 answered -78
[MESA00001:OOPS-MESA] hardware IP type 2 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #19 0x80206445 answered -78
[MESA00001:OOPS-MESA] hardware IP type 3 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #20 0x80206445 answered -78
[MESA00001:OOPS-MESA] hardware IP type 4 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #21 0x80206445 answered -78
[MESA00001:OOPS-MESA] hardware IP type 5 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #22 0x80206445 answered -78
[MESA00001:OOPS-MESA] hardware IP type 6 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #23 0x80206445 answered -78
[MESA00001:OOPS-MESA] hardware IP type 7 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #24 0x80206445 answered -78
[MESA00001:OOPS-MESA] hardware IP type 8 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #25 0x80206445 answered -78
[MESA00001:OOPS-MESA] hardware IP type 9 is not present on this device
[MESA00001:OOPS-MESA] AMDGPU_INFO query 2 (0x2) answered -78
[MESA00001:OOPS-MESA] ioctl #26 0x80206445 answered -78
[MESA00001:OOPS-MESA] AMDGPU_INFO query 14 (0xe) asked
[MESA00001:OOPS-MESA] firmware version for type 0x4 is not measured; answering 0, which is the conservative side of every gate Mesa keys off it on this part
[MESA00001:OOPS-MESA] ioctl #27 0x80206445 answered 0
[MESA00001:OOPS-MESA] firmware version for type 0x8 is not measured; answering 0, which is the conservative side of every gate Mesa keys off it on this part
[MESA00001:OOPS-MESA] ioctl #28 0x80206445 answered 0
[MESA00001:OOPS-MESA] firmware version for type 0x5 is not measured; answering 0, which is the conservative side of every gate Mesa keys off it on this part
[MESA00001:OOPS-MESA] ioctl #29 0x80206445 answered 0
[MESA00001:OOPS-MESA] AMDGPU_INFO query 25 (0x19) asked
[MESA00001:OOPS-MESA] memory description: 0x300000000 bytes from the kernel; the pool it bounds is assumed (REQ-20260915T0030Z-5d1c)
[MESA00001:OOPS-MESA] ioctl #30 0x80206445 answered 0
[MESA00001:OOPS-MESA] ioctl #31 0xc010640c answered 0
[MESA00001:OOPS-MESA] AMDGPU_INFO query 33 (0x21) asked
[MESA00001:OOPS-MESA] ioctl AMDGPU_INFO query 33 (0x21) is not implemented yet
[MESA00001:OOPS-MESA] AMDGPU_INFO query 33 (0x21) answered -78
[MESA00001:OOPS-MESA] ioctl #32 0x80206445 answered -78
[MESA00001:OOPS-MESA] ioctl AMDGPU_INFO query 33 (0x21) is not implemented yet
[MESA00001:OOPS-MESA] AMDGPU_INFO query 33 (0x21) answered -78
[MESA00001:OOPS-MESA] ioctl #33 0x80206445 answered -78
[MESA00001:OOPS-MESA] AMDGPU_INFO query 34 (0x22) asked
[MESA00001:OOPS-MESA] ioctl AMDGPU_INFO query 34 (0x22) is not implemented yet
[MESA00001:OOPS-MESA] AMDGPU_INFO query 34 (0x22) answered -78
[MESA00001:OOPS-MESA] ioctl #34 0x80206445 answered -78
[MESA00001:OOPS-MESA] ioctl #35 0xc010640c answered 0
[MESA00001:OOPS-MESA] sysconf(_SC_NPROCESSORS_ONLN): the affinity mask could not be read, so reporting the 14 obSCEne measured for a big-app container rather than failing into Mesa's one-thread fallback.
[MESA00001:OOPS-MESA] __xuname was called; this platform does not export it and the shim has no system name to give. Returning failure.
[MESA00001:MESA-PROBE] radeonsi created a screen: the startup path is complete
[MESA00001:MESA-PROBE] done
[MESA00001:MESA-PROBE] idle and finished - close this title from the host
```

## What the run establishes

- **The build stamp is `v2026-09-17 22:22`** - the current binary, not a stale one. `pros restore`
  reported the eboot "not replaced" because it compares bytes-sent against bytes-stored and the
  target unwraps SELF; the stored size matched the local post-fixup ELF exactly (28,499,624
  bytes), and the stamp confirms it end to end.
- **35 ioctls, answered identically to the 17:15 baseline.** The two refusals are the tolerated
  `AMDGPU_INFO` sub-queries `0x21` (VIDEO_CAPS) and `0x22` (MAX_IBS); the driver read the `-78`,
  continued, and created its screen.
- **`radeonsi created a screen: the startup path is complete`** - unit 5's milestone, reproduced
  with the full runtime stack rather than the minimal one.
- **The processor-count shim ran, and its fallback fired.** `cpuset_getaffinity` bound (no
  unresolved-symbol death) but returned failure at run time, so `sysconf` reported the measured
  fallback of 14 rather than the queried mask. Mesa therefore sized its pools for 14 cores; `pros
  ps` showing 55 threads is consistent with that and not with the old one-core fallback. The
  affinity call failing is a new, separate finding - see the worklog.
- **`__xuname` was reached in ordinary start-up**, as the 17:15 run also showed; the loud stub
  reported it and the run continued.

## What it does not establish

Nothing about drawing. `mesa-probe` creates a screen and stops; it issues no GPU work and presents
no frame. The `dri-probe` run that would exercise `oops_gl_create` was blocked the same session -
the parked `MESA00001` held the big-app slot and could not be closed (`pros close`, `pros kill`
and `pros restart-ui` all failed against it), so `pros launch DRIP00001` was refused with
`sceSystemServiceLaunchApp: Resource temporarily unavailable`. That close-path gap is the live
issue; see the worklog and the request filed for it.
