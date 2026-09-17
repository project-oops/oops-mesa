# 031. The extrapolation checked out, and now it is checked

**2026-09-17** - roadmap unit 6

## What this entry is

Worklog 030 caught this repository claiming, implicitly, that a radeonsi colour target is already
in the layout the display scans out - on evidence that covered one 64 KiB block. It said settling
it needed no hardware, only a wider loop, and that rebuilding the derivation tool properly was the
first job of unit 6 rather than something to squeeze in.

This is that tool. The answer is that the extrapolation was right. It is now proven rather than
assumed, with a control, and it is checked on every `make check`.

## `tools/tiling-compare`

It compares two real implementations rather than a restatement of either.

**The oops-sdk side is recovered by tiling, not by reimplementing.** A linear surface is filled so
that pixel `(x, y)` holds a unique identifier, `agc_tile_surface` - the function the display
actually consumes - tiles it, and the result is scanned to see which byte offset each identifier
landed at. Whatever that function does, including anything this author has misunderstood about it,
is what gets compared. Restating the basis vectors in the tool would have been the same mistake
worklog 024 removed from the chip-identification test.

**The Mesa side is the real AddressLib from the pinned checkout**, sixteen `.cpp` files compiled
with AddressLib's own flags from its `meson.build`, called through `AddrCreate` and
`Addr2ComputeSurfaceAddrFromCoord`. No meson, no Mesa build, no LLVM - the same shape
`tools/preamble-dump` already established for a host tool against the pin.

Four things are fixed and each says why in the source: the chip identity `device_info.c` reports,
the derived `GB_ADDR_CONFIG`, `ADDR_SW_64KB_R_X`, and `pipeBankXor` of zero. That last one is not
a convenience: `ac_surface.c`'s `use_tile_swizzle` returns false when `get_display_flag` is set, so
a scannable surface carries no surface-level swizzle. Passing anything else would have compared
addrlib against a surface the display could never scan out.

## Two choices that make the answer mean something

**A 3x2 block grid, not 2x2.** The obvious surface is four blocks square, and it would have been
worthless for the actual question. With a 2x2 grid the block index `by * tiles_x + bx` runs
0, 1, 2, 3 whichever way round you read it, so a row-major layout and a column-major one agree and
the comparison cannot tell them apart. 3x2 distinguishes them and puts a third value in the x
direction.

**A control.** "Zero pixels disagree" is worth nothing on its own, because a comparison that cannot
detect a difference reports agreement too. So the same comparison runs twice - once with the
derived `GB_ADDR_CONFIG` and once with `NUM_PIPES` changed from sixteen pipes to eight - and the
second is *expected to fail*. If it does not, the tool prints `INCONCLUSIVE` and says the agreement
means nothing.

This is the discipline the obSCEne bus applies to its own sweeps, applied here. It would have been
easy to ship the first version of this tool, which ran 2x2 with no control, and record a clean
result that established less than it appeared to.

## The result

```
tiling-compare: 384x256, 32bpp, ADDR_SW_64KB_R_X, pipeBankXor 0
  family 0x8F  revision 0x82
  tiled surface is 393216 bytes, 3x2 blocks of 64 KiB
  98304 pixels compared per configuration

  derived  GB_ADDR_CONFIG 0x00000004 (16 pipes): 0 disagree
  control  GB_ADDR_CONFIG 0x00000003 (8 pipes):  92160 disagree
      first at (8, 0): tiler 0x100, addrlib 0x900
```

98,304 pixels across six blocks, not one. Nothing disagrees under the derived value, and 94% of
pixels disagree under a value wrong in one field - including at `(8, 0)`, which is inside the first
block, so the control is not merely detecting a block-ordering difference.

**A radeonsi 64KB_R_X colour target is already in the layout the display scans out.** Presentation
can be a flip rather than a copy, and worklog 030's worry is retired.

## What this also does for worklog 017

The derivation's conclusion was correct and is now known to be broader than it was proven to be.
Sixteen pipes and a 256 B interleave reproduce the tiler across blocks, not only within one, and
the eight-pipe control shows that the derivation's `NUM_PIPES` finding was load-bearing rather than
incidental.

`drm_device.c`'s provenance comment was scoped to one block last entry. It can now say more, and
points here rather than repeating the numbers.

## Checked, not just run

`make check` grew `check-tiling`, which regenerates the output into `build/` and fails if it
differs from the tracked copy - the same contract `check-preamble` has. So a pin bump that changes
AddressLib, or an edit to the tiler's vectors, shows up as a diff in a tracked file rather than as
a picture that is almost right during bring-up. It skips rather than fails without `clang++` or a
sibling oops-sdk, because CI may have neither.

The tool that produced worklog 017's answer was a one-off in a scratch directory and is gone. This
one is in the repository, which was the other half of worklog 030's complaint.

## State

`make check` passes with the new gate. 104 host checks, 0 failed - unchanged, since this adds a
tool rather than winsys behaviour. Nothing deployed. `-5c9d` remains the outstanding bus question.
