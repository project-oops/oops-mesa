# 074. The errno was the bug, and worklog 049 had it wrong

**2026-09-25** · corrects [049](049-the-dri-frontend-dups-the-device-fd-and-the-token-could-not-be-dupped.md)
· `gl-cts` (oops-apps) is the title that found it

The DRI frontend refused every screen for a title that had escaped the sandbox, with **no ioctl at
all** in the log. That signature has exactly one source: `pipe_loader_drm_probe_fd` opens with

    if (fd < 0 || (new_fd = os_dupfd_cloexec(fd)) < 0) return false;

and everything past it - `loader_get_driver_for_fd` -> `drmGetVersion` - would leave ioctls behind.

## What the hardware said

    dup probe on fd 11: F_DUPFD_CLOEXEC=-1 errno 45, F_DUPFD=15 errno 0

**EOPNOTSUPP (45), not EINVAL (22), and `F_DUPFD` returns a working duplicate.** Upstream bails
with `if (errno != EINVAL) return -1;` before ever reaching that fallback, so patch 003's
pass-the-fd-through was compensating for a path that had never been allowed to run.

049 recorded both commands failing with EINVAL "even on a genuine open regular file
(`/app0/eboot.bin`, fd 11)", over three hardware runs. This is the same file at the same fd number
and disagrees on both counts. Only the CLOEXEC variant is unsupported here.

Patch 003 now lets EOPNOTSUPP through to `F_DUPFD`, and no longer throws away a successful
duplicate when `F_GETFD`/`F_SETFD` fail - that is the same family trap one level down, and this
platform has now been shown twice to refuse one member of an fcntl family while serving another.
A dup without close-on-exec is still a dup, because a title never execs.

With that, the frontend runs radeonsi's whole device init: 74 ioctls, `GL is current: screen,
drawable and context are up`, two 1920x1080 scanout buffers, `oops_display_open`.

## Two wrong theories, both of which looked right

**"The descriptor must be dup-able."** Nothing dups - patch 003 passes the fd through, as its own
note says. The requirement was weaker than the code claimed.

**"It must be fstat-able, and stderr is not."** stderr fstats fine and answered two
`DRM_IOCTL_VERSION` calls, naming `amdgpu 3.54.0`. It was tried as the device fd and got exactly as
far as the eboot did.

Each was measured, each was wrong, and each cost a hardware run. The fd was never the problem.

## Why it took so long to see

`driCreateNewScreen3` returns a bare NULL, and Mesa's own reasons go to a log level this platform
cannot turn on, because `getenv` answers null for every name. "the DRI frontend would not create a
screen" stood for three different causes in two days.

`src/platform/dri_loader.c` now prints, before the frontend is asked: the fd's `fstat`, the same
dup sequence `os_dupfd_cloexec` will perform with both errnos against `EINVAL`, and
`drmGetVersion`. That splits the failure at the seams these runs actually found - a driver named
means the descriptor and the ioctl path are both good and the frontend declined further in.

## Open

A title that reaches `/data` before drawing loses `/app0` to the sandbox escape, so
`oops_winsys_open` claims the device descriptor from `.init_array` now, before any title code runs.
That is committed but **is not what fixed this** and remains separately unverified against a title
that does not escape.

`AMDGPU_GEM_METADATA` answers `-ENOSYS`; the frontend carries on without it.
