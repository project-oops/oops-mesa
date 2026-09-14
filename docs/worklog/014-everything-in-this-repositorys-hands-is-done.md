# 014. Everything in this repository's hands is done

**2026-09-14** - roadmap unit 4, the last items that were not waiting on the console

## What changed

Three loose ends from worklog 013, and with them the count of things nobody provides went from
75 to 71. Everything remaining is a question for the console rather than work for this
repository.

**`__cpu_model` comes from the compiler's own runtime.** AddressLib asks
`__builtin_cpu_supports("avx2")` exactly once, to pick between two swizzle paths that compute the
same answers at different speeds. That builtin reads a structure the compiler owns and this
target has no copy of. Three options: answer false and lose the fast path, fake the structure and
guess at a layout that is not ours, or take the compiler's.

The third is right and it is safe across operating systems, which was checked rather than
assumed. The object is 11 KB, defines exactly `__cpu_model`, `__cpu_features2` and
`__cpu_indicator_init`, and has **no undefined symbols at all**. It is CPUID and nothing else, so
there is no Linux in it to carry into a FreeBSD target. The build checks that property every time
and refuses if it ever stops holding.

**`__dso_handle` is in `src/runtime/abi.c`.** It is the handle a shared object passes to the
exit-time destructor registrar. Nothing is unloaded here, so the value only has to be unique and
stable, and its own address is both, which is what a static link does everywhere else.

**The software rasteriser's entry points are classified, not counted.** They come from the
video-layer winsys, video codecs are disabled in this configuration, and a title does not link
that archive. They sit with the other gallium drivers as references the build graph carries and
nobody has to write.

## What is left, and it is all one question

Seventy-one names, and every one of them is for obSCEne:

- **68 ordinary C library names** - `atoi`, `close`, `mmap`, the maths family - already filed as
  `REQ-20260914T1743Z-2e08`.
- **3 of a different kind**, added to that request today: `_ThreadRuneLocale`, the locale object
  FreeBSD's ctype macros reach through, so something as ordinary as `isspace()` pulls it in;
  `__xuname`, behind the `uname` macro, which radeonsi calls when it reports what it is running
  on; and `pthread_sigmask`, the one thread name with no vendor twin recorded at all.

## Surprises

- **The answer to "provide it, fake it, or turn it off" was none of the three.** The compiler
  already had a correct, self-contained answer sitting in its own runtime library, and the only
  work was checking it was safe to carry. That check is the part worth keeping: an object with no
  undefined symbols cannot have an operating system inside it.
- **A performance hint was nearly answered with a lie.** Reporting no AVX2 would have been
  invisible and cost tiling speed forever. It was only worth the ten minutes because the
  alternative was a number nobody would ever have re-examined.

## Next

Nothing. Every remaining item in the build's own accounting is a question already on the obSCEne
bus, and the startup path is blocked on `GB_ADDR_CONFIG` regardless. The next move belongs to a
run on the console.
