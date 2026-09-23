# Solid texturing: a 3D texture sampled on hardware

**2026-09-23** · firmware 12.40 · the same retail unit as the earlier records · `mesa-demos`
`stex3d`, title `MDEM00001` · companion to
[shadow mapping](shadow-mapping-and-arb-fragment-program-fw1240.md)

A torus textured from a volume rather than from an image. `glTexImage3D` and 3D sampling, neither
of which had been exercised here.

## The run

| | |
|---|---|
| presents | 32, settling to **4.45 ms** a frame |
| GPU faults | 0 |
| GL errors | 0 |
| chained command buffers | 1, walked by `submit_chain` - `0x400003b00` to `0x400018000` |

The ten "not implemented" lines in the log are the familiar harmless set: two `AMDGPU_INFO`
queries, `GEM_METADATA`, the CPU-count fallback and the shell's own background transitions.
Nothing on the texture path refused anything.

## Why the picture is the evidence and the log is not

`stex3d` prints nothing about itself - no "using `GL_EXT_texture3D`" line, no mode report. So
unlike [`shadowtex`](shadow-mapping-and-arb-fragment-program-fw1240.md), which announces the
extensions it took, a clean run here proves only that the driver accepted the calls. **Thirty-two
frames with no errors does not mean the right voxels came back.** Two captures settle it:

**No seam, and no pinching at the inner ring.** A 2D texture on a torus always shows a UV seam
somewhere and distortion where the surface curves through the hole. The noise here is at uniform
density across the whole body including the inner surface, which is what a coordinate that is a
*position in a volume* gives and a surface parameterisation does not.

**The pattern is three-dimensional noise, not one slice.** The failure mode of a wrong or ignored
`r` coordinate is a single slab repeated over the surface - visible immediately as banding or as
the same 2D image everywhere. The mottling is irregular in all three directions.

**It stays locked to the geometry.** Two captures at different rotations show the texture turning
with the torus rather than swimming across it, which is solid texturing behaving as it should.

## What this does not establish

The *values* have not been compared against the same demo on a desktop. The pattern is a
procedural noise volume the demo builds itself, so a subtly wrong filter or a half-texel offset
would look exactly like this. What is established is that a 3D texture is created, bound and
sampled with three coordinates, and that the third one is doing work.

`GL_EXT_texture3D` is among the 322 the driver advertises
([the advertised surface](the-advertised-surface-measured-fw1240.md)); this moves it from claimed
to drawn.
