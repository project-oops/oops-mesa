# The clang 21 bump, verified: the hash holds, and the black cube was not the compiler

**2026-09-22** · firmware 12.40 · the same retail unit as the earlier records · the acceptance
[worklog 067](../worklog/067-unit-8-needs-a-cpp-standard-library-nobody-costed.md) set and
[D013](../decisions/D013-the-collection-compiles-with-clang-21-and-the-pin-is-enforced.md) owed

D013 moved the collection to clang 21 and said so plainly in its own closing paragraph: *"No
hardware ran. Nothing here claims the bump is verified."* This is that run.

Both titles were rebuilt against the clang-21 Mesa and deployed with `pros probe`, which attaches
to the log before launching - the ordering that matters for a title which emits once and parks.

## Which compiler actually built what

Checked rather than assumed, because the `meson-log.txt` in the build tree still says clang 18:
it is the configure log from an earlier session and meson did not rewrite it. The objects are the
authority, and they disagree with it.

| artefact | producer |
|---|---|
| `build/mesa/.../dri_target.c.o` | `Debian clang version 21.1.8 (++20251221033036+2078da43e25a-...)` |
| `build/mesa/.../libgl_public.c.o` | the same |
| the titles | clang 21.1.8, from the WSL builder |

The build hash `2078da43e25a` is the llvm-project commit `llvmorg-21.1.8` - the same revision
`oops-apps/src/oops-deps/libcxx/upstream.lock` pins. The compiler and the C++ library it will
compile are one revision, which is what oops-apps#D005 wanted from the bump.

**A stale `meson-log.txt` is a trap worth naming.** It is the file anyone would reach for to
answer "which compiler built this", it is plausible, it is wrong, and nothing about it looks
stale. `readelf -p .comment` on an object answers the question properly.

## dri-probe: the hash is bit-exact

```
GL_VERSION   4.6 (Compatibility Profile) Mesa 26.2.2 (git-3281a69a8b)
GL_RENDERER  AMD Radeon Graphics (radeonsi, gfx1013, ACO, DRM 3.54)

first render: draw err=0x0 read err=0x0 | centre R=255 G=0 B=0 | corner R=64 G=128 B=191
glsl render:  compile vs=1 fs=1 link=1 draw err=0x0 | centre R=64 G=64 B=128
frame-hash 0x5188ddb7 | mod-pixels 373248 | centre-pixel 0xff404080 | corner-pixel 0xff0d0d14
```

| | clang 18 oracle | clang 21 | |
|---|---|---|---|
| frame hash | `0x5188ddb7` | `0x5188ddb7` | unchanged |
| modified pixels | 373248 | 373248 | unchanged |
| centre pixel | `0xff404080` | `0xff404080` | unchanged |
| corner pixel (the clear) | `0xff0d0d14` | `0xff0d0d14` | unchanged |

Every pixel of a 1920x1080 frame through Mesa's GLSL compiler, ACO, and the radeonsi state
emitter is identical across a three-major-version compiler change. Direct scanout still takes
both buffers (flip indices 2 and 3) and present is still a flush and a flip.

**`0x9dbfe189` is not this title's to answer.** Worklog 067 listed it beside `0x5188ddb7` as one
of the collection's two oracles, and it is gl1-cube's - oops-gl's own stack, no Mesa in it. The
distinction is already written down in
[the GLSL record](glsl-pipeline-and-frame-hash-fw1240.md); verifying it belongs to whoever owns
that title.

## mesa-cube: the pacing holds, and the cube is black

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

The spread is not noise and not a regression: the reports are 300-frame averages, and one dropped
frame in a window raises the average by `(33366 - 16683) / 300` = **55.6 us**. Every value
observed is `16683 + 55.6n`. Three windows landed on 16683-16685, which is n=0. So the title is
vsync-locked at 59.94 Hz and drops roughly one frame in 300, and the arithmetic says so rather
than the eye.

**And the cube draws as a solid black silhouette.** Shape, spin, depth test, cull, scanout and
pacing all exactly right; the overlay on top renders correctly; the colour is gone.

## Why that is not the compiler

It was minutes from being filed as a clang 21 regression, and the thing that stopped it was an
asset in another repository: `oops-apps/src/oops-mesa/mesa-cube/assets/demo.gif`, captured on
hardware at **19:57 on 2026-09-21** - after the clang-21 Mesa rebuild at 18:31 - showing the cube
correctly textured. And with no overlay on it.

So the compiler was already 21 when that frame was good, and the overlay is what arrived between.
The mechanism is a state leak, filed as `REQ-20260922T0040Z-c93d` against oops-sdk:
`oops_hud_create` ends with `glBindTexture(GL_TEXTURE_2D, 0)`, unbinding whatever the caller had
on the active unit, while `oops_hud_begin`/`end` a few lines later save and restore that same
state properly. The title binds its checkerboard once at setup and points its sampler at unit 0;
by the first frame the unit is empty, `texture()` returns zero, and the shader's shade term
multiplies it to black.

**The lesson is about the oracle, not the overlay.** `0x5188ddb7` held perfectly through a change
that visibly broke the other title on the same panel, because dri-probe does not sample a texture.
A hash is only an oracle for the path it covers, and the gap this one leaves is the texture unit.

## What this settles

- The clang 21 bump is **verified** for oops-mesa: identical output, identical pacing, no
  fallbacks, no faults.
- Unit 8's toolchain prerequisite is met on hardware, not just on the build machine.
- Nothing in oops-mesa is implicated in the black cube; both requests are filed and the fix is
  oops-sdk's.
