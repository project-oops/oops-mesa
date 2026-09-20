# 062. The shader ILLEGAL_INST was a zeroed opcode table: `.init_array` was never run

**2026-09-20** - roadmap unit 6

## The finding, in one line

The title never ran its C++ dynamic initialisers (`.init_array`), so ACO's opcode table
`aco::instr_info` stayed zero, and ACO assembled every instruction with **opcode 0** - which on
GFX10 is a GFX11-or-reserved encoding. The GPU faulted decoding it. The gfx_level was correct the
whole way down; the table it indexed was empty.

## How worklog 061's "gfx11 ISA" resolved

061 established the shader was gfx11-encoded (`be800086` = `s_mov_b32` op 0, where GFX10 is op 3)
and that every device-description path computed GFX10 correctly. Instrumenting ACO end to end
(temporary `OOPS-DIAG` fprintfs, three hardware runs) closed it:

- `hwip`/`chip`/`aco`/`emit` all reported **gfx_level 12 (GFX10)** - winsys, `ac_gpu_info`, the
  ACO options, and `program->gfx_level` at emit. Not a device_info bug.
- The generated table is correct: `opcode_gfx10[s_mov_b32] = 3`.
- The assembler correctly selected the gfx10 table (`asmctx: ctx.gl=12 table=gfx10`).
- But **`instr_info.opcode_gfx10[s_mov_b32]` read 0 at runtime** (`g10_smov=0`). The table in
  memory did not match the file.

The object file said why: `aco::instr_info` is in `.bss` (`nm` type `B`, section
`.bss._ZN3aco10instr_infoE`, 0x126a0 bytes), filled at start-up by `_GLOBAL__sub_I_aco_opcodes.cpp`
which copies the `.rodata..Lconstinit` blocks in. That constructor is registered in `.init_array`.
Nothing here runs `.init_array`, so the copy never happened and the table stayed zero. Every
opcode in the faulting shader was 0 - `be800086` (SOP1 op 0), `d4000002` (VOP3 op 0), `e0001000`
(MUBUF op 0) - exactly a zeroed lookup.

The custom link script (oops-apps `local_tls.ld`) did not place `.init_array` or define its bounds,
and no start-up code walked it. On an ordinary system the crt objects do both; a title here is
linked with its own entry point and none of them.

## Not just ACO

The same `.init_array` also holds GLSL's `builtin_functions`/`builtin_types` constructors and the
compiler runtime's `__cpu_indicator_init` (behind `__builtin_cpu_supports`, which AddressLib uses
to pick a swizzle path). All three were also silently not running. One fix covers all of them.

## The fix

Run the initialisers at start-up, the crt job this target has no crt for:

- **oops-mesa `src/runtime/abi.c`** - `oops_mesa_run_init_array()` walks `__preinit_array_start..end`
  then `__init_array_start..end`, once. Kept beside `__dso_handle` and the glapi-TLS stub, which
  are start-up-object stand-ins for the same reason. The module's own relocations are applied
  already (function pointers and the GOT work), so the entries are correctly relocated - they were
  only never called; this does not depend on what the platform loader does with `DT_INIT_ARRAY`.
- **oops-apps `local_tls.ld`** - a `.preinit_array`/`.init_array`/`.fini_array` block with
  `PROVIDE_HIDDEN` bounds symbols, in the writable segment (each entry carries a RELATIVE
  relocation). The map confirms four entries now bounded: libcpu_model, the two GLSL tables and
  aco_opcodes.
- **oops-apps dri-probe `dri_probe_main.c`** - `dri_probe_start` calls it before `oops_gl_create`.

## Verification

Rebuilt/relinked with the `OOPS-DIAG` probes still in Mesa, so the run reads the table directly.

**Confirmed on hardware (run of 2026-09-20).** The probes flipped exactly as predicted:

```
asmctx: prog.gl=12 ctx.gl=12 table=gfx10 g10_smov=3 g11_smov=0 sel_smov=3   (was g10_smov=0)
emit:   prog.gfx_level=12 wave=64 dw=13 first=d7460000 04010c0c              (was be800086 ...)
```

`instr_info.opcode_gfx10[s_mov_b32]` now reads 3, the shader assembles as real GFX10 code, the
wavefronts execute, and the frontend runs all the way through:

```
oops_gl_create returned a handle: this is the first time that has happened
GL_VERSION:  4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)
GL_RENDERER: AMD Radeon Graphics (radeonsi, gfx1013, ACO, DRM 3.54)
GL_VENDOR:   AMD
```

**This is upstream Mesa/radeonsi running OpenGL on Prospero hardware** - screen, drawable,
context, make-current and the first three `glGetString` calls, all real. The single `.init_array`
gap was the whole of the shader wall.

## The next wall: a null call in presentation

Immediately after `GL_VENDOR`, `oops_gl_present` faults with a SIGSEGV, `rip=0` - a call through a
null function pointer (fault address 0, "user read instruction, page not present"; contained, the
process dies and the slot frees like every fault this unit). This is the flush/flip path the title
always expected to be hard (D009, the open `sceVideoOutRegisterBuffers2` question, and the
render-target-alignment concern on the oops-sdk side). It is a fresh problem well past the shader
one and is unit 6's next thread: identify the null entry `oops_gl_present` reaches and why.

## After it is confirmed

The temporary `OOPS-DIAG` probes (ac_gpu_info.c chip/hwip, si_shader_aco.c aco, aco_interface.cpp
emit, aco_assembler.cpp asmctx) and the winsys diagnostics (`queue_self_test`,
`oops_winsys_dump_bos`, the CREATE/MMAP/VA/CLOSE lifecycle logs) all come out together - Mesa
holds no patch for any of this; the fix is entirely in the shims and the title. mesa-probe's own
`local_tls.ld` needs the same `.init_array` block before it can run anything that dynamic-inits.
