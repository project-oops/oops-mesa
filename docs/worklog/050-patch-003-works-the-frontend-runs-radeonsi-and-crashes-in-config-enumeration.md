# 050. Patch 003 works: the DRI frontend runs radeonsi, and crashes enumerating configs

**2026-09-19** - roadmap unit 6

## The headline

`dri-probe` with patch 003 ran on hardware and **the Gallium DRI frontend got past the fd dup and
drove radeonsi through its entire device initialisation** - 39 ioctls, versus zero on every prior
run. The fd fix (worklog 049, patch 003) is verified: `os_dupfd_cloexec` now passes the winsys
device fd through when `fcntl(F_DUPFD)` returns EINVAL, and the frontend proceeds.

The capture is [`docs/hardware/dri-frontend-runs-radeonsi-fw1240.log`](../hardware/dri-frontend-runs-radeonsi-fw1240.log).
The ioctl sequence is the same device-identification path `mesa-probe` established - version
queries, `AMDGPU_INFO` 0x16/0x15/0x03/0x02, `GB_ADDR_CONFIG` derived, the nine IP-type probes
refused with -78, firmware versions answered 0, memory `0x300000000` - reaching `sysconf` and
`__xuname` exactly where `mesa-probe` does. So through this path, radeonsi built a `pipe_screen`.

This is the first time the **frontend** path (as opposed to `mesa-probe`'s direct
`radeonsi_screen_create`) has run radeonsi at all. It is the route a real GL title takes.

## Where it crashed, and how that was located

The process took a fatal signal rather than parking - gone from `pros ps`, where every earlier run
sat parked at 55 threads. The dump:

```
signal: 11 (SIGSEGV), reason: page fault (user write data, page not present)
fault address: 0x0
rip: 0x010a0bf0
backtrace: 0x1099e50  0x1099617  0x108ad83  0x0
```

A **null-pointer write** - not the `rip: 0x0` exit fault, a real code address writing to address 0.
The title's text loads at 0x400000, so subtracting that gives link addresses, resolved against
`build/dri-probe.map`:

- `rip` 0xca0bf0 is inside **`dri_init_screen`** (0xca0380 + 0x931), specifically its tail, where
  `dri_fill_in_modes` is inlined (`src/gallium/frontends/dri/dri_screen.c`).
- backtrace 0xc99e50 is inside **`driCreateNewScreen3`**, which calls `dri_init_screen`.

So: `driCreateNewScreen3` -> `dri2_init_screen` returns the radeonsi `pipe_screen` ->
`dri_init_screen` -> `dri_fill_in_modes` enumerates the framebuffer configs, and null-writes there.
`dri_fill_in_modes` walks a format table, calls `p_screen->is_format_supported` (radeonsi's) and
`driCreateConfigs`/`driConcatConfigs` to build the config list. The write to 0x0 is in that loop.

Line-level symbolisation is not available from the shipped archives (`-O2`, no `-g`), so
`dri_screen.c` was recompiled with `-g` added to the exact ninja command (same `-O2`, so the code
does not move) and `addr2line -i` run on the known offset - no hardware needed, the address is
fixed. The inline chain is exact:

```
dri_init_screen        dri_screen.c:660
  dri_fill_in_modes    dri_screen.c:465   (configs = driConcatConfigs(configs, new_configs))
    driConcatConfigs   dri_screen.c:292   (all[index++] = a[i])   <- the fault
```

**The null write is `all[index++] = a[i]` with `all == NULL`** - `all` is the result of
`malloc((i + j + 1) * sizeof *all)` two lines up, and there is no NULL check. The fault address is
exactly 0x0, the first store (index 0), which confirms `all` is NULL: **`malloc` returned NULL.**

## Why malloc returned NULL: the libc heap, most likely

The allocation that failed is tiny - a handful of config pointers. A tiny `malloc` failing means
the **libc heap is exhausted**, not that this request is large. And the shape fits:

- `mesa-probe` calls `radeonsi_screen_create` directly and reached ~65 MB and parked fine.
- `dri-probe` goes through the frontend, which runs `st_api_query_versions` *before*
  `dri_fill_in_modes` - and that spins up a throwaway GL context to probe versions, which is a lot
  of allocation `mesa-probe` never does. By the time config enumeration asks for its small array,
  the heap is full.

And the process param backs this up: `oops-sdk/src/system/procparam.c`'s `oops_libc_param` is 0xA8
bytes **all zero except the size field** - none of libc's heap-tuning entries (`sceLibcHeapSize`,
`sceLibcHeapExtendedAlloc`) is set, so the heap is whatever the default is and does not grow on
demand.

**Not yet confirmed against the alternative**, which the crash cannot rule out on its own: a
corrupt or non-NULL-terminated config array would make `while (a[i]) i++` count a huge `i`, so
`malloc((huge)*8)` returns NULL and the same 0x0 write happens. Distinguishing needs the size
logged at the failure - a small debug patch to `driConcatConfigs` logging `i`/`j`, run autonomously
(a crash frees the slot, so no manual close). That confirmation gates the fix: if the counts are
small, the fix is the libc heap param (a shared oops-sdk ABI change needing the field offsets from
a public source, not a guess); if huge, the bug is upstream in config creation.

## Confirmed: the libc heap is small (2026-09-19)

A heap-ceiling probe added to `dri_probe_main` - `malloc` 8 MB blocks until failure, touch them,
count, free - ran on hardware and reported:

```
heap ceiling: 0 MB across 0 x 8MB blocks before malloc failed
```

**The first 8 MB block failed at startup**, before any GL work. Yet the same run drove radeonsi
through all 39 ioctls and crashed later on the *tiny* `driConcatConfigs` array. That is the
signature of a small libc heap, not a corrupt count: radeonsi's large buffers go through the winsys
(the 12 GB direct-memory pool), but its CPU-side structures use libc `malloc`, and that heap cannot
even satisfy one 8 MB request - so the cumulative small allocations (radeonsi screen, then the
throwaway GL context in `st_api_query_versions`) exhaust it, and config enumeration's small array is
the one that tips it over. Heap exhaustion confirmed; the corrupt-count alternative is ruled out
(a corrupt count would not also make an 8 MB startup allocation fail).

**The fix is to give libc a large, growable heap** via the `sce_process_param` libc entry, whose
fields are all zero today (`oops-sdk/src/system/procparam.c`). That is a shared oops-sdk change and
a platform-ABI detail - the offsets of `sceLibcHeapSize` / `sceLibcHeapExtendedAlloc` within the
0xA8-byte libc param must come from a source, not a guess, because libkernel writes into that
structure during init and a wrong layout breaks every title. Next: establish that layout from a
public homebrew SDK (or an obSCEne measurement) and set the heap to extended/grow-on-demand.

## Filed, and a fallback if the answer is slow

obSCEne `REQ-20260919T1335Z-7b90` asks for the authoritative mechanism: which the platform's libc
reads (process-param field offsets, a global, or a `sceLibcHeapSetAddressRange*` call) and the
grow-on-demand values. It cites this crash, the 0 MB ceiling, and obscene worklog 019's own lead
(which found the same small heap and explicitly said not to guess the mechanism).

A shadow-`malloc` fallback (route Mesa's allocations through oops-sdk's `heap.c`) was considered and
**rejected on the merits**, for two reasons found by reading the code:

- It is architecturally backwards. libc's `malloc` is not broken here - radeonsi allocated through
  it and ran 39 ioctls - it is merely *capped*. The fix is to raise the cap (7b90), not to build a
  parallel heap to avoid it.
- It has its own unmeasured dependency anyway. `heap.c` is backed by raw `sys_call(SYS_mmap)`, which
  needs a `sys_call_init` a title never does; the winsys gets memory in a title through
  `sceKernelAllocateDirectMemory`, but that is write-combined GPU memory, wrong for a CPU heap. A
  title-viable CPU-memory source (anonymous `mmap`, or `sceKernelMapFlexibleMemory`) is itself
  unconfirmed on this leg.

So there is no sound autonomous shortcut; 7b90 is the fix. The heap probe stays in `dri-probe`
meanwhile, reporting the ceiling each run so the fix is visible when it lands (0 MB now; non-zero
once the cap is raised).

## What it means

The startup surface is deeper than it has ever been. Everything up to and including a radeonsi
`pipe_screen` works through the real frontend. What does not yet work is turning that screen into
the DRI config list a context needs - the step between "the driver initialised" and "a GL context
can be made". That is genuinely new ground; no run had reached it.

The null write is in Mesa's own generic config code, which works on every ordinary platform, so the
cause is most likely something radeonsi's `is_format_supported` reaches that depends on a device
value this winsys assumes rather than measures (six device-info groups are assumed, firmware
versions are 0, `GB_ADDR_CONFIG` is derived) - or a config allocation this configuration returns
empty. The `-g` line will point at which.

## Not yet fixed

The crash is undiagnosed at line level and unfixed. The fd fix that got here (patch 003) is solid
and is the durable win of this run. `oops_winsys_open` now hands out a real openable descriptor
(the title's own `/app0/eboot.bin`), which patch 003's passthrough carries through the frontend's
dup - both measured to be necessary and sufficient to reach radeonsi through the frontend.

## State

`./bin/oops-mesa check` passes, pin `mesa-26.2.2`, **3 patches**. Host suite 109/0. Identity guard
clean. The console holds a parked... no - it holds nothing: this run crashed, so the big-app slot
is free. Next: the `-g` line for the null write, then the fix.
