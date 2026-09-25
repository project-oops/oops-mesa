# CLAUDE.md

Agent rules for oops-mesa. Read the root [AGENTS.md](../AGENTS.md),
[CONVENTIONS](../docs/CONVENTIONS.md) and [STYLE](../docs/STYLE.md) first; this file only
adds to them.

## Scope

Upstream Mesa running on Prospero-generation hardware for homebrew built on oops-sdk. This
repository owns only the shims that differ from a Linux host. Mesa provides OpenGL; none of it
is written here.

## Mesa submodule

- Mesa is a submodule and stays unmodified in place. The pin is in `dependencies.mk`.
- A change to Mesa is a numbered patch under `patches/`, applied at build time, with a header
  saying what it changes and why it cannot be a shim.
- A patch longer than a screen belongs in a shim or upstream.
- The pin and the patch set are one unit: bumping the pin means re-applying the patches and
  re-running the hardware tests.

## Three shims

- **winsys**: buffers, mapping, submission, fences.
- **platform**: presentation to the display oops-sdk opens.
- **runtime**: what Mesa asks of a C library, mapped onto the SDK and the platform's own C
  library.
- A change that fits none of the three needs a decision before the code.
- The GL API, the GLSL compiler, the hardware driver, the tiling library and the shader
  backend are Mesa's. A bug in them is reported upstream, not worked around here.

## Hardware facts

- Every hardware fact the shims use (register semantics, descriptor layouts, packet formats,
  driver calling conventions) is a measurement with a cited source.
- The source is a stable pointer: a Mesa file and line, a `data/` or `docs/hardware/` record,
  or a test. Never a request or worklog ID.
- Preference order: an oops-sdk oracle record, then an obSCEne measurement, then a public
  source.
- A fact first seen in another project's source is a candidate until a run on this hardware
  confirms it.

## Failure

- A frame that did not retire is a failure, never a fallback to software rendering. This
  differs from the collection's CPU-rasterization fail-safe: a software frame would hide the
  fault this project exists to find.

## Hosted titles

- A title linking oops-mesa carries a C runtime and is not freestanding. The SDK fragment and
  the packaging label it hosted.
- No oops-sdk document implies hosted and freestanding titles are interchangeable.
- This divergence from the collection's freestanding rule is confined to titles that link
  this project (D002).

## Toolchain

- The Mesa build runs in the image described by `toolchain/Dockerfile`.
- WSL `oops-builder` builds the shims' host tests.
- The hardware is reached through Prosperous.
- C code is formatted with the collection `.clang-format` ([STYLE section 2](../docs/STYLE.md#formatting)),
  and `./bin/oops-mesa check` runs it.

## Start here

```sh
./bin/oops-mesa check     # pin, patches, documents, formatting
docs/ROADMAP.md           # intended order of work
```
