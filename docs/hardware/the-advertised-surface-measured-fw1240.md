# The advertised GL surface

Firmware 12.40, retail unit. Title `MDEM00001`, `mesa-demos` `glinfo`, 2026-09-22. Companion to
[GL_SURFACE.md](../GL_SURFACE.md) and
[D014](../decisions/D014-the-scope-is-gl-3-3-and-4-6-is-radeonsis-claim-not-ours.md). The full
extension list is [`gl-extensions-fw1240.txt`](gl-extensions-fw1240.txt), one per line, sorted.

## Driver identification

```
GL_VERSION:                   4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)
GL_RENDERER:                  AMD Radeon Graphics (radeonsi, gfx1013, ACO, DRM 3.54)
GL_VENDOR:                    AMD
GL_SHADING_LANGUAGE_VERSION:  4.60
GL_EXTENSIONS:                322 extensions
```

## GL_SURFACE.md tiers against the measurement

| GL_SURFACE.md | measured |
|---|---|
| compute is reachable: it dispatches on the graphics ring, advertised because ACO exists | `GL_ARB_compute_shader`, `GL_ARB_compute_variable_group_size` and `GL_ARB_shader_storage_buffer_object` advertised, on a device that answers no `COMPUTE` IP |
| sparse is advertised and refused: a derived cap meeting `AMDGPU_VA_OP_REPLACE` in `src/winsys/buffers.c` (line 383) | `GL_ARB_sparse_buffer`, `GL_ARB_sparse_texture`, `GL_ARB_sparse_texture2` advertised |
| interop is advertised and refused: `FENCE_TO_HANDLE`, `GEM_METADATA`, `GEM_USERPTR` | `GL_EXT_memory_object`, `GL_EXT_semaphore`, `GL_AMD_pinned_memory` advertised |
| `ARB_sync` is answered hollowly | `GL_ARB_sync` advertised; submission is synchronous |

radeonsi advertises compute on `support_aco || support_llvm`, not on a compute ring, and a GL
context dispatches on graphics.

## Compressed texture formats

Every family is advertised: `GL_EXT_texture_compression_s3tc`, `GL_EXT_texture_compression_dxt1`,
`GL_ANGLE_texture_compression_dxt3`, `GL_ANGLE_texture_compression_dxt5`, `GL_S3_s3tc`,
`GL_ARB_texture_compression_rgtc`, `GL_EXT_texture_compression_rgtc`,
`GL_EXT_texture_compression_latc`, `GL_ATI_texture_compression_3dc`,
`GL_ARB_texture_compression_bptc`, `GL_KHR_texture_compression_astc_ldr`,
`GL_KHR_texture_compression_astc_sliced_3d`. `glCompressedTexImage2D` is not called by this run.

## Extension name matching

`shim/include/glad/glad.h` in `mesa-demos` matches extension names whole-word, because a plain
`strstr` finds `GL_EXT_texture` inside `GL_EXT_texture3D`. The measured list has a case:

```
GL_EXT_polygon_offset          absent
GL_EXT_polygon_offset_clamp    advertised
GL_ARB_polygon_offset_clamp    advertised
```

Of the nine extensions that shim queries, this is the one not advertised.

## Log path requirements

A ported program's output reaches the log only when:

- `src/runtime/stderr_to_klog.c` interposes plain `printf` as well as the set Mesa calls
  (`fprintf`, `vfprintf`, `fwrite`, `fputs`, `fputc`, `puts`, `fflush`);
- the size fallback tests for a refused display, not `gl == NULL`, since `oops_gl_create` returns
  a usable handle when the display open fails;
- `emit_formatted` carries a line longer than 512 bytes whole (the extension string is longer).

The list is what the driver claims; it is not a conformance result.
