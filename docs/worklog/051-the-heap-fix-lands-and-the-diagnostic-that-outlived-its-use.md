# 051. The heap fix lands, and a diagnostic that outlived its use

**2026-09-19** - roadmap unit 6

## The fix

obSCEne `REQ-20260919T1335Z-7b90` resolved within the hour, authoritatively: the
`sce_process_param` `libc_param` (0xA8 bytes) is what `libSceLibcInternal` reads, and
`oops-sdk/src/system/procparam.c` had left it size-only with every other field zero. That
selected libc's **internal-memory fallback** - a static ~8-13 MiB heap that ignores the
application's parameters, which is exactly the heap `dri-probe` exhausted in `driConcatConfigs`
(worklog 050).

The fix, applied to `procparam.c` with the resolution's field offsets and sentinels (retail
`AgcCompositor.elf` analysis, obscene#D298, a measured 432-448 MiB budget):

- `mode = 0` - **Application Heap Mode**, not the internal fallback.
- `version = 14`.
- `heap_size -> 0xFFFFFFFFFFFFFFFF` - grow to the container budget.
- `extended_alloc -> 1` - grow on demand.
- the two trailing parameter blocks (0x78/v2, 0xC0/v3) and the alloc-settings block, all pointed
  to as the layout requires (every entry is a pointer to its value, filled by link-time
  relocations; `init_alloc` is mutable because libkernel writes to it).

`_Static_assert(sizeof == 0xA8)` holds, oops-sdk builds, and every title picks it up (it is shared
process-param code). The change is measured, not guessed - which is the only reason it was made to
a structure libkernel writes into.

## The fix worked, and the proof was a diagnostic breaking

The first run with the fix crashed **`SceShellCore`**, not the title:
`SYSTEM_INTERNAL_SERVICE_TIMEOUT_ASYNC`, the shell's launch watchdog. The title printed its first
line and then the heap-ceiling probe - `malloc` 8 MB blocks until failure - ran.

That probe was written when the heap was 0 MB, to measure how small it was. With the fix in place
the heap **grows on demand**, so the probe no longer stops at a small ceiling: it keeps allocating
toward its 32 GB cap, committing flexible memory, and the app stays busy long enough that the shell
declares a launch timeout and kills it. The heap-ceiling line never printed because the probe never
finished. So the diagnostic confirmed the fix by the very way it failed - a 0 MB heap could not
have done this.

The probe is removed. A diagnostic sized for the broken state is a hazard in the fixed one, which
is worth remembering: instrument for the bug, delete it with the bug.

## The cost: the console is wedged

The shell-watchdog kill left the title's `eboot.bin` locked - `pros restore` gets `550 Text file
busy`, `pros close` gets its connection forcibly closed, and the process is already gone from
`pros ps`. The mapping did not release. This clears with a reboot (and re-running the exploit); it
is a console-side recovery, and it is the price of the probe mistake rather than anything wrong
with the fix.

## State

`procparam.c` carries the App-Heap-Mode param; oops-sdk builds; identity guard clean. `dri-probe`
is rebuilt **without** the probe, ready to deploy the moment the console is back. The expectation
for that run: the heap no longer starves, so `driConcatConfigs` succeeds and the frontend proceeds
into the drawable / context / `dri_make_current` path that no run has reached - the first genuinely
new ground past config enumeration.

Note also `-83df` resolved on the bus with the `sceVideoOutSetBufferAttribute2` attribute-block
layout (80-byte extent, field offsets), which is the presentation surface's descriptor - relevant
to `oops_gl_present` later, downstream of getting a context.
