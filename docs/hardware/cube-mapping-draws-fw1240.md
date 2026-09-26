# Cube mapping on hardware

Firmware 12.40, retail unit. Title `MDEM00001`, `mesa-demos` `cubemap`, 2026-09-22.
Companion to [the advertised surface](the-advertised-surface-measured-fw1240.md).

`cubemap` draws a checkerboard room from a six-face cube texture with a reflection-mapped sphere
in the middle of it. Both the room and the sphere are on screen.

## Features exercised

| | |
|---|---|
| `GL_ARB_texture_cube_map` | the room's walls are cube faces sampled as 2D textures |
| `GL_REFLECTION_MAP_ARB` texgen | the sphere's texture coordinate is generated from the normal and the eye vector |
| driver-side mipmap generation | the demo sets `GL_GENERATE_MIPMAP_SGIS` and calls `glGenerateMipmapEXT` on a cube target (`upstream/src/demos/cubemap.c`, lines 445 and 472); every minification filter it cycles through is `*_MIPMAP_*` |
| a mesh from this repository's stack | the sphere, below |

## The sphere

`cubemap` reflection-maps a `glutSolidSphere(2.0, 20, 20)`. oops-sdk builds `glutSolidSphere` on
GLU quadrics (`oops-sdk/src/gl/glut.c`), and a Mesa-linked title cannot link the SDK's GLU, so the
title supplies its own in `oops-apps/src/oops-titles/mesa-demos/shim/glu_quadrics_on_mesa.c`.

Reflection mapping derives its texture coordinate from the normal, so a coherent reflection across
the surface shows that the shim's normals and radeonsi's texgen agree. The run does not separate
the two: a wrong-but-consistent convention in both would look the same.

The silhouette carries a ring of high-frequency speckle. The mesh is 20 slices by 20 stacks, so
the silhouette is a 20-gon; reflection mapping amplifies normal variation near the silhouette; and
the environment is a maximum-contrast checkerboard. The frame is not compared against the same
demo on a desktop. `cubemap` takes keys (`upstream/src/demos/cubemap.c`, line 336) that separate
the causes:

| key | effect | what it distinguishes |
|---|---|---|
| `f` | cycles the 12 min/mag filter pairs | a fringe that survives `GL_LINEAR_MIPMAP_LINEAR` is not a sampling artefact |
| `Z` | pushes the eye out to 90 units | faceting shrinks with the sphere; sampling noise does not |
| `m` | switches texgen between `GL_REFLECTION_MAP` and `GL_NORMAL_MAP` | normal-map texgen does not sweep at the silhouette |
| `s` | toggles `GL_TEXTURE_CUBE_MAP_SEAMLESS` | whether face-edge seams contribute |

## Presentation

```
600x500 would not scan out; falling back to the display's own 1920x1080
```

At 600x500 the buffers carry metadata (`1310720 bytes, plain is 1228800`). At the display's own
extent they go to direct scanout. Present times after the first flip are 1.6-4.7 ms, each
`read=0 mirror=0`. `GEM_METADATA is not implemented yet` is refused repeatedly; it is the
advertised-and-refused interop tier of [GL_SURFACE](../GL_SURFACE.md).
