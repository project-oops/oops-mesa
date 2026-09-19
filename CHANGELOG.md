# Changelog

oops-mesa publishes a static SDK for titles built on oops-sdk. Until the first SDK exists there
is no version; the commit a consumer was built against is the only version that means anything,
and the Mesa pin in `dependencies.mk` is part of that identity.

Entries are grouped **Added / Changed / Fixed**, newest first.

## Unreleased

### Added

- 2026-09-17: **Mesa's diagnostics reach the log** (`src/runtime/stderr_to_klog.c`). In title space
  file descriptors 1 and 2 are open, accept writes, return the full byte count with `errno` zero,
  and reach nothing - measured both ways, with `SYS_klog` surfacing in the same eboot sweep as the
  control (`REQ-20260917T0233Z-5c9d`, sweeps `20260917-043235` payload and `20260917-095820`
  eboot). `dup2(1, 2)` does not help, because fd 1 is dead too. Mesa reports its fatal errors with
  `fprintf(stderr, ...)` and its own logger defaults there, so without this a failed screen
  produced only this shim's own refusals and never Mesa's conclusions. The interception covers the
  seven stdio entry points the **built archives** reference - `fprintf`, `vfprintf`, `fwrite`,
  `fputs`, `fputc`, `puts`, `fflush`, `perror` - rather than the two the sources appear to use:
  clang rewrites literal `fprintf` into `fwrite`, `%s` into `fputs` and single characters into
  `fputc`, so counting call sites in the sources would have carried half the diagnostics and
  dropped the rest. `fputc` forces line buffering, and long lines are split rather than truncated
  because klog drops past about 128 bytes. Fifteen host checks in a new `tests/runtime_test.c`,
  which `make test` runs as a second binary - the subject replaces the stdio a test suite would
  otherwise report through, so sharing one would have let it pass silently. Worklog 036.
- 2026-09-17: **`tools/tiling-compare`, and with it the fact that a radeonsi colour target is
  already in the layout the display scans out.** Worklog 017 matched addrlib against oops-sdk's
  tiler "across all 16,384 pixels of the block" - one 64 KiB block - and the conclusion was
  quietly carried to whole surfaces. It now holds for six: 98,304 pixels over a 3x2 block grid,
  nothing disagreeing, with a control at eight pipes instead of sixteen that disagrees on 92,160
  of them, so the comparison is known to be able to detect a difference. The grid is 3x2 rather
  than square because a 2x2 one cannot tell row-major from column-major. Neither side is restated:
  the tiler's offsets are recovered by running `agc_tile_surface`, and the Mesa side is the pinned
  AddressLib compiled with its own flags. `make check` gained `check-tiling`, which compares a
  fresh run against the tracked output like `check-preamble` does, so a pin bump or a tiler edit
  that changes the answer shows up as a diff rather than as a picture that is almost right.
  Worklog 031.
- 2026-09-17: **`DRM_IOCTL_GET_CLIENT` is answered, and it comes before everything else.**
  `amdgpu_device_initialize` opens with `amdgpu_get_auth`, returns its error without touching
  anything else, and reaches this ioctl because `drmGetNodeTypeFromFd` cannot answer
  `DRM_NODE_RENDER` on a platform with no DRM major and no `/dev/dri` - no descriptor this shim
  could hand back would take the render-node shortcut. Until now it fell to the default branch and
  refused, so device initialisation stopped on its first statement, ahead of the version, ahead of
  `ACCEL_WORKING`, ahead of the `GB_ADDR_CONFIG` read libdrm itself makes, and ahead of every line
  of `ac_query_gpu_info`. The reply reports unauthenticated, which is not a guess: it is what
  libdrm's own render-node branch assigns without asking. `DRM_IOCTL_GET_CAP` is answered in the
  same change - zero for `SYNCOBJ_TIMELINE`, which is the single lever behind D007, and zero for
  `ADDFB2_MODIFIERS`; every other capability refuses by name. Worklog 021 carries the full startup
  path with each call's failure disposition. Eight host checks.
- 2026-09-17: **`DRM_IOCTL_SYNCOBJ_CREATE` and `SYNCOBJ_DESTROY` are answered** (`src/winsys/syncobj.c`).
  `amdgpu_winsys_create` creates a timeline syncobj *before* it asks the device anything and treats
  a failure as fatal, so these gate every other answer the winsys gives - `GB_ADDR_CONFIG`, the DRM
  version, `HW_IP_INFO` and `FW_VERSION` all live inside `ac_query_gpu_info`, which runs later.
  A syncobj here is a handle in this repository's own table, because the fence it wraps is memory
  this shim allocated (D007). `WAIT`, `SIGNAL` and `RESET` refuse and name themselves: nothing
  submits work yet, and obSCEne measured that the platform exports no way to block on a fence in
  any case - the whole event-queue family is absent on 12.40, with controls resolving in the same
  check (`REQ-20260916T2208Z-7e29`). Eleven host checks. Worklog 020, D007.

### Fixed

- 2026-09-17: **`AMDGPU_CHUNK_ID_FENCE` is honoured, and without it every fence wait would have
  failed.** `amdgpu_fence_wait` reads the user fence *first* and returns success without touching
  a syncobj when `*user_fence_cpu >= afence->seq_no`; `amdgpu_cs_has_user_fence` is true for GFX,
  the only engine this winsys answers for, so every submit carries the chunk. Both values are this
  shim's - the sequence number is what `oops_winsys_cs` already returns in `arg->out.handle`, and
  the slot is a buffer radeonsi mapped at context creation - but the chunk was being skipped under
  a comment saying synchronous submission had nothing to do. True for the two syncobj chunks, not
  for this one: the memory is read later by a caller that does not know the submit was
  synchronous, and an unwritten slot reads zero against a sequence starting at one. The number is
  now written *after* the fence fires, never before, because publishing it early would report a
  submission complete while the GPU was still working. Adds `oops_winsys_bo_cpu_range`, which
  bounds-checks the caller's offset rather than trusting it. Eight host checks. Worklog 028.
- 2026-09-17: the syncobj table's size **was justified by a false premise**, and both the size and
  the refusal message are corrected. It said radeonsi creates "one syncobj per winsys and a handful
  per context" and that 256 slots therefore caught a leak. radeonsi creates a syncobj *per fence* -
  `amdgpu_fence_create` calls `ac_drm_cs_create_syncobj2` on every one - with one fence per flush,
  and fences are reference counted, so every fence an application holds keeps a syncobj alive and
  `glFenceSync` bounds none of that. Reaching the limit is ordinary use, not a bug, so the message
  no longer accuses the caller of leaking; it says a limit was reached, that the flush asking for
  it fails, and which constant to raise. The table stays fixed and still refuses rather than
  growing - a shim should not allocate without bound on a submission path - but is now 1024 slots,
  stated as a policy past ordinary use rather than a measurement. Worklog 027.

### Changed

- 2026-09-17: **`ids_flags` is decided rather than zeroed**, and the part now reports
  `AMDGPU_IDS_FLAGS_FUSION` (D008). Mesa reads the field in four places and a zeroed one is four
  claims, not an absent one. Three stay clear with reasons - no trusted memory, no preemption
  (which on this part is the only gate on register shadowing, and submission here has no
  preemption notion), and no conformant truncation (unmeasured, and clear is the side that keeps
  Mesa's texture-gather workarounds on). `FUSION` changes because clear told Mesa the part has
  dedicated VRAM while `oops_winsys_memory_info`, in the same file, already answers vram,
  cpu_accessible_vram and gtt as one CPU-visible pool; they are one claim and now agree. Every one
  of the ~24 `has_dedicated_vram` consumers was read first: surface layout is untouched on this
  part, so worklog 017's derivation still holds, and everything that genuinely changes is on the
  context path rather than screen creation. Six host checks. Worklog 025.
- 2026-09-17: the chip-identification check **runs Mesa's `ASICREV_IS` rather than a copy of its
  constants**. `amdgpu_asic_addr.h` includes nothing, so the host suite can use it directly; the
  old test defined `GFX1013_RANGE_LOW`/`HIGH` beside a comment saying it would notice if Mesa
  moved, which copied constants cannot do. The values were correct, so no verdict changes - but a
  pin bump that moves the range now fails here instead of misidentifying the chip at run time.
  `mesa/src` joins the host suite's include path. Worklog 024.
- 2026-09-17: **`mesa-probe` injects its own executable name**, which removes a null dereference
  three lines past the one worklog 019 fixed. With `getenv` answering null honestly,
  `driParseConfigFiles` falls through to `util_get_process_name()`, which on this target is
  `getprogname()` passed straight through - and a null from there reaches
  `strcmp(exec, data->execName)` in `parseAppAttr`, unguarded, against every entry in Mesa's
  built-in driconf database that names an executable. Whether `getprogname` returns a string or a
  null in a title started by the system loader is not measured anywhere; `driInjectExecName` -
  upstream's own seam, used by its own tests - makes the question moot, and the title genuinely
  knows its name. A second instance of the same defect exists in radeonsi's context path and is
  deliberately left alone: it is unreachable only because the option cache is incomplete, so
  widening `mesa_probe_options` would expose it. Worklog 023.
- 2026-09-17: **`getenv` is defined locally**, and it was the import killing the title on hardware.
  `libSceLibcInternal` does not export it on firmware 12.40 - eleven candidates swept,
  `libc-controls` and `kernel-controls` 3 of 3 in the same check, and `getenv` alone absent
  (`REQ-20260917T0025Z-1f6d`, sweep `20260917-013336`). `driParseConfigFiles` reaches it through
  `os_get_option` as its first platform call, which is why the title died before the winsys was
  touched. Unlike the other stubs this one returns null *correctly* rather than as a failure - no
  environment means no name is set - so it says so once and is quiet after. Worklog 019.
- 2026-09-17: a Mesa-linked title's **import manifest is complete** - 504 names placed, none
  unknown, none naming a library that cannot resolve it. Three imports removed by setting
  `NO_REGEX` in the cross file (their only call site is an expat handler and expat is not linked);
  six defined in the new `src/runtime/libc_absent.c` after obSCEne established on the payload leg
  that the platform exports none of them (`REQ-20260916T2200Z-4d7e`, sweep `20260916-235024`, with
  27-of-27 and 11-and-11 positive controls validating the leg). The six are loud stubs, not quiet
  ones: none is on a reachable path, so if one is called the useful fact is that it was called.
  Worklog 018.
- 2026-09-17: `toolchain/build-mesa.sh` reconfigures from scratch when `cross-prospero.ini` changes.
  meson reads `[built-in options]` only at first configure, so a later edit was silently discarded -
  1,171 targets rebuilt with the old flags and reported success. It now compares a hash of the cross
  file, not a timestamp, because `build.ninja` is regenerated every build. Worklog 018.
- 2026-09-16: `GB_ADDR_CONFIG` (0x263e) is **answered**, from `OOPS_GB_ADDR_CONFIG` = `0x00000004`.
  The register is still unreadable; the value is *derived* by inverting Mesa's addrlib against
  oops-sdk's hardware-validated tiler (`agc_tiler.c`), keeping the candidates whose swizzle
  reproduces the tiler across all 16,384 pixels of the block. 32 of 224 candidates match and all
  agree on `NUM_PIPES` = 16 pipes and 256 B interleave; `MAX_COMPRESSED_FRAGS` and `NUM_PKRS` are
  left zero because nothing on this path reads them. The matching mode is `64KB_R_X`. Under an
  RB+ (GFX10.3) identity *nothing* matches, which settles the open GFX10.3-or-GFX1013 question
  against GFX10.3 and makes the value dependent on the chip identity in `device_info.c`. It is
  not the register's value on the console and is logged as derived, not measured. Three host
  checks, on the fields the derivation pins rather than on the literal. Worklog 017.
- 2026-09-16: the DRM interface version is **3.54**, was 3.49. Mesa's `ac_query_gpu_info` refuses
  below 3.54 before it asks anything else, so the startup path had been stopping on the version
  call and would have gone on stopping there with the register answered. 3.54 claims nothing:
  every capability Mesa gates on the minor sits at 55 or above, and the workaround branches keyed
  below 63 stay active. Worklog 017.
- 2026-09-16: `AMDGPU_INFO_HW_IP_INFO` answers for GFX - one ring, GFX10 - and refuses every other
  engine. Mesa fails the device unless GFX or COMPUTE reports a ring, and it discards a COMPUTE
  answer on this part by name, so GFX is the only route. The ring is the one obSCEne's
  `166-agc/driver-submit-fence` and D003's route measurement have both seen retire. Worklog 017.
- 2026-09-16: `AMDGPU_INFO_FW_VERSION` answers zero rather than refusing, because a refusal is
  fatal to the device and zero is the conservative side of every gate Mesa keys off these
  versions on this part. Logged as unmeasured. Worklog 017.
- 2026-09-16: `AMDGPU_INFO` is exported as `oops_winsys_info`, so the host suite can reach the
  query radeonsi asks everything through without a bound graphics driver. Worklog 017.
- 2026-09-15: `AMDGPU_INFO_READ_MMR_REG` for `GB_ADDR_CONFIG` (0x263e) now refuses with `-EACCES`
  and the measured reason: obSCEne confirmed the register is privileged and a userspace COPY_DATA
  read *faults* the GPU (`REQ-...-6f14`, sweep 20260915-150825, `GPU_FAULT_BAD_COMMAND_ASYNC`).
  A one-line seam `OOPS_GB_ADDR_CONFIG` waits for a cited value; the suggested `0x00000244` is not
  adopted (uncited, no such Mesa constant, and the part is GFX1013 not GFX10.3). Worklog 016.

### Added

- 2026-09-15: `AMDGPU_INFO_MEMORY`, answered from the kernel's own direct-memory size at the moment
  of asking rather than from a constant (12 GiB on firmware 12.40, obSCEne `020-memory/direct-size`).
  All three heaps are the one pool. Which pool the figure bounds is assumed and logged as such
  (`REQ-20260915T0030Z-5d1c`). Ten host checks (worklog 016).
- 2026-09-15: **a title links and packages upstream Mesa.** `oops-apps/src/mesa-probe` produces a
  19.6 MB module for the console and a SELFish title archive. Adds `oops-mesa.mk`, the `USE_MESA`
  switch in oops-apps, `tools/generate-imports.sh`, and the duplicate-symbol fix: libdrm's
  archives are partially linked and their hidden symbols localized, which is upstream's own
  declaration enforced rather than a rename (worklog 015).
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
