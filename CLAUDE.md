# CLAUDE.md

How oops-mesa is built and the constraints to honour when changing it.

**Read [the master agent instructions](../AGENTS.md) and [the OOPS conventions](../docs/CONVENTIONS.md) first.**
Provenance, naming, decision logs, worklogs and gates are shared across the collection and
centralised in the parent repository. This file holds only what oops-mesa adds.

## Mission, in one breath

Upstream Mesa running on Prospero-generation hardware for homebrew built on oops-sdk, with
this repository owning only the shims that differ from a Linux host: memory and submission,
presentation, and the C-runtime surface. Mesa provides OpenGL; we never write any of it.

## Principles

### 1. Mesa is a submodule and stays unmodified in place

The pin lives in `dependencies.mk`. Changes to Mesa are numbered patches under `patches/`,
applied at build time, each with a header saying what it changes and why it could not be a
shim. A patch that grows past a screen is a sign the change belongs in a shim or upstream.
Bumping the pin means re-applying the patches and re-running the hardware tests; the pin and
the patch set are one unit.

### 2. Three shims, nothing else

The winsys (buffers, mapping, submission, fences), the platform (presentation to the display
oops-sdk opens) and the runtime (what Mesa asks of a C library, mapped onto the SDK and the
platform's own C library). If a change does not fit one of the three, stop and write a decision
before writing the code. The GL API, the GLSL compiler, the hardware driver, the tiling library
and the shader backend are Mesa's; a bug in them is reported upstream, not patched around here.

### 3. Every hardware fact is a measurement with a citation

The shims program the hardware through facts: register semantics, descriptor layouts, packet
formats, the driver's calling conventions. Each one must trace to an oops-sdk oracle record, an
obSCEne measurement, or a public source, in that order of preference, and says which in a
comment. A fact first seen in another project's source is a candidate until a run on this
hardware confirms it (orbistoun worklog 541 is the worked example). Nothing here is read from a
vendor binary.

### 4. Honest failure over plausible output

A verb with nothing behind it exits 1 and says what is missing (oops-sdk#D001). A build that
cannot reach the hardware says so. A frame that did not retire is a failure, never a fallback to
software: this is the same rule oops-gl adopted after its badge lied (orbistoun worklog 539).

### 5. Hosted titles are labelled hosted

A title linking oops-mesa carries a C runtime and is not freestanding. The SDK fragment says
so, the packaging says so, and no oops-sdk document may imply the two kinds of title are
interchangeable. This is a stated divergence from the collection's freestanding rule, recorded
in D002, and it is confined to titles that link this project.

## Toolchains

Container first, as everywhere in the collection: the Mesa build runs in the image described
by `toolchain/Dockerfile`. WSL `oops-builder` builds the shims' host tests; the hardware is
reached through Prosperous. Never install a toolchain machine-wide for this project.

## Start here

```sh
./bin/oops-mesa check     # is the tree sound: pin, patches, documents
docs/ROADMAP.md           # what is next, in order, and the measurement that gates the route
```

Write a decision as a choice is made, a worklog entry when a unit of work completes, and cite
sibling projects' entries with the repository prefix (`orbistoun#541`, `oops-sdk#D005`).
