# radeonsi screen creation on hardware

Firmware 12.40, retail unit. Title `MESA00001` (`oops-apps/src/oops-mesa/mesa-probe`), 2026-09-17,
a build with Initial Exec TLS and without the DRI frontend. System log read from the console;
only the leading timestamps are removed.

## Captured log

```text
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
[MESA00001:OOPS-MESA] sysconf(58) is not answered here; this shim knows the page size and the page count and nothing else
[MESA00001:OOPS-MESA] sysconf(57) is not answered here; this shim knows the page size and the page count and nothing else
[MESA00001:OOPS-MESA] __xuname was called; this platform does not export it and the shim has no system name to give. Returning failure.
[MESA00001:MESA-PROBE] radeonsi created a screen: the startup path is complete
[MESA00001:MESA-PROBE] done
```

`radeonsi created a screen` is `mesa_probe_main.c`, line 206, reached only when
`radeonsi_screen_create` returns a non-null `pipe_screen`.

## The ioctl trace

The numbered trace is every ioctl from `amdgpu_get_auth` to the end of `ac_query_gpu_info`, in
order. 33 answer `0`. The `AMDGPU_INFO` sub-queries `0x21` (`VIDEO_CAPS`, asked twice by the
driver's own retry) and `0x22` (`MAX_IBS`) answer `-78` (`ENOSYS`), and the driver continues to
create its screen. `0x80206445` is the `AMDGPU_INFO` command; `0xc010640c` is its sibling on the
generic path.

`sysconf(57)` and `sysconf(58)` are `_SC_NPROCESSORS_CONF` and `_SC_NPROCESSORS_ONLN`. This build
answers `-1` to both, and `util/u_cpu_detect.c` falls back on an unknown core count. `__xuname`
is reached in ordinary start-up.

## Exit fault

From the same launch:

```text
371:[MESA00001:MESA-PROBE] radeonsi created a screen: the startup path is complete
372:[MESA00001:MESA-PROBE] done
374:# A user thread receives a fatal signal

# fault address: 0000000000000000
# rax: 000000000000001c  rbx: 0000000000000000
# rcx: 00000008000004ec  rdx: 000000000000001c
# rsi: 00000007eeffbab0  rdi: 0000000000000001
# rbp: 0000000000000000  rsp: 00000007eeffbdb0
# rip: 0000000000000000  eflags: 00010206
```

This build returns from its entry point after `done`. A `big-app` container may not terminate
itself: `exit`, `_Exit`, `sceKernelExit` and the shell-level kills are absent, `_exit` raises
`SIGSYS` on syscall 1, and a return from the entry point transfers to zero because the dynamic
linker gives it no caller frame. The conforming ending prints a last line and idles; see
[the full-stack record](full-stack-parks-fw1240.md).
