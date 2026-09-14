# Changelog

oops-mesa publishes a static SDK for titles built on oops-sdk. Until the first SDK exists there
is no version; the commit a consumer was built against is the only version that means anything,
and the Mesa pin in `dependencies.mk` is part of that identity.

Entries are grouped **Added / Changed / Fixed**, newest first.

## Unreleased

### Added

- 2026-09-14: `src/runtime/threads.c`, the thread surface Mesa needs mapped onto the vendor's.
  All 26 functions, with the trailing-name arity written out per call and vendor failures
  translated through the measured `0x8002_0000 | errno` scheme so a condition-variable timeout
  reads as a timeout. Verified by compiling against the platform's own `pthread.h` (worklog 011).
- 2026-09-14: patch 001 gained a third hunk: the device descriptor is kept rather than duplicated
  with `fcntl`, because it is a token naming the one device rather than a kernel object.
- 2026-09-14: `AMDGPU_INFO_ACCEL_WORKING`, the first thing libdrm asks. `READ_MMR_REG` refuses:
  `GB_ADDR_CONFIG` is the one hardware register that blocks a screen and nothing in the
  collection has read it (`REQ-20260914T1712Z-5b60`, worklog 010).
- 2026-09-14: **libdrm now calls the shim.** `patches/001-libdrm-reaches-the-shim-instead-of-a-kernel.patch`,
  two guarded hunks routing `drmIoctl` and `drm_mmap` to the winsys, with the error conventions
  reconciled. Mesa rebuilds with it and the built archives carry the references (worklog 009).
- 2026-09-14: `GEM_WAIT_IDLE` and `GEM_OP`, taking the winsys to ten of its 22 commands doing real
  work, and `tests/platform_double.c`, a host stand-in for the platform memory calls so the suite
  can build a real buffer and take it through its whole life. 47 checks (worklog 008).
- 2026-09-14: contexts and buffer lists. A context carries the reset history submission already
  knows, so radeonsi can act on a failed stream; a buffer list checks every handle it names, which
  turns a stale one into a refusal rather than a later GPU fault (worklog 007). Eight of the 22
  commands now do real work and the host suite runs 31 checks.
- 2026-09-14: the six winsys commands on the path to a first frame: the driver version query,
  buffer create, buffer map, address mapping, command submission and the fence wait. Submission
  is synchronous, using oops-gl.s proven end-of-pipe sequence as a second stream (worklog 006).
  `./bin/oops-mesa test` runs seventeen checks.
- 2026-09-14: `src/winsys`, the shim: device open, close and ioctl dispatch with all 22 commands
  present and the unimplemented ones refusing under their own name, plus the device description
  radeonsi reads, written in three tiers so each field says whether it was measured, quoted from
  Mesa to identify the chip, or assumed. `./bin/oops-mesa test` is real: eight host checks.
- 2026-09-14: **upstream Mesa 26.2.2 compiles for this console.** `./bin/oops-mesa build`
  produces 48 static archives for `x86_64-unknown-freebsd`, including radeonsi, ACO, NIR, GLSL,
  AddressLib and libdrm's `libdrm_amdgpu.a`. Adds D005 (libdrm is carried, the shim sits under
  it), a target build of libelf and the C++ standard library from the pinned checkout, and a
  probe archive generated from obSCEne's import census so the build answers "does this function
  exist" from measurements rather than from a link flag (worklog 005).
- 2026-09-14: the container build. `./bin/oops-mesa build` stages the C-library headers from the
  pinned checkout, configures upstream Mesa for `x86_64-unknown-freebsd` inside the toolchain
  image, and stops at the first demand this platform cannot meet. That demand is `libdrm`, which
  is the winsys shim of roadmap unit 5 (worklog 004). Adds `toolchain/stage-sysroot.sh`,
  `toolchain/cross-prospero.ini` and `toolchain/build-mesa.sh`.
- 2026-09-14: D004 pins what Mesa compiles against: the FreeBSD source checkout orbistoun already
  harvests from, so the two projects cannot disagree about this platform. `SYSROOT_SOURCE`,
  `SYSROOT_PIN` and `SYSROOT_STAT_LAYOUT` are set in `dependencies.mk`. Nothing is downloaded,
  and the pin claims only what Mesa is compiled against, not that the platform matches it.
- 2026-09-14: `docs/hardware/mesa-thread-surface-fw1240.md`, unit 4's thread work list computed
  from Mesa's C11 threads layer and this collection's own corpora rather than a hardware run.
  The surface is closed at 26 functions and every one has a vendor twin, so unit 4 is a known
  quantity before it starts (worklog 003).
- 2026-09-14: `tools/preamble-dump`, a host tool that builds against the pinned Mesa and
  prints the command-stream preamble radeonsi emits for this hardware, as C data and as a
  decoded listing; both outputs are tracked and `make check-preamble` keeps them in step with
  the pin. It is the input to the route measurement, which passed (D003 result, worklog 002).
- 2026-09-14: the repository: licence, line-ending policy, verbs, the Mesa submodule pinned at
  `mesa-26.2.2`, the container image definition, decisions D001-D003, the roadmap and worklog
  001. No source yet.
