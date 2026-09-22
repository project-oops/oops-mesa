# D013 - The collection compiles with clang 21, and the pin is enforced rather than described

**decided** · 2026-09-21 (the route was chosen in worklog 067 and executed the same day, after
unit 6 closed; the measurements below were taken during the execution and one of them corrects
D006)

This is the collection-level decision D006 declined to make alone and
[worklog 067](../worklog/067-unit-8-needs-a-cpp-standard-library-nobody-costed.md) recorded the
choice of. It is written here, in oops-mesa's log, because the reasoning chain that leads to it
is here - D006, worklog 067 - and the collection has no decision log of its own. Cite it from
elsewhere as `oops-mesa#D013`.

## The choice

**clang 21 everywhere that compiles C or C++, and `silkeh/clang:21` is the authority.** WSL
`oops-builder` remains a permitted runner and is correct exactly while it agrees.

`obscene` and `oops-apps` each carry a `toolchain.mk` that refuses to compile against a
different major. `oops-mesa`'s runner *is* its container, so the `FROM` tag in
`toolchain/Dockerfile` is its pin and a `toolchain.mk` beside it would be a second place to be
wrong. `OOPS/tools/check-toolchain.sh` fails when the numbers disagree, or when a repository
that compiles declares none.

## What the problem actually was, which is not "the pin was 18"

The pin was **not enforced anywhere**. `oops-sdk/Makefile`, `obscene/Makefile` and
`oops-apps/common/app.mk` all said bare `CC := clang`, and `oops-mesa/toolchain/Dockerfile` said
`FROM silkeh/clang:18`. So on 2026-09-21, before anything here changed, the same source compiled
with **clang 21 under WSL and clang 18 under Docker**, and which one you got depended on where
you were standing. Three documents asserted a pin - `AGENTS.md`, `CLAUDE.md`,
`docs/USER_GUIDE.md` - and no build checked it.

That is worse than being pinned to the older compiler, because it is invisible. Both compilers
produce objects, both links succeed, and the difference surfaces somewhere else entirely: as a
C++ standard library that will not build, or as a fixture that churns. A bump that only changed
`18` to `21` in those four places would have left the actual defect in place.

**So the decision has two halves and the second is the load-bearing one.** Moving to 21 is what
unblocks the C++ work. Making the pin refuse to be wrong is what stops the next version skew
being found by its symptoms.

## The measurement that justifies 21, re-taken

D006 said, of building libc++ from the pinned checkout: *"43 of its 71 sources fail, on
constructs in its own type traits that clang 18 does not implement."* That was the reason to
move, so it was re-measured on the way rather than assumed - both compilers, the same tree, the
same flags `toolchain/build-mesa.sh` uses, every `.cpp` under `toolchain/libcxx-src/src`
compiled one at a time:

| compiler | compiled | failed | of |
|---|---|---|---|
| clang 18.1.8 | 26 | **39** | 65 |
| clang 21.1.8 | 59 | **6** | 65 |

The denominator is 65 rather than D006's 71 because the `freebsd-src` pin has moved since; the
shape of D006's claim is confirmed and its numbers are a dated record of a different tree, which
is what a decision log is for.

**The six that still fail under 21 are not the failure D006 described.** Not one is a type-trait
construct. Read individually:

- `pstl/libdispatch.cpp` - `'dispatch/dispatch.h' file not found`. Apple's libdispatch. Upstream's
  own build never compiles this off Darwin.
- `support/ibm/xlocale_zos.cpp` - `'__support/ibm/xlocale.h' file not found`. z/OS. Same.
- `experimental/tzdb.cpp`, `experimental/tzdb_list.cpp`, `experimental/time_zone.cpp` - the
  experimental time-zone database, which needs an opt-in this build does not set.
- `charconv.cpp` - `'shared/fp_bits.h' file not found`. **This one is ours.** The header is
  `libc/shared/fp_bits.h` in llvm-project, shared between libc and libc++, and neither checkout
  carries it: FreeBSD does not vendor LLVM's libc into `contrib`, and oops-apps' sparse checkout
  did not list it. In oops-apps it is fixed by adding `libc` to `UPSTREAM_SPARSE`, which this
  change does. In oops-mesa's tree the header has no source, so `charconv` stays absent there
  until one is found.

So the honest statement is **not** "clang 21 fixes libc++". It is: clang 21 removes every failure
that was attributable to the compiler being older than the library, and what remains is two files
that must never build for this target, three behind an opt-in, and one missing header with a
name and a cause.

## Why the container is the authority and WSL is not

Both report `21.1.8` today. Only one of them promises to tomorrow.

The compiler in `oops-builder` is Ubuntu's `clang` package - installed version `1:21.1.6-71`,
which is also the apt *candidate*, so the next `apt upgrade` moves it with the distribution's
index and nothing in this collection would know. An image tag resolves to a digest and is the
same on any machine that pulls it.

There is a second reason, and it is the stronger one: `silkeh/clang:21`'s own version string is
`Debian clang version 21.1.8 (++20251221033036+2078da43e25a-...)`, and `2078da43e25a` is the
commit `oops-apps/src/oops-deps/libcxx/upstream.lock` pins libc++ to. **The authoritative
compiler and the standard library it compiles are the same LLVM revision**, which is not a
coincidence worth relying on silently.

WSL stays a permitted runner because it is much faster over a Windows mount and because obSCEne's
documented round-trip depends on it. The guard is what makes "permitted" safe: agreement is
checked at every build rather than asserted in a document.

## What this does not change

**`orbistoun` stays on LLVM 18** (`orbistoun#D681`, amended the same day). Its shader fixtures
are reproduced by a *reference* toolchain, where the version is part of the expected output
rather than a means to it - 18 and 19 already disagree about the first word of every compute
fixture. `tools/check-toolchain.sh` deliberately does not check it, and D681 names this entry
back so the exclusion cannot be read as drift.

**Mesa keeps `-fno-exceptions -fno-rtti`.** Mesa is written not to throw and ACO sets both for
itself; the cross file's reasoning is unchanged by a newer compiler. The exceptions work this
bump unblocks is a *title-side* C++ runtime in oops-apps, alongside the existing
`-fno-exceptions` one, not a change to how Mesa is built.

**D006 is amended, not reversed.** Its central choice - that oops-mesa provides the definitions
Mesa actually references rather than a whole standard library - still holds, and the tool that
keeps it honest still exists. What is now wrong in it is the framing: "the collection's compiler
is clang 18" and "moving the container to clang 21 is not this repository's call". The first is
no longer true and the second has been answered.

## What was verified before this was written

- Mesa 26.2.2 at the pinned commit rebuilt in `silkeh/clang:21`: 1277 targets, no duplicate
  symbols, 20 undefined (the platform's to answer), 46 archives in `build/link-order.txt`.
- The guard rejects what it is for: a wrong major (`make CC=gcc`), an absent compiler
  (`make CC=clang-18`), and a `toolchain.mk` no Makefile includes. `make clean` still works with
  no compiler at all, because a guard that stops you tidying up is one people route around.
- `tools/check-toolchain.sh` fails on the current tree, naming `oops-sdk`, which is the one
  repository whose pin could not be wired - see the note in the worklog entry. That failure is
  the gate working.

**The hardware acceptance in worklog 067's plan has been run, on 2026-09-22, and it passed** -
[the record](../hardware/the-clang-21-bump-verified-fw1240.md), and
[worklog 073](../worklog/073-the-bump-is-verified-and-the-oracle-had-a-gap.md). `dri-probe`
returns `0x5188ddb7` with the same 373248 modified pixels and the same centre and corner values;
`mesa-cube` holds 59.94 fps with no CPU pixel work, no presentation fallbacks and no faults. The
bump is verified for oops-mesa, and unit 8's toolchain prerequisite is met on hardware rather than
only in the container.

Two corrections to what this entry assumed while it was being written:

- **`0x9dbfe189` was never this repository's to answer.** Worklog 067 listed it beside
  `0x5188ddb7` as one of the collection's two oracles and it reads here as though both were
  oops-mesa's acceptance. It is gl1-cube's - oops-gl's own stack, with no Mesa in it - as
  [the GLSL record](../hardware/glsl-pipeline-and-frame-hash-fw1240.md) already says. Verifying it
  belongs to whoever owns that title.
- **`build/mesa/meson-logs/meson-log.txt` says clang 18 and is wrong.** Meson wrote it at
  configure time in an earlier session and did not rewrite it for the rebuild, so the file most
  likely to be reached for when checking "which compiler built this" answers with the old one.
  `readelf -p .comment` on an object gives `21.1.8 (++...2078da43e25a...)`, the same llvm-project
  revision oops-apps pins for libc++.

What the run also showed, and what this decision cannot take credit for avoiding: `mesa-cube`
draws black, and it is not the compiler. The cause is a texture-unit state leak in an overlay
added to the title after the bump, filed as `REQ-20260922T0040Z-c93d` against oops-sdk. It is
recorded here because `0x5188ddb7` held bit-for-bit through a change that visibly broke the other
title on the same panel - a hash is an oracle only for the path it covers, and this one does not
cover a texture.
