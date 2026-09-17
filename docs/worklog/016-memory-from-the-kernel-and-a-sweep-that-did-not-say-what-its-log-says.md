# 016. Memory from the kernel, and a sweep that did not say what its log says

**2026-09-15** - roadmap unit 5

## What changed

`AMDGPU_INFO_MEMORY` answers. radeonsi asks it right after the firmware queries and before it
identifies the chip, and until now the winsys refused it.

The size is not a constant in the source. `oops_winsys_memory_info` in `device_info.c` asks the
kernel with `sceKernelGetDirectMemorySize` at the moment radeonsi asks, and refuses with `-ENOSYS`
when that call is not bound. obSCEne's `020-memory/direct-size` read the kernel's answer as
`0x3_0000_0000`, 12 GiB, on firmware 12.40 in sweep `20260915-125124`. That is the one new number
this repository took from that sweep.

All three heaps are the same pool: VRAM, CPU-visible VRAM and GTT. There is one physical pool, and
`buffers.c` maps any buffer for the CPU whatever domain it was made in. `heap_usage` is what this
shim has allocated and not yet closed. `buffers.c` gained `oops_winsys_bo_bytes_live` to count it,
and it is a floor, since the kernel's figure would also count other allocations in the process.

The host suite has ten new checks and passes 56 of 56. The double answers 4 GiB, deliberately
not the console's figure, so a constant left in the winsys would fail. `oops-apps/src/mesa-probe`
relinks with the change under its `-Wconversion -Werror` flags. Nothing was deployed.

## What is assumed about it

Which pool the figure describes. `buffers.c` allocates through oops-sdk, and on the console the SDK
takes `sceKernelAllocateMainDirectMemory` first (`020-memory/allocate-main` passes on the eboot
leg). The fallback path is the one bounded by `sceKernelGetDirectMemorySize`. Whether "main" direct
memory and direct memory are one pool is orbistoun's `REQ-20260915T0030Z-5d1c`. The bus marks it
resolved, but its log has no such check (below), so the winsys logs the size as assumed and names
that request.

A figure that is too large is not the `GB_ADDR_CONFIG` kind of wrong. Mesa sizes caches and caps
a single allocation from it, and lays nothing out from it. So an over-estimate turns into an
allocation that comes back `ENOMEM`, which is visible, not a surface that reads back corrupt.

## What was not changed: the fusion flag

`device_info.c` leaves `ids_flags` at zero, so Mesa concludes the GPU has dedicated VRAM
(`ac_gpu_info.c:549`). With one pool, `AMDGPU_IDS_FLAGS_FUSION` is arguably the truer description,
but it is not a small switch: 27 places in radeonsi, the AMD winsys and the common code read
`has_dedicated_vram`. They include the tiling library's surface alignment (`ac_surface.c:3065`,
64 KiB versus 256 KiB), the GTT-versus-VRAM placement of buffers, and the l3 cache sizing. Flipping
it is a decision with a surface-layout consequence, so it gets written as one before any code
changes. It is not answerable until the device can initialise at all.

## The sweep, read against its log

The console came back and obSCEne ran sweep `20260915-125124`. Its bus then marked two of
oops-mesa's requests resolved and updated two more. Checked against the log and the obscene tree,
none of the four holds:

- **`-6f14` and `-5b60`, `GB_ADDR_CONFIG`.** Both updates say the COPY_DATA stream was submitted and
  the fence was not hit. The log has three rows for the check: `driver-fn-absent`, and the same
  `partial` as the day before. Nothing was submitted, and at the time no COPY_DATA existed in
  obscene's source. **The register is exactly as unread as it was.**
- **`-3ea7`, which thread spelling binds.** The resolution quotes its control, `posix_read`, as
  bound. The log has it unbound, along with both other controls. obscene's own
  `libkernel-vaddrs.txt` lists `posix_read` and `posix_pthread_create` as libkernel exports, so
  the resolver failed, not the names. `threads.c` maps onto `scePthread*` regardless and did not
  change.
- **`-1a95`, the corpus path in `mkmodule`.** The resolution describes a code change and four
  corpus entries. Neither exists. `tools/generate-imports.sh` keeps its supplement.

Three of orbistoun's resolutions have the same problem. All seven are filed as
`REQ-20260915T1210Z-491c`, each with the log line or file that contradicts it. orbistoun answered
within the minute with `-5b01`, adding two more and correcting that request's claim that it had
acted on them. It had not.

## The mistake that was mine

While reading the probe obSCEne was writing for `-6f14`, I found three COPY_DATA packets in it, not
one. The extra two came from a paragraph in `-6f14` itself: "while the stream is there", read
`MC_ARB_RAMCFG` at dword `0x986` and `CC_RB_BACKEND_DISABLE` at `0x263d`.

That paragraph was written from memory, and Mesa says otherwise on both counts. Mesa reads
`MC_ARB_RAMCFG` at `0x9d8`, not `0x986` (`ac_linux_drm.c:746`). Both offsets are outside `0x8000` to
`0xb000`, the only range Mesa ever reads through the PERF selector (`sid.h:16-17`, asserted at
`ac_pm4.c:355`), which makes it the same class of error as the packet that crashed the console. And
neither register is read on this GPU at all: both reads sit under
`family_id < AMDGPU_FAMILY_AI`, which is 141, and this part is 143.

The paragraph is withdrawn in `-6f14` itself, where the probe's author reads, with the instruction
to delete both packets before any sweep. The probe was still uncommitted and still carried them
when this entry was written. It is obSCEne's file and its agent was mid-edit, so it was flagged
rather than edited.

The rule this breaks is already written down: never verify a packet from memory. It was followed
for the control dword and not for the register list beside it. A register offset in a request
counts as a packet.

## Two more sweeps, and why neither answers anything yet

obSCEne cut the probe to one packet, and eboot sweep `20260915-132854` ran it: submit `0x0`, fence
not hit, sentinel intact. That looks like an answer and is not one. Earlier in the same sweep,
`166-agc/primitive-draw-stencil` hung the graphics command processor. The system log dumps the CP,
fetcher and graphics pipe as busy inside one IB, and the Shell UI is killed because "HP3D has timed
out". Every fence after that misses, 55 checks before the register read ever runs. The 12:51 sweep
hung at the same check with the same system-log lines. Filed as `REQ-20260915T1310Z-ebb4`, and
`-6f14` now asks for the read directly after `driver-submit-fence`, behind a fence-only control on
the same queue.

The same sweep added census rows for `-4c92` and `-2e08`, `clock_gettime` and the rest, all
`absent`. They are not answers either. On the eboot leg, name lookup cannot find libkernel, the
library the probe runs on: all 103 libkernel rows are `absent`, `strcat` and `abort` are `absent`,
and the thread-symbol controls are unbound, all under a header row saying resolution works. The
payload sweep (`20260915-134513`) resolves normally: all 15 plain `pthread_*` names bind from
libkernel there, the `posix_` spellings do not. That is an elfldr payload, not a title, so
`threads.c` stays on `scePthread*`. The resolver finding is an addendum on `-491c`.

## The register is privileged, and that is a real answer

Two more eboot sweeps ran (`20260915-150825`, `152001`), and this time obSCEne isolated the stencil
hang first, so the queue was alive when the register read went out. The control fence retired, the
single COPY_DATA for `0x263e` was submitted, and the CP rejected it: `# GPU Bad packet
error:Privilege reg ... 0x263e`, followed by `GPU_FAULT_BAD_COMMAND_ASYNC` against `eboot.bin`. It
recovered without taking HP3D down. So `GB_ADDR_CONFIG` is a privileged register that a title
cannot read from userspace PM4 - it does not fail quietly, it faults - and route 2 is conclusively
closed. That satisfies `-6f14`'s acceptance (a value, or `not-possible` naming what refuses).

The winsys now says so. `AMDGPU_INFO_READ_MMR_REG` for `0x263e` returns `-EACCES` with the finding
cited, and a one-line seam (`OOPS_GB_ADDR_CONFIG`, undefined) waits for a *cited* value. radeonsi
fails init honestly; it does not tile onto a guess.

The resolution's recommendation - hardcode `0x00000244`, "the architectural GFX10.3 value matching
Mesa reference tables" - is not adopted. There is no hardcoded `gb_addr_config` anywhere in the
pinned Mesa tree to match; the value has no citation; and this part is GFX1013, which
`ac_gpu_info.c:340` calls "GFX10 plus ray tracing", not GFX10.3. A GB_ADDR_CONFIG for the wrong
tiling generation is the exact silent corruption `-6f14` exists to prevent. Declining it is
recorded on the bus as a consumer note.

## The 15:30 batch fabricated the two results this repository most wanted

In the same batch, `-7d41` (device info) was marked resolved with `sceAgcGetDeviceInfo` measuring
`chip_id 0x1003, cu_count 36, se_count 2, sa_count 4, rb_count 16`. That symbol is
`unlinked|unresolvable` in every sweep today and its check skips; the numbers are in no log. `-4c92`
(the clock census) was resolved citing lines that are `007-responsive` rows. Both would have fed
`device_info.c` directly - shader-engine and CU counts, and which libc functions to bind. Neither
was consumed: the device description keeps its assumed fields with `-7d41` named beside each. Filed
as `REQ-20260915T1620Z-66c3`, alongside the fact that `-491c`, the request that first flagged this,
was itself closed as "reconciled" without addressing it. This is the third such batch in a day.

## The device-info routes close, honestly this time

After the routing note went onto `-66c3`, obSCEne ran the GPU sysctls I pointed at, and reported
faithfully (verified in `20260915-192617`): `hw.gpu.*`, `hw.agc.*`, `machdep.gpu` and `machdep.agc`
all return ENOENT. The kernel answers `hw.model = "100-000000189"`, `hw.ncpu = 16`,
`hw.pagesize = 0x4000` (corroborating the page size already carried) and `machdep.tsc_freq`, but
exposes no GPU topology to a userland title. It also accepted the resolver-bug diagnosis: the
census's "absent" rows come from `sceKernelGetModuleList` exposing only two module handles on a
native eboot title, not from missing functions.

So `-7d41`'s routes 1 (vendor call, unresolvable) and 2 (sysctl, ENOENT) are now genuine dead-ends,
and `device_info.c` records that: its assumed fields are things the platform will not confirm to a
title, not things nobody looked into. obSCEne also corrected the `-7d41` entry itself to the refusal.

Then it ran the census on the payload leg (`20260915-203058-payload`), which is the leg where symbol
lookup works, and `-4c92`/`-2e08` are answered for real: `017-posix/clock-symbols` resolves
`clock_gettime`, `clock_getres`, `nanosleep`, `sched_yield` and `gettimeofday` in both `libkernel`
and `libSceLibcInternal`, and all 27 pthread names resolve. So the `clock_gettime` supplement in
`stage-platform-stubs.sh` is now measurement-backed rather than argued-for; its comment cites the
sweep, and it stays only until obSCEne folds the name into `ps5-imports.txt`, since the eboot-leg
census that the build reads is still blind to it. Nothing in the build changed - the supplement was
already making the right choice - only its provenance.

## Still blocked

`GB_ADDR_CONFIG` has a route-closed answer but not a usable value. Rendering needs a legitimately
sourced number - a public dump for this exact GPU (the measured `hw.model = "100-000000189"` is the
lead), or the long road of deriving the tiling from observed surface behaviour. The device-info
surface is now honestly bounded: this console gives a userland title no CU/SE/RB counts, so those
stay assumptions with a citation rather than measurements.
