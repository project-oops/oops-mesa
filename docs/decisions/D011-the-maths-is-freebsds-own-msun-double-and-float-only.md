# D011 - The maths is FreeBSD's own msun, built for the target, double and float only


**decided** · 2026-09-17 (the alternative was tried at one-function scale first, and it worked)

D002 says Mesa reaches the platform's C library by published names, and for most of them it does.
Arithmetic is the exception, and it is not a small one: the platform exports almost none of it.

## What was measured

obSCEne swept the linked title's complete undefined set on firmware 12.40
(REQ-20260917T1640Z-5b28, 139 names, controls 3 of 3 on both legs). `sin`, `cos`, `floor`, `ceil`,
`log`, `log10`, `atan2`, `powf`, `round`, `trunc` and their float twins came back absent, and
absent from this collection's export census as well - so absent twice over, by two independent
methods. Nine more that the sweep never reached (`acosf`, `expf`, `logf`, `log2`, `lround` and
kin) were undefined in the link too.

An absent name is not a link error here, because a title places its imports from a mined corpus
and links with `--unresolved-symbols=ignore-all`. It loads, and the console kills it on the first
call with `PRX_NOT_RESOLVED_FUNCTION`. So this had to be answered before anything drew a triangle.

## The choice

**`lib/msun` out of the checkout D004 already pins, compiled for the target in the container,
linked as `libm.a`. Double and float precision only.**

Three things follow from that and are part of the decision, not details of it:

- **Long double is not built.** No `ld80`, and a long-double source is skipped when the
  double-precision twin beside it exists. Mesa's GL and GLSL paths do not use long double, and the
  consequence is chosen: a reference to `sinl` is an undefined symbol at this desk instead of a
  number computed at the wrong precision on the console.
- **The `.S` files under `amd64/` are not used.** They are optimisations of functions that all
  have C implementations, and FreeBSD's own build makes them conditional for that reason.
- **A name the platform does export is still imported.** This is not a wholesale replacement of
  the platform's C library; it answers the arithmetic and nothing else.

## Why not write the functions here

That was tried, at the honest scale for it. `pow` was a hand-written shim in `libc_absent.c`:
repeated multiplication, exact over the whole-number exponents its two callers actually use, loud
about refusing a fractional one. That is a defensible shim and it was the right thing while
nothing better existed.

Twenty-three functions is a different proposition. A correctly-rounded transcendental is a
project, and an almost-correct one is precisely the plausible-looking answer principle 4 exists to
refuse - with a failure mode that never announces itself, because a shader compiled from a
slightly wrong `log` renders slightly wrong forever. The one place this repository may not trade
accuracy for convenience is the place where the error is invisible.

## Why this implementation and not another

Because it is the one the headers already describe. D004 pins a FreeBSD checkout for the C-library
headers Mesa compiles against; `lib/msun` in that same checkout at that same revision is that
library's own arithmetic. Taking a libm from anywhere else - openlibm, musl, a vendored copy -
would mean a second upstream at a second revision, and a header set describing an implementation
the build does not use.

It is also an established shape here rather than a new mechanism: `stage-sources.sh` already takes
libelf and libc++ out of the same object store with `git archive`, and the container already
compiles both for the target. libm is a third entry in a list of two.

## What this costs

42 KB of module, and a build step. `pow` left `libc_absent.c` because the linker reported it
defined twice, which is the correct failure and how the overlap was found.

## What this also settled, once the conflict resolved

Six names that *are* fully specified - the `__isfinite` and `__isnormal` predicates - come from
msun as a side effect, and `__isinff` is written out because FreeBSD keeps the `isinf` family in
libc rather than msun. Those six were among the 33 where the export census and the sweep
disagreed, and they were defined while that was still open, on this rule:

> a symbol whose value is fully specified can be defined locally without prejudicing an open
> measurement; a symbol that describes the platform cannot, and stays an import.

**The rule held, and the conflict resolved against the census.** REQ-20260917T1818Z-9f41 closed
on 2026-09-17: string and NID dynamic resolution is authoritative, all 33 are unbindable
natively, and the census rows were captured under **GEN=4** - the PS4 backward-compatibility
container, where Orbis' `libSceLibcInternal` did export the C runtime and the maths. Native
Prospero replaced it with a stripped PRX. So census presence never implied bindability, and every
one of the 33 had to be local.

The rule is worth keeping for the next time two measurements disagree: it picked exactly the
subset that was safe to act on early, and the eleven it refused to cover were precisely the ones
that needed the answer - a locale table, a filesystem, a clock, a program name. Those are now
supplied too, and the locale table is upstream's own (`lib/libc/locale/table.c`, staged from the
same pin as msun for the same reason) rather than a guess at one.
