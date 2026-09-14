# 012. Asking the build what it still needs, instead of guessing

**2026-09-14** - roadmap units 3 and 4

## What changed

`tools/what-is-still-needed.sh` asks the built archives what they reference, subtracts everything
that answers, and sorts the remainder into piles. It is the answer to a question that has been
open since worklog 005: what does Mesa want from a C runtime, beyond threads?

The first run said 432 symbols were missing. The number now is 95, and almost all of the
difference was this repository being wrong rather than anything being absent.

| | |
|---|---|
| referenced by the archives | 8982 |
| answered by the archives | 8655 |
| answered by the shims | 26 |
| answered by staged libraries | 14 |
| the platform exports, measured | 169 |
| other gallium drivers, not built on purpose | 23 |
| **nobody provides** | **95** |

## Three things the tool found that were my fault

- **The archive list was hand-written and stale.** It omitted libdrm's core archive, Mesa's
  dispatch table, its C11 threads layer and its hash library, so hundreds of symbols were
  reported missing that were merely never built. The list now comes from the link line of Mesa's
  own DRI module, which is the one target that pulls together everything a driver needs, so it
  cannot drift from what the build produces. Taking every archive instead was tried first and is
  wrong: it drags in Google Test.
- **The census was read too narrowly.** Only `OBS|sym` rows counted, so `malloc` and eighty
  others were reported missing when the census plainly exercises them in `OBS|try` rows. A probe
  that called a function is stronger evidence than a name in a symbol table, and both count now.
- **Staged libraries were not counted.** libelf is built from the pinned checkout and a title
  links it, so its fifteen symbols were never missing.

## What is actually left, and who can answer it

- **70 C library names**, all ordinary published ones - `atoi`, `close`, `mmap`, `free`, the
  maths family. The expectation is that nearly all are present and were never mined.
- **8 thread names** the mine skipped. The census has `scePthreadRwlockattr*` and
  `scePthreadBarrierattr*` but not the operations those attributes configure, which is the same
  hole it showed for `clock_gettime`: attribute functions with nothing to attribute to.
- **8 C++ runtime symbols** and 3 compiler internals, which are the C++ standard library's own
  runtime rather than anything this platform is expected to export. That is its own decision and
  is not in the request.

`REQ-20260914T1743Z-2e08` carries the first two groups with the accounting above, so the reader
can see that this census already answers 169 of the questions a real Mesa asks and these are the
rest.

## Also this entry

The thread shim grew the four families Mesa uses outside its C11 layer: read-write locks,
barriers, condition-variable attributes, and two odds. Every twin is declared weakly, so one the
platform does not export refuses at its first call with a name attached rather than failing the
whole link over a function Mesa may never reach.

## Surprises

- **A work list is worth nothing until the thing it measures is complete.** The first 432 was
  confidently produced, and two thirds of it was noise from a build that was not building
  everything. The number only became useful once the archive list stopped being something a
  person maintained.
- **Other drivers' entry points are in the missing list and are not missing.** Mesa's static
  pipe loader carries a table naming every gallium driver it knows, so twenty-three references to
  drivers this configuration deliberately does not build show up as unresolved. They are
  separated rather than counted, because a work list with them in it hides the real ones.

## Next

Nothing in the startup path moves until `GB_ADDR_CONFIG` comes back. The C++ runtime question is
the largest thing still fully in this repository's hands: eight symbols, and a decision about
whether the platform's own C++ library answers them or whether one is built here.
