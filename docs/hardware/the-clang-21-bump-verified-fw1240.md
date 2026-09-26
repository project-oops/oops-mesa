# clang 21 build on hardware

Firmware 12.40, retail unit, 2026-09-22. `dri-probe` and `mesa-cube` rebuilt against the clang-21
Mesa and deployed with `pros probe`, which attaches to the log before launching. Verifies
[D013](../decisions/D013-the-collection-compiles-with-clang-21-and-the-pin-is-enforced.md).

## Compiler provenance

`readelf -p .comment` on the objects, not the build tree's `meson-log.txt` (a configure log meson
does not rewrite, which names clang 18):

| artefact | producer |
|---|---|
| `build/mesa/.../dri_target.c.o` | `Debian clang version 21.1.8 (++20251221033036+2078da43e25a-...)` |
| `build/mesa/.../libgl_public.c.o` | the same |
| the titles | clang 21.1.8, from the WSL builder |

`2078da43e25a` is the llvm-project commit `llvmorg-21.1.8`, the revision
`oops-apps/src/oops-deps/libcxx/upstream.lock` pins.

## dri-probe frame hash

```
GL_VERSION   4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)
GL_RENDERER  AMD Radeon Graphics (radeonsi, gfx1013, ACO, DRM 3.54)

first render: draw err=0x0 read err=0x0 | centre R=255 G=0 B=0 | corner R=64 G=128 B=191
glsl render:  compile vs=1 fs=1 link=1 draw err=0x0 | centre R=64 G=64 B=128
frame-hash 0x5188ddb7 | mod-pixels 373248 | centre-pixel 0xff404080 | corner-pixel 0xff0d0d14
```

| | clang 18 | clang 21 |
|---|---|---|
| frame hash | `0x5188ddb7` | `0x5188ddb7` |
| modified pixels | 373248 | 373248 |
| centre pixel | `0xff404080` | `0xff404080` |
| corner pixel (the clear) | `0xff0d0d14` | `0xff0d0d14` |

Direct scanout takes both buffers (flip indices 2 and 3); present is a flush and a flip. The hash
is defined in [the GLSL record](glsl-pipeline-and-frame-hash-fw1240.md). dri-probe samples no
texture, so the hash does not cover the texture unit.

## mesa-cube pacing

6600 frames over 110 seconds, 22 rate reports:

```
frame  900: 16685 us a frame over the last 300
frame 2400: 16684 us
frame 4800: 16683 us
frame 6600: 16740 us
```

| | clang 18 | clang 21 |
|---|---|---|
| frame time | 16682 us | 16683-16794 us |
| CPU pixel work | `read=0 mirror=0` | `read=0 mirror=0` |
| flip submit | 10-12 us | 10-12 us |
| presentation fallbacks | 0 | 0 |
| faults | 0 | 0 |

The reports are 300-frame averages; one dropped frame in a window adds
`(33366 - 16683) / 300` = 55.6 us, and every value is `16683 + 55.6n`. The title is vsync-locked
at 59.94 Hz and drops about one frame in 300.

The cube in this run draws as a black silhouette with correct shape, spin, depth, cull and
overlay. The cause is outside the compiler: the overlay's `oops_hud_create` unbinds texture unit 0
(`oops-sdk/src/hud/hud.c` saves and restores the binding), and
`oops-apps/src/oops-mesa/mesa-cube/assets/demo.gif`, captured on hardware from the clang-21 Mesa
without the overlay, shows the cube textured.
