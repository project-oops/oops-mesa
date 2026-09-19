# 048. The full stack ran on hardware, and the parking ending met its catch

**2026-09-17** (run) / **2026-09-19** (written up) - roadmap units 5 and 7

## What happened

`mesa-probe` was deployed and launched on the retail console with Prosperous - `pros restore`,
`pros launch MESA00001`, `pros logs` - for the first time since the 17:15 module. This build
carried everything since: libm, the C locale tables, the C++ ABI shims, the fourteen names `-a4f2`
measured absent, the GL entry points, the processor-count shim, and the parking ending. Ten
distinct changes, none of them previously observed on hardware.

**It ran clean.** The capture is
[`docs/hardware/full-stack-parks-fw1240.md`](../hardware/full-stack-parks-fw1240.md), verbatim.
Build stamp `v2026-09-17 22:22` - the current binary. 35 ioctls, answered exactly as the 17:15
baseline answered them, the two tolerated `AMDGPU_INFO` refusals in the same places.
`radeonsi created a screen: the startup path is complete`. No `PRX_NOT_RESOLVED_FUNCTION`
anywhere - every one of the ten changes bound and ran.

Then the line that only this run could produce:

```text
[MESA00001:MESA-PROBE] done
[MESA00001:MESA-PROBE] idle and finished - close this title from the host
```

and `pros ps` showing it `SLEEP`, 55 threads, steady across repeated samples. **The parking
ending works.** The 17:15 run ended in a fault at `rip: 0x0`; this one ends idle and stays there.
The 55 threads are worth noting on their own - that is Mesa sizing its pools for a many-core
machine, which is the processor-count shim (`-6b3e`) taking effect; the old `-1` fallback would
have given a one-thread pool.

## The catch: a parked title cannot be closed from `pros`

Parking is the right ending, and it immediately exposed the thing that makes it incomplete. With
the probe idle and holding the big-app slot, none of Prosperous's process-control verbs could
close it:

```
pros close MESA00001   -> did not close (tried twice)
pros kill 1802         -> did not end
pros restart-ui        -> UI came back; MESA00001 still SLEEP
```

and so the next probe would not start:

```
pros launch DRIP00001  -> sceSystemServiceLaunchApp: Resource temporarily unavailable
```

This is not a surprise about signals - big-apps have always ignored them, and that is written down
(`ps5-close-app-via-jetkvm`). What is new is that it was measured against a *parked* title, and it
contradicts the `-2e71` resolution, which named "let the host close the app (`pros close <ID>`)" as
the conforming lifecycle. This run is the first to exercise that close path, and it did not close.

The capability exists - the shell's own tile "Close" (JetKVM: PS > home > F3 > Close) ends it
cleanly, which is a SceShellCore close rather than a signal. So `pros close` is reaching for
something the platform ignores for homebrew big-apps, while a working close sits one layer over in
the UI. That is transport's to see, and is filed as Prosperous `REQ-20260919T0912Z-b1e4`.

**The trade is still worth it.** Parking replaced a coredump-every-run crash with a clean idle;
the cost is that the title must be closed from outside, which was always true (the crash never
freed the slot for `pros` either - the console was reset between runs historically). What the run
sharpened is that the outside close needs to become a `pros` verb, or the build/deploy/launch loop
stays hands-on.

## One real new finding inside the good run

`cpuset_getaffinity` **bound but returned failure at run time.** The log shows the fallback firing:

```text
sysconf(_SC_NPROCESSORS_ONLN): the affinity mask could not be read, so reporting the 14 obSCEne
measured for a big-app container rather than failing into Mesa's one-thread fallback.
```

So the symbol resolves - no unresolved-symbol death, which is what `-a4f2`/`-6b3e` established - but
the *call* did not succeed the way it did in obSCEne's probe. The fallback of 14 is the
measured-correct value, so nothing was wrong downstream; Mesa still got 14 and sized 55 threads for
it. But the runtime query I preferred over a hardcoded constant was not actually working, which
would defeat the whole reason it was a query: tracking the 12-vs-14 variance `-6b3e` warned about.

**The code read found the bug, and it was mine.** The shim called `cpuset_getaffinity` with
`CPU_LEVEL_WHICH`; FreeBSD's own `sysconf(_SC_NPROCESSORS_ONLN)` uses `CPU_LEVEL_ROOT`
(`lib/libc/gen/sysconf.c:595` at the D004 pin). The difference is not cosmetic: `CPU_LEVEL_ROOT`
asks for the container's CPU *budget* - the root set, which for a jail-shaped sandbox is the
limited count that actually applies, exactly the 14-of-16 this is trying to read - while
`CPU_LEVEL_WHICH` asks for the current affinity *mask*, which a freshly-launched process need not
have set and which the sandbox evidently declined. The level was a plausible guess where the
reference had the measured idiom; it is now `CPU_LEVEL_ROOT`, citing `sysconf.c`.

That is a code fix against the authoritative source (D004's whole premise), not a hardware guess,
so it went in now. Whether it makes the runtime query succeed is pending the next hardware run -
until then the fallback still gives the correct 14, so the fix cannot regress anything.

## What did not happen, and why

`dri-probe` did not run. It was deployed (`pros restore` succeeded, the stored size matched the
local ELF), but the parked `MESA00001` held the slot and could not be cleared, so its launch was
refused. It remains the run that would first exercise `oops_gl_create` on hardware.

Between the run and this write-up the console was rebooted and is **not currently exploited** -
`pros check` shows elfldr (:9021) not answering - so nothing can be deployed or launched until the
exploit chain is re-run, which is a console-side action. The `dri-probe` run is therefore pending
that, not pending any code here.

## State

`./bin/oops-mesa check` passes, pin `mesa-26.2.2`, 2 patches. Identity guard clean. One code change
this entry - the `CPU_LEVEL_ROOT` fix in `libc_absent.c` - and all three Mesa titles rebuilt and
repackaged against it (459 imports each, 0 unknown). One request filed (Prosperous `-b1e4`). The
`dri-probe` hardware run and the affinity fix's runtime verification both wait on the console being
re-exploited.
