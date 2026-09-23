# Shadow mapping draws, and it chose the assembly shader path to do it

**2026-09-23** · firmware 12.40 · the same retail unit as the earlier records · `mesa-demos`
`shadowtex`, title `MDEM00001` · follows
[the frame-3 fault, resolved](third-frame-with-an-fbo-faults-the-gpu-fw1240.md)

The first demo run after the command-buffer chain fix, and the first depth-comparison sampling on
this console.

## What the driver picked, in its own words

`shadowtex` probes for several paths and announces the one it takes:

```
Using GL_ARB_depth_texture
and GL_ARB_shadow
and GL_ARB_fragment_program
Rendering 1024 x 1024 depth texture
```

| | |
|---|---|
| `GL_ARB_depth_texture` | a depth buffer used as a texture |
| `GL_ARB_shadow` | sampling it through a **comparison** rather than a fetch - `TEXTURE_COMPARE_MODE` |
| `GL_ARB_fragment_program` | **the assembly shader path**, `glProgramStringARB`, which nothing here had touched |
| 1024x1024 depth texture | rendered every frame, so this is a real render-to-texture load |

`GL_ARB_fragment_program` is the one worth dwelling on. It is neither fixed-function nor GLSL: it
is the pre-GLSL assembly program interface, and radeonsi compiles it through the same back end. The
demo offers a fixed-function arm and took this one instead, so the choice is the driver's and the
run is evidence for a path no probe here had exercised.

## The run

33 presents, **zero GPU faults, zero GL errors**, settling at 4.46 ms a frame after the first:

```
present us: flush=4455 read=0 mirror=0 disp=11 total=4466
```

The picture is the demo's own scene: a blue ground plane, a green sphere, a yellow and a red
solid, and a cast shadow on the plane whose shape follows the geometry above it.

## Two things this is not

**It did not exercise the chain fix.** `shadowtex` logs **zero** chained command buffers - it
never outgrows its buffer - so it would have run before
[`46303e4`](third-frame-with-an-fbo-faults-the-gpu-fw1240.md) as well. The fix matters to
`fbotexture`, which chains about thirteen times a frame; this record is not evidence for it.

**It is not a statement that the shadow is *right*.** A shadow is present, has structure
consistent with the objects casting it, and moves with the scene. Whether it lands where it
should, and whether the Z bias is correct, has not been compared against the same demo on a
desktop. The demo carries the keys to settle it - `i` shows the depth texture image, `m` the
depth texture mapping, `n` the shadowed view, `b`/`B` adjust the bias - and the keyboard works as
of `oops-sdk` `382321d`, so that is a run away whenever the answer is wanted.
