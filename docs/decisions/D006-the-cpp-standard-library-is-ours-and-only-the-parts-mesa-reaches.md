# D006 - The C++ standard library is ours, and only the parts Mesa reaches

**Status:** decided
**Date:** 2026-09-26

`src/runtime/cxx_support.cpp` defines the C++ standard-library symbols Mesa references,
written as ordinary C++ against the headers Mesa compiles against. Nothing else of libc++
is built or linked into Mesa. `tools/what-is-still-needed.sh` lists every symbol the built
archives reference that nothing answers, so a new C++ reference shows up before any link.

**Why:** Mesa reaches a handful of definitions, and a standard library nobody links is
weight with an ABI surface. Writing them as C++ makes the mangled names right by
construction. If the list outgrows a screen, the answer is building libc++ with the pinned
compiler (D013), not a longer file.

**Rejected:**
- The platform's own C++ library: its 666 mangled symbols in `libSceLibcInternal` are a
  different STL with no `_ZNSt3__1` names, so libc++-compiled code cannot link to it.
- All of libc++ from the pinned checkout: `charconv.cpp` has no `libc/shared/fp_bits.h` in
  that tree, and the rest is unused weight.
