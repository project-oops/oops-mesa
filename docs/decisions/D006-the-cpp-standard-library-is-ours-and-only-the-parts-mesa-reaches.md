# D006 - The C++ standard library is ours, and only the parts Mesa reaches


**decided** · 2026-09-14 (both alternatives were tried and measured before this was written)

Mesa is a C++ project where it matters: ACO, its shader backend, is all of it. So a C++ standard
library has to come from somewhere, and the two obvious somewheres are both closed.

## The choice

**`src/runtime/cxx_support.cpp` provides the definitions Mesa actually references, written as
ordinary C++ against the same headers Mesa compiles against.** Eight of them today. Nothing else
of libc++ is built or linked.

Writing them as C++ rather than as hand-mangled names is the point: the mangled names come out
right by construction, and a caller that wants a different overload gets a link error rather than
a wrong function.

## Why the platform's own C++ library cannot serve

The platform has one. obSCEne's census records **666 mangled C++ symbols** in
`libSceLibcInternal`. Not one of them is in libc++'s ABI namespace: the census contains zero
`_ZNSt3__1` symbols, and the names it does contain - `std::_Num_int_base`,
`std::filesystem::_Close_dir` - are Microsoft's STL internals.

So the platform's C++ library is a third implementation, with its own ABI. Code compiled against
libc++'s headers cannot link against it, and code compiled against its headers is not something
this project has or wants.

## Why libc++ is not built from the pinned checkout

It was tried, as libelf is built, and it does not compile. The checkout's libc++ is
`_LIBCPP_VERSION 210108`, which is LLVM 21. The collection's compiler is clang 18, three major
versions behind, and libc++ expects a compiler at least as new as itself: 43 of its 71 sources
fail, on constructs in its own type traits that clang 18 does not implement.

Moving the container to clang 21 would fix it and is not this repository's call: CLAUDE.md pins
clang 18 to match oops-sdk and oops-apps, so the whole collection moves together or not at all.

## What keeps this honest as Mesa changes

The risk of providing a subset is that the subset silently stops matching. It cannot here:
`tools/what-is-still-needed.sh` takes every symbol the built archives reference and subtracts
everything that answers, so a new C++ reference appears in its output as soon as it exists,
before any link is attempted.

**If that list grows past a screen, the answer is a newer compiler for the whole collection, not
a longer version of that file.** Written here rather than left to whoever is looking when it
happens.

## What this costs, stated

Anything in libc++ that Mesa does not currently reach is absent. A future Mesa that starts using
`std::filesystem` or a locale facet meets a link error, not a fallback. That is the right
direction for this project, and it is why the tool that names the set is part of the build rather
than something run when somebody remembers.
