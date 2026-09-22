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

The title does not receive them. Confirmed by the owner the same evening: no key has any effect in
any demo, across all four runs.

**The cause is not on the console, and the two -1s above are a red herring.** `oops_keyboard_read`
- the function `glut_pump_keyboard` calls every frame, and the only route a character has into a
GLUT program - refuses on its third line:

```c
/* oops-sdk/src/input/keyboard.c:28 */
#define OOPS_KEY_RECORD_FIELDS_CONFIRMED 0
/* …:253 */
if (!OOPS_KEY_RECORD_FIELDS_CONFIRMED) {
    return OOPS_KEYBOARD_ELAYOUT;
}
```

No read is attempted. `sceKeyboardReadState` is never called, so what it would have returned, and
whether privilege and focus mattered, is unmeasured - the -1s are logged by `oops_keyboard_init`
and nothing stores, tests or returns them.

This is oops-sdk's **capture gate**, and it is the SDK behaving correctly rather than a bug:
`API_REFERENCE.md:421` describes the same arrangement for video and audio decode - *"the settled
interface but return `ELAYOUT` until an obSCEne struct-layout probe confirms the layouts"* - and
`src/input/mouse.c:24` carries it for the mouse. An interface that would have to guess at a struct
layout refuses instead, which is D001 applied to a record shape.

The open question is whether this particular gate is **stale**: eight lines below it the record is
introduced as *"Verified 96-byte PS5 native hardware keyboard report layout … hardware
measurements"*, with every offset spelled out. Either the comment overstates or the gate was never
flipped after the measurement landed. Asked as `REQ-20260922T1900Z-3f6a` (oops-sdk: which is it?)
and `REQ-20260922T1905Z-9c31` (obSCEne: capture the record, and the mouse's with it).

**The pad is not affected and is the way in.** `oops-sdk/src/gl/glut.c:369` synthesises GLUT
keyboard and special events from pad buttons, and OPTIONS - which ends every run on this title -
is three lines below the map in the same function, so the path is demonstrably live:

| button | delivers | reaches |
|---|---|---|
| Square | `' '` | **23 of the 56 demos** bind space |
| Circle | `'\033'` | 48 bind escape |
| Cross | `'\r'` | |
| D-pad | `GLUT_KEY_*` | |

That is seven characters against 54 demos that register `glutKeyboardFunc`, so most of the set's
mode switches stay out of reach. `REQ-20260922T1900Z-3f6a` asks oops-sdk for `glutOopsPostKey`,
which would let this title carry a build-time key script beside `DEMO=`; it cannot be done here,
because `oops-apps/common/app.mk:461` compiles every source in one command and the callback being
hooked is static inside `glut.c`.
