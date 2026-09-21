# 071. Two buffers, and the pacing that had to come with them

**2026-09-21** - roadmap unit 6, D012 step 3

## What happened

The drawable now has two colour buffers, both named to VideoOut at display open, alternated every
present. `mesa-cube` runs at **59.94 frames a second, vsync-locked**, with no tear line on the
panel and no fallback to the readback path.

```text
frame  300: 16872 us a frame over the last 300
frame  600: 16683 us
frame  900: 16684 us
frame 1200: 16682 us
frame 1500: 16682 us
```

| | before step 3 | one buffer | two, paced |
|---|---|---|---|
| frame time | 36769 us | - | **16682 us** |
| frame rate | 27.2 fps | - | **59.94 fps** |
| present | 36668 us | 4762 us | 5083 us |
| `read` + `mirror` | 24203 us | 0 | **0** |
| GPU (`flush`) | 4195 us | 4751 us | 4428 us |

The title does 5.08 ms of work and waits 11.6 ms for vsync - **70% of the frame budget idle**. It
went from CPU-bound at 27 fps to display-bound at 60 with room to spare.

## The flip is free, and the number that said otherwise could not have known

Steady-state `disp` on the single-buffer run was **5 to 11 us**. The flip submit costs microseconds,
as `agc_display.c` has always said it does.

That matters because worklog 070 quoted `disp` as 3653 us, and the first double-buffered run quoted
11598 us, and this entry nearly recorded "double buffering made the present three times worse".
Both numbers are **first-flip overhead**. `dri-probe` presents exactly once and parks, so every
`present us` line it has ever produced is a measurement of the first flip and nothing else. It was
never capable of showing the steady state, and two conclusions were drawn from it anyway.

A probe that stops after one frame cannot measure a per-frame cost. That is obvious written down
and was not obvious in use, because the number looked like all the others.

## The overrun, which is the real content

The first double-buffered `mesa-cube` run was excellent and then stopped being excellent:

```text
flush=4751 read=0 mirror=0 disp=11 total=4762
flush=4751 read=0 mirror=0 disp=6  total=4757
...
direct scanout: flip refused rc 0xffffffff - falling back to readback
frame 300: 35335 us a frame over the last 300
```

A present costing 4.76 ms is about **210 frames a second submitted into a flip queue obSCEne
measured at 26 deep** (`080-video/visual-flip`), against a display that consumes 60 a second. It
fills in well under a second. `sceVideoOutSubmitFlip` then refuses, and this shim fell back to
reading the frame out for the rest of the run - correctly, but permanently.

**Going faster than the display is not a performance win, it is a bug that looks like one.** The
fix is `oops_display_wait_scanout`, which oops-sdk already had for exactly this: "the buffer that
was on screen before the last one is free to draw into". It goes *after* the flip, because the flip
is what frees the other buffer - waiting first would wait for something not yet asked for. It is
bounded at about 100 ms and reports a timeout rather than hanging, so a stalled display costs frame
rate instead of the title.

## The mistake that repeated inside one day

The refusal logged `rc 0xffffffff`. That is `-1`, this shim's own sentinel: `oops_display_flip_index`
answers -1 for every failure and puts the platform's real code in the display's last error.

The identical mistake had already been made and already been fixed this morning, in
`try_direct_scanout`, where a bare `index -1` said nothing until `oops_display_get_last_error` was
read beside it. The two call sites were written hours apart and the lesson did not travel between
them - which is worth recording precisely because the fix was known, available, and not applied.

Both paths now report the display's error rather than their own return value.

## What makes the buffers alternate

`oops_get_buffers` hands back whichever image `gl->draw` names, and present advances it. The
frontend only asks again when the drawable's stamp moves, so present calls
`dri_invalidate_drawable`, which bumps the stamp and clears `texture_mask` (`dri2.c:97-103`). That
is the same mechanism a DRI3 loader uses on a present-complete event.

Both buffers must pass the no-compression size gate or neither is offered: a pair where one is
scannable and the other is not would alternate between a flip and a copy, which is harder to reason
about than either alone and would show as a stutter rather than a fault.

## What is next

- **Unit 6 is closed and stays closed.** This is quality inside it, not a new gate.
- **oops-gl gets this for free.** `oops-sdk#REQ-20260919T1927Z-7e21` wants direct scanout against
  its own linear scratch copy; the pacing requirement and the 26-deep queue apply to it identically,
  and `oops_display_wait_scanout` is already the call.
- **`oops_display_try_gpu_tiler` is now moot for this path.** D012 raised it as the largest saving
  left in the readback route; that route is no longer on the frame path at all.
