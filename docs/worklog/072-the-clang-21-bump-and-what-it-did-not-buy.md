# 072. The clang 21 bump, and what it did not buy

**2026-09-21** - toolchain, D013

## What was done

The collection's compiler moved from clang 18 to clang 21, and the pin stopped being a sentence
in a document. `toolchain/Dockerfile` is `silkeh/clang:21`; `obscene` and `oops-apps` each gained
a `toolchain.mk` that refuses to compile against another major; `OOPS/tools/check-toolchain.sh`
fails when the repositories disagree. The reasoning is [D013](../decisions/D013-the-collection-compiles-with-clang-21-and-the-pin-is-enforced.md);
this entry is what happened while doing it.

Mesa 26.2.2 at the pinned commit rebuilt in the new image: 1277 targets, no duplicate symbols,
20 undefined for the platform to answer, 46 archives in `build/link-order.txt`.

## The finding: the pin was never 18, it was "whichever runner you used"

This was sequenced as a version bump and it is not one. Before anything changed,
`oops-sdk/Makefile`, `obscene/Makefile` and `oops-apps/common/app.mk` all said bare
`CC := clang`, and WSL `oops-builder` had **already** been carrying clang 21.1.8 for some time.
Only `toolchain/Dockerfile` said 18. So the same source was being compiled by two different
compilers depending on which runner a session happened to use, and had been for a while.

The evidence it had been going on unnoticed is in this collection's own files:
`oops-apps/src/oops-deps/libcxx/upstream.lock`, written earlier the same day by the oops-sdl
thread, pins libc++ at `llvmorg-21.1.8` and says **"21.1.8 because that is the clang in the
builder"**. A dependency had already been chosen against the unpinned compiler.

So "bump 18 to 21" would have changed the smaller half. The half that mattered was making a
build refuse to run against the wrong one.

## The surprise: D006's number is right and its conclusion is now half wrong

D006 says 43 of libc++'s 71 sources fail under clang 18, on type-trait constructs the compiler
does not implement. Re-measured on today's tree, both compilers, same flags, one source at a
time:

| compiler | compiled | failed | of |
|---|---|---|---|
| clang 18.1.8 | 26 | 39 | 65 |
| clang 21.1.8 | 59 | 6 | 65 |

The denominator moved (65, not 71) because the `freebsd-src` pin has moved since D006 was
written. The claim's shape holds.

**What is worth knowing is the six.** Not one of them is a type trait:

- `pstl/libdispatch.cpp` wants Apple's `dispatch/dispatch.h` and `support/ibm/xlocale_zos.cpp`
  wants z/OS's. Upstream never builds either off those platforms. They were being counted as
  failures in a total that implied a compiler problem.
- Three are the experimental time-zone database, behind an opt-in this build does not set.
- `charconv.cpp` wants `shared/fp_bits.h`, which is `libc/shared/fp_bits.h` in llvm-project -
  **a header libc++ reaches for across subtree boundaries**. Neither checkout had it: FreeBSD
  does not vendor LLVM's libc into `contrib`, and oops-apps' sparse checkout did not list it.
  It is in the pinned commit and was simply never checked out.

So the honest headline is not "clang 21 fixes libc++". It is that clang 21 removes every failure
attributable to the compiler being older than the library, and what is left is two files that
must never build here, three behind a flag, and one missing header with a name.

## The other surprise: a fetch that reported success and did nothing

Adding `libunwind` and `libc` to `UPSTREAM_SPARSE` and running the canonical
`make libcxx-upstream` **printed nothing, exited 0, and left the tree exactly as it was.**

`common/upstream-fetch.sh` stamps a completed fetch with the revision and the patch set's
checksum, and skips when both match. It did not stamp the sparse path list, so widening the list
is invisible to it. The caller then fails later on a missing header, with nothing connecting that
to the fetch that had just declined to run.

Fixed in the script rather than worked around, per AGENTS.md section 1: the stamp now carries a
sorted key of the sparse set, so changing the paths re-fetches and reordering them does not. It
is worth saying why this one is nastier than it looks - a no-op fetch does not merely fail to do
its job, it leaves every later step reasoning confidently about a tree that is not what the lock
says it is.

That cost about twenty minutes of believing the lock and disbelieving the directory listing.

## What this does not claim

**No hardware ran.** The acceptance worklog 067 set - frame hashes `0x9dbfe189` and `0x5188ddb7`
unchanged, `mesa-cube` still 16682 us/frame at 59.94 fps with 0 fallbacks and 0 faults - has not
been tested, because a console run happens when the owner asks for one. Until those come back,
this is a reproduced build and a measured C++ blocker, not a verified bump. A changed hash would
be a finding and nobody has looked.

**`oops-sdk` is not pinned.** `.claude/settings.json` denies read and write on
`oops-sdk/Makefile` and `oops-sdk.mk`, which are the two files the guard would go in. The gate
reports it rather than passing over it, which is the intended behaviour of a gate written against
a defect that is still there. Wiring it is one `include` line and is the owner's to unblock.

## What is next

- The hardware acceptance above, when a console run is wanted.
- `REQ-20260921T1830Z-b4d1` on the obSCEne bus: `dl_iterate_phdr` is measured absent in
  `libSceLibcInternal` but `libkernel` and `self` were never asked, and the answer decides
  whether a built libunwind can find its frame tables at all or whether a title must register
  its own. That is the difference between a stock libunwind and a different build of one, and
  guessing it is how a runtime that half-supports exceptions gets shipped.
- Unit 8 remains where worklog 067 left it. The compiler was one prerequisite and the unwinder
  is the other.
