# 049. The DRI frontend dups the device fd, and a token cannot be dupped

**2026-09-19** - roadmap unit 6

## The run

`dri-probe` (`DRIP00001`) ran on hardware for the first time - the title that calls
`oops_gl_create`, i.e. brings GL up through the Gallium DRI frontend rather than calling
`radeonsi_screen_create` directly the way `mesa-probe` does. It was deployed and launched with
Prosperous; the console had been rebooted and re-exploited, so the big-app slot was free.

The whole of the title's output:

```text
[DRIP00001:DRI-PROBE] bringing GL up through the DRI frontend (v2026-09-19 10:15)
[DRIP00001:OOPS-MESA] getenv: this platform exports none, so every name reads as unset. ...
[DRIP00001:OOPS-GL]   the DRI frontend would not create a screen
[DRIP00001:DRI-PROBE] GL did not come up; the shim's last line above names the step
[DRIP00001:DRI-PROBE] done
[DRIP00001:DRI-PROBE] idle and finished - close this title from the host
```

Two things are immediately legible. The parking ending worked again - clean `idle and finished`,
`pros ps` showing it `SLEEP` at 55 threads. And GL did not come up, at a precise place:
`driCreateNewScreen3` returned NULL, and **not a single ioctl fired** - where `mesa-probe`'s direct
call produced 35. So the frontend declined in its own setup, before radeonsi ran at all.

## The root cause, confirmed from source

`getenv` running but no ioctls bounds the failure tightly, and reading the frontend at the pin
closes it completely:

- `driCreateNewScreen3` (`dri_util.c`) parses config - that is the `getenv` - then calls
  `dri2_init_screen` for a DRI3 screen.
- `dri2_init_screen` (`dri2.c`) does `pipe_loader_drm_probe_fd(&screen->dev, screen->fd, false)`
  and only creates a pipe screen if it returns true.
- `pipe_loader_drm_probe_fd` (`pipe_loader_drm.c`) **dups the fd first thing**:
  `os_dupfd_cloexec(fd)` before anything touches the device.
- `os_dupfd_cloexec` (`os_file.c`) is `fcntl(fd, F_DUPFD_CLOEXEC, 3)`.

And `oops_winsys_open()` returned `OOPS_WINSYS_FD` - `0x0057`, a fixed token, never a real kernel
descriptor. `fcntl(0x57, F_DUPFD_CLOEXEC)` on a number the kernel never handed out fails, so the
probe returns false, `dri2_init_screen` returns NULL, `driCreateNewScreen3` returns NULL. The
`drmGetVersion` that would have issued the first ioctl is downstream of the dup and is never
reached - exactly matching the empty ioctl trace.

`mesa-probe` never hit this because it bypasses `pipe_loader` entirely and hands the token straight
to `radeonsi_screen_create`; the token only ever flowed to the ioctl path, which is patched to the
shim and accepts it. The frontend is the first caller to treat the descriptor as a real fd - and
it is entitled to. The old `oops_winsys_ioctl` comment had even predicted it: "if Mesa ever
duplicates the descriptor - and a loader that takes ownership of a device fd is entitled to."

## The fix

A synthetic token is not enough; the descriptor has to be a real, dup-able fd. Three changes in
`src/winsys/drm_device.c`, all argued from the measurement:

1. **`oops_winsys_open()` returns a real fd.** It duplicates an already-open standard descriptor -
   `fcntl(2, F_DUPFD_CLOEXEC, 3)`, i.e. a dup of stderr, which is open on every leg
   (`-5c9d`) and needs no filesystem the sandbox might lack. That is the same call the frontend
   will make on the result, so if it succeeds the frontend's dup will too. It falls back to the
   token, preserving `mesa-probe`'s path, if the dup ever fails.

2. **The ioctl gate serves the device fd and its dups, but still refuses a bogus one.** The
   frontend dups the fd and radeonsi issues its ioctls on the *dup*, so a gate that refuses
   anything but the original turns a working driver into a silent `-EBADF` wall. The first cut of
   this served *any* fd - which the host suite immediately caught as over-correction, because it
   dropped a safety property the suite encodes: a command on a descriptor that was never opened
   should still be refused. The two are distinguishable without tracking dups - **a dup is a real
   open descriptor and a never-opened number is not**, which `fcntl(fd, F_GETFD)` reports. So an
   open fd is served as the dup it is; a bogus one is refused with `-EBADF`. The gate was never the
   interception mechanism anyway - patches 001/002 route `drmIoctl` and the inline `drm_ioctl`
   here by call site.

3. **`oops_winsys_close`** accepts the device fd or an open dup, by the same `F_GETFD` test, and
   does nothing - a title parks rather than exits, so there is nothing to reclaim.

## Test coverage, and a stale test found on the way

The fix has host coverage now - `tests/winsys_test.c` gains a check that an open dup is served past
the gate while the same number, once closed, is refused. That is the whole of the fix in a form
that runs on the build machine, so it cannot silently regress the way it could only be *found* on
hardware.

Adding it surfaced that the winsys host suite was **already red at HEAD**, unrelated to this work:
`DRM_CAP_PRIME` was changed to answer with 0 (D009, worklog 032 - `u_screen.c:139` reads it into
`caps->dmabuf` and 0 means "no cross-process sharing", said deliberately) but the test still
expected the old `-EINVAL` refusal. The code is right and the test was stale; the test now expects
the answered-0 and checks a genuinely-unknown capability for the `-EINVAL` path. Suite: 109 checks,
0 failed.

One portability wrinkle: the host compiles this target file against glibc, which gates
`F_DUPFD_CLOEXEC` behind a feature macro the FreeBSD sysroot exposes directly. A one-line fallback
to `F_DUPFD` covers the host build only - a title never execs, so the close-on-exec flag is
immaterial - and the shipped target binary uses the real constant.

Safety checked before writing it: `oops_winsys_mmap` does `(void)fd` and maps through oops-sdk from
the offset token, and nothing else in the winsys reads, writes or mmaps the fd - only `ioctl`,
which never reaches the kernel for it. So the descriptor aliasing stderr's sink is immaterial; its
identity is all that is used.

## Status: built, not yet verified on hardware

Both titles are rebuilt and packaged with the fix. It is **not** verified on hardware yet: the
`dri-probe` instance from this run parked and holds the big-app slot, and `pros close`, `pros kill`
and `pros restart-ui` all fail against a parked title (Prosperous `-b1e4`), so a second launch
needs the slot freed by the shell UI's own Close or a reboot - a console-side action. The JetKVM
video stream was not loading this session, so the close could not be driven blind.

The next launch will show whether the real fd carries the probe past the dup. **The rest of the
post-dup path has been pre-verified from source and the link map**, so that one scarce run (each
now costs a manual slot-free) tests as much as possible rather than stopping at the next
easily-foreseen wall:

- `loader_get_driver_for_fd` → `drmGetVersion` → `oops_winsys_version` returns `"amdgpu"`, with the
  two-pass length/fill protocol handled (drm_device.c) - and pipe_loader maps `amdgpu` → `radeonsi`.
- `loader_get_pci_id_for_fd` failing is non-fatal: the probe falls through to `PIPE_LOADER_DEVICE_PLATFORM`.
- `get_driver_descriptor("radeonsi")` resolves: `dri_target.c.o` is linked (the map shows
  `pipe_radeonsi_create_screen` from it) and carries the descriptor table.
- `pipe_loader_create_screen` → `radeonsi_screen_create` is the exact 35-ioctl path `mesa-probe`
  already drove to a screen.

So the fd fix has a good chance of reaching a created screen, at which point the probe enters
genuinely new territory - `choose_config`, `dri_create_drawable`, `driCreateNewContext`,
`dri_make_current`, and the `getBuffers` callback where `dri_create_image` allocates the colour
buffer - none of which any run has exercised. That, not the fd, is where the next real unknown is.

## Second and third runs: neither `fcntl` dup command works on stderr

Two attempts at "hand out a real fd" both fell back to the token on hardware (2026-09-19):

- Run 2: `fcntl(2, F_DUPFD_CLOEXEC, 3)` → EINVAL (22). I read this as "command 17 unsupported,
  fall back to `F_DUPFD` like `os_dupfd_cloexec` does", and added that fallback.
- Run 3: `fcntl(2, F_DUPFD, 3)` → **also EINVAL.** So it is not the CLOEXEC variant; *both* dup
  commands are refused on stderr.

That kills the simple reading and leaves a sharper question the earlier runs could not separate:
is the dup **command** rejected platform-wide, or is **stderr specifically** not a dup-able
descriptor (the same programme of sweeps found stdout/stderr accept writes that go nowhere, so
they may be special sinks rather than ordinary fds)? `fcntl` itself is genuinely present
(0x800000b50, measured), so this is the kernel refusing the operation, not a missing symbol. And
`dup`/`dup2` are corpus-placed in libkernel but **never measured on the native leg**, so calling
them blind is the GEN=4 trap `-9f41` warned against.

This matters beyond the shim: the frontend's `os_dupfd_cloexec` uses the same `fcntl(F_DUPFD)`, so
if the command is rejected wholesale, no fd this shim hands out will survive the frontend's dup
either, and the fix becomes a Mesa patch rather than a better fd.

## Fourth run: one decisive probe

Rather than guess a fourth time, `oops_winsys_open` now opens a real file the title certainly has -
its own `/app0/eboot.bin` - and tries the same dup on it, logging every step. The two outcomes
each name the fix:

- **the real file dups** → stderr was the problem, not the command → the device fd becomes that
  real dup-able file fd, the frontend's `os_dupfd_cloexec` works, and this run also fixes it.
- **the real file will not dup either** → the dup command is rejected platform-wide → the fix is a
  numbered Mesa patch to `os_dupfd_cloexec` (pass the fd through when it cannot be dupped), which
  needs no working dup primitive at all.

Either way the next run ends the guessing. Built; pending a slot to launch.

## State

`./bin/oops-mesa check` passes, pin `mesa-26.2.2`, 2 patches. Identity guard clean. The change is
in the winsys shim, one of the three the collection sanctions (CLAUDE.md principle 2); no Mesa
patch was needed.
