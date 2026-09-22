# The advertised surface, measured: 322 extensions, and every prediction in GL_SURFACE.md held

**2026-09-22** · firmware 12.40 · the same retail unit as the earlier records · `mesa-demos`
`glinfo`, title `MDEM00001` · companion to [GL_SURFACE.md](../GL_SURFACE.md) and
[D014](../decisions/D014-the-scope-is-gl-3-3-and-4-6-is-radeonsis-claim-not-ours.md)

[GL_SURFACE.md](../GL_SURFACE.md) was derived entirely from reading source and said so at the
top: *"No part of it has been on hardware."* This is the part that has been. The full extension
list is [`gl-extensions-fw1240.txt`](gl-extensions-fw1240.txt), one per line, sorted.

## What the driver says about itself

```
GL_VERSION:                   4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)
GL_RENDERER:                  AMD Radeon Graphics (radeonsi, gfx1013, ACO, DRM 3.54)
GL_VENDOR:                    AMD
GL_SHADING_LANGUAGE_VERSION:  4.60
GL_EXTENSIONS:                322 extensions
```

`GL_SHADING_LANGUAGE_VERSION` is new - the earlier records carried the version string but never
the shading language, and 4.60 is what a 4.6 context owes.

## Every prediction held

GL_SURFACE.md's three tiers were worked out from `si_gfx_context.c`, `ac_gpu_info.c` and this
repository's own winsys. All of it is confirmed:

| GL_SURFACE.md said | measured |
|---|---|
| compute is **reachable** - dispatches on the graphics ring, advertised because ACO exists rather than because a ring does | `GL_ARB_compute_shader`, `GL_ARB_compute_variable_group_size` and `GL_ARB_shader_storage_buffer_object` all advertised, on a device that answers no `COMPUTE` IP |
| sparse is **advertised and refused** - a derived cap meeting `AMDGPU_VA_OP_REPLACE` at `src/winsys/buffers.c:383` | `GL_ARB_sparse_buffer`, `GL_ARB_sparse_texture`, `GL_ARB_sparse_texture2` all advertised |
| interop is **advertised and refused** - `FENCE_TO_HANDLE`, `GEM_METADATA`, `GEM_USERPTR` | `GL_EXT_memory_object`, `GL_EXT_semaphore`, `GL_AMD_pinned_memory` all advertised |
| `ARB_sync` is **answered hollowly** - right answer, wrong reason | `GL_ARB_sync` advertised; submission is still synchronous |

**The compute row is the one worth dwelling on.** A reader looking at `drm_device.c` answering
only the GFX ring would reasonably conclude GL 4.3 compute is unavailable here. The driver
advertises it anyway, because radeonsi never wanted the ring: it is `support_aco || support_llvm`
that decides, and a GL context dispatches on graphics regardless. That was worked out from source
before this run and the run agrees.

## Compressed textures: advertised, all of them

Asked on 2026-09-22 and answered here. Every family is present:

`GL_EXT_texture_compression_s3tc`, `GL_EXT_texture_compression_dxt1`,
`GL_ANGLE_texture_compression_dxt3`, `GL_ANGLE_texture_compression_dxt5`, `GL_S3_s3tc`,
`GL_ARB_texture_compression_rgtc`, `GL_EXT_texture_compression_rgtc`,
`GL_EXT_texture_compression_latc`, `GL_ATI_texture_compression_3dc`,
`GL_ARB_texture_compression_bptc`, `GL_KHR_texture_compression_astc_ldr`,
`GL_KHR_texture_compression_astc_sliced_3d`.

**Advertised is not working.** `glCompressedTexImage2D` has still never been called through this
shim, and nothing here changes that - the driver's claim is now measured, the path is not.

## The one that justifies a design decision

`shim/include/glad/glad.h` in `mesa-demos` matches extension names **whole-word**, and its header
says why: a plain `strstr` finds `GL_EXT_texture` inside `GL_EXT_texture3D`. That was written as
a precaution against a hypothetical. It is not hypothetical:

```
GL_EXT_polygon_offset          absent
GL_EXT_polygon_offset_clamp    advertised
GL_ARB_polygon_offset_clamp    advertised
```

A naive `strstr` would have reported `GL_EXT_polygon_offset` present, and the demo that tests it
would have taken a path for an entry point that is not there. Of the nine extensions that shim
queries, eight are advertised and this is the one that is not - so the boundary check is doing
work on the very first run that could exercise it.

## What is still only advertised

Nothing in this document is a conformance result and it must not be cited as one. 322 names is
what the driver claims; `mesa-cube`'s texture and depth test and `gears` are the extent of what
has been *drawn*. The gap between those two numbers is what unit 8 exists to close, and
[the census](../../../oops-apps/src/oops-titles/mesa-demos/README.md) lists which of the 37
building demos would narrow it fastest.

## Three bugs this run found, all in the log path

Worth recording because all three were invisible until a program that was not Mesa tried to
print, and all three are fixed:

1. **`printf` was never interposed.** `src/runtime/stderr_to_klog.c` defined `fprintf`,
   `vfprintf`, `fwrite`, `fputs`, `fputc`, `puts` and `fflush` - the set Mesa calls. Mesa never
   calls plain `printf`; a ported program does. The first `glinfo` run brought up GL, queried the
   driver, printed its whole answer and parked, with every line going into the void.
2. **The size fallback tested `gl == NULL`.** `oops_gl_create` treats a failed display open as
   non-fatal and returns a usable handle, so the case that matters - GL up, display refused -
   sailed past the check. `glinfo` asked for 1280x720 and got a context nothing could show.
   `gears` had fallen back correctly only because *its* 300x300 create failed outright, which
   made a broken test look like a working one.
3. **A formatted line longer than 512 bytes was cut in half.** `emit_formatted` clamped to
   `sizeof(buf) - 1` and dropped the rest silently. The second run returned 511 characters of the
   extension string, ending mid-token. Only visible because the next line began in the middle of
   a word.
