# 013. The C++ runtime question, closed

**2026-09-14** - roadmap unit 4, and D006

## What changed

The eight C++ symbols worklog 012 left open are answered, and the count of things nobody
provides went from 95 to 75. The C++ runtime is gone from that list entirely.

`src/runtime/cxx_support.cpp` provides them as ordinary C++ written against the same headers Mesa
compiles against, so the mangled names come out right by construction rather than being spelled
by hand. Eight definitions: `std::mutex`'s lock, unlock and destructor over the threads the shim
already provides; the hash table's prime sequence; the byte hash; libc++'s own abort; and two
exception constructors.

`toolchain/stage-libelf.sh` became `stage-sources.sh` and now stages both upstream libraries,
because the two scripts differed only in which subtree they took.

## Both alternatives were tried, and both are closed

**The platform's own C++ library.** It has one: obSCEne's census records 666 mangled C++ symbols
in it. Not one is in libc++'s ABI namespace - zero `_ZNSt3__1` symbols in the whole census - and
the names it does carry, `std::_Num_int_base` and `std::filesystem::_Close_dir`, are Microsoft's
STL internals. It is a third implementation with its own ABI, so nothing compiled against libc++'s
headers can link against it.

**Building libc++ from the pinned checkout**, the way libelf is built. The staged libc++ is
`_LIBCPP_VERSION 210108`, which is LLVM 21; the container's compiler is clang 18. Forty-three of
its seventy-one sources fail, on constructs in libc++'s own type traits that clang 18 does not
implement. libc++ expects a compiler at least as new as itself.

D006 records both, and records that if the subset ever grows past a screen the answer is a newer
compiler for the whole collection rather than a longer support file. That decision belongs to the
collection, not here: clang 18 is pinned to match oops-sdk and oops-apps.

## Surprises

- **Mesa compiles against libc++ 21's headers with clang 18 perfectly well.** The library's own
  sources do not. A consumer touches a small enough part of the headers that the version gap
  never shows; the library touches all of it. That is why the failure appeared only when
  something tried to build libc++ itself, four steps after the headers were staged.
- **A half-built library in the sysroot is worse than none.** The failed attempt left an archive
  with 28 of 71 objects in it, and the accounting tool duly counted its symbols as provided,
  reporting 34 staged where the truth was 14. It was deleted and the number went back. A
  partial artefact that looks complete is exactly the shape of thing this collection refuses
  elsewhere, and it arrived here by accident rather than by decision.

## What is left, in full

Seventy-five, and now they sort cleanly:

- **68 ordinary C library names**, already on the obSCEne bus as `REQ-20260914T1743Z-2e08`.
- **4 platform and compiler internals**: `_ThreadRuneLocale`, `__cpu_model`, `__dso_handle`,
  `__xuname`. These come from the C runtime's startup objects rather than from a library, and
  they belong with whatever oops-sdk does about a hosted title's entry point.
- **2 software-rasteriser entry points** that a configuration without the software winsys should
  not be asking for, and one stray `pthread_sigmask`.

## Next

Nothing in the startup path moves until `GB_ADDR_CONFIG` comes back. What remains in this
repository's hands is small and mostly waiting on the census answer.
