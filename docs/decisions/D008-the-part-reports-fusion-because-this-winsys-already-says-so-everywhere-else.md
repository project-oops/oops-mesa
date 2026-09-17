# D008 - The part reports FUSION, because this winsys already says so everywhere else

**decided** · 2026-09-17

`drm_amdgpu_info_device.ids_flags` has been zero since `device_info.c` was written, by `memset`
rather than by choice. It carries four bits Mesa reads, and leaving it at zero is a claim about all
four. Three of those claims are right. One contradicts an answer this same file already gives.

## What the four bits do

| Bit | Mesa reads it as | Consumers |
|---|---|---|
| `FUSION` (0x1) | `has_dedicated_vram = !FUSION` | ~24 sites across radeonsi, the winsys, `ac_surface`, `ac_descriptors` |
| `PREEMPTION` (0x2) | `has_kernelq_reg_shadowing` | one, and on this part it is the only gate |
| `TMZ` (0x4) | `has_tmz_support` | one |
| `CONFORMANT_TRUNC_COORD` (0x8) | `compiler_info.conformant_trunc_coord` | three, all in shader lowering |

## Three of them stay clear, and not by default

**`TMZ` stays clear.** Nothing in this collection does trusted memory, and no path here asks for a
protected buffer. Zero is the true answer. Note the one place it is fatal - `radeonsi_screen_create_impl`
fails the screen if `DBG(TMZ)` is requested and `has_tmz_support` is false - is unreachable, because
that debug flag comes from an environment variable and there is no environment.

**`PREEMPTION` stays clear**, and on this part that is a direct decision rather than a conservative
one. The gate is:

```c
info->has_kernelq_reg_shadowing = device_info->ids_flags & AMDGPU_IDS_FLAGS_PREEMPTION &&
                                  info->gfx_level < GFX11 &&
                                  !(info->userq_ip_mask & (1 << AMD_IP_GFX));
```

Both other conditions are true here - GFX10 is below GFX11, and `userq_ip_mask` is zero because
`AMD_USERQ` is unset - so this bit alone decides whether Mesa enables register shadowing. Shadowing
means the kernel saves and restores register state across preemption of a queue. Submission here is
`sceAgcDriverSubmitDcb` onto a queue this shim creates, with no preemption notion anywhere in it.
Claiming the bit would ask Mesa to rely on a mechanism that does not exist.

**`CONFORMANT_TRUNC_COORD` stays clear**, and this one is conservative on purpose. It reports
whether the hardware's texture coordinate truncation is already conformant; when it is clear, Mesa
lowers `tg4` differently and sets `lower_array_layer_round_even`. Those are workarounds. A
workaround applied to hardware that does not need it costs some shader instructions; a workaround
omitted on hardware that does need it produces wrong pixels. Until something measures which this
part is, wrong-but-slower beats fast-but-wrong.

## `FUSION` is set

Zero says the part has dedicated VRAM. `oops_winsys_memory_info`, in the same file, answers:

```c
out->vram = heap;
out->cpu_accessible_vram = heap;
out->gtt = heap;
```

one pool, three times, with its own comment saying "every byte of 'VRAM' is CPU-visible". That is
what `FUSION` means. The two answers cannot both be right, and the memory one is the one with a
measurement behind it - `sceKernelGetDirectMemorySize`, and oops-sdk allocating every buffer
through the main direct-memory pool and mapping it for the CPU.

So this is not a new assumption being added. It is an existing assumption being made consistent
with a measured answer that already contradicts it.

The most visible consequence is `si_get.c`:

```c
caps->uma = !sscreen->info.has_dedicated_vram;
```

`PIPE_CAP_UMA` is what Gallium reports to applications about whether the memory is unified. On this
console it is. Answering otherwise tells every application the opposite of the truth about the one
thing that most changes how it should allocate.

## Why this is safe to change now, which is the reason to change it now

Flipping a bit with 24 consumers, none of which has ever run, deserves the check that it does not
disturb what is about to be tested. Every consumer was read. On this part:

- **Surface layout is untouched.** The one `has_dedicated_vram` branch in `ac_surface.c` that
  forbids swizzle modes sits inside `if (info->gfx_level >= GFX11)`, and the `maxAlign` choice at
  `ac_surface.c:3065` is 64 KiB either way for single-sample surfaces. So worklog 017's derivation -
  which matched the tiler against addrlib under the current settings - still describes what addrlib
  will produce. This was the check that could have blocked the change and it comes out clean.
- **`max_heap_size_kb` is unchanged**, because it picks between `vram_size_kb` and `gart_size_kb`
  and this winsys reports the same pool as both.
- **`si_buffer.c:141` and `si_shader_binary.c:317` are unchanged**, because they need
  `has_dedicated_vram && !all_vram_visible`, and `all_vram_visible` is true here for the same
  reason - one pool, all of it CPU-visible - so both sides of the flip give false.
- **Four more sites do not apply**: `discardable_allows_big_page` and `always_allow_dcc_stores`
  need GFX10_3 or later, `has_set_context_pairs_packed` needs GFX11, and `si_gfx_context.c:100`
  needs CHIP_RAVEN.
- **Two `si_gfx_screen.c` sites short-circuit**: both are `... || info->gfx_level >= GFX10`, which
  is already true.

What is left that genuinely changes - `caps->uma`, `is_apu`, buffer domain selection in
`amdgpu_bo.c`, a descriptor field, command-stream handling in `amdgpu_cs.cpp`, and two texture
transfer paths - is **all on the context and draw path**, none of it on screen creation.

That is the argument for doing it today rather than later. The change cannot destabilise the
milestone this repository is about to test for the first time (worklog 022: a non-null
`pipe_screen`), and it will already be right when the context path is reached. Making it after a
successful screen run would mean changing behaviour under a result rather than before one.

## What would reverse this

A measurement showing the graphics memory is *not* the pool `sceKernelGetDirectMemorySize` reports -
which is exactly what `REQ-20260915T0030Z-5d1c` asks, and is still open. If that came back saying
the GPU has a separate pool the CPU cannot see, then both this bit and the memory answer would be
wrong together, and they should be corrected together. They are one claim, which is the point of
writing this down rather than setting a bit.
