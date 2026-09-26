# D004 - The sysroot is the FreeBSD checkout orbistoun already reads

**Status:** decided
**Date:** 2026-09-14

Mesa compiles against the C-library headers of the FreeBSD source checkout orbistoun
harvests its constants from, found through `OOPS_MESA_FREEBSD_SRC` and pinned by
`SYSROOT_PIN` in `dependencies.mk`. The sysroot is a compile-time reference, not an ABI
contract: where a structure differs between the checkout and the platform, the runtime
shim owns the conversion in a named function, and `SYSROOT_STAT_LAYOUT` selects the
`freebsd11` layouts for `stat` and `dirent`.

**Why:** the platform does not name its generation (`kern.osrelease` reads
`0.0-prototype`), so no pin is correct by measurement. One checkout shared with orbistoun
makes the two projects unable to disagree about this platform (orbistoun#D374). A modern
checkout carries the old layouts explicitly (`struct freebsd11_stat`,
`struct freebsd11_dirent`), and it costs no download.

**Rejected:**
- An older FreeBSD release tarball: still a guess at the generation, and a second source
  that can drift from orbistoun's.
- Headers written here: a second, unreviewed description of the same library.
