# oops-mesa

**Upstream Mesa on Prospero-generation hardware, with nothing but a shim to maintain.**

OpenGL 3.3 Core and GLSL 3.30 for homebrew built on [oops-sdk](../oops-sdk/), provided by an
unmodified upstream Mesa pinned as a submodule. This repository owns only the pieces that differ
between a Linux box and the hardware: memory, submission and fences (the winsys), presentation
(the platform), and the C-runtime surface Mesa stands on. Mesa's OpenGL, GLSL compiler, hardware
driver, tiling library and shader backend are consumed, never edited in place.

Status: **skeleton**. Nothing renders yet. The roadmap below says what comes first and why.

## What is here

| path | what it is | who writes it |
|---|---|---|
| `mesa/` | upstream Mesa, submodule, pinned in `dependencies.mk` | upstream |
| `patches/` | numbered patches applied to Mesa at build time, each small and explained | us |
| `src/winsys/` | buffers, mapping, command submission, fences, over the vendor driver calls oops-sdk binds | us |
| `src/platform/` | presentation through the display the SDK opens, and the runtime layer Mesa stands on | us |
| `toolchain/` | the container build: image, cross file, the pinned C-library headers | us |
| `tools/` | host tools built against the pinned Mesa; `preamble-dump` prints the preamble radeonsi emits for this hardware, and its two outputs are tracked as data | us |
| `docs/` | decisions, roadmap, worklog | us |

Nothing in `src/` exists yet; the directories appear with the unit of work that fills them.

## How a title consumes it

Through a static SDK linked by the same `app.mk` every oops-apps title uses, packaged by
SELFish and deployed by Prosperous. The application sees standard EGL and OpenGL 3.3; the one
platform-specific value is the native window handle, which is the display oops-sdk already
opens. A title built this way is hosted, not freestanding: it carries the runtime layer this
repository provides. That is a different contract from the rest of oops-sdk and is stated
wherever the two could be confused.

## Read next

- [docs/ROADMAP.md](docs/ROADMAP.md): the units of work, in order, and the measurement that
  gates the route.
- [docs/DECISIONS.md](docs/DECISIONS.md): why a shim under Mesa rather than a driver of our own,
  what hosts Mesa, and which driver route is tried first.
- [docs/WORKLOG.md](docs/WORKLOG.md): what has been done, in order, with the surprises.
- [CLAUDE.md](CLAUDE.md): the constraints this project adds to the shared OOPS conventions.

## Verbs

```sh
./bin/oops-mesa check    # the submodule is at its pin and the tree has what CI expects
./bin/oops-mesa build    # fails loudly until the container build exists (roadmap unit 3)
./bin/oops-mesa test     # fails loudly until there is something to test
./bin/oops-mesa clean
```

## Provenance

Mesa is a dependency, MIT licensed, and stays a submodule. The hardware facts the shims rely on
come from oops-sdk's oracle records and obSCEne's measurements on the hardware, with public
sources cited beside them; see [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) for what was consulted
and the shared [OOPS conventions](../docs/CONVENTIONS.md) for the rule.

Licensed under MIT or Apache-2.0, at your option.
