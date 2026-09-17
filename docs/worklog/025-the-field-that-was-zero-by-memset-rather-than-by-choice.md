# 025. The field that was zero by memset rather than by choice

**2026-09-17** - roadmap unit 5

## What this entry is

`ids_flags` has been outstanding since `device_info.c` was written: a field nobody decided, left at
whatever `memset` gave it. This closes it, with D008 carrying the argument.

The interesting part is not the bit that changed. It is that leaving the field alone was never
neutral - a zeroed `ids_flags` is four claims, and one of them contradicted an answer the same file
already gave.

## Four bits, not one field

Mesa reads `ids_flags` in exactly four places, all in `ac_gpu_info.c`:

| Bit | becomes | reached from |
|---|---|---|
| `FUSION` | `has_dedicated_vram = !FUSION` | ~24 sites, including `caps->uma` |
| `PREEMPTION` | `has_kernelq_reg_shadowing` | one, and on this part it is the *only* gate |
| `TMZ` | `has_tmz_support` | one |
| `CONFORMANT_TRUNC_COORD` | `compiler_info.conformant_trunc_coord` | three, in shader lowering |

Three stay clear and now say why. `TMZ` because nothing here does trusted memory. `PREEMPTION`
because on GFX10 with no user queues it alone decides register shadowing, and submission here is
`sceAgcDriverSubmitDcb` with no preemption notion anywhere in it - claiming the bit would ask Mesa
to rely on a mechanism that does not exist. `CONFORMANT_TRUNC_COORD` because it is unmeasured, and
clear is the side that keeps Mesa's texture-gather workarounds *on*: a workaround on hardware that
does not need it costs instructions, and one omitted on hardware that does need it produces wrong
pixels.

## The contradiction

`FUSION` clear tells Mesa this part has dedicated VRAM. Fifty lines below, the same file answers:

```c
out->vram = heap;
out->cpu_accessible_vram = heap;
out->gtt = heap;
```

one pool, three times, under a comment that already says "every byte of 'VRAM' is CPU-visible".
That is what `FUSION` means. Both answers cannot be right, and the memory one has a measurement
behind it.

So `FUSION` is now set. This adds no assumption - it makes an existing one consistent with a
measured answer that already disagreed with it. The two are one claim, and D008 says so explicitly:
if `REQ-20260915T0030Z-5d1c` comes back showing the graphics pool is not the one
`sceKernelGetDirectMemorySize` reports, both are wrong together and get corrected together.

The visible effect is `caps->uma`, the Gallium capability telling applications whether memory is
unified. On this console it is. Answering otherwise misinforms every application about the one
thing that most changes how it should allocate.

## The check that could have stopped it

Flipping a bit with 24 consumers, none of which has ever executed, needs the check that it does not
disturb the thing about to be tested. Reading every consumer:

The one that mattered was `ac_surface.c`. If `has_dedicated_vram` changed which swizzle modes
addrlib may choose, worklog 017's derivation - which matched oops-sdk's tiler against addrlib under
the *current* settings - would no longer describe what addrlib produces, and the flip would have
quietly invalidated two days of work. It does not: that branch sits inside
`if (info->gfx_level >= GFX11)`, and the `maxAlign` choice is 64 KiB either way for single-sample
surfaces. Clean.

Four more sites need GFX10_3, GFX11, or CHIP_RAVEN and do not apply. Two short-circuit on
`|| gfx_level >= GFX10`, already true. `max_heap_size_kb` picks between two numbers this winsys
reports as the same pool. `si_buffer.c` and `si_shader_binary.c` need
`has_dedicated_vram && !all_vram_visible`, and `all_vram_visible` is true here for the same
one-pool reason, so both sides of the flip give false.

What genuinely changes - `caps->uma`, `is_apu`, buffer domains, a descriptor field, command-stream
handling, two texture transfer paths - is **all on the context and draw path, none of it on screen
creation.**

That is why this was worth doing today rather than after a run. It cannot destabilise the milestone
this repository is about to test for the first time, and it will already be right when the context
path is reached. Changing it after a successful screen run would mean altering behaviour underneath
a result instead of ahead of one.

## Six checks

Four on the bits, and two pairing `FUSION` with the three heaps being equal. That pairing is the
point rather than belt and braces: they are one statement about this console, and a change to
either that does not change the other has broken it.

Host suite 96 of 96, up from 90. The decisions index regenerated to eight entries. Nothing
deployed.

## What is still assumed in that file

Six groups, unchanged, all carrying `REQ-20260914T1558Z-7d41`: the virtual address range, the
shader engine and CU counts, the render backend count, the graphics context count, the memory type
and bus width, and the page-table fragment size. `ids_flags` is deliberately **not** counted among
them any more - three of its bits are statements about what this shim does, which it is entitled to
make, and the fourth is tied to the memory answer rather than standing alone.
