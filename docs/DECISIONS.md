# Decisions

Numbered, with reasoning, as they are made. The reasoning is the point - it is what stops a
choice being re-litigated by somebody who only has the choice.

**This log starts on the first day.** The repository was opened with three founding choices
written down before any source existed: what it is (D001), what hosts Mesa (D002), and which
driver route is tried first and what measurement gates it (D003). Two of them were made without
the owner's word on the mechanism and carried `assumed` until they had it; both are settled now,
and D003's measurement has since been run and passed (worklog 002). D004 pins what Mesa compiles
against. The status column stays the review queue.

**This table is generated.** Edit an entry under `decisions/`, then run
`tools/split-decisions.sh --index oops-mesa`. A number resolves to exactly one file.

| | # | decision | status | date |
|---|---|---|---|---|
| 🟢 | D001 | [A shim under upstream Mesa, not a driver of our own](decisions/D001-a-shim-under-upstream-mesa-not-a-driver-of-our-own.md) | decided | 2026-09-14 |
| 🟢 | D002 | [Mesa runs on the platform's own C library, reached by published names](decisions/D002-mesa-runs-on-the-platforms-own-c-library-reached-by-published-names.md) | decided | 2026-09-14 |
| 🟢 | D003 | [Mesa's AMD driver first, a driver of our own second, gated by one measurement](decisions/D003-radeonsi-first-own-driver-second-gated-by-one-measurement.md) | decided | 2026-09-14 |
| 🟢 | D004 | [The sysroot is the FreeBSD checkout orbistoun already reads, and the shim owns every crossing point](decisions/D004-the-sysroot-is-the-freebsd-checkout-orbistoun-already-reads.md) | decided | 2026-09-14 |
| 🟢 | D005 | [The winsys shims the kernel interface, and libdrm is carried rather than replaced](decisions/D005-the-winsys-shims-the-kernel-interface-not-libdrms-api.md) | decided | 2026-09-14 |
| 🟢 | D006 | [The C++ standard library is ours, and only the parts Mesa reaches](decisions/D006-the-cpp-standard-library-is-ours-and-only-the-parts-mesa-reaches.md) | decided | 2026-09-14 |
| 🟢 | D007 | [A syncobj is a local handle onto a polled 64-bit fence, and it starts binary](decisions/D007-a-syncobj-is-a-local-handle-onto-a-polled-64-bit-fence.md) | decided | 2026-09-17 |
| 🟢 | D008 | [The part reports FUSION, because this winsys already says so everywhere else](decisions/D008-the-part-reports-fusion-because-this-winsys-already-says-so-everywhere-else.md) | decided | 2026-09-17 |

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
