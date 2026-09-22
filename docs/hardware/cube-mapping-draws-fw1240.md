# Cube mapping draws, and the thing it maps onto is ours

**2026-09-22** · firmware 12.40 · the same retail unit as the earlier records · `mesa-demos`
`cubemap`, title `MDEM00001` · companion to
[the advertised surface](the-advertised-surface-measured-fw1240.md)

The first frame on this console that is neither a triangle, a cube nor a gear. `cubemap` draws a
checkerboard room from a six-face cube texture and puts a reflection-mapped sphere in the middle
of it, and both halves are on screen.

## What this measures that nothing before it did

| | |
|---|---|
| `GL_ARB_texture_cube_map` | advertised in the 322, now sampled: the room's walls are cube faces used as plain 2D textures |
| `GL_REFLECTION_MAP_ARB` texgen | the sphere's texture coordinate is generated from the normal and the eye vector, per fragment |
| driver-side mipmap generation | the demo's default path sets `GL_GENERATE_MIPMAP_SGIS` and calls `glGenerateMipmapEXT` on a cube target (`upstream/src/demos/cubemap.c:445,472`); the minification filters it cycles through are all `*_MIPMAP_*` |
| a **non-trivial mesh from this repository's stack** | see below |

## The sphere is the measurement, not the subject

`cubemap` reflection-maps a `glutSolidSphere(2.0, 20, 20)`. oops-sdk builds `glutSolidSphere` on
GLU quadrics (`oops-sdk/src/gl/glut.c:461`), and a Mesa-linked title cannot link this SDK's GLU
(`REQ-20260922T0940Z-5c17`), so the title supplies its own in
`oops-apps/src/oops-titles/mesa-demos/shim/glu_quadrics_on_mesa.c`.

Earlier today that shim was a set of no-ops that logged their own absence, and the run drew a
correct room around nothing. The quadrics were written, and this is the run after.

**What makes it a measurement rather than a picture is the coupling.** Reflection mapping derives
its texture coordinate from the *normal*. A sphere with correct positions and absent or wrong
normals would have been geometrically right and reflected nothing, or reflected the wrong face -
and it would have read as a cube-map failure. That the reflection is coherent across the surface
is a statement about the normals, and the normals are ours.

That cuts the other way too and is the honest limit of this record: this run does not separate
"radeonsi's texgen is right" from "the shim's normals are right", it only shows the two agree. A
wrong-but-consistent convention in both would look like this.

## The silhouette is faceted, and that is upstream's tessellation

The sphere's edge carries a ring of high-frequency speckle. Three things account for it without
anything being wrong:

- the mesh is **20 slices by 20 stacks**, upstream's own argument - the silhouette is a 20-gon
- reflection mapping *amplifies* normal variation: near the silhouette the reflection vector
  sweeps through a large solid angle over a few pixels, so adjacent facets sample far-apart parts
  of the cube map
- the environment is a maximum-contrast checkerboard, which is the worst case for that sweep

It has **not** been compared against the same demo on a desktop, so this is a mechanism and not a
verdict. The check that would settle it is cheap and is not done here: the demo's `m` key cycles
the minification filter and its `s` key toggles `GL_TEXTURE_CUBE_MAP_SEAMLESS`, and a silhouette
that changes with neither is geometry rather than sampling.

## Presentation

Familiar, and one line of it is a fix from earlier today doing its job:

```
600x500 would not scan out; falling back to the display's own 1920x1080
```

At the requested 600x500 the buffers carried metadata - `1310720 bytes, plain is 1228800` - and
would have cost a copy per frame. At the display's own extent they are clean and go to direct
scanout. Present times after the first flip settle between 1.6 ms and 4.7 ms, every one of them
`read=0 mirror=0`.

`GEM_METADATA is not implemented yet` is refused repeatedly and remains harmless; it is
[the advertised-and-refused interop tier](../GL_SURFACE.md) being asked and answering.

## What went into oops-sdk from this

Per the standing instruction that findings which generalise are written where the next port will
meet them, `oops-sdk/docs/PORTING.md` gained two things:

1. the exact list of GLUT shapes that do not survive the Mesa path and the eight symbols they
   need - ten entry points, not "the `glutSolid*` family", and the cube and teapot are fine
2. **the limit of the undefined-symbol check**: it asks whether a name is defined, and a stub is
   defined. Papering over a missing dependency with a silent no-op buys a clean build and a wrong
   picture from the one check that was protecting you. This run is the worked example.
