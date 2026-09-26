# Shadow mapping through ARB_fragment_program

Firmware 12.40, retail unit. Title `MDEM00001`, `mesa-demos` `shadowtex`, 2026-09-23, a build
including the command-buffer chain fix of
[the frame-3 record](third-frame-with-an-fbo-faults-the-gpu-fw1240.md).

## The path the demo takes

`shadowtex` probes for several paths and prints the one it takes:

```
Using GL_ARB_depth_texture
and GL_ARB_shadow
and GL_ARB_fragment_program
Rendering 1024 x 1024 depth texture
```

| | |
|---|---|
| `GL_ARB_depth_texture` | a depth buffer used as a texture |
| `GL_ARB_shadow` | sampled through `TEXTURE_COMPARE_MODE` comparison |
| `GL_ARB_fragment_program` | the assembly program interface, `glProgramStringARB`, compiled by radeonsi through the same back end as GLSL; the demo offers a fixed-function arm and does not take it |
| 1024x1024 depth texture | rendered every frame |

## The run

33 presents, zero GPU faults, zero GL errors, 4.46 ms a frame after the first:

```
present us: flush=4455 read=0 mirror=0 disp=11 total=4466
```

The frame shows the demo's scene: a blue ground plane, a green sphere, a yellow and a red solid,
and a cast shadow on the plane that follows the geometry and moves with the scene.

`shadowtex` logs zero chained command buffers, so this run does not exercise the chain fix.

The shadow's placement and Z bias are not compared against the same demo on a desktop. The demo's
keys `i` (depth texture image), `m` (depth texture mapping), `n` (shadowed view) and `b`/`B`
(bias) show them.
