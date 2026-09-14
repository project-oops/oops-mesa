# 007. Contexts and buffer lists do the work that is actually available to them

**2026-09-14** - roadmap unit 5, the two commands radeonsi asks for before its first draw

## What changed

`DRM_IOCTL_AMDGPU_CTX` and `DRM_IOCTL_AMDGPU_BO_LIST` are implemented in `src/winsys/context.c`.
Eight of the winsys's 22 commands now do real work; the host suite runs 31 checks.

Neither of these has a direct equivalent here, and the temptation with both was to hand back a
handle and forget about it. That is the shape of bug this collection keeps finding: a call that
succeeds having done nothing, and a failure much later with no line pointing back. So each does
what is genuinely available to it.

## A context cannot carry a priority, but it can carry a history

On Linux a context is a scheduling entity with a priority and a reset record. This platform
exposes one queue, so the priority is recorded and honoured nowhere, and saying so in a comment
is the whole of that.

The reset record is different, because the shim already knows something real: submission waits
for its own fence, so it knows when a stream failed to retire. A failed submission is now counted
against the context that issued it, and the reset query answers from that count. radeonsi uses
this to decide whether to throw a context away and start again, so a wrong answer is expensive in
both directions.

What it reports when nothing has gone wrong is "no reset", and that is the truthful reading of
*this shim saw no failed submission* rather than a claim the GPU is healthy. If the driver ever
gains a way to be told about a reset it did not cause, that belongs in the same place.

`SET_STABLE_PSTATE` is refused rather than accepted and ignored. Pinning clocks is the system's
business on this platform, and a caller that asked for it and got success would reasonably
believe the clocks were pinned.

## A buffer list has nothing to make resident, so it checks instead

Everything allocated here is resident the moment it exists, so the list's stated purpose does not
apply. What survives is that the list is a statement about which handles are live, and checking
it turns a stale handle into a refusal with a number in it, here, instead of a GPU fault in a
later frame with nothing pointing back at the list that named it.

That check found a bug in the buffer table within minutes of existing.

## Surprises

- **Zero is a valid physical offset, and the buffer table was reading it as "free".** Liveness
  was a sentinel - "the physical offset is not negative" - and oops-sdk's own header says in as
  many words that zero is a real offset and is never used to mean unknown. So every handle in an
  empty table looked live, and a buffer list naming a stale handle was cheerfully accepted.
  Liveness is its own flag now. The host suite caught this on the first run of the buffer-list
  test, which is the second time in two entries that the test for a thing found a bug in the
  thing underneath it.
- **The useful work in a command with no equivalent is usually a check.** Both of these looked
  like pure bookkeeping before they were written. Each ended up carrying one thing worth having:
  a reset history that radeonsi can act on, and a handle check that fails early.

## Next

Fourteen commands still refuse, and most of them should keep refusing until something asks:
sharing, metadata, user queues. The ones likely to be asked for first are `GEM_WAIT_IDLE` and
`GEM_OP`, and after that the remaining `AMDGPU_INFO` queries, which are the same measurement
`REQ-20260914T1558Z-7d41` is waiting on.
