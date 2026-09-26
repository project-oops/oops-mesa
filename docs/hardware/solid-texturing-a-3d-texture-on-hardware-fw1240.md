# Solid texturing with a 3D texture

Firmware 12.40, retail unit. Title `MDEM00001`, `mesa-demos` `stex3d`, 2026-09-23. Companion to
[shadow mapping](shadow-mapping-and-arb-fragment-program-fw1240.md).

`stex3d` textures a torus from a volume: `glTexImage3D` and sampling with three coordinates.

## The run

| | |
|---|---|
| presents | 32, settling to 4.45 ms a frame |
| GPU faults | 0 |
| GL errors | 0 |
| chained command buffers | 1, walked by `submit_chain` - `0x400003b00` to `0x400018000` |

The log's ten "not implemented" lines are two `AMDGPU_INFO` queries, `GEM_METADATA`, the CPU-count
fallback and the shell's background transitions. Nothing on the texture path refuses.

## Evidence in the frame

`stex3d` prints nothing about its own mode, so the evidence is the picture, from two captures at
different rotations:

- No UV seam and no pinching at the inner ring: the noise has uniform density across the whole
  body, including the inner surface, as a coordinate that is a position in a volume gives.
- The pattern is irregular in all three directions; an ignored or wrong `r` coordinate gives a
  single slab repeated over the surface.
- The texture turns with the torus rather than swimming across it.

The sampled values are not compared against the same demo on a desktop; a subtly wrong filter or a
half-texel offset looks the same. `GL_EXT_texture3D` is in
[the advertised surface](the-advertised-surface-measured-fw1240.md).
