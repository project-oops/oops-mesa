# Decisions

The decisions in force, one file each under `decisions/`. Format and rules are in
[STYLE](https://github.com/project-oops/OOPS/blob/main/docs/STYLE.md#decisions).

**This table is generated.** Edit an entry under `decisions/`, then run
`tools/split-decisions.sh --index oops-mesa`. A number resolves to exactly one file.

| | # | decision | status | date |
|---|---|---|---|---|
| 🟢 | D001 | [A shim under upstream Mesa's radeonsi, not a driver of our own](decisions/D001-a-shim-under-upstream-mesa-not-a-driver-of-our-own.md) | decided | 2026-09-26 |
| 🟢 | D002 | [Mesa runs on the platform's own C library, reached by published names](decisions/D002-mesa-runs-on-the-platforms-own-c-library-reached-by-published-names.md) | decided | 2026-09-14 |
| 🟢 | D004 | [The sysroot is the FreeBSD checkout orbistoun already reads](decisions/D004-the-sysroot-is-the-freebsd-checkout-orbistoun-already-reads.md) | decided | 2026-09-14 |
| 🟢 | D005 | [The winsys shims the kernel interface, and libdrm is carried](decisions/D005-the-winsys-shims-the-kernel-interface-not-libdrms-api.md) | decided | 2026-09-14 |
| 🟢 | D006 | [The C++ standard library is ours, and only the parts Mesa reaches](decisions/D006-the-cpp-standard-library-is-ours-and-only-the-parts-mesa-reaches.md) | decided | 2026-09-26 |
| 🟢 | D007 | [A syncobj is a local handle onto a polled 64-bit fence, and it is binary](decisions/D007-a-syncobj-is-a-local-handle-onto-a-polled-64-bit-fence.md) | decided | 2026-09-17 |
| 🟢 | D009 | [The display scans out Mesa's colour buffer](decisions/D009-the-display-adopts-mesas-buffer-not-the-other-way-round.md) | decided | 2026-09-26 |
| 🟢 | D010 | [The platform shim is a DRI loader, not EGL](decisions/D010-the-platform-shim-is-a-dri-loader-and-egl-waits.md) | decided | 2026-09-17 |
| 🟢 | D011 | [The maths is FreeBSD's own msun, double and float only](decisions/D011-the-maths-is-freebsds-own-msun-double-and-float-only.md) | decided | 2026-09-17 |
| 🟢 | D013 | [The collection compiles with clang 21, and the pin is enforced](decisions/D013-the-collection-compiles-with-clang-21-and-the-pin-is-enforced.md) | decided | 2026-09-21 |
| 🟢 | D014 | [The GL version radeonsi reports is its claim, and the project claims what it measured](decisions/D014-the-scope-is-gl-3-3-and-4-6-is-radeonsis-claim-not-ours.md) | decided | 2026-09-26 |

| | meaning |
|---|---|
| 🟢 | settled, and the reasoning rests on something checkable |
| 🟡 | assumed or proposed - made without input, and in the review queue |
| 🔴 | reversed, superseded or blocked |
| ⚪ | no status recorded |

A date with `~` is **not recorded** - it is worked out from the dated entries either
side, because an entry between two of them was written between their dates. `~` alone
is a day both neighbours agree on; `~a..b` is a span, and no day inside it is claimed;
`~>a` and `~<a` are entries with a dated neighbour on only one side. A bare `-` has no
dated entry either side to reason from.
