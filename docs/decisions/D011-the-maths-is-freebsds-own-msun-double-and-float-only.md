# D011 - The maths is FreeBSD's own msun, double and float only

**Status:** decided
**Date:** 2026-09-17

`lib/msun` from the checkout D004 pins is compiled for the target in the container and
linked as `libm.a`, double and float precision only. Long double and the `amd64/*.S`
optimisations are not built. The locale table is upstream's `lib/libc/locale/table.c` from
the same pin.

**Why:** the platform exports almost no arithmetic natively; `sin`, `floor`, `powf` and
their kin are absent, and an absent import loads and then fails on first call with
`PRX_NOT_RESOLVED_FUNCTION`. msun is the implementation the pinned headers already
describe. Mesa's GL and GLSL paths do not use long double, so a `sinl` reference is a link
error rather than a wrong-precision result.

**Rejected:**
- Hand-written maths in `libc_absent.c`: an almost-correct transcendental renders slightly
  wrong forever, with nothing to report it.
- openlibm, musl or a vendored libm: a second upstream at a second revision.
