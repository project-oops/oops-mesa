# 070. The CPU stops touching the pixels

**2026-09-21** - roadmap unit 6, D009, D012 step 3

## What happened

The display now scans the buffer radeonsi renders into. A present is a flush and a flip; nothing is
read back, nothing is mirrored, nothing is tiled.

```text
colour buffer 8847360 bytes, plain 64KB_R_X is 8847360: no compression, offering it for scanout
direct scanout: VA 0x400600000 (8847360 bytes) has flip index 2
present us: flush=22 read=0 mirror=0 disp=3653 total=3675
presentation succeeded: the frame is on the display
```

**37229 us to 3675 us.** Photographed off the panel: the GLSL triangle, solid, smoothly
interpolated, upright. Roadmap unit 6's gate - "the swap costs milliseconds, not the 508 ms the CPU
path costs today" - is met by an order of magnitude more than it asked for, and the 508 ms figure
itself was never real (worklog 068 measured 37 ms).

## The two things that had to be true

**The buffer must be named before the display opens.** `-9a4c` measured VideoOut registration as
single-shot and immutable: a second `sceVideoOutRegisterBuffers2` returns
`SCE_VIDEO_OUT_ERROR_SLOT_OCCUPIED` however it is shaped, `sceVideoOutUnregisterBuffer(s)` are not
exported at all, and a concurrent handle is refused. So `oops_gl_create` builds the colour image
first and hands it to `oops_display_open_adopting`. D009 wrote that requirement down a week before
it could be measured.

**The surface must carry no compression.** This is the part that went wrong, and it is the entry's
real content.

## The 49152 bytes, found three times and used on the third

The first direct scanout put a **sparse lattice** on the panel - triangle correctly placed, sized
and oriented, with only scattered pixels inside it carrying colour. Read as a swizzle mismatch:
blocks agreeing, block interiors disagreeing. `REQ-20260921T1349Z-8b52` went to obSCEne asking
which swizzle the display scans at 1920x1080, since `-2d7f` had only measured 128x128 - one block,
where an `_X` mode's pipe rotation cannot show.

**The swizzle was never wrong.** Mesa's `CB_COLOR0_ATTRIB3 = 0x0dc6c000` (`SW_MODE 27`) is
bit-for-bit identical to the display tiler across a full 1080p frame: 2073600 pixels, **zero
mismatches**, all 135 blocks.

The 49152 bytes are `surf->u.gfx9.color.display_dcc_size` - Mesa's displayable **DCC** metadata.
oops-sdk registers buffers with `dcc_control = 0`, so the display scanned compressed colour as raw
pixels, which is exactly what a lattice looks like.

That number had been measured, recorded and set aside **twice before it was used**. It is in the
size-probe result (`8896512 bytes ... 49152 more than a bare 64KB_R_X`), where the surface was
deliberately labelled "tiled, and at least `64KB_R_X`" rather than `64KB_R_X` *because the
arithmetic did not close*. It is in D012, as "metadata or a coarser alignment". It is in `-8b52`'s
own text, offered to obSCEne as supporting evidence for the swizzle theory. Each time it was
written down honestly and then stepped over.

**The hedge was the finding.** When a measurement is qualified because something does not add up,
that qualification is what should gate the next step - not the part that was understood. The
checking had been done; it just was not allowed to stop anything.

## The fix, and why it is not a lever

`dri_create_image` now passes `__DRI_IMAGE_USE_SCANOUT | __DRI_IMAGE_USE_FRONT_RENDERING` instead
of zero. radeonsi disables DCC for `PIPE_BIND_USE_FRONT_RENDERING` when the modifier is
`DRM_FORMAT_MOD_INVALID` (`si_texture.c:236-242`), which is this image exactly - it is created with
no modifiers, which is also why `dri2_query_image` reports the modifier `unavailable`.

It is the honest flag rather than one that happens to work: compression is disabled for front
rendering **because a surface cannot be scanned out while it is being drawn into**, and that is
precisely the arrangement. Mesa's allocation drops to 8847360 bytes, the plain figure.

No Mesa patch was needed, which keeps principle 1 intact - the whole change is two flags in a call
the shim already makes.

## What stops it happening again

The shim no longer takes the flag's word for it. A buffer is offered to the display **only if its
allocated size equals the computed plain `64KB_R_X` figure exactly** - height padded to a multiple
of 128, `1920 * 1152 * 4 = 8847360`. Anything larger carries metadata the display was not told
about, and present falls back to copying. The log says which it chose and why.

That check is what was missing the first time, and it is deliberately exact rather than a
tolerance: the failure it guards against is a *fast wrong frame*, which is worse than the slow
right one it replaces, and principle 4 has a name for that.

## What is next

- **Double buffering.** One image is registered, so the display scans what radeonsi is drawing the
  next frame into - correct pixels with a tear line. Two images alternated is the fix, and it is
  now worth building because the path underneath it works.
- **`oops_display_try_gpu_tiler` can stop being a question.** D012 recorded that the CPU tiler was
  chosen on untested reasoning and cost 8270 us. It is no longer on the path at all, so that
  question closes without needing an answer.
- **oops-gl wants this.** `oops-sdk#REQ-20260919T1927Z-7e21` asks for the same thing against its
  own linear scratch copy, and `oops_display_open_adopting` was written for both callers. The DCC
  finding applies to it too.
