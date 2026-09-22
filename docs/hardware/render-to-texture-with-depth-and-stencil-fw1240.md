# Render to texture, with depth and stencil, correct on the first run

**2026-09-22** · firmware 12.40 · the same retail unit as the earlier records · `mesa-demos`
`fbotexture`, title `MDEM00001` · companion to
[cube mapping](cube-mapping-draws-fw1240.md) and
[the advertised surface](the-advertised-surface-measured-fw1240.md)

Every earlier run on this console drew one pass into the window. This one renders a complete
scene into off-screen memory with three attachments, then samples that memory as a texture and
draws it. Nothing in the frame is wrong.

## What is on screen, and why every part of it is the right answer

| what the picture shows | what produced it |
|---|---|
| a salmon teapot, shaded | `glutSolidTeapot(0.5)` at `fbotexture.c:176`, drawn **into the FBO** |
| a sharp-edged diamond of flat blue cut out of the teapot's side | the stencil pass: a diamond drawn under `glStencilFunc(GL_NEVER, 1, ~0)` with `GL_REPLACE` on fail, so it writes stencil and no colour (`:120`), then `glStencilFunc(GL_NOTEQUAL, 1, ~0)` (`:139`) rejects the teapot's fragments there |
| the blue showing through the diamond | the FBO's own `glClearColor(0.5, 0.5, 1.0)` - the teapot is absent, not painted over |
| the image bright at the top and dark at the bottom | the on-screen quad's per-vertex colour: `0.25` grey at the two bottom vertices, white at the two top (`:226`, `:231`), modulating the texture |
| a grey border around it | the window's `glClearColor(0.25, 0.25, 0.25)` (`:217`), with a square quad in a 16:9 frustum |

The gradient is the one worth pointing at. It is not a lighting artefact and not a present-path
problem: it is Gouraud-interpolated vertex colour multiplying a sampled texture, which means the
default `GL_MODULATE` texture environment is also being exercised and is right.

## What this settles

- **`GL_EXT_framebuffer_object`.** An FBO with a 512x512 2D texture as colour attachment 0,
  reporting `GL_FRAMEBUFFER_COMPLETE` both at creation and on the per-frame re-check at `:423`.
  The demo prints `Framebuffer incomplete!!!` when it is not; that line is absent.
- **A 24-bit depth renderbuffer**, allocated at the size asked for and attached. The teapot is
  correctly occluded against itself - spout, handle and lid resolve - so the depth test is live in
  the off-screen pass, not merely attached.
- **An 8-bit stencil renderbuffer**, as a *separate* renderbuffer rather than packed. The diamond
  edge is exact.
- **The round trip.** The texture that was rendered into is then bound and sampled in a second
  pass to the window. Off-screen and on-screen agree.
- **`CheckError` is silent.** The demo calls `glGetError` at five points and prints on any
  non-zero; nothing printed.

It also incidentally confirms a claim written into `oops-sdk/docs/PORTING.md` earlier the same
day: that `glutSolidTeapot` needs no GLU and therefore survives the Mesa path. It was reasoned
from reading `glut.c`; here it is drawn.

## One frame is correct

`fbotexture.c:61` is `static GLboolean Anim = GL_FALSE`. Animation is off by default, no idle
function is installed, and nothing posts a redisplay, so the log carrying exactly one
`present us:` line is the specified behaviour and not a stall. Anyone reading a quiet log from
this demo should check that line before diagnosing a hang.

## Presentation

`400x400` would not scan out - `dce-ft` opened and immediately closed - and the fallback took the
display's own `1920x1080`, where the buffers are 8847360 bytes with no metadata and go to direct
scanout. That is the third demo the fallback has carried. First and only present:
`flush=62585 read=0 mirror=0 disp=11615 total=74200`, the flush being first-frame shader
compilation.

## Keyboard: the system sees keys; whether the title does is still open

`sceKeyboardInit` returns 0 and `sceKeyboardOpen` returns two handles, but
`sceKeyboardSetProcessPrivilege(1)` and `sceKeyboardSetProcessFocus(1)` both return -1. Late in
this run the shell logged seven `PostExternalKeyboardReceived UserId=0x1ea2f4d9
characterCode=0x32` - a `2`, which this demo does not bind - so keystrokes reach the system and a
keyboard is attached.

That does not establish that the title receives them, and no run so far has demonstrated a demo
responding to one. **The cheapest test is this demo**: `a` toggles animation, so a single press
turns one present line into a stream. Until that is seen, treat "press a key to check X" as an
unproven method rather than a plan.
