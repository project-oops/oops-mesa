# D001 - A shim under upstream Mesa, not a driver of our own


**decided** · 2026-09-14

Found by the prior-art audit (orbistoun#541): five public repositories render on this
hardware, and the most complete of them is an unmodified Mesa with a hardware layer beneath it.
The owner's requirement, stated the same day, was a usable OpenGL for homebrew built on oops-sdk
"without maintaining thousands of lines", with Mesa "as a submodule and nothing but a shim".

## The choice

oops-mesa owns three shims and a patch set, and nothing else:

- **winsys**: buffers, mapping, command submission and fences, over the vendor driver calls
  oops-sdk already binds by published name;
- **platform**: presentation into the display oops-sdk opens;
- **runtime**: the C-library surface Mesa stands on (D002).

Mesa is a submodule pinned in `dependencies.mk` and is never edited in place. What must change
inside Mesa is a numbered patch under `patches/`, small, explained in its header, re-applied on
every bump. The OpenGL API, the GLSL compiler, the hardware driver, the tiling library and the
shader backend are Mesa's and stay Mesa's.

This repository supersedes oops-gl for anything a user runs. oops-gl stays in oops-sdk as the
fixed-function measuring instrument it is, and its oracle records become this project's
hardware tests. The two never link into the same title.

## Why

- **The surface is Mesa's problem by design.** Mesa was built around exactly this seam: the
  driver asks the platform for buffers, a submit and a fence, and everything above that is
  generic. A shim is the honest name for the piece that fits that seam.
- **A driver of our own is the expensive route.** The closest prior art carries an
  11,000-line gallium driver plus a 5,500-line native backend, and Mesa already ships the AMD
  driver that code re-implements. Writing another is a choice to maintain what upstream
  maintains.
- **Everything the shims need is a measured fact.** Register semantics, descriptor layouts,
  packet formats and the driver's calling conventions are what oops-gl and obSCEne produce with
  records. This project is where those records get consumed.
- **Licence.** Mesa is MIT. The prior art is GPL and can only be read. A shim written from our
  own measurements under MIT/Apache is the only shape that stays distributable with the rest of
  the collection.

## What this rules out

Writing GL entry points, a shading-language compiler, or a gallium driver in this repository.
A change that needs any of those is either an upstream contribution or a sign the design is
wrong, and either way it is a new decision, not a patch.
