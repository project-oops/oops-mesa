# Full runtime stack on hardware

Firmware 12.40, retail unit (CFI-1116A). Title `mesa-probe` (`MESA00001`), build
`v2026-09-17 22:22`, 2026-09-17. Deployed with `pros restore`, started with `pros launch`, log
captured with `pros logs`.

The build carries the whole runtime surface: libm, the C locale tables, the C++ ABI shims, the
fourteen `libc_absent` names, the GL entry points, the processor-count shim and the parking
ending. Every import binds: there is no `PRX_NOT_RESOLVED_FUNCTION` and no fault. The title ends
at `idle and finished` and parks; `pros ps` shows it `SLEEP` at 55 threads, stable across
repeated samples. The startup ioctls match [the screen-created record](screen-created-fw1240.md).

## Captured log

Verbatim from `pros logs`, with the surrounding shell noise removed.

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

## Facts from the log

- The build stamp `v2026-09-17 22:22` is the deployed binary. `pros restore` reports the eboot
  "not replaced" because it compares bytes sent against bytes stored and the target unwraps SELF;
  the stored size matches the local post-fixup ELF (28,499,624 bytes).
- 35 ioctls. The two refusals are the `AMDGPU_INFO` sub-queries `0x21` (VIDEO_CAPS) and `0x22`
  (MAX_IBS); the driver reads the `-78`, continues and creates its screen.
- `cpuset_getaffinity` binds but returns failure at run time, so `sysconf` reports the measured
  fallback of 14 and Mesa sizes its pools for 14 cores, consistent with the 55 threads in
  `pros ps`.
- `__xuname` is reached in ordinary start-up; the stub reports it and the run continues.

`mesa-probe` creates a screen and stops; it issues no GPU work and presents no frame. A parked
`MESA00001` holds the big-app slot: `pros close`, `pros kill` and `pros restart-ui` do not end it,
and a following `pros launch DRIP00001` is refused with
`sceSystemServiceLaunchApp: Resource temporarily unavailable`.
