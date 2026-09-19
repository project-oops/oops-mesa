# radeonsi creates a screen on the hardware, and what the same capture says about the exit

**2026-09-17** · firmware 12.40 · retail unit · roadmap unit 5's useful milestone

This is the citation for the claim that unit 5's startup path is closed on hardware rather than
only in the tabulation. It is the tail of the system log from a launch of `MESA00001`
(`oops-apps/src/oops-mesa/mesa-probe`), captured after the Initial Exec change in worklog 040 and
before the DRI frontend was linked.

The record exists because the run happened interactively and the log was read from the console
rather than produced by an obSCEne check, so there was no report file to point at. Copying the
lines here is what makes the milestone citable at all (CLAUDE.md, principle 3); nothing has been
edited but the leading timestamps.

## The lines

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

`radeonsi created a screen` is `mesa_probe_main.c:206`, reached only when
`radeonsi_screen_create` returns non-null. That is unit 5's stated milestone: a non-null
`pipe_screen` on firmware 12.40.

## Thirty-five commands, and the two that were refused

The numbered trace is every ioctl the driver issued from `amdgpu_get_auth` to the end of
`ac_query_gpu_info`, in order. Thirty-three were answered `0`; the two refused are
`AMDGPU_INFO` sub-queries `0x21` (`VIDEO_CAPS`) and `0x22` (`MAX_IBS`), each answered `-78`
(`ENOSYS`). **Both refusals are tolerated**: the driver asked, read the failure, and continued to
create its screen, which is the evidence that leaving them unimplemented is a real choice rather
than a deferred bug. `0x21` is asked twice, which is the driver's own retry and not a duplicate
line.

`0xc010640c` is `DRM_IOCTL_AMDGPU_INFO`'s sibling on the generic path and `0x80206445` is the
`AMDGPU_INFO` command itself; both are already tabulated in worklog 021.

## Two things in the same capture that were not the milestone

**`sysconf(57)` and `sysconf(58)` are the processor counts**, `_SC_NPROCESSORS_CONF` and
`_SC_NPROCESSORS_ONLN`. The shim answers neither, so Mesa is told `-1` for both and falls back to
whatever it does with an unknown core count - which for `util/u_cpu_detect.c` means its thread
count for shader compilation. This is not a crash and was not noticed when the capture was first
read; it is recorded here because the fix belongs to the runtime shim and the number is available
from the platform.

**`__xuname` was reached**, so something takes the `uname` path in ordinary start-up rather than
only in a debug dump. The stub is loud by design and this is exactly the case it exists to
report.

## The fault, which is after `done`

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

`rip` and the fault address are both zero on the line after the probe's own `done`. **It is
strictly after the work**, so it does not qualify the milestone above - the screen was created and
the probe ran to its own end.

That fault is now explained, and it is architectural rather than a bug in this title.
REQ-20260917T1450Z-2e71 resolved at 16:15Z: a `big-app` container is not permitted to terminate
itself, because process lifecycle belongs to the shell. `exit`, `_Exit`, `sceKernelExit` and the
shell-level kills are absent; `_exit` exists and raises `SIGSYS` for want of permission on syscall
1; and returning from the entry point transfers to zero because the dynamic linker gives it no
caller frame. The conforming ending is to print the last line and idle while the host closes the
app.

So **this capture's tail is what the old ending looked like, and both Mesa titles have since
stopped doing it** (worklog 044). A future capture of the same run should end at `idle and
finished` with no register dump after it. That difference is the only thing about this record that
is out of date, and it is left in place deliberately: it is what was measured.
