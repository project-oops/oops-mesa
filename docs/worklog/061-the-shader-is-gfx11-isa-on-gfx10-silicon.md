# 061. The shader ILLEGAL_INST is a wrong-ISA-generation shader (gfx11 on gfx10 silicon)

**2026-09-20** - roadmap unit 6

## What the faulting bytes decode to

The shader at h7 that worklog 060 confirmed uploads correctly -
`be800086 be810083 d4000002 02000880 00000001 e0001000 80020200 bf800000` - was
disassembled both ways with `llvm-mc` (the build image's LLVM 18):

| dword | bytes | gfx1030 / gfx1013 (the silicon) | gfx1100 (RDNA3) |
|---|---|---|---|
| 0 | `be800086` | **invalid encoding** | `s_mov_b32 s0, 6` |
| 1 | `be810083` | **invalid encoding** | `s_mov_b32 s1, 3` |

`llvm-mc` for gfx1013 emits `s_mov_b32 s0, 6` as `0xbe800386` (SOP1 op **0x03**); the
shader's byte is `0xbe800086` (op **0x00**), which is the **gfx11** SOP1 renumbering.
So the uploaded shader is **gfx11 (GFX11) machine code**, and the gfx10.3 silicon
decodes the earlier ops, loses instruction alignment, and faults at dword 5. This is not
a memory or mapping fault (all cleared through worklog 060) - it is the wrong ISA.

## Why that should be impossible, and is

The compiler is ACO (`-Damd-use-llvm=false`, `-Dllvm=disabled`; there is no LLVM in the
build). ACO's assembler splits SOP1 opcodes on `gfx_level >= GFX11`
(`aco_assembler.cpp:100`), and every ACO invocation - main shaders and prolog/epilog
parts alike - takes its level from `screen->info.gfx_level` (`si_shader_aco.c:47`,
`si_fill_aco_options`). So the shader being gfx11 means `info.gfx_level >= GFX11` at
runtime.

But `gfx_level` is derived purely from `info->ip[AMD_IP_GFX].ver_{major,minor}`
(`ac_gpu_info.c:761-787`), which come from our `HW_IP_INFO` answer. The shim answers
`hw_ip_version 10.1`, `ip_discovery_version 0`, `available_rings 0x1`
(`drm_device.c:332`), and for `FAMILY_NV`+`GFX1013` `ac_fill_hw_ip_info` forces
`ver_minor = 1` (`ac_gpu_info.c:504-509`) -> **10.1 -> GFX10**. Every input was checked:

- `FAMILY_NV == 0x8F` (`amdgpu_asic_addr.h:27`), which is exactly what the shim reports,
  so the classifier reaches `CHIP_GFX1013` and the `ver_minor=1` fixup applies.
- The disk shader cache is off two ways over (`-Dshader-cache=disabled` at build, and
  `getenv` returns null for every name so no cache dir is ever found) - there is no stale
  or foreign blob being replayed.
- `struct drm_amdgpu_info_hw_ip` is byte-identical between the shim's vendored header and
  Mesa's own, and Mesa zero-inits `ip_info` before the ioctl, so a short copy could not
  leave `ip_discovery_version` non-zero.
- No patch touches chip identification.

Static analysis is unanimous: our inputs produce GFX10. The hardware ran GFX11. The gap
is between "what ac_gpu_info should compute" and "what it computed", and only a runtime
observation closes it.

## The instrumented build (staged, not yet run)

Three temporary `fprintf(stderr, "OOPS-DIAG ...")` probes were added (stderr reaches the
klog via `src/runtime/stderr_to_klog.c`), Mesa rebuilt, the title relinked and packaged:

- `ac_gpu_info.c` in `ac_fill_hw_ip_info` (GFX only): the raw ioctl `ip_info`
  (`disc`, `hw_ver`, `rings`) and the computed `ip_gfx` major.minor.rev.
- `ac_gpu_info.c` after `gfx_level` is set: `gfx_level`, `family`, `ip_gfx` (with the
  GFX10/GFX10_3/GFX11 enum values printed so the integer is legible: GFX10=12,
  GFX10_3=13, GFX11=14).
- `si_shader_aco.c` in `si_fill_aco_options`: the `gfx_level`/`family`/`stage` each ACO
  compile receives.
- `aco_interface.cpp` in `aco_compile_shader` (after `emit_program`): `prog.gfx_level`,
  wave size, code length and the first two output dwords - so the faulting bytes
  (`be800086 ...`) can be matched to a specific compile and its level. This is what
  settles the last branch: if all the level logs say GFX10 but this one shows the gfx11
  bytes coming out of an ACO compile whose `prog.gfx_level` is somehow GFX11, or shows
  those bytes never emitted at all (a blob), the run says which.

One run reads them out: if `ip_gfx` is 11.x the ioctl answer is not arriving as the shim
sends it; if it is 10.1 but `gfx_level` is GFX11 the derivation is being overridden; if
the chip log says GFX10 but the aco log says GFX11 something corrupts `info` between. The
probes are diagnostics only and come out (with `queue_self_test`, `oops_winsys_dump_bos`
and the lifecycle logs) once the shader executes cleanly; they are **not** committed.

## Next

A hardware capture of the DIAG lines (race-proof: subscribe `pros logs`, wait, then
launch), then fix whatever the lines point to - most likely a device-description field
the runtime reads differently from how the winsys means it. The console fault here is
contained exactly as every run this unit (process dies, slot frees).
