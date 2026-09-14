# 004. The container build reaches Mesa's first real demand, and it is libdrm

**2026-09-14** - roadmap unit 3, done

## What changed

`./bin/oops-mesa build` is wired and does the whole thing: stages the C-library headers from the
checkout D004 pins, starts the container, configures upstream Mesa for
`x86_64-unknown-freebsd`, and stops at the first demand this platform cannot meet, with the
reason on standard error and a non-zero exit (CLAUDE.md, principle 4).

- **`toolchain/stage-sysroot.sh`**, which reads 920 headers out of the pinned checkout's object
  store with `git archive`. It never writes to that checkout and never touches its
  sparse-checkout configuration, so orbistoun's constant harvest, which reads the same tree,
  cannot be disturbed by it.
- **`toolchain/cross-prospero.ini`**, the meson cross file: clang 18 and the target triple
  oops-apps builds titles with, minus the freestanding flags, because a title linking oops-mesa
  is hosted (D002).
- **`toolchain/build-mesa.sh`**, the configure and build, with every Mesa option carrying the
  reason it is set.
- **The image gained a working meson.** Debian bookworm ships 1.0.1 and the pinned Mesa requires
  1.4.0 or newer, so meson now comes from pip at a pinned version and the Dockerfile says why.

## The gate, and what it produced

Unit 3's gate was to reach Mesa's first compile error against the shim's headers, and have that
error be unit 4's work list. It stopped earlier than that and said something more useful:

```text
Message: libdrm 2.4.133 needed because amdgpu has the highest requirement
Run-time dependency libdrm found: NO (tried pkgconfig)
mesa/meson.build:1921:15: ERROR: Dependency "libdrm" not found, tried pkgconfig
```

**radeonsi will not configure without libdrm.** That is the seam D003 described in the abstract,
now with a name and a version on it: the driver asks a kernel interface what the device is and
submits through it, and on this platform that interface is the winsys shim of unit 5. Everything
before it passed - every compiler feature check, every function check, `posix_memalign`,
`clock_gettime`, `dlopen`, `struct dirent` having `d_type`.

The surface unit 5 has to answer is bounded and now roughly measured: Mesa's AMD winsys calls
about eleven `xf86drm` functions and a libdrm_amdgpu API in the tens of entry points. The exact
list needs libdrm's own header to separate it from Mesa's identically-prefixed internals, and
that is unit 5's first task rather than a number worth guessing here. Mesa bundles the kernel
interface definitions it needs, so what is missing is the userspace wrapper, not the protocol.

## What was filed elsewhere

`REQ-20260914T1558Z-7d41` on the obSCEne bus: the device-information surface. Before radeonsi
programs anything it asks what the device is - family, memory, clocks, shader engines, compute
units, and the virtual address range it may map in - and oops-mesa would currently answer from a
PC part of the same family rather than from this console. The request asks for whatever the
vendor libraries and the kernel will say, by four named routes, and states the assumptions it
would replace.

## Surprises

- **An installed header set is not a copy of `include/`.** The first staging copied that
  directory and Mesa's very first configure probe failed on a missing `errno.h`. FreeBSD's
  `include/Makefile` links nine headers out of `sys/sys` and a list of directories out of `sys/`,
  and `math.h` ships with the maths library. The staging script now follows those lists and
  checks that the Makefile still agrees with the copy of them it holds.
- **A headers-only sysroot makes every link probe fail for the wrong reason.** meson concluded
  that the atomic builtins needed `-latomic` because its link test could not find a libc, which
  is not what it was asking. The cross file now says `-nostdlib` with undefined symbols allowed
  and states what that costs: a `links()` answer in this build means the mechanics worked, not
  that the platform has the symbol. Which symbols the platform has is a separate question with
  its own evidence, and the build is careful not to look like it answered it.
- **The measurement in D004 held up.** The sysroot compiled `stdio.h`, `pthread.h`,
  `sys/stat.h`, `time.h` and `math.h` on the first try once the layout was right, and Mesa's
  hundreds of configure probes ran against it without a single header complaint.
- **Git Bash rewrites container paths.** `-w /w` reached the daemon as a drive letter. The verb
  sets `MSYS_NO_PATHCONV` and uses `pwd -W` where it exists, so the same script works from this
  shell and from a Linux one.

## Next

Unit 5, the winsys, is now the critical path rather than unit 4: nothing else in Mesa configures
until libdrm is answered. Unit 4's thread surface is already written down and is not blocking.
The first task is to separate libdrm_amdgpu's real API from Mesa's internals of the same name,
which needs libdrm's own header rather than inference from Mesa's call sites.
