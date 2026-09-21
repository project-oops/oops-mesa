# 069. The display takes the address, but not a third buffer

**2026-09-21** - roadmap unit 6, D009, D012 step 3

## What happened

D012 step 3 - hand the display radeonsi's own colour buffer so present is a flush and a flip - was
built and tried on hardware three times. It does not work yet, and each refusal was more
informative than the one before it. The last one is a platform question nobody here can answer, and
it is filed.

The measurement that made it worth doing is worklog 068: the GPU draws the frame in 4.2 ms and the
CPU spends 32.5 ms carrying it to the screen. Every one of those 32.5 ms exists to move pixels out
of one buffer and into another.

## Three refusals

| attempt | code | what it established |
|---|---|---|
| the foreign buffer at slot index 2 | `0x80290001` | something is wrong; `INVALID_VALUE` names no argument |
| **the identical call, with the display's own live buffer address** | `0x80290001` | **the address is not what is refused** |
| the whole set - the display's two plus the foreign one - re-registered at index 0 | `0x80290010` | the slot complaint is gone; something else objects |

The middle row is the one worth the run. `0x80290001` is `SCE_VIDEO_OUT_ERROR_INVALID_VALUE`
(obscene D214, D301) and it is a catch-all, so the code alone could not say whether the address or
the slot was rejected. Re-issuing the call with **one input changed** - an address VideoOut had
already accepted, named rather than handed over - and getting the same code separates them.

**That answers D009's long-standing open question.** D009 said whether
`sceVideoOutRegisterBuffers2` constrains a buffer's address "only becomes answerable when there is
a surface to register", and that the first attempt to register a radeonsi buffer *is* the
measurement. It is answered **no**: nothing has to be relocated into the display's VA
neighbourhood. What does not exist is slot 2 - oops-sdk's display registers two buffers when it
opens, so 0 and 1 are all there are.

## The API both projects needed

Adding a third buffer means re-registering the set, and this shim cannot assemble that set safely:
`oops_display_scanout` reports the display's buffers **relative to its current flip index**, not in
registration order, so building the array from outside could swap 0 and 1 and leave the display
drawing into the buffer it is showing. The display is the only thing that knows the order.

So oops-sdk gained `oops_display_adopt_buffer` and `oops_display_flip_index`
(`include/oops/display.h`, implemented in `src/agc/agc_display.c`), at the owner's instruction.
They are written for both callers rather than for Mesa: `oops-sdk#REQ-20260919T1927Z-7e21` wants
exactly this for oops-gl, against its own linear scratch copy. oops-sdk's suite stayed at 315/315,
and this shim no longer names a single `sceVideoOut*` symbol.

On hardware the three-buffer registration returns **`0x80290010`**, which appears nowhere in
obSCEne's records - its display decisions carry `0x...01`, `03`, `08`, `09`, `0b` and `15`, but not
`10`. Nor is it alignment: D253 settled that at 64 KiB and this buffer sits at `0x400600000`, which
is already 64 KiB aligned. Filed as `REQ-20260921T1202Z-9a4c`, asking what the code means and
whether a registered set can be extended at all, with the arms that would decide it. `-2d7f`
carries a note pointing oops-gl at the same findings, because `-7e21` will meet this identical wall.

## The surprise: an instrument that could not say why

The first run through the new API reported `index -1` and nothing else. The return code was not
lost - it was simply unreachable: `agc_log` in oops-sdk only emits through a logger callback, and
`dri-probe` installs none, so **zero** `agc-` lines appear in that title's log at all.

It was recoverable without another speculative run. `agc_display_adopt_buffer` folds the code into
the display's last error as `0xE4000000 | (rc & 0xFFFFFF)`, and `oops_display_get_last_error` is
public. One line turned `-1` into `0x80290010`.

This is the third time in two days that an instrument has answered "no" when it meant "I could not
look" - `nm` reporting zero symbols for a `-fvisibility=hidden` title, `dri2_query_image` leaving a
zero in place for a modifier it declined to report, and now a log line with nowhere to go. The
pattern is worth the name: **a negative result is only evidence if the instrument could have
produced a positive one**, and a control that proves it could is cheap next to the run it saves.

## What it cost

Nothing. The readback path ran untouched behind the fallback all three times, and every refusal
ended with `presentation succeeded` and the frame hash unchanged at `0x5188ddb7`. The risky part of
trying a display registration - losing the output - was designed out before the first attempt:
indices 0 and 1 keep their addresses whether the call succeeds or fails.

## What is next

- **`-9a4c`.** It is the only thing between unit 6 and closed, and it cascades: unit 6 gates the
  clang 21 bump (worklog 067), which gates unit 8.
- **Double buffering, once registration lands.** The image loader hands the frontend one back
  image, so a working direct scanout would show the buffer radeonsi is drawing the next frame
  into - correct pixels with a tear line. Two images alternated is the fix, and it is deliberately
  not bundled with this: one failure mode at a time.
- **Not worth building yet:** rendering the drawable Y-inverted to remove the `mirror` pass
  (3.3-5.1 ms, about 10% of the frame). It optimises the path step 3 deletes. Worth it only if
  `-9a4c` comes back saying a set cannot be extended at all.
