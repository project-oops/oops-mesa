# 026. Twenty-one more fields that were zero without saying so

**2026-09-17** - roadmap unit 5

## What this entry is

D008 found that `ids_flags` being zero was four undeclared claims rather than an absent answer, and
that one of them contradicted the memory description in the same file. The obvious question is how
many other fields are in that position.

The answer is twenty-one. This is the audit of all of them. Unlike `ids_flags`, none turns out to
be wrong - but "none of them is wrong" is only worth something if somebody checked, so this records
the check rather than the intuition.

## The method

Mesa reads `drm_amdgpu_info_device` in one file. Every field it touches, against every field
`device_info.c` sets:

```
$ grep -o "device_info->[a-z_0-9]*" src/amd/common/ac_gpu_info.c | sort -u    # 36 fields
$ grep -o "out->[a-z_0-9]*"         src/winsys/device_info.c   | sort -u      # 15 of them
```

The difference is twenty-one fields that arrive at Mesa as zero because `memset` put them there.
For each: what does Mesa compute from it, and does anything act on the result?

## Inert: twelve of them

Mesa either guards the zero or only reads the field on a generation this part is not.

| Field | Why zero reaches nothing |
|---|---|
| `num_cu_per_sh` | one consumer, in perfcounters, behind `MAX2(1, ...)` |
| `num_shader_visible_vgprs` | explicitly guarded - `if (device_info && device_info->num_shader_visible_vgprs)` |
| `gl1c_cache_size`, `gl2c_cache_size`, `tcp_cache_size`, `mall_size` | read only under `gfx_level >= GFX11`; on GFX10 Mesa substitutes its own constants (`tcp_cache_size = 16384`, `l1_cache_size = 128 KiB`) |
| `num_sqc_per_wgp`, `sqc_inst_cache_size`, `sqc_data_cache_size` | the `ac_print_gpu_info` dump only, which needs a debug flag that needs an environment |
| `chip_rev` | one consumer, behind `info->gfx_level == GFX12` |
| `pa_sc_tile_steering_override` | GFX9 |
| `vce_harvest_config` | video, and `HW_IP_INFO` answers no video engine at all |
| `tcc_disabled_mask` | subtracted from `max_tcc_blocks`, which is also zero |
| `enabled_rb_pipes_mask_hi` | ORed in as the high 32 bits of `enabled_rb_mask`; the sixteen bits this file does set all fit in the low half |
| `pci_rev` | the RGP trace header only |

## Zero has an effect, three times, and the effect is right

**`num_tcc_blocks`** is the one that looked dangerous. It produces `l2_cache_size = 0` through a
chain of multiplications, and a zero cache size is the shape of a division waiting to happen. It
is not:

```
$ grep -rE "/\s*(info|sscreen->info|sctx->screen->info)->(num_tcc_blocks|l2_cache_size)" src/
(no matches)
```

The only arithmetic consumer of `l2_cache_size` is the attribute ring size, which needs GFX11. The
other derived value is `tcc_rb_non_coherent`, and there zero is *indistinguishable from the right
answer*: the flag needs `!util_is_power_of_two_or_zero(num_tcc_blocks)`, so both 0 and a
sixteen-TCC part give false.

**`device_id`** becomes `info->pci_id`, which reaches `caps->device_id` - what Gallium reports to
an application asking what GPU it is on. No PCI identifier for this part has been measured by
anything in this collection, so zero is the honest answer rather than a missing one. Inventing a
Navi 10 identifier here would be exactly the kind of borrowing the provenance rule exists to stop.

**`high_va_offset` and `high_va_max`** leave the VA manager's high range empty, which makes
`amdgpu_query_sw_info(amdgpu_sw_info_address32_hi)` - a fatal call inside `ac_query_gpu_info` -
answer from the 32-bit range instead. That is deliberate: nothing in this shim hands out a high
virtual address, and the one address space oops-gl has ever seen the driver use starts at
`0x2_0000_0000`.

## The distinction this makes, and why it is in the code

`device_info.c` now carries this list, and it says explicitly that none of these is counted in the
"N groups are assumed" figure the winsys logs. That is not bookkeeping pedantry:

- an **assumed** field is one where a value was chosen and could be wrong - the CU counts, the
  render backend count, the address range. Those carry `REQ-20260914T1558Z-7d41`.
- a field **left zero** is one where zero is what this shim means. Nothing would be improved by
  measuring it, because Mesa does not act on it here.

Conflating the two inflates the assumed count with fields nobody needs to answer, which makes the
figure less useful exactly where it is meant to be a warning. Worth being precise about, because
that log line is the thing a future reader will use to judge how much of the device description is
real.

## What this does not say

That the values would be right on another part, another generation, or another path. Every verdict
above is scoped to GFX10.1 and to screen creation. The context and draw paths have not been
audited this way, and `num_tcc_blocks` in particular is the kind of field a binning or cache-flush
heuristic could start reading the moment drawing begins - `si_state_binning.c:294` already reads
it, behind a `MAX2` with the render backend count, which is the sort of guard that exists because
somebody once passed a zero.

`-7d41` stays resolved. It asked whether the platform exposes a device-information surface, the
answer was no, and nothing here changes that. This narrows what a future answer would need to
cover, which is useful if one ever arrives from a different direction - `hw.model` reading
`100-000000189` is still the only lead in the collection.

## State

96 host checks, 0 failed - unchanged, because this adds no behaviour. Comments only, plus this
entry. All gates pass. Nothing deployed.
