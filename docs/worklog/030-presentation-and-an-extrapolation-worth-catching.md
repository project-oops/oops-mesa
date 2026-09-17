# 030. Presentation, and an extrapolation worth catching

**2026-09-17** - roadmap unit 6

## What this entry is

The third shim - presentation - does not exist yet, and `src/` holds only `winsys` and `runtime`.
This traces what it will have to do, by reading what the display already accepts and what radeonsi
will produce.

Most of it lines up. One thing does not line up as well as this repository has been assuming, and
catching that is the point of the entry.

## What the display accepts

oops-sdk's `agc_display.c` sets up scanout like this:

```c
disp->tiling_mode = 0;   /* 0 = tiled mode required by libSceVideoOut on this port */
sceVideoOutSetBufferAttribute2(attr, 0x8000000000000000ULL,
                               (uint32_t)disp->tiling_mode, width, height, 0, 0, 0);
...
sceVideoOutRegisterBuffers2(disp->handle, 0, 0, buffers, 2, attr, 0, 0);
```

Two buffers, registered up front at fixed addresses, **tiled** rather than linear, 32 bits per
pixel (`uint32_t *target_gpu_fb[2]`). There is a third allocation, `linear_scratch_fb`, which is
what `agc_tile_surface` reads from when the CPU has drawn something linear.

The tiled layout is `agc_tiler.c`'s: 128x128 pixel blocks of 64 KiB each, and a surface is
`ceil(w/128) * ceil(h/128)` of them:

```c
size_t tiles_x = ((size_t)width + AGC_TILE_DIM - 1u) / AGC_TILE_DIM;
size_t tiles_y = ((size_t)height + AGC_TILE_DIM - 1u) / AGC_TILE_DIM;
return tiles_x * tiles_y * (size_t)AGC_TILE_BYTES;
```

This layout is not a guess: gl-cube draws through it, on hardware, and the compositor displays the
result.

## What Mesa will produce

Mesa is configured `-Dplatforms=`, `-Degl-native-platform=surfaceless`, gbm and glx disabled. So
there is no window system and no swap chain: a title renders to a framebuffer object and the shim
is what gets those pixels onto a registered scanout buffer.

Worklog 017 established that under the derived `GB_ADDR_CONFIG`, the swizzle mode addrlib selects
is `64KB_R_X`, and that it reproduces `agc_tiler.c`'s basis vectors exactly.

The obvious conclusion - and the one this repository has been carrying implicitly since worklog
017 - is that radeonsi's colour target is already in the display's layout, so presentation is a
flip rather than a copy.

## The extrapolation

That conclusion is broader than the evidence.

The derivation compared addrlib's computed byte offset against the tiler's for **all 16,384 pixels
of the block**. 16,384 is 128 x 128, which is exactly one block. The tiler's 14 basis vectors cover
x bits 0 to 6 and y bits 0 to 6 - within-block coordinates, every one of them. Block coordinates
are the bits *above* those, and `agc_tile_surface` does not consult the lookup table for them at
all: it walks blocks in a loop and places block `(bx, by)` at `(by * tiles_x + bx) * 64 KiB`, plain
row-major, with no swizzle between blocks.

An `_X` mode is named for a XOR. On GFX10 the `_X` modes fold bits of the block's position into
lower address bits, which is what spreads consecutive accesses across pipes - and `NUM_PIPES`, the
field the derivation pinned to 16 pipes, is what controls it.

So: for block `(0, 0)` the two layouts are established to agree, pixel for pixel. For any other
block they have not been compared, and there is a named mechanism by which they could differ.

**This is not a claim that they differ.** It is a claim that this repository does not know, and has
been writing as though it did. A surface that matched in the first 64 KiB and diverged after it is
precisely the shape of bug that produces a picture which is almost right - the failure oops-gl's
badge taught this collection to distrust (orbistoun#539).

## Settling it costs no hardware

The derivation ran on the build machine, against the real addrlib from the pinned Mesa. Extending
it is the same job with a wider loop: compute addrlib's offset for pixels in blocks `(1, 0)`,
`(0, 1)` and `(1, 1)` of a surface several blocks wide, and compare against
`(by * tiles_x + bx) * 65536 + tiler_offset(x & 127, y & 127)`.

Three outcomes, all useful:

- **They agree everywhere.** Direct scanout is real, presentation is a flip, and the derivation's
  conclusion is broader than it was proven to be rather than narrower.
- **They agree within blocks and disagree between them.** The display's tiling is a non-XOR mode
  and radeonsi must be told to use one, or presentation needs a resolve pass. Either is a design
  decision, made with the fact in hand.
- **The comparison cannot be set up** - for instance because addrlib will not produce a
  multi-block surface under this chip identity. That is itself a finding about the derivation.

The tool was a one-off and is not in the repository, which is its own small lesson: a measurement
worth making twice is worth committing. Rebuilding it is the first task of unit 6 rather than
something to squeeze into the end of an iteration, because doing it badly would produce a second
confident answer about one block.

## What else presentation will need

Recorded now while the reading is fresh, not yet decided:

- **Getting Mesa to render into a buffer it did not allocate.** The scanout buffers exist before
  any GL context does, at addresses `sceVideoOutRegisterBuffers2` was given. The natural seam is
  `pipe_screen::resource_from_handle`, which is how a Linux compositor hands Mesa a buffer; the
  winsys would answer it from `buffers.c`'s table rather than from a DMA-BUF.
- **Which of the two buffers is being drawn into**, and the flip. oops-sdk already owns this -
  `sceVideoOutSubmitFlip` and the flip queue whose depth `REQ-20260909T1020Z-a51e` measured at 26 -
  so the shim schedules rather than implements it.
- **Whether the format word matches.** The display is given `0x8000000000000000` and 32-bit pixels;
  what Gallium format that corresponds to is not written down anywhere in the collection.

## State

No code changed. 104 host checks, 0 failed. This entry is a correction to an assumption and a work
list, not an implementation. `-5c9d` remains the outstanding bus question.
