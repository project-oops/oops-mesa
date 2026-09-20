# 055. The submit path is fixed to the proven recipe, and the fence still will not retire

**2026-09-19** - roadmap unit 6

## Why the submission did not retire: three winsys bugs, all now fixed

Worklog 054 left the first GPU submission not retiring. A queue self-test - submit the end-of-pipe
fence stream *alone*, in proven `oops_mem_alloc` memory, and see if it retires - isolated the
question from radeonsi's stream and found the winsys submit path itself was the wall. Three bugs,
each measured against oops-gl's proven `gl_hw_flush` (oops-sdk `gl_context.c`) and obSCEne's
`166-agc/graphics-submit` (`obscene src/probe/sections/agc.c`, sweep 20260911-011650, fence retired
`0xbeefcafe`):

1. **Wrong queue type.** `submit.c` created a **type 3** queue (compute) and submitted graphics
   work to it. type 0 is the 3D graphics queue (obSCEne `166-agc/driver-queue-types`). Fixed to
   type 0.
2. **The queue was never submitted to.** The original `sceAgcDriverSubmitDcb` takes no queue
   argument, so the created queue went unused; the work went to a default context. Switched to
   `sceAgcDriverSubmitCommandBuffer(queue, desc)` - the call oops-gl uses - with SubmitDcb as the
   fallback.
3. **No cache flush, and no context register.** `oops_mem_alloc` is write-back, so the CPU's
   stores to the command stream sat in cache and the GPU fetched stale bytes; and a fresh graphics
   queue has no colour target, so the `CACHE_FLUSH_AND_INV_TS` event had nothing valid to flush.
   Added the DCB clflush (as `gl_hw_flush` does) and the single `SET_CONTEXT_REG` /
   `CB_COLOR0_BASE` preamble obSCEne emits before the same RELEASE_MEM.

All three are correct against the proven recipe and are keepers. Host suite 111/0.

## And it still does not retire - but the diagnostic points off this code

With all three fixed, the instrumented self-test on hardware:

```
queue self-test: q=80058a8b8 scb=1 dcb=1 submit_rc=0 fence=0x11111111 -> no-retire
```

The queue is created, both submit entry points are bound, the submit is **accepted** (`rc=0`), and
the fence **stays armed** - the driver takes the stream and the GPU does not run it. The winsys now
matches obSCEne's recipe field for field, so this is not a difference in this code.

**It matches a wall obSCEne is hitting right now.** obSCEne `REQ-20260919T2048Z-a6c2` (a re-file of
`-b52e`), OPEN as of this writing: its GPU colour-draw control fences "**all failed to retire
(`fence-hit 0x0`) in both sweeps**", and it attributes that to the GPU already being hung. Those are
this morning's sweeps, after oops-sdk `042d236` (Sat 2026-09-19 10:27, +1087 lines in `gl_draw.c`,
the change the standing note "GL console render target suspect" flags). obSCEne's *retiring*
measurement is from 2026-09-11, before it. So the current console/oops-sdk state has GPU submissions
not retiring, and this winsys submission hits the same thing.

## State / next

The submit path is correct and done. The remaining non-retirement is very likely the same GPU-state
/ oops-sdk regression `-a6c2` is tracking, not a winsys bug - which means oops-mesa's first frame is
blocked behind that, not behind more winsys work. Two ways to confirm:

1. A **reboot** clears a hung GPU ring; re-running the self-test on a freshly-booted console would
   say whether the wall is a wedged GPU (retires clean) or the regression persists (still armed).
2. Watch `-a6c2`: when a control fence retires again on this firmware, re-run the probe unchanged.

The self-test stays in `submit.c` for now, because it is the cheapest read of whether the queue
executes; it comes out once a fence retires (instrument for the bug, delete it with the bug). The
`SET_CONTEXT_REG` preamble, the flush and the type-0 / SubmitCommandBuffer path all stay - they are
the proven recipe regardless of the current GPU state.
