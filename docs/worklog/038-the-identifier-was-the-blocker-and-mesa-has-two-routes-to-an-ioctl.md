# 038. The identifier was the blocker, and Mesa has two routes to an ioctl

**2026-09-17** - roadmap unit 5

## What this entry is

The startup path ran on hardware for the first time. Worklog 021 tabulated every call from
`amdgpu_get_auth` to the end of `ac_query_gpu_info` and closed it on paper; unit 5's state line
said "None of it has run". It has now. `MESA00001` answers thirty-five commands and reaches
radeonsi's screen creation, where it stops on something new.

Two findings got it there, and neither was in the module.

## The title could not load, and nothing in it was wrong

`mesa-probe` was refused by the loader before any of its code ran, at
`sceSblAuthMgrAuthHeader`, with nothing in the log naming a cause. It was deployed as
`PPSA90010`. `PPSA` is the vendor's retail prefix.

Rebuilt as `MESA00001` and packaged through the same path, it loads. `tls-probe`, refused
identically as `PPSA90011`, loads as `TLSP00001`.

**This is recorded as a correlation, not a mechanism.** Two things changed between the refused
runs and the loading ones: the identifier, and a return to `make title` plus `pros restore` after
a detour of hand-pushing unwrapped modules. The identifier is the strong candidate - the failure
was in header authentication, which is where an identifier would be checked - but this entry does
not claim which, because claiming more than was measured is what cost the day described below.

`oops-apps/common/app.mk` now refuses a vendor prefix at build time.

## `PT_TLS` is honoured, and the previous answer was wrong

`tls-probe` exists to answer REQ-20260915T0001Z-8d72, which asked whether the loader honours a
`PT_TLS` segment in a title's own image. It does:

```
[TLSP00001:TLS-PROBE] after:  tls_word=0xc0ffee01 tls_other=0xc0ffee02
[TLSP00001:TLS-PROBE] verdict: the loader honours PT_TLS in a title's own image
```

Two updates had been posted to that request stating the opposite, with a table of six modules and
a "minimal control" behind them. Every row of that table varied the identifier alongside the
segment, so the whole case rested on a variable nobody had held fixed. A retraction is on the
SELFish bus. `link/native_eboot.ld` should get the segment after all, and the plan to patch
Mesa's eight thread-local symbols is withdrawn.

## Mesa has two routes to the same command set

Patch 001 makes libdrm's `drmIoctl` call the shim. That is not the only way Mesa issues an
ioctl, and its own header says so - the sentence is the whole bug:

> These are identical to libdrm functions drmCommandWrite* and drmIoctl, but unlike libdrm,
> these are inlinable.

`src/util/os_drm.h` defines `drm_ioctl` as `static inline`, calling `ioctl(2)` directly.
`drm_ioctl_write` and `drm_ioctl_write_read` funnel through it, and the whole `ac_drm_query_*`
family goes that way. So:

| route | used by | reached the shim |
|---|---|---|
| libdrm `drmIoctl` | `amdgpu_device_initialize` | yes |
| Mesa inline `drm_ioctl` | `ac_drm_query_*` | no - called a kernel that is not there |

`ac_drm_query_gpu_info` took the second route and failed. Patch 002 puts that one call site on
the shim, guarded by `OOPS_MESA_WINSYS` like 001. It could not be a shim and more sharply than
001: these are `static inline` functions in a header, compiled into every caller, so there is no
symbol to interpose on.

## How long that took to see, and why

Four hardware runs, and the reason is worth writing down because the instruments were the
problem.

The first log put `drmGetDevice2 failed` at the failure point. It is not fatal -
`require_pci_bus_info` is false - and the real stop was later. The apparent order was wrong
because the runtime's stdout interception holds a partial line until its newline while the winsys
writes straight through, so winsys lines overtook Mesa's. Fixed: the winsys flushes the partial
line before writing its own. An occasional split line is the price; a wrong order is not visible
at all.

Then a trace naming each `AMDGPU_INFO` query the first time it was asked. It hid the thing it was
built to find, because the failing query was a *repeat* of one already traced and the handler that
failed it said nothing.

Then failure logging on every path that could refuse - and the log came back clean, because the
call never arrived.

**The instrument that actually worked was the simplest one: number every ioctl and print what it
answered.** Twelve calls, all `0`, then radeonsi giving up with no thirteenth. That gap is what
named the second route. A numbered log of every call is what you want *before* forming a
hypothesis, not after three.

## Where it stops now

Thirty-five ioctls answered, then:

```
# exception: 0xa0020103 (PRX_RUNTIME_ERROR)
# rip: 000000080003ad99
```

`rip` in libkernel, with a backtrace into the title. That is the shape of a call through an
unresolved import, not a fault in Mesa's logic - the same failure `getenv` presented with, whose
mechanism is written above `getenv` in `src/runtime/libc_absent.c`.

A title links with `--unresolved-symbols=ignore-all` (`oops-apps/common/app.mk:172`), so a name
the platform does not export links silently and traps when first called. Overriding that flag
lists them:

```
make elf TARGET_LD_FLAG="-Wl,--unresolved-symbols=report-all"
```

Twenty names, all present in the generated import manifest with a library assigned - which is the
point, because `getenv` looked exactly as fine until it was swept. REQ-20260917T1610Z-9c3e asks
obSCEne for the export status of all twenty, flagging `sysconf`, `sysctl`, `getrlimit` and
`strndup` as the ones radeonsi plausibly calls during screen creation.

Nothing will be shimmed on a guess. A shim that replaces a working platform function is a silent
divergence; one that does not replace a missing one is a crash.

## Three smaller things, each of which had cost time

**A title did not relink when Mesa moved underneath it.** The archives were named in `LDFLAGS`,
where make sees a flag and not a file, so a title whose own sources were untouched was "up to
date" - and the wrap step faithfully packaged a stale module. One deployment ran the previous
binary and its identical log read as "the fix did nothing". `app.mk` now carries
`PAYLOAD_EXTRA_DEPS += $(OOPS_MESA_LIBS)`, inside the `USE_MESA` guard, so no freestanding title
is affected.

**`pros restore` calls a correct deploy incomplete.** It compares bytes sent against bytes the
target reports, and the target unwraps the SELF container, so the payload on disk is larger than
what was sent. The warning says "it was not replaced" about a file that was replaced, and it very
likely explains a reading that led to hand-pushing raw modules past SELFish.
REQ-20260917T1500Z-3e57 is on the Prosperous bus.

**`AMDGPU_INFO_HW_IP_COUNT` is answered**, as 1 for graphics and a refusal otherwise. Mesa
tolerates its failure, so this unblocks nothing; it is answered because the `HW_IP_INFO` case next
door already states this device presents one graphics IP with one ring, and two handlers
describing one device must not disagree.
