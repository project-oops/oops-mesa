# D004 - The sysroot is the FreeBSD checkout orbistoun already reads, and the shim owns every crossing point


**decided** · 2026-09-14 (the owner asked for a choice with its reasoning rather than a question)

D002 said Mesa compiles against "a pinned set of FreeBSD C-library headers" and left which one
open. This settles it, and narrows what the pin is claiming.

## The choice

`SYSROOT_SOURCE` is the **FreeBSD source checkout orbistoun already harvests its constants
from**, found through `OOPS_MESA_FREEBSD_SRC` with a relative sibling path as the default, never
an absolute one. `SYSROOT_PIN` is that checkout's revision. Nothing is downloaded: the collection
already keeps this checkout, and pointing at it is what makes the pin mean something.

Where a structure changed shape between the target's generation and the checkout's, oops-mesa
takes the **pre-12 layout**, as a build setting rather than a compiled-in assumption, matching
orbistoun's default and for its reason (orbistoun#D374).

## Why this pin and not an older release

**The platform refuses to name its generation, and that is measured.** obSCEne read the kernel's
own knobs on firmware 12.40 (`135-sysctl`):

```text
kern.ostype     "FreeBSD"
kern.osrelease  "0.0-prototype"                              scrubbed
kern.version    "r226974/releases/12.40 Nov 27 2025 02:23:38"  the vendor's build tag, not a release
```

So no pin can be correct by measurement. Every candidate is an assumption, and the question
becomes which assumption is cheapest to hold and easiest to correct.

- **One source of truth beats a closer guess.** orbistoun reads this platform's constants and
  structures out of one checkout; a second project reading a different FreeBSD generation for the
  same console is exactly the drift orbistoun#D374 was written to stop, reintroduced one
  repository along. Pinning the same checkout makes a disagreement between the two projects
  impossible rather than merely unlikely.
- **A modern checkout carries the old layouts explicitly, which an old tarball does not.**
  `sys/sys/stat.h` defines `struct freebsd11_stat` beside `struct stat`, and `sys/sys/dirent.h`
  defines `struct freebsd11_dirent` beside `struct dirent`, both verified in the pinned revision.
  Both layouts are citable from one source, and which one the target uses stays a stated
  hypothesis instead of being buried in the choice of download.
- **It costs no download and no new dependency.** The checkout is already a prerequisite of the
  collection's constant harvest.

## What the pin is claiming, which is less than it looks

**The sysroot is a compile-time reference, not an ABI contract.** That distinction is what makes
a 16-CURRENT checkout safe against a much older user space, and it follows from D002's own shape:
the runtime shim sits between Mesa and the platform, so every function where a layout actually
crosses the boundary is one the shim writes.

Mesa sees the header's types. The shim's own body calls the vendor function and converts. That is
the same act as mapping `pthread_create` onto `scePthreadCreate`, which
`docs/hardware/mesa-thread-surface-fw1240.md` already lists in full, and it is why the thread
types are ours to define rather than FreeBSD's.

So the pin is claiming: *these are the declarations Mesa is compiled against*. It is not claiming
that the platform's library matches them. Where the two differ, the difference lives in a shim
function, named, with the conversion visible.

The constants that do not move - error numbers, signal numbers, socket constants - are safe to
take from any generation, which orbistoun established for its own harvest and which applies here
unchanged. The structures that move are the short list, and they are the shim's problem by
construction.

## What would overturn it

A measurement that names the target's generation, or a structure Mesa touches whose layout the
shim cannot own because Mesa passes it to the platform directly rather than through a shim
function. The second is the one to watch for during unit 3; it would not change the pin, but it
would add a row to the shim and a line here.

## A correction to a sibling's decision

orbistoun#D374 says "the checkout is FreeBSD 15". At the revision pinned here it is
`__FreeBSD_version 1600020`, which is 16-CURRENT. The reasoning in that decision is unaffected,
since its point is that the checkout is newer than the target and by how much does not matter,
but the generation named in its prose is stale and the drift it warns about applies to its own
text.
