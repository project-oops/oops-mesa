# 047. Every import is now a measurement

**2026-09-17** - roadmap unit 7

## What this entry is

The C-runtime surface is closed, and closed in the strong sense rather than the "nothing is
crashing" sense:

```text
  the platform exports             98   (measured on the native leg)
  probably exported                 0   (GEN=4 census only - ADVISORY)
  MEASURED ABSENT, STILL IMPORTED   0
  NOBODY PROVIDES                   0
```

Every symbol a Mesa title imports has been resolved to an address on retail firmware 12.40. Not
inferred from a census, not assumed from a name that looks standard - measured, with a virtual
address, on the leg a title actually runs on. 181 undefined symbols at the start of the day are
**95**, and every one of them binds.

## How the last 34 were found and answered

Worklog 046 left the tool able to say exactly which imports had never been measured natively: 29
known only from the GEN=4 census, plus 5 nobody had an opinion on. That list was filed as
`REQ-20260917T2045Z-a4f2` and swept within twenty minutes: **20 present, 14 absent**.

The 14 are now defined here, and which are live was established the same way as before - by
reading which object references each:

| | reached from |
|---|---|
| `isatty` | the GLSL lexer. flex asks whether its input is a terminal, on **every compile** |
| `__cxa_atexit` | the GLSL built-in function table |
| `__cxa_guard_acquire` / `_release` | the ASTC lookup tables' function-local static |
| `_ZnwmRKSt9nothrow_t`, `__isthreaded`, `stpcpy`, `__fpclassifyf` | assorted, cheap, exact |
| `setjmp`, `longjmp` | SPIR-V's error recovery - not reached by a GL 3.3 title |
| `sigaction`, `atof` | Mesa's HUD, switched on by an environment this platform does not have |
| `shm_open` | `util/anon_file.c`, wanting an anonymous file to back a shared buffer |

Three of those deserve their reasoning restated, because "stub it" was the wrong answer for each:

- **`__cxa_atexit` is not a stub that declines to work - returning 0 is its whole contract here.**
  It registers a destructor to run at process exit, and this process does not exit: a big-app
  container cannot terminate itself and a title parks instead (worklog 044). Accepting the
  registration and never firing it is exactly what happens on any platform where a process is
  killed rather than exited.
- **The guards are real, with atomics**, because `texcompress_astc_luts.cpp` has a function-local
  static and Mesa is multithreaded. A guard that lets two threads through runs the initialiser
  twice, which is a corrupted lookup table rather than a crash - the worst kind of bug to ship.
- **`longjmp` traps.** `setjmp` returning 0 is honest, since 0 is what a direct call returns and
  there is genuinely nothing to restore. `longjmp` has no honest option: it is declared not to
  return and there is no saved context, so every behaviour is wrong except stopping. Writing the
  real pair is forty instructions of assembly guarding a path that needs a working SPIR-V front
  end before it matters; the trap names the situation instead, and says where the work goes.

## Two more holes in the instrument, both mine

Worklog 046 fixed this tool for trusting a GEN=4 census. It had two more defects, and finding them
cost one duplicate symbol and one wrong request.

**It only knew about oops-mesa's own shims.** The "answered by the shims" pile is built by
compiling `src/runtime` and `src/winsys`. A title links more than that - oops-sdk's freestanding
helpers among them - so `memcmp` was reported unprovided when `src/system/freestd.c` had defined
it all along. Acting on that produced `duplicate symbol: memcmp` at link, which is the harmless
direction for this mistake and is how it surfaced. The tool now compiles `freestd.c` too.

**It read a hardcoded list of sweeps.** `a4f2`'s results landed in
`20260917-220500-payload.obs.obs.log` - note the doubled extension, where the two earlier sweeps
are `-payload.obs.log` - so the tool did not see them, and reported `flock`, `ftruncate`,
`isspace` and `strrchr` as unmeasured immediately after obSCEne had measured all four present.
The list is a date-scoped glob now, because a hardcoded one silently loses a sweep whenever the
naming moves.

Both are the same class as the census defect: **a pile built from a subset of the evidence, with
nothing saying it was a subset.**

## And an inference of mine that the sweep disproved

The request said six of the 34 were "already answered by execution" and asked obSCEne not to spend
a slot on them - `memcpy`, `memset`, `memcmp`, `__cxa_atexit` and the two guards - on the grounds
that `MESA00001` answered 35 ioctls and created a screen, and nothing gets that far without them.

**Four of the six are absent.** `memcpy` and `memset` resolve; `memcmp` and the three C++ ABI
entries do not. The run survived because clang inlines most small `memcmp` calls and because
nothing had yet reached a static-local initialiser. Had obSCEne taken the suggestion, four names
would have been marked settled on an argument rather than a measurement - and `__cxa_atexit` is on
the GLSL compile path, so the first shader would have found it.

The lesson is narrow and worth keeping: *"it must be bound, because the thing ran"* is only sound
for code the run actually executed, and a link is not an execution. The same sentence with "the
census says so" in place of "the thing ran" is the mistake worklog 046 was about.

## State

`mesa-probe` and `dri-probe` relink, place 459 imports with 0 unknown, and package; `tls-probe`
too. `./bin/oops-mesa check` passes, pin `mesa-26.2.2`, 2 patches. Identity guard clean on
oops-mesa and oops-apps.

Nothing has run on hardware. The console still carries the 17:15 module, which now predates ten
distinct changes.

## What is left on this unit

Nothing that is measurable from here. Every import binds, every shim's reachability has been
checked against the objects that reference it, and both instruments - the link with
`--error-limit=0` and this tool - agree on zero.

What remains for unit 7 is not analysis but a run.
