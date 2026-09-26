# D002 - Mesa runs on the platform's own C library, reached by published names

**Status:** decided
**Date:** 2026-09-14

Mesa is a hosted program. The runtime shim gives it a C library by importing the names the
platform's C library exports under their standard names, mapping `pthread_*` onto the
vendor thread API, and implementing Mesa's C11 threads layer over that. A title that links
oops-mesa is hosted, not freestanding; the SDK fragment and the packaging label it so.

**Why:** oops-sdk already binds vendor exports by published name, and extending that to
`malloc` and `snprintf` is the same act with more names. The C library names are measured
(`obscene/data/hardware/ps5-imports.txt`, section `035-libc`). The thread names are not
served in the app sandbox, so they map to vendor twins, listed in
[the thread surface record](../hardware/mesa-thread-surface-fw1240.md), with arities taken
from each vendor declaration.

**Rejected:**
- Porting an MIT C library (musl) to this kernel: its system-call layer is Linux-only, so
  threads and futexes would be rebuilt on vendor primitives - a project in itself.
- Keeping oops-mesa titles freestanding: Mesa cannot run without a C library.
