# D012 - The present path round-trips through linear, and the fix is staged behind a measurement

**decided** · 2026-09-21 (proposed, measured and shipped the same day; the title keeps the shape
the decision had when it was made - the fix *was* staged behind a measurement, the measurement came
back, and step 3 is on the frame path)

Unit 6's gate asks for a present costing milliseconds. This entry is not that optimisation. It is the
finding that came out of looking for it, which is that the present path as written does a
substantial amount of work for no effect at all, and a statement of what to do about it in what
order.

## What the present path actually does

`oops_gl_present` (`src/platform/dri_loader.c`) currently:

1. `dri_flush_drawable` - finishes the frame on the GPU.
2. `glReadPixels(GL_BGRA, GL_UNSIGNED_BYTE)` - **detiles** radeonsi's colour target into a linear
   image. Worklog 064 picked it for exactly that property: it is the one path that detiles.
3. A vertical row mirror, in place, because `glReadPixels` reads bottom-up and scanout is top-down.
4. `oops_display_present` -> `agc_display_present` -> `agc_display_flip_from`, which calls
   `agc_tile_surface(target_gpu_fb, src, ...)` - it **tiles** that linear image into the scanout
   buffer.

Step 4's destination is the scanout buffer, and `agc_display.c` sets `disp->tiling_mode = 0`, which
`agc_display_scanout_layout` reports as layout `2` - `OOPS_DISPLAY_SCANOUT_RX`, documented in
`oops-sdk/include/oops/display.h` as "the GPU's 64KB_R_X render-target swizzle at 32 bits a pixel".

Step 2's source is a radeonsi render target, which is the same swizzle. That equivalence is not an
assumption: worklog 031 established it byte-for-byte across blocks with a control, and
`tools/tiling-compare` keeps it true - it runs in `./bin/oops-mesa check` and passes today.

**So the path detiles 64KB_R_X into linear, and then tiles linear back into 64KB_R_X.** Two
full-frame swizzle passes and a full-frame mirror, between two buffers in the same layout.

## Why this was not visible

Each half is individually correct and each was written at a different time for a different reason.
`glReadPixels` was chosen because it detiles, which was the problem in front of worklog 064.
`oops_display_present` was the display's existing entry point and it tiles, because every caller it
had until now handed it a linear image. Neither knew about the other, and the frame that came out
was correct - pixel-exact, then hash-exact - so nothing pointed at it. It cost only time, and until
today nothing measured the time.

D009 came close enough to be worth quoting: it wrote the blit down as the *fallback*, noting that
"because the layouts match it is a `memcpy` rather than a retile". What it did not say - because
the present path did not exist yet - is that the path eventually built would do the retile anyway,
in both directions.

## The decision

**Stage the fix, and let the measurement choose the order.** Three steps, of which only the first
is already done.

**Step 1 - measure. Done, and the answer is in.** On 2026-09-21 `DRIP00001` reported:

```text
present us: flush=19 read=23296 mirror=3260 disp=10654 total=37229
```

| part | cost | share |
|---|---|---|
| `flush` | 19 us | ~0% - the GPU had already finished; the probe calls `glFinish` before presenting |
| `read` | 23296 us | **63%** |
| `mirror` | 3260 us | 9% |
| `disp` | 10654 us | 29% |
| total | 37229 us | 37 ms a frame |

**The readback dominates, so step 2 is not worth building and step 3 is the whole prize.** That is
this entry's own rule applied to its own measurement, and it is worth being explicit that the rule
was written before the number was known.

It also corrects the roadmap's inherited figure: the CPU present costs **37 ms**, not 508 ms.
Nothing here had ever measured it.

The recommendation is **robust to the one thing still unknown** (the buffer's layout, below).
Either way the CPU has to read a frame out of GPU memory, and that read is the 23 ms:

- if the colour buffer is linear, `glReadPixels` is a plain copy and step 2 would pay the same
  read and then still owe the tile, because scanout is `64KB_R_X` and a linear source is not;
- if it is `64KB_R_X`, step 2 saves both swizzles but still pays the uncached read.

Step 3 is the only option that never touches the pixels with the CPU at all, and it is the right
destination in both worlds.

**Its implementation is not robust to the answer, though, and an earlier draft of this entry said
it was.** `agc_display.c` sets `tiling_mode = 0`, commented "tiled mode required by
libSceVideoOut on this port", so the display scans `64KB_R_X`. If radeonsi's colour buffer is
already that, step 3 is registration and nothing else. If it is linear, registering it is not
enough - radeonsi has to be made to render into a tiled surface first, which is a larger job and a
different one. So the layout decides **how much step 3 costs**, not whether to do it.

**Step 2 - the copy, if the measurement supports it.** oops-sdk already exposes what this needs,
added for "a renderer that draws the scanout buffers itself": `oops_display_scanout` hands back the
buffer the next flip will show, `oops_display_wait_scanout` paces against the 26-deep flip queue,
`oops_display_flip_scanout` flips "the next buffer as it was drawn - nothing tiled or copied into
it", and `oops_display_use_scanout` declares the intent once. Copying radeonsi's target into that
buffer and flipping it removes both swizzle passes and needs no registration change.

**Step 3 - register radeonsi's buffer and copy nothing. Built, measured, and working on hardware
as of 2026-09-21.**

> ```text
> colour buffer 8847360 bytes, plain 64KB_R_X is 8847360: no compression, offering it for scanout
> direct scanout: VA 0x400600000 (8847360 bytes) has flip index 2
> present us: flush=22 read=0 mirror=0 disp=3653 total=3675
> ```
>
> **37229 us to 3675 us, a tenfold cut, and the CPU no longer touches a single pixel.** The frame
> was photographed off the panel: the GLSL triangle, solid and smoothly interpolated, upright.
> Unit 6's gate - "the swap costs milliseconds, not the 508 ms the CPU path costs today" - is met
> with an order of magnitude to spare.
>
> **Two buffers and flip pacing followed the same day** (worklog 071). `mesa-cube` holds
> **16682 us a frame - 59.94 fps, vsync-locked** - against 36769 us before, with no tear line and
> no fallback. The present costs 5083 us of a 16.7 ms budget, so 70% of the frame is idle: the
> title went from CPU-bound at 27 fps to display-bound at 60 with room to spare.
>
> Two corrections to the numbers above, both from `mesa-cube` being able to measure what
> `dri-probe` cannot. The steady-state flip costs **5 to 11 us**, not the 3653 us quoted here - a
> title that presents once and parks only ever measures the *first* flip. And a present of 4.76 ms
> is 210 frames a second into a 26-deep queue draining at 60, which overran it in under a second
> until `oops_display_wait_scanout` was added after the flip.
>
> Two things had to be true and both were measured rather than assumed:
>
> - **The buffer has to be named before the display opens.** VideoOut registration is single-shot
>   and immutable (`-9a4c`), so `oops_gl_create` now builds the colour image first and hands it to
>   `oops_display_open_adopting`. D009 called this a week earlier: "Registration moves from 'at
>   display open, before any GL exists' to 'once the surface radeonsi will draw into exists'."
> - **The surface must carry no DCC.** See below - this is the part that went wrong first.

> **Attempted on hardware 2026-09-21. The buffer is acceptable; the slot is not.**
>
> `oops_gl_present` now tries once to hand the display radeonsi's colour buffer and flip it, with
> the readback path kept behind it as a fallback. The registration was refused:
>
> ```text
> direct scanout: register VA 0x400600000 (8896512 bytes) at index 2 -> rc 0x80290001
> direct scanout: same call, display's own VA 0x4000000000 -> rc 0x80290001 (so the INDEX was refused, not the address)
> ```
>
> `0x80290001` is `SCE_VIDEO_OUT_ERROR_INVALID_VALUE` and names no argument, so the call was
> re-issued with one input changed - an address VideoOut had already accepted. Identical refusal.
> **So D009's open question is answered: the address is not constrained, and nothing has to move.**
> What does not exist is slot 2. oops-sdk's display registers two buffers at open, so 0 and 1 are
> the only indices there are.
>
> **The remaining work is a re-registration, and it belongs to the display rather than to this
> shim.** Adding a third buffer means calling `sceVideoOutRegisterBuffers2` with the whole set,
> and this shim cannot assemble that set safely: `oops_display_scanout` reports the display's two
> buffers *relative to its own current flip index*, not in registration order, so re-registering
> them from out here could swap 0 and 1 and leave the display drawing into the buffer it is
> showing. The display knows the order; nothing else does.
>
> That points at an oops-sdk display call - adopt a foreign buffer, re-register the set, return its
> index - which is **also what `oops-sdk#REQ-20260919T1927Z-7e21` wants for oops-gl**, for the same
> reason and against the same linear-scratch copy. One call serves both projects.
>
> **That call now exists.** `oops_display_adopt_buffer` and `oops_display_flip_index`
> (`oops-sdk/include/oops/display.h`, implemented in `src/agc/agc_display.c`): the display
> re-registers its own two buffers plus the foreign one as a set of three at index 0, in
> registration order, from the one place that knows that order. oops-sdk's own suite stayed at
> 315/315. This shim no longer touches `sceVideoOutRegisterBuffers2` at all.
>
> **And the platform refuses that too, with a different code.** On hardware the three-buffer
> registration returns **`0x80290010`** - not the `0x80290001` the bad slot gave, so the slot
> complaint is gone and something else objects. That code appears nowhere in obSCEne's records:
> its display decisions carry `0x...01`, `03`, `08`, `09`, `0b` and `15`, but not `10`. Nor is it
> alignment, which D253 settled at 64 KiB - this buffer sits at `0x400600000`, which is 64 KiB
> aligned already.
>
> So the remaining question is whether a registered buffer set can be **extended at all** on this
> port, and it is platform data rather than anything this repository can reason out. Filed as
> `REQ-20260921T1202Z-9a4c`, with the arms that would decide it (re-register unchanged; register
> three; register after an unregister; register before the first flip). `-2d7f` carries a note
> pointing oops-gl at the same findings, since `-7e21` will meet this identical wall.
>
> **What this cost and what it bought.** The readback path still runs, untouched, behind a
> fallback that has now been exercised three times on hardware - every refusal ended with
> `presentation succeeded` and the frame hash unchanged at `0x5188ddb7`. Against that, D009's
> address question is answered, the slot rule is known, the API both projects need exists, and the
> one remaining unknown is named, filed, and shared rather than sitting in one project's head.

This is D009's stated
direction, and D009 named the one thing it waits on - "whether `sceVideoOutRegisterBuffers2`
constrains the address of a buffer it is given" - and said the question "only becomes answerable
when there is a surface to register" and that "the first attempt to register a radeonsi buffer *is*
the measurement". **That surface now exists.** `sceVideoOutRegisterBuffers2` takes
`buffers[].data` as plain pointers; `agc_display.c` passes its own mapped addresses at
`AGC_VM_BASE` = `0x40_0000_0000`, and `device_info.c` reports radeonsi a VA range of
`0x2_0000_0000`-`0x400_0000_0000`, which contains it. So if the address turns out to be
constrained, the shim can place the surface there through a range it already controls.

### `REQ-7e21` is not ours, and this entry used to say it was

Until 2026-09-21 this repository called step 3 "`REQ-7e21`" - in the roadmap, in two hardware
records, in worklogs 065, 066 and 068, and twice above. That identifier belongs to
**`oops-sdk#REQ-20260919T1927Z-7e21`**, filed by oops-gl, and it says that *oops-gl* should render
directly into `64KB_R_X` scanout buffers instead of copying through a linear scratch framebuffer.
It was re-filed as `oops-sdk#REQ-20260920T0745Z-2d7f`, whose header reads `from: oops-sdk
(oops-gl)`.

The borrowing is understandable - the two projects have the same problem and the same answer,
because both stacks currently render somewhere linear and then tile on the way to scanout. It is
still wrong, in two ways worth separating:

- **It attributes our work to another project's request**, so a reader chasing the citation lands
  in oops-gl's backlog and finds nothing about Mesa.
- **It implies this is a measurement obSCEne could deliver, and it is not.** obSCEne measures the
  platform. "Render into a buffer the display scans out" is implementation work in this
  repository's own shim; there is nothing for the bus to measure and no request should exist.

So step 3 is **D012's**, named as such, and oops-gl's request is cited as the sibling reference it
is. The convention for that is the repository prefix (`../CONVENTIONS.md`), which is what the two
identifiers above now carry.

## Why the order is measure-first rather than build-first

Because step 2 may be worth nothing, and the reason is specific rather than cautious.

A radeonsi colour target is GPU memory, and a CPU read of it may be far slower than the swizzle
arithmetic it would replace. This is not hypothetical on this hardware: `gl1-cube` carries the
note that "the target itself is uncached for the CPU and a full read of it drops the demo to two
frames a second", which is why that title reads the CP's cached copy instead. Meanwhile
`glReadPixels` is not a naive CPU read - Mesa is free to blit to a staging buffer and read that -
so the existing step 2 may already be close to the best way to get these pixels to the CPU.

If that is how it falls, then the detile is not the waste, the *retile* is, and the copy of step 2
saves only the second pass and the mirror. If instead the read dominates, step 2 saves little and
step 3 is the whole prize, because it is the only option that never touches the pixels with the
CPU at all.

One line of measured output separates those two worlds. Guessing between them and building the
wrong one is the avoidable mistake here.

## What is not established

- ~~That this drawable's buffer is 64KB_R_X.~~ **Answered on 2026-09-21: it is tiled, not
  linear.** The shim asks Mesa for the image's GEM handle and the winsys for what it allocated
  against it:

  ```text
  colour buffer gem 5: 8896512 bytes (linear 8294400, 64KB_R_X 8847360) -> tiled
  ```

  **8896512 bytes, 602112 more than a linear frame**, so `dri_create_image` with `use = 0` and no
  modifier gets a tiled surface from addrlib, as expected. Two things follow, and only the first
  is certain:

  - **Step 3 is registration, not a rendering change.** The expensive branch - "make radeonsi draw
    tiled first" - is not needed.
  - **It is bigger than a bare `64KB_R_X` surface by 49152 bytes**, which is exactly three 16 KiB
    pages, and the total is exactly 543 of them. The winsys rounds to a page, but 8847360 is
    already 540 whole pages, so radeonsi asked for more than the surface itself - metadata or a
    coarser alignment. So this is recorded as **tiled, and at least `64KB_R_X`**, not as "is
    `64KB_R_X`". Step 3 registers a buffer with `sceVideoOutRegisterBuffers2`, and what the
    display scans has to match what radeonsi writes, so the extra 48 KiB is worth understanding
    before that call rather than after it.

  This also confirms a prediction from the other direction. `oops-sdk#REQ-20260920T0745Z-2d7f`
  measured the RDNA2 colour block **dropping pixels** on a 1920x1080 target with a linear swizzle,
  and this probe's `mod-pixels` has matched the analytic triangle area exactly on four runs - which
  it could not do if the target were linear. Two unrelated measurements, one conclusion.

### The 49152 bytes were the whole answer, and this entry walked past them twice

The first direct scanout put a **sparse lattice** on the panel: the triangle in the right place, at
the right size, the right way up, and inside it only scattered pixels carrying colour. The obvious
reading was a swizzle mismatch - block layout agreeing, block interior disagreeing - and this entry
said so, and `REQ-20260921T1349Z-8b52` was filed asking which swizzle the display scans.

**The swizzle was never wrong.** `-8b52` measured Mesa's `CB_COLOR0_ATTRIB3 = 0x0dc6c000`
(`SW_MODE 27`, `64KB_R_X`) against the display tiler across a full 1080p frame: **bit-for-bit
identical over all 135 blocks, 2073600 pixels matched, zero mismatches.**

What was wrong was compression. The 49152 bytes are
`surf->u.gfx9.color.display_dcc_size` - Mesa's **displayable DCC metadata** - and oops-sdk
registers buffers with `dcc_control = 0`, so the display scanned compressed colour as raw pixels.
A lattice is what that looks like.

**That number had been found, written down, and reasoned past - twice.** It is in this entry above
("49152 bytes, which is exactly three 16 KiB pages ... radeonsi asked for more than the bare
surface, likely metadata or extra alignment") and in `-8b52`'s own text, both times as an
unexplained excess recorded carefully and then set aside while a hypothesis was built on the parts
that *were* understood. The surface was labelled "tiled, and at least `64KB_R_X`" rather than
`64KB_R_X` **precisely because the size did not add up**, and that caution was then not carried
into the decision to offer the buffer.

The lesson is narrower than "check things", because the checking was done. It is: **an unexplained
quantity is a finding, not a footnote.** When a measurement is deliberately hedged, the hedge is
the part that should gate the next step.

The fix is Mesa's own flag rather than a patch or a lever. `dri_create_image` is now called with
`__DRI_IMAGE_USE_SCANOUT | __DRI_IMAGE_USE_FRONT_RENDERING`; radeonsi disables DCC for
`PIPE_BIND_USE_FRONT_RENDERING` when the modifier is `DRM_FORMAT_MOD_INVALID`
(`si_texture.c:236-242`), which is this image exactly. It is the honest flag and not a coincidence:
compression is disabled for front rendering **because a surface cannot be scanned out while it is
being drawn into**, which is the arrangement here.

And the shim no longer takes anyone's word for it. The buffer is offered only if its allocated size
equals the computed plain `64KB_R_X` figure exactly - `1920 * 1152 * 4 = 8847360`. Anything larger
carries metadata the display was not told about, and present copies instead. The check is the thing
that was missing the first time.

  **Instrumented on 2026-09-21, asked once, and the first answer was unreadable through a fault in
  the instrument.** `oops_get_buffers` queries the image once it exists -
  `__DRI_IMAGE_ATTRIB_STRIDE` and the two halves of the DRM format modifier. The run reported:

  ```text
  colour buffer: 1920x1080 stride 7680, modifier 0x0000000000000000 (linear stride would be 7680)
  ```

  `stride 7680` is exactly `1920 * 4`, and it is **not** discriminating: radeonsi reports a tiled
  surface's padded row pitch, and 1920 is already a multiple of 128, so linear and `64KB_R_X` give
  the same number here.

  The modifier is worse than not discriminating - it is ambiguous. `dri2_query_image` returns
  **false and leaves `*value` untouched** when the modifier is `DRM_FORMAT_MOD_INVALID`
  (`mesa/src/gallium/frontends/dri/dri2.c:1144-1153` at the pin). The first version of this code
  discarded that return value and initialised the locals to zero, so a failed query and a genuine
  `DRM_FORMAT_MOD_LINEAR` - which *is* zero - print identically. The shim now reports each query's
  own success beside its value, so the next run distinguishes them. The lesson is the same one
  `-9f41` taught about the export census: a zero that might be "no answer" is not an answer.
- ~~The row order.~~ **Answered by the working path, not by a measurement aimed at it.** The
  mirror was a property of `glReadPixels` being bottom-up, and the direct path does not call it -
  radeonsi's target is scanned as it stands and the frame is upright on the panel. The question
  only ever existed for the copy.
- ~~The cost of any of it.~~ **Measured** (worklog 068, 070, 071): the copy path cost 36668 us, of
  which 32473 us was CPU; the direct path costs 5083 us, of which 4428 us is the GPU drawing. The
  roadmap's inherited "508 ms" was never real and is corrected there.

## One decision this measurement put back in play, and then closed again

> **Moot as of the same day.** The CPU tiler is no longer on the frame path at all - step 3 landed,
> so there is no tile to move to the GPU. The question below is kept because it is still live for
> the *fallback* path, which runs when a buffer fails the no-compression gate, and because "a
> decision made on reasoning that measurement could now test" is worth leaving visible rather than
> deleting once it stops being urgent.

`dri_loader.c` does **not** call `oops_display_try_gpu_tiler`, and says why: "CPU tiling is left in
place ... so the display converts frames on the CPU and never contends for the GPU queue radeonsi
drives; that is what lets the two run in one title."

That was decided on reasoning, before any of this was measured, and the reasoning has never been
tested. What is measured now is its price: `disp` is **8270 us**, about 22% of the frame, and it is
the CPU tiling that comment is protecting. oops-sdk already has the compute-shader tiler built and
validated against its own CPU tiler.

So there is a real question with a real number on it - does moving the tile to the GPU cost more in
contention than the 8.3 ms it saves? - and it is answerable in one run, on a feature that already
exists. It is **not** worth answering before `-9a4c`: if a buffer set turns out to be extendable,
step 3 deletes the tile entirely and the question is moot. If `-9a4c` says a set is fixed, this
becomes the largest single saving still available in the path that has to stay.

Recorded rather than acted on, because "a decision made on reasoning that measurement could now
test" is exactly the kind of thing that otherwise stays unexamined for being already written down.

## What would reverse this

A measurement showing the detile dominates and the retile is cheap: then step 2 is not worth
building at all and the work goes straight to step 3. That is a reversal of the plan, not of the
finding - the round trip is still a round trip either way.
