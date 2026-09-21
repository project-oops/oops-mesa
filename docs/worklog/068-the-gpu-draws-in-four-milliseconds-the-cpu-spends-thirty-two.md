# 068. The GPU draws in four milliseconds, and the CPU spends thirty-two delivering it

**2026-09-21** - roadmap unit 6, unit 7, D012 step 3

## What happened

The example title ran on hardware for the first time, and the present path was measured. The
record is [the-present-path-measured-fw1240.md](../hardware/the-present-path-measured-fw1240.md).

`mesa-cube` (`MCUB00001`) draws a textured, depth-tested cube through a matrix pipeline and
presents every frame. It held **36.7 ms a frame** - 27 fps - across more than a thousand frames
with zero faults, and the four-part present report finally separates the GPU's work from the
CPU's:

| part | cost | share |
|---|---|---|
| `flush` - **the GPU drawing** | 4195 us | 11% |
| `read` - `glReadPixels` | 20442 us | 56% |
| `mirror` - the row swap | 3761 us | 10% |
| `disp` - tile and flip | 8270 us | 23% |

**The GPU finishes in 4.2 ms and the CPU spends 32.5 ms carrying the result to the screen.**

## Why this needed the example title, and not another probe

`dri-probe` measures the same three CPU parts and reports `flush = 19 us`. That looked like the
GPU being free and it is not: the probe calls `glFinish` before presenting, so its GPU work has
already happened and is off the clock. Only a title that presents *without* finishing first - which
is what an actual title does - puts the render inside the measurement.

That is worth stating because the example title was built as an example, not as an instrument, and
it turned out to be the only thing here that could take this reading. A probe that stops after one
frame cannot see a steady state, and one that synchronises before presenting cannot see the render.

## What it settles

- **D012 step 3 has a number now.** It removes `read`, `mirror` and `disp`, leaving
  `flush` plus a 25 us flip submit: about **4.2 ms** against 36.7 ms. Unit 6's "the swap costs
  milliseconds, not 508" stops being aspiration.
- **D012's step 2 is measured out of contention.** Copying into the scanout buffer would remove
  `mirror` and `disp` - 12 ms - and keep the 20 ms `read`, because a copy still pulls the pixels
  through the CPU. The entry predicted this before the number existed, and the number agreed.
- **The roadmap's 508 ms was never measured.** The CPU present costs 37 ms, on two titles across
  four runs. That figure was inherited and nothing here had checked it.
- **Unit 7's example title is no longer only built.** It renders, presents continuously, and holds
  a steady frame time - and it exercises a texture, a sampler, a depth buffer, an element buffer
  and a matrix pipeline, none of which anything here had touched.

## The surprise: the instrument lied, quietly, and the fix is the interesting part

The same change added a query for the colour buffer's layout, because both of D012's routes depend
on it. The first version discarded `dri2_query_image`'s return value and initialised its locals to
zero. It printed:

```text
colour buffer: ... modifier 0x0000000000000000 (linear stride would be 7680)
```

which reads as "the buffer is linear" and is nothing of the sort. Mesa returns **false and leaves
the value untouched** when the modifier is `DRM_FORMAT_MOD_INVALID` (`dri2.c:1144-1153`), so a
failed query and a genuine `DRM_FORMAT_MOD_LINEAR` - which is zero - print identically. The
reading was thrown away rather than acted on, the shim now reports each query's own success, and
the re-run said `unavailable`: the surface has no modifier, because it is created with
`modifiers = NULL`.

So the layout is still unknown, and that is a better place to be than believing it is linear. It
is the same lesson `-9f41` taught about the export census, arriving from a different direction: a
zero that might mean "no answer" is not an answer, and an instrument that cannot tell them apart
is worse than no instrument, because its output looks like data.

## What is next

- **The layout, by a different route.** The modifier is a dead end - there is not one. The GEM
  handle (`__DRI_IMAGE_ATTRIB_HANDLE`) plus the winsys's own record of the allocation would settle
  it by size: `64KB_R_X` pads 1080 to 1152 and occupies 8847360 bytes against linear's 8294400.
- **D012 step 3 itself**, which is now the only thing between unit 6 and closed, and is worth
  32 ms a frame.
- **A citation this repository had wrong.** Until today, oops-mesa called that work `REQ-7e21` in
  the roadmap, two hardware records, D012 and worklogs 065 and 066. That identifier is
  `oops-sdk#REQ-20260919T1927Z-7e21`, filed by **oops-gl** about oops-gl's own scratch-framebuffer
  copies, and re-filed as `oops-sdk#REQ-20260920T0745Z-2d7f`. Both projects have the same problem
  and the same answer, which is presumably how it was borrowed. Corrected everywhere, and D012
  carries the reasoning - including that no request should exist for this at all, because obSCEne
  measures the platform and rendering into a scanout buffer is work in our own shim.
- Also fixed in passing and confirmed on hardware: `chunk kind 6` no longer prints ten times a
  frame, the present line is said once rather than per frame, and both obSCEne lines state their
  resolved status correctly. The frame hash `0x5188ddb7` held a fourth time.
