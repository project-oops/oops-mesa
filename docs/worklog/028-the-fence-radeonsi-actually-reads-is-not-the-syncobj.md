# 028. The fence radeonsi actually reads is not the syncobj

**2026-09-17** - roadmap unit 5

## What this entry is

D007 deferred `SYNCOBJ_WAIT`, `SIGNAL` and `RESET` on the grounds that "nothing submits work yet,
so nothing can signal". Tracing the submission path to see what those three would need turned up
something that changes the shape of the problem: **on this part, radeonsi's fence wait does not
reach the syncobj at all when things are working.** It reads a value out of a buffer.

That value was never being written. This writes it.

## What a submit actually carries

`amdgpu_cs_submit_ib` builds up to eight chunks. With `has_timeline_syncobj` false - which is what
D007 decided and `GET_CAP` carries - the set is:

| Chunk | Contents |
|---|---|
| `BO_HANDLES` | the buffer list |
| `SYNCOBJ_IN` | dependencies, flat array of handles (the timeline variant is skipped) |
| `SYNCOBJ_OUT` | handles to signal - **always at least one**, the CS's own fence |
| `FENCE` | a buffer handle and a byte offset |
| `IB` | one to three of them: preamble, gang, main |

The `FENCE` chunk is the interesting one, and it is not optional here:

```c
static bool amdgpu_cs_has_user_fence(struct amdgpu_cs *acs)
{
   return acs->ip_type == AMD_IP_GFX || acs->ip_type == AMD_IP_COMPUTE ||
          acs->ip_type == AMD_IP_SDMA;
}
```

GFX is the only engine this winsys answers for, so every submit carries one.

## Why it matters more than the syncobj

`amdgpu_fence_wait` reads the user fence **first**:

```c
user_fence_cpu = afence->user_fence_cpu_address;
if (user_fence_cpu) {
   if (*user_fence_cpu >= afence->seq_no) {
      afence->signalled = true;
      return true;                     /* returns without touching the syncobj */
   }
   ...
}
...
if (ac_drm_cs_syncobj_wait(afence->aws->dev, &afence->syncobj, 1, abs_timeout, 0, NULL))
   return false;
```

So `SYNCOBJ_WAIT` is the *fallback*, reached only when the user fence has not landed. Both of the
values it compares are this shim's:

- `afence->seq_no` is what `amdgpu_fence_submitted` was handed, which is what
  `ac_drm_cs_submit_raw2` returned, which is `arg->out.handle` from `oops_winsys_cs` - a counter
  this file already keeps.
- `*user_fence_cpu` points into a buffer radeonsi allocated at context creation and mapped for the
  CPU, at the offset the `FENCE` chunk names.

`submit.c` was skipping that chunk, under a comment saying there was nothing to do while
submission is synchronous. For the two syncobj chunks that reasoning holds - a dependency named in
a synchronous world has already retired, because the submit that produced it did not return until
it had. For the `FENCE` chunk it does not: the memory is read later, by a caller that has no idea
the submit was synchronous, and an unwritten slot reads as zero against a sequence number that
starts at one.

So **every fence wait would have failed**, by falling through to a `SYNCOBJ_WAIT` that refuses -
and it would have looked like the syncobj work D007 deferred was the blocker, when the actual
blocker was four lines of chunk handling.

## What was written

`oops_winsys_cs` now remembers the `FENCE` chunk's buffer handle and offset during the chunk walk,
and after the fence stream has fired it writes the sequence number there.

The ordering is the correctness argument and is worth stating plainly: the number is published
**after** the wait, never before. radeonsi reads `*user_fence >= seq_no` as "this submission is
done", so publishing it early would make that true while the GPU was still working - the same
silent lie the badge taught this collection to refuse (orbistoun#539, CLAUDE.md principle 4).

`buffers.c` gained `oops_winsys_bo_cpu_range(handle, offset, bytes)`, which maps the buffer if it
is not mapped and **checks the range**. The offset arrives from the caller; a chunk naming an
offset past the end of the buffer it also names would otherwise corrupt whatever follows instead
of saying so.

A refused write is logged and is not fatal. The submission genuinely did retire, and `WAIT_CS`
answers from the same sequence number, so the work is reported correctly either way - what is lost
is the fast path, and radeonsi falls back to `SYNCOBJ_WAIT`, which refuses under its own name.
Worse than a working fast path, better than a fence that reads as signalled without being written.

## What this does to D007

It does not overturn it - a syncobj is still a local handle, and wait/signal/reset are still
unwritten. What it changes is the *urgency*: those three are no longer on the road to a first
frame. They are the fallback for a path that should not be taken, plus the interop surface
(`fence_import_sync_file`, `fence_export_sync_file`) that worklog 027 established is unreachable
without cross-process sharing.

That is a better position than D007 assumed, and it was found by reading rather than by a run.

## Eight checks, and one they caught

The new checks cover `oops_winsys_bo_cpu_range`: a valid range, the last eight bytes, one byte
past the end, an offset beyond the buffer, an unknown handle, and a closed buffer.

The first version of them failed, and the failure was mine rather than the code's: they were
written against the 4096 bytes requested, and the winsys rounds every buffer up to a 16 KiB page,
so the "past the end" cases were comfortably inside the allocation. Fixed by asking for exactly one
page, with the reason written beside it - a boundary test has to be against the real end of the
allocation, not the end of what was asked for.

Host suite 104 of 104, up from 96.

## State

Nothing on the submit path has run on hardware. Everything above is read from Mesa's source and
from this repository's own counter, and the whole of it is testable only as far as the host suite
reaches - which is the buffer bookkeeping, not the submission. `-5c9d` is still outstanding.
