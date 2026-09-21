# The present path, measured: the GPU draws in 4 ms and the CPU spends 32 ms delivering it

**2026-09-21** · firmware 12.40 · the same retail unit as the earlier records · roadmap unit 6, D012 step 3

Two titles, one question. `dri-probe` (`DRIP00001`) was re-run with the present path instrumented,
and `mesa-cube` (`MCUB00001`) ran on hardware for the first time - the example title, a textured,
depth-tested cube animated through a matrix pipeline, presenting every frame.

Both were deployed and followed with `pros probe`, Prosperous's one-command probe loop. Its first
version launched before attaching to the log and lost every line a parking title emits; the
ordering was fixed the same afternoon and it now says `attaching to <id>'s log before launch` and
captures cleanly. Worth recording because the same race is the reason the manual recipe always
subscribed first.

## What the present costs

`oops_gl_present` reports four disjoint parts that sum to the total. From `mesa-cube`, averaged
over the 18 reports in one 40-second capture:

| part | cost | share | what it is |
|---|---|---|---|
| `flush` | 4195 us | 11% | `dri_flush_drawable` - **the GPU drawing the frame** |
| `read` | 20442 us | 56% | `glReadPixels` - the CPU pulling it out of GPU memory |
| `mirror` | 3761 us | 10% | the CPU row swap, bottom-up to top-down |
| `disp` | 8270 us | 23% | `oops_display_present` - the CPU tile, plus a ~25 us flip submit |
| **total** | **36668 us** | | |

**The GPU renders the cube in 4.2 ms. The CPU then spends 32.5 ms carrying it to the screen.**
89% of the frame is the present path, and all of it is CPU work on pixels the GPU had already
finished.

`dri-probe` reports the same three CPU parts (`read=22526 mirror=5119 disp=9561 total=37223` on
its 2026-09-21 run) but its `flush` reads **19 us**, not 4195. That is not a different GPU: the
probe calls `glFinish` before presenting, so its GPU work is already complete and off the clock.
Only a title that presents without finishing first - which is what a real title does - separates
the two. That is why the example title was worth building.

## The roadmap's 508 ms was never measured

Unit 6's gate reads "the swap costs milliseconds, not the 508 ms the CPU path costs today". The
present costs **37 ms**, reproduced on two titles and four runs. The 508 ms figure is inherited
and nothing in this repository had ever checked it against this code.

## What D012 step 3 is worth, in numbers

D012's step 3 - render into a buffer the display scans out, so the CPU never touches the pixels -
removes `read`, `mirror` and `disp`. What is left is `flush` plus the flip submit: about
**4.2 ms**, against 36.7 ms today. Roughly 235 frames a second of headroom, which vsync caps at
60, and unit 6's "milliseconds, not 508" becomes arithmetic rather than aspiration.

D012's step 2 - copy into the scanout buffer rather than registering - is measured out of
contention by the same table. It would remove `mirror` and `disp` (12 ms) and keep `read` (20 ms),
because the copy still reads GPU memory with the CPU.

## Stability

`mesa-cube` ran continuously for the whole capture with **zero faults and zero refusals**, flips
600 through 1620 - over a thousand frames in the window, and it was already past frame 600 when
the capture attached. Frame time held at 36729-36808 us across four 300-frame windows, a spread
under 0.2%.

```text
[MCUB00001:MESA-CUBE] frame  600: 36769 us a frame over the last 300
[MCUB00001:MESA-CUBE] frame  900: 36808 us a frame over the last 300
[MCUB00001:MESA-CUBE] frame 1200: 36729 us a frame over the last 300
[MCUB00001:MESA-CUBE] frame 1500: 36769 us a frame over the last 300
```

Photographed off the panel: a spinning cube, yellow and blue checkerboard, with the shading band
darkest around its equator - which is `0.55 + 0.45 * abs(normalize(v_dir).y)` doing what it says.
So the texture, the sampler, the depth test, the index buffer and the matrix pipeline all work,
and none of that had been exercised here before.

## What changed in the shim, and what the run confirms

- **`chunk kind 6` is gone.** `AMDGPU_CHUNK_ID_BO_HANDLES` is now a named case in `submit.c`
  instead of falling to `default:`, and the ~10 lines a frame it used to print are absent.
- **The present line is said once.** It was unconditional, which cost one line for a title that
  presents once and one line *per frame* for a title that does not.
- **Both obSCEne lines read correctly.** `7d41` no longer advertises itself as the request that
  would settle the assumed device fields - it is resolved, and its answer is that nothing on this
  platform will tell a userland title otherwise. `5d1c` no longer says the pool is assumed; it is
  measured, one pool, from that request's own log rows.
- **The frame hash held a fourth time.** `0x5188ddb7`, `mod-pixels 373248`, `centre 0xff404080`,
  `corner 0xff0d0d14`.

## What it does not establish

**The colour buffer's layout, and the attempt is worth recording because it failed usefully.**
The shim now queries the drawable image and reports:

```text
colour buffer 1920x1080: stride 7680 (ok, linear would be 7680), modifier 0x0000000000000000 (unavailable)
```

Neither half answers the question. `stride 7680` is exactly `1920 * 4`, and radeonsi reports a
tiled surface's padded pitch - 1920 is already a multiple of 128, so linear and `64KB_R_X` give
the same number. The modifier is **unavailable**: `dri2_query_image` returns false when the
modifier is `DRM_FORMAT_MOD_INVALID` (`dri2.c:1144-1153`), which is what a surface created with
`modifiers = NULL` has. So the surface has no modifier to report, rather than having a linear one.

The first version of this instrument did not check those return values and initialised its locals
to zero, so it printed `modifier 0x0` for a failed query - indistinguishable from a genuine
`DRM_FORMAT_MOD_LINEAR`, which is also zero. That reading was discarded rather than believed. The
lesson is `-9f41`'s: a zero that might mean "no answer" is not an answer.

The layout still matters, because it decides what step 3 costs - registration alone if radeonsi is
already drawing `64KB_R_X`, or making it draw tiled first if it is not.

**Settled later the same day, by size rather than by modifier.** The shim now asks Mesa for the
image's GEM handle (`__DRI_IMAGE_ATTRIB_HANDLE`) and the winsys for what it allocated against it -
neither number derived from the width and height here, which would have answered the question with
the assumption it was meant to test:

```text
colour buffer gem 5: 8896512 bytes (linear 8294400, 64KB_R_X 8847360) -> tiled
```

**Not linear**, by 602112 bytes. So step 3 is registration and not a rendering change, which is
the cheaper of the two branches.

It is also 49152 bytes - three 16 KiB pages - larger than a bare `64KB_R_X` surface, and the total
is exactly 543 pages where 8847360 is already 540 whole ones. So radeonsi asked for more than the
surface, and this record says **tiled, and at least `64KB_R_X`** rather than naming the swizzle
outright. What the display scans has to match what radeonsi writes, so that extra is worth
understanding before `sceVideoOutRegisterBuffers2` is called rather than after.

The answer was predicted from the other direction and agrees:
`oops-sdk#REQ-20260920T0745Z-2d7f` measured the RDNA2 colour block dropping pixels on a 1920x1080
target with a linear swizzle, and this probe's `mod-pixels` matches the analytic triangle area
exactly on four runs, which a dropping target could not do.
