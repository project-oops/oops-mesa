# 044. A title finishes by not finishing, and the answer had been on the bus for four hours

**2026-09-17** - roadmap units 5 and 7

## What this entry is

`REQ-20260917T1450Z-2e71` resolved at 16:15Z. Worklogs 042 and 043 were both written after that
and both cite it as open, because the bus was checked at 18:18Z and again at 20:28Z and the
request's *status line* was read rather than its content - it had moved from the open list, and I
was looking for my own two requests.

The answer changes code that shipped today, so it is worth its own entry rather than an edit.

## What was measured

No userland call terminates a `big-app` process, and that is architecture rather than a gap
(check `017-posix/process-exit-candidates`, `reports/hardware/20260917-160206-payload.obs.log`
lines 281-307):

| | |
|---|---|
| `exit`, `_Exit` (`libSceLibcInternal`) | **absent** |
| `sceKernelExit`, `sceKernelExitProcess` | **absent** |
| `sceSystemServiceKillLocalProcess`, `…KillApp`, `sceShellCoreUtilExitApp` | **absent** |
| `_exit` (`libkernel`, `0x8000003f0`) | **present**, and raises `SIGSYS` |
| `abort` (`0x8000be410`), `quick_exit`, `scePthreadExit` | present |

`_exit` being present and unusable is the interesting row. It reaches FreeBSD's `SYS_exit`,
syscall 1, which a `big-app` container's credentials do not permit: the kernel logs
`eboot.bin calls exit()` and then raises `SIGSYS`. And returning from the entry point faults at
`rip: 0x0` because the dynamic linker transfers control with no caller frame - `%rbp` zero,
`%rsp` holding argc - so the `ret` pops a null address.

Process lifecycle belongs to `SceShellCore`. The conforming ending is to print the last line you
have to print and then idle, letting the host close the app over JSON-RPC: no coredump, no crash
report, no hung GPU ring. That is what obSCEne's own eboot sweeps have always done.

## What changed here

**`exit` in `libc_absent.c` parked instead of trapping.** Worklog 042 called the trap "the honest
end, not a placeholder for a better one". The observation behind it was right - there is nothing
to delegate to - and the remedy was wrong, for a reason the resolution makes plain: a trap
produces the three artefacts the conforming pattern produces none of. It also costs the
diagnostic, which is the part that matters most. The log is the only account of why Mesa gave up,
and dying inside the logging process is the worst possible moment to do it. `nanosleep` carries
the idle loop, since this file has no oops-sdk dependency and `nanosleep` is exported and
measured.

**Neither probe returns from its entry point any more.** `mesa-probe` and `dri-probe` each end in
a `park()` that says `idle and finished - close this title from the host` and sleeps. Every exit
path goes through it, including the early ones - the winsys refusing to open, and GL failing to
come up.

This is why the tail of
[`screen-created-fw1240.md`](../hardware/screen-created-fw1240.md) looks the way it does. That
record's register dump is not a failure of the run it documents; it is what the old ending looked
like on a *successful* run. The record now says so and is otherwise left alone, because it is what
was measured.

## What did not change, and one thing that was already right

`abort` is present, and `__assert` and `__cxa_pure_virtual` still call it. That is deliberate:
both of those mean an invariant has already been reported broken, and stopping at the fault with
the message logged is the point of them. Parking would turn a detected bug into a hang. The
distinction is between *finishing* - which is what `exit` means, and which now parks - and
*failing*, which should still be loud.

`REQ-20260917T0233Z-5c9d` also resolved, at 09:20Z, and that one needed nothing: it measured that
on the eboot leg `write(1)` and `write(2)` both return the full byte count with `errno` zero and
**nothing reaches the captured log**, with `dup2` no help because fd 1 is equally disconnected,
and `SYS_klog` the only route that surfaces. `stderr_to_klog.c` was built against exactly that and
cites the sweep in its header. What was stale was `mesa_probe_main.c`, which still called it an
open question in two places - now corrected, and pointed at the interception that answers it, so
a reader of a failing run knows to look for `MESA`-tagged lines as well as `MESA-PROBE` ones.

## The process failure worth recording

I checked the bus twice while this answer sat on it, and both times I looked only at whether *my*
requests had moved. A resolution addressed to another title - `2e71` was filed by `gl1-probe` -
answered a question I had written into three documents and one shim, and I kept citing it as open
while building on the assumption it was.

Reading the open list is not checking the bus. The thing that would have caught this is checking
whether anything I *cite* has changed state, which is a different query and a cheap one.

## So the bus was then audited properly, and the result is mostly reassuring

The fix for the failure above is a different query: not "have my requests moved" but "has anything
I *cite* changed state". Run across every request id in this repository's documents and source -
28 of them - it says:

- **25 RESOLVED, 2 OPEN** (`-9f41`, `-6b3e`, both mine and both filed today), and one more on
  another bus: SELFish `-8d72`, the `PT_TLS` request, still open. That one matters because both
  Mesa titles carry a `local_tls.ld` whose comment says to delete it when `-8d72` resolves. It has
  not, so the script stays. Prosperous `-3e57` is also still open.
- **Four ids are on no bus at all**: `-1f6d`, `-4d7e`, `-7e29`, `-9a3e`, cited between them in
  `D007`, `libc_absent.c`, `syncobj.c` and three worklogs. No request with those timestamps exists
  under any suffix on any of the three buses.

That last one looked serious, so every claim resting on those four was checked against the sweep
logs they name, row by row. **All of it holds.**

| claim | cited as | in the log |
|---|---|---|
| controls resolved 27 of 27 on the payload leg | `20260916-235024` | 27 rows at `0x1`, payload leg |
| `getenv` absent from `libSceLibcInternal` | `20260917-013336` | `…\|getenv\|libSceLibcInternal\|0x0\|bool` |
| `sceAgcDriverAddEqEvent` and the Eq family absent | `20260917-001421` | three rows at `0x0`, plus `OBS\|sym\|…\|absent` |
| its controls resolved | `20260917-001421` | `CreateQueue`, `DestroyQueue`, `SubmitDcb` all `0x1` |
| fence is 64-bit | `20260917-001421` | `fence-val-lo 0xbeefcafe`, `fence-val-hi 0x12345678`, `fence-bytes-landed 0x8` |

So the evidence is real and the four dead ids are labels rather than the citation: **every one of
those notes already names its sweep**, which is what made the audit possible in a few minutes. The
request id is the part that cannot be followed, and it is the part that matters least.

Nothing is rewritten on the strength of that. Six files would change to remove a broken label from
citations whose evidence verifies, which is churn; and the ids may yet be real - a bus is one
agent's file, and an id absent from the copy at this desk is not proof it never existed. Recorded
here so the next reader does not repeat the twenty minutes.

**One genuine finding fell out of it.** The same `20260916-235024` sweep shows all 28 POSIX
`pthread_*` symbols reading `0x0` on the **eboot leg** while 27 resolve on the payload leg. A
title is the eboot leg, so POSIX threads are simply not there for one - which is exactly why
`threads.c` maps Mesa's C11 threads onto the vendor `scePthread*` twins. That decision was made
for a different reason and this is independent confirmation of it.

## Done afterwards: the ending became oops-sdk's, not each title's

`gl1-probe` was flagged rather than fixed here, on the grounds that changing how a title
terminates changes what a launcher reading its exit code observes. It was picked up immediately
after and the answer to that worry is that **there is no launcher left to read a code** - the
process cannot exit to hand one over. Its verdict was never only in the code either:
`report_total` prints `gl1-probe: NN/NN passed on hardware`, and `-2e71` names that exact line as
the completion sentinel a harness watches. So parking discards nothing.

Two things were settled while doing it, and both changed the shape of this fix:

- **It belongs in oops-sdk.** `oops_system_park_until_closed()` is now declared in
  `oops/system.h` and defined in `src/system/system.c` - an existing file, so no Makefile
  registration was needed. The measurement is written up at the declaration, which is where a
  reader of a title's one-line call will look.
- **It had to be self-contained.** The obvious implementation calls `oops_time_sleep_ms`, and
  that would have been a quiet defect: four titles link `system.c` **without** `time.c` -
  `tls-probe`, `injector`, `tracer`, `pad-viz` - and a title's link ignores unresolved symbols
  rather than failing, so the tidier version buys a symbol resolving nowhere that traps when
  first called. It binds `sceKernelUsleep` weakly itself instead, the way `time.c` does.

`mesa-probe` and `dri-probe` keep a local `park()` that says their own tagged line and then calls
the helper, so the loop is shared and the log line stays greppable per title. `tls-probe` now
parks too - same shape, same harness, and its two verdict lines were the whole output being
followed by a crash report.

`oops_system_park_until_closed` verified **defined rather than imported** in every title built
(the `nm` check that matters, since an app link would have ignored it silently). oops-sdk builds
under `-Werror` and its 229 tests pass; `oops-apps check` is green across all ten selftests.

## Not applied to six titles, and that is a decision rather than an omission

`gl1-cube`, `gallery`, `net-tool`, `pad-viz`, `porthole` and `seashell`'s sibling paths still
return, and they are **not** the same case. For a harness-driven probe the current ending is a
crash report attached to a good result, and parking is strictly better. For a title a person
quits, the crash at least ends the process - the console returns to the dashboard - whereas
parking leaves it frozen on screen with no way out but the console's own close.

The same resolution names the right ending for those: `sceSystemServiceNavigateToGoHome()`, which
returns the display to the home screen and then idles. `system.c` already binds it weakly. That
is a different helper with a different contract, and choosing it per title is a judgement about
each title's users rather than a mechanical substitution, so it is left to whoever owns them.

## State

Both Mesa titles relink, place 480 imports with 0 unknown, and package. `./bin/oops-mesa check`
passes, pin `mesa-26.2.2`, 2 patches. Identity guard clean on oops-mesa and oops-apps. Neither
title has run on hardware.
