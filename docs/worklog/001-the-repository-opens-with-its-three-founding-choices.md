# 001. The repository opens with its three founding choices written down

**2026-09-14** - roadmap unit 1, after the prior-art audit (orbistoun#541)

## What changed

A new repository in the collection, `oops-mesa`, initialised locally under the OOPS identity
and not yet a submodule of the parent (that needs the remote to exist; the parent's project
lists name it already). It holds:

- upstream Mesa as a shallow submodule at `mesa-26.2.2`
  (`3281a69a8bfd9f997e91c15ed0e6290cae12dd32`), pinned in `dependencies.mk`;
- the collection's licence pair, line-ending policy and verb script; `check` is real and
  verifies the pin, the patch headers and the required documents; `build` and `test` fail
  loudly with what would have to exist first;
- the container image definition (clang 18, meson, ninja, Mesa's Python generators, no LLVM);
- decisions D001 (a shim under Mesa, not a driver), D002 (Mesa runs on the platform's own C
  library reached by published names; assumed, a stated divergence from the freestanding rule),
  D003 (radeonsi first, own driver second, gated by one measurement; assumed);
- the roadmap, with the route measurement as unit 2, before any shim code.

No source. `src/` appears with the unit that fills it.

## What it unblocks

Unit 2, the measurement that decides the driver route, can run on the oops-gl instrument
tomorrow: emit radeonsi's initial graphics state through the existing submission path, watch
the compositor, hash a known frame. Everything after it depends on that answer, which is why
nothing after it was started.

## Surprises

- **The SDK was never freestanding in the sense the rule reads.** `oops-sdk/src/thread/thread.c`
  binds the vendor thread API by published name, as the graphics driver, the display and the
  pad are bound. D002 extends the same act to the C library's exported names; the divergence
  from "freestanding only" is one of degree, and it is written down as one.
- **The C library's standard names were measured on firmware 12.40 on 2026-08-30**, two weeks
  before anyone asked: obSCEne's census bound `malloc`, `fopen`, `qsort`, `sinf` and `snprintf`
  from the platform's C library. The `pthread_*` names are mined candidates only; the thread
  mapping in D002 exists because of that gap, and closing it is a one-line census request.
- **Upstream is on 26.2.2, not 26.2.0.** The closest prior art pins the 26.2.0 tarball; this
  repository pins the latest patch release of the same series so the bump history starts from
  a known point.
- **The parent's tools enumerate projects by name in two shell lists** (`bin/oops`,
  `tools/check-decisions.sh`). A new repository is invisible to the gates until both are
  edited, which is easy to forget and cheap to do; both now name `oops-mesa`.

## Next

Unit 2, then unit 3's container build up to Mesa's first compile error against the runtime
shim's headers, which is the work list for unit 4. The `pthread_*` census request goes on the
obSCEne bus with unit 4.
