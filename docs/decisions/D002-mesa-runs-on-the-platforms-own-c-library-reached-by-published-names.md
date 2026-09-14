# D002 - Mesa runs on the platform's own C library, reached by published names


**decided** · 2026-09-14 (assumed at 12:50, the owner said yes at 13:10 the same day)

Made without the owner's word on the mechanism; the shape ("a translation layer, minimum
code") is theirs. It is a stated divergence from the collection's freestanding rule and needs
that word before the first line of the runtime shim is written.

## The choice

Mesa is a hosted program. It needs an allocator, threads, mutexes and condition variables,
string and memory routines, formatted output, a little file access for its shader cache, and a
maths library. oops-mesa provides that surface as a **runtime shim** with three parts:

1. **Names the platform's C library already exports bind directly.** The title imports
   `malloc`, `qsort`, `snprintf`, `sinf`, `fopen` and their kin as weak undefined symbols by
   their standard names, exactly as oops-sdk imports the vendor graphics and thread APIs today,
   and the loader binds them from the platform's C library at start.
2. **Standard thread names map onto the vendor thread API.** oops-sdk already binds
   `scePthreadCreate`, join, detach, yield and the mutex family by published name. The shim
   provides `pthread_*` entry points over them, a mapping table with bodies, unless the standard
   names are found to bind directly.
3. **Mesa's own C11-threads layer is implemented over that**, so Mesa's code sees the interface
   it already abstracts.

The compile-time interface is a pinned set of FreeBSD C-library headers, because the kernel is
FreeBSD-derived and the struct layouts and error numbers must match the library that actually
answers at run time. They are fetched into an ignored directory and verified against
`dependencies.mk`; they are never tracked.

## Why

- **It is what the collection already does, one level up.** oops-sdk is freestanding in the
  sense that it links no C library, not in the sense that it makes no library calls: threads,
  the graphics driver, the display and the pad are all vendor exports bound by published name.
  Extending that to `malloc` and `snprintf` is the same act with more names.
- **The names are measured.** obSCEne's census of 2026-08-30 on firmware 12.40 bound
  `malloc`, `fopen`, `qsort`, `sinf` and `snprintf` from the platform's C library
  (`obscene/data/hardware/ps5-imports.txt`, section `035-libc`). The `pthread_*` names are
  mined candidates and not yet bound, which is why part 2 exists and why a census request is
  the first step.
- **The thread names are a harder case than the rest, and the reason is known.** The portable
  names exist and retail titles import them, but from the `libScePosix` **library**
  (orbistoun#D349 built its declaration from four titles' own import tables), and obSCEne
  measured that `libScePosix` does not load in the app sandbox: `OBS|module|libScePosix|0x0`,
  every import beside it unresolvable, while the `libkernel` imports on neighbouring lines
  resolve. obSCEne's own `017-posix` skips its checks on that leg for this reason. So part 2 is
  the likely outcome rather than the fallback, and what is still open is narrow: whether
  `libkernel` serves the portable thread names under either spelling, plain or `posix_`-prefixed.
  That is `REQ-20260914T1443Z-3ea7`, and it is an optimisation rather than a gate: **the mapping
  table is already known to be buildable.** Mesa touches `pthread_*` from one file, its C11
  threads layer, so the surface is closed at 26 functions, and every one has a vendor twin
  (`docs/hardware/mesa-thread-surface-fw1240.md`): 16 bound and working in oops-sdk today, 7
  named `present` in obSCEne's libkernel census, 3 in orbistoun's libkernel symbol inventory.
  Arities come from the vendor declaration per name, never from a rule: create, mutex init and
  cond init each take a trailing name argument the POSIX form has no place for, a trap orbistoun
  hit and recorded (its D385 and D475) and one oops-sdk's own declarations already get right.
- **The alternative is weeks.** An MIT C library ported to this kernel (musl's system-call
  layer is Linux-only; threads and futexes would have to be rebuilt on the vendor primitives)
  is the clean-room ideal and a project in itself. It stays the fallback if the exported names
  turn out insufficient, not the default.
- **It is a translation layer**, which is the owner's stated intent: a table of names on one
  side, the platform's own functions on the other, nothing invented in between.

## What this diverges from, and how it is confined

AGENTS.md section 3 says "freestanding C runtime only". A title linking oops-mesa is hosted.
The divergence is confined to titles that link this project, labelled in the SDK fragment and
the packaging, and never implied to be the same contract as the rest of oops-sdk (CLAUDE.md,
principle 5). If the owner does not accept it, the fallback is the MIT C library port and the
roadmap's unit 4 becomes that project.
