# 043. The first GL call found a library nobody built, and a manifest that placed it in the vendor's

**2026-09-17** - roadmap unit 6

## What this entry is

`oops-apps/src/oops-mesa/dri-probe` exists: the first title that calls `oops_gl_create`. Worklog
041 and 042 both had to end with the same sentence - the platform shim compiles, links and
packages, and **nothing has ever called it** - so every statement about it was a statement about
source code.

Writing the thing that calls it found a bug of a kind this project has not hit before, and the bug
is more interesting than the title.

## What the probe does, and why it is a second title

`mesa-probe` calls `radeonsi_screen_create` directly. That is the winsys path and it is what
proved unit 5. `dri-probe` calls `oops_gl_create`, which goes through the frontend -
`driCreateNewScreen3`, a drawable, a context, `dri_make_current` - and the frontend creates **its
own** screen on the way through. One title doing both would have two screens and no clean
attribution for a failure, so they are separate and `mesa-probe` stays the control.

It then takes one step further than context creation, because that step costs nothing and
separates two failures that otherwise look identical: it calls `glGetString`. A context that is
current but cannot dispatch a GL call is a different problem from one that never became current,
and the frontend's dispatch is thread-local by construction - `_mesa_glapi_tls_Dispatch` - so this
is also the first thing to exercise the TLS model worklog 040 settled rather than merely link
against it.

It does not draw. `oops_gl_present` is expected to refuse and says why; it is called anyway,
because the flush half runs and reaching it means the frontend called back into the shim's
`getBuffers`, which is where Mesa allocates the colour buffer. A refusal there is a *successful*
exercise of the callback path.

## `glGetString` was undefined, and that was not a link error

The link succeeded. `glGetString` was an undefined symbol, which a title links with
`--unresolved-symbols=ignore-all` and resolves at load instead.

Nothing in the build defines it. Nothing defines `glClear` or `glViewport` either - **no archive
in this build carries a single public `gl*` name.** `libglapi.a` is built, is in
`link-order.txt`, and is not it: compiled `-DMAPI_MODE_SHARED_GLAPI` with hidden visibility, what
it carries is the dispatch machinery and 1,648 `_dispatch_stub_*` symbols. That is the inside of
GL, not its API.

The public entry points are in `libglapi_bridge` - `libgl_public.c` plus the generated table -
which upstream declares `build_by_default : false`, because the only target that normally wants it
is libGL, and libGL is a GLX thing this build has no reason to produce. So nothing asked for it,
ninja never built it, and the archive simply was not there.

Unlike EGL, there is nothing to work around: it is a `static_library` (D010 was about upstream
building EGL `shared_library()` regardless of `default_library=static`; this is not that). It only
had to be asked for.

## The part that matters: the manifest placed it in the platform's own OpenGL

`generate-imports.sh` reported **0 unknown**, and it was telling the truth. Every name found a
library. `glGetString` found this one:

```text
glGetString libSceGLSlimServerVSH,libSceGLSlimVSH,libScePigletv2VSH 0xc6a6e24bb3ccad21 ... fn
```

`libSceGLSlimServerVSH` is the platform's own PS4-era OpenGL. Five independent name lists back
that row, so the corpus was not guessing - it was answering a question nobody should have asked.
A title built from that manifest would have loaded and called **the vendor's GL** instead of
Mesa's, or trapped on a library that is not resident. Neither outcome looks like a build problem
from the console, and neither would have been easy to read in a log.

**"0 unknown" is not a safety property.** The corpus having an answer and the answer being right
are different things, and every previous entry in this worklog that leaned on that count was
leaning on less than it thought. That is the real finding here; the missing archive is just the
first instance of it.

### So the check now refuses these names before the corpus is consulted

`generate-imports.sh` fails, loudly, on any undefined symbol in Mesa's or this project's own
namespace - `gl[A-Z]`, `_mesa_`, `dri[A-Z_]`, `radeonsi_`, `ac_`, `aco_`. It is a statement about
ownership rather than a heuristic about intent: there is no circumstance in which a title should
resolve `glGetString` or `radeonsi_screen_create` from a platform library, so if one is undefined
an archive is missing from the link and importing it is always the wrong repair. The message says
that, and says to check whether the defining target was built at all.

Tested both ways: it passes on both titles as they stand, against 209 real undefined symbols, and
it catches `glGetString`, `_mesa_error`, `radeonsi_screen_create` and `driCreateNewScreen3` while
leaving `malloc`, `snprintf` and `ioctl` alone.

## Why the bridge is taken whole, and what that revealed about the link

`--whole-archive`, which is not the usual answer and is not laziness.

A title's own sources are placed **after** the archives on the link line, and a static archive
member is only pulled in to satisfy a reference the linker has already seen. So a symbol
referenced only by the title and defined only in an archive is never pulled. Nothing inside Mesa
references `glGetString` on a title's behalf, so on-demand linking cannot work here however the
order is arranged.

That also explains something that had been read as ordinary: `radeonsi_screen_create` resolves for
`mesa-probe` **not** because the title references it, but because `dri_target.c.o` - first in the
link order - happens to reference it too, which pulls the member before the title's object is ever
scanned. Worklog 041 noticed that object mattered and got the reason slightly wrong; this is the
full version.

The deeper fix is to reorder `app.mk` so every title's objects precede the archives. That is not
done here, because it changes the link of every title in oops-apps to solve a problem one of them
has. What `app.mk` did get is one line: `PAYLOAD_EXTRA_DEPS` now filters flags out of
`OOPS_MESA_LIBS`, so that list can carry the `--whole-archive` pair without make treating a flag
as a file it has no rule for.

## Numbers

| | |
|---|---|
| `libglapi_bridge.a` | 1,300 public entry points, one undefined name (`_mesa_glapi_tls_Dispatch`, which `libglapi.a` defines) |
| `glGetString` in `dri-probe` | was: imported from `libSceGLSlimServerVSH`. now: **defined, from Mesa** |
| `mesa-probe` module | 28,252,848 → 28,494,144 bytes (+241 KB, the entry points) |
| `dri-probe` module / eboot | 28,492,840 / 26,630,944 bytes |
| imports, both titles | 480 placed, **0 unknown**, and now also 0 owned-namespace names |

Both titles package. `./bin/oops-mesa check` passes, pin `mesa-26.2.2`, 2 patches. Identity guard
clean on oops-mesa and oops-apps.

## A mistake worth recording, because it was my own rule

Halfway through this I ran `make imports` **after** `make title` and got a 400-line manifest
instead of 480 - the shared list and nothing else. The fixup rewrites `build/<app>.elf` in place
into a module, so `nm` cannot read it and finds no undefined symbols at all, and the generator
dutifully wrote a manifest for a binary that imports nothing. Both titles were briefly carrying
it.

The order is fresh link, then `make imports`, then `make title`, and it is written down in
`mesa-probe`'s Makefile precisely because this has happened before. Both titles were rebuilt in
that order and are correct now.

**A comment was evidently not enough, so the generator now refuses instead.** A title that links
Mesa cannot import nothing - it needs `malloc` at the very least - so an empty undefined list is
proof the read failed rather than a result. `generate-imports.sh` checks `nm`'s exit status,
refuses an empty list, quotes what `nm` said, names the likely cause and gives the order to
follow. Tested both ways: exit 1 with no manifest left behind against a fixed-up module, and the
normal sequence still produces 480 lines and packages.

That is two guards added in one entry, both against the same shape of failure - a tool reporting
success for a question it could not actually answer.

## What has not happened

Neither title has run. The console still carries the 17:15 module from before any of today's
work - libm, the runtime names, the C++ ABI change, the GL entry points and this title are all
unexercised on hardware.

`dri-probe` is the run that would answer the most at once, which is also the argument against
doing it casually: if it stops, the candidates now include the frontend, the config choice, the
drawable, the context, make-current, the TLS dispatch and the first GPU allocation this path has
ever attempted. The probe is built to name which, one line per step, and that is the point of it.
