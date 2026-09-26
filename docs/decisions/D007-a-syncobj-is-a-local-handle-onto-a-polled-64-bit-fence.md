# D007 - A syncobj is a local handle onto a polled 64-bit fence, and it is binary

**Status:** decided
**Date:** 2026-09-17

A syncobj is a handle in the winsys's own table, holding a 64-bit fence value and the
address the GPU writes it to. Creating and destroying one touches no hardware. Waiting
polls. `GET_CAP` answers `DRM_CAP_SYNCOBJ_TIMELINE` with zero, so Mesa uses binary
syncobjs. Wait, signal and reset refuse by name until something submits against them.

**Why:** here the fence is memory the shim allocated, so the handle namespace is already
ours and there is nothing to ask the platform for. The platform has no blocking wait: the
event-queue and wait-rendering symbols are absent on firmware 12.40, and a submitted
command buffer writes a full 64-bit value when it retires. That 64-bit monotonic value is
also what a timeline needs, so enabling timelines later is a `GET_CAP` answer and the
timeline half, not a hardware question.

**Rejected:**
- Timeline syncobjs now: radeonsi works without them, and claiming a capability nothing
  exercises is a plausible success.
- A blocking wait: the platform provides nothing to block on.
