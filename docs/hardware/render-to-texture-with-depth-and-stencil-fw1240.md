# Render to texture with depth and stencil

Firmware 12.40, retail unit. Title `MDEM00001`, `mesa-demos` `fbotexture`, 2026-09-22.
Companion to [cube mapping](cube-mapping-draws-fw1240.md) and
[the advertised surface](the-advertised-surface-measured-fw1240.md).

`fbotexture` renders a scene into an FBO with three attachments, then samples the colour
attachment as a texture in a second pass to the window. The frame is correct.

## The frame

| on screen | source (`fbotexture.c`) |
|---|---|
| a shaded salmon teapot | `glutSolidTeapot(0.5)`, line 176, drawn into the FBO |
| a sharp flat-blue diamond cut out of the teapot | a diamond drawn under `glStencilFunc(GL_NEVER, 1, ~0)` with `GL_REPLACE` on fail (line 120), then `glStencilFunc(GL_NOTEQUAL, 1, ~0)` (line 139) rejects the teapot's fragments there |
| the blue inside the diamond | the FBO's `glClearColor(0.5, 0.5, 1.0)` |
| bright at the top, dark at the bottom | the window quad's vertex colour, `0.25` grey at the bottom vertices and white at the top (lines 226, 231), under the default `GL_MODULATE` |
| a grey border | the window's `glClearColor(0.25, 0.25, 0.25)` (line 217), a square quad in a 16:9 frustum |

## Features exercised

- `GL_EXT_framebuffer_object`: a 512x512 2D texture as colour attachment 0, reporting
  `GL_FRAMEBUFFER_COMPLETE` at creation and on the per-frame check (line 423). The demo's
  `Framebuffer incomplete!!!` line is absent.
- A 24-bit depth renderbuffer; the teapot's spout, handle and lid occlude correctly.
- A separate 8-bit stencil renderbuffer; the diamond edge is exact.
- Rendering into a texture and sampling it in a second pass.
- `glGetError` at the demo's five `CheckError` points prints nothing.
- `glutSolidTeapot` needs no GLU and draws on the Mesa path.

`fbotexture.c` line 61 is `static GLboolean Anim = GL_FALSE`: no idle function and no redisplay,
so the log carries exactly one `present us:` line.

## Presentation

`400x400` does not scan out (`dce-ft` opens and closes); the fallback takes the display's
`1920x1080`, where the buffers are 8847360 bytes with no metadata and go to direct scanout. The
single present: `flush=62585 read=0 mirror=0 disp=11615 total=74200`, the flush being first-frame
shader compilation.

## Keyboard input

`sceKeyboardInit` returns 0 and `sceKeyboardOpen` returns two handles;
`sceKeyboardSetProcessPrivilege(1)` and `sceKeyboardSetProcessFocus(1)` both return -1. The shell
logs `PostExternalKeyboardReceived UserId=0x1ea2f4d9 characterCode=0x32`, so keys reach the
system. No key reaches the title: in this build `OOPS_KEY_RECORD_FIELDS_CONFIRMED` in
`oops-sdk/src/input/keyboard.c` is 0, so `oops_keyboard_read`, the only route a character has into
a GLUT program, returns `OOPS_KEYBOARD_ELAYOUT` without calling `sceKeyboardReadState`.

`oops_keyboard_poll_buttons` reads the same record through the same call, and SeaShell uses it on
this hardware (`oops-apps/src/oops-utilities/seashell/home_main.c`), decoding 22 distinct HID
usage codes:

| field | offset | used by SeaShell |
|---|---|---|
| `intercepted` | 0x08 | yes |
| `connected` | 0x10 | yes |
| `keycodes[16]` | 0x20 | yes |
| `modifiers` | 0x1c | no |

The pad path is live: `oops-sdk/src/gl/glut.c` (line 369) maps Square to `' '`, Circle to
`'\033'`, Cross to `'\r'` and the D-pad to `GLUT_KEY_*`. 23 of the 56 demos bind space and 48
bind escape; 54 register `glutKeyboardFunc`.
