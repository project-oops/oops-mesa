# Acknowledgements

oops-mesa is a shim under an unmodified upstream Mesa. It contains no code from the projects
below other than Mesa itself, which is a pinned dependency and stays a submodule. Everything
else is credited as a **reference that was read**: a fact taken from it is recorded with its
source and confirmed on the hardware before the shims rely on it. Reading is not lifting; see
the shared [OOPS conventions, section 1](../docs/CONVENTIONS.md#1-provenance-is-a-hard-boundary).

## Dependency

- **Mesa 3D** (MIT and per-file licences), pinned at the commit in `dependencies.mk`. The GL
  API, GLSL, NIR, the AMD driver, the address library and the ACO shader backend are Mesa's.

## Prior art read for candidates, 2026-09-14

Read in full in the prior-art audit (orbistoun worklog 541) and its addendum. All are
GPL-3.0-or-later and none of their code is here; each hardware fact they confirmed or
contradicted is listed in that worklog with repository, commit and path.

- `mpereiraesaa/ps5-agc-gears` at `3b9b69f601939449faf20d32b80e680cf9ff441f`: the presentation
  model (tiled scanout buffers, a flip packet in the command stream, retirement on fence plus
  flip token), the depth register block, the shader-package header layout, and reading the
  vendor library's own register defaults instead of deriving them.
- `mpereiraesaa/ps5-vulkan` at `43e287e09591a094bc6591bcb73e192466782ec1`: descriptor encoding,
  the 64KB_R_X de-tiling equations' existence, the suspend point after a submit.
- `mpereiraesaa/ps5-xash3d-halflife` at `be0731b4fcc4233349bba5e0d3ff33c79428f695`: the linear
  image descriptor including the pitch field, the cache-acquire contract after CPU writes.
- `mpereiraesaa/ps5-homebrew-lab` at `843c40511e0b79b7e50054f77e40b40512e08712`: the two-quad
  depth litmus and the lab's own provenance notes.
- `blackbearreloaded/ps5-opengl` at `7f9bfabdddb187a11e4401058eba8c9e55194d0a`: the shape of a
  gallium driver and native backend on this hardware, the scanout buffer sizing, render-target
  barrier events, and the direct-memory type it allocates with. It is the closest prior art to
  this project and the reason this project is a shim rather than a driver.

## Public references not yet consulted

- **OpenGNM** (MIT): named by ps5-opengl as the source of shader-container declarations. It
  will be credited here with a commit when it is read.
- **FreeBSD** (BSD-2-Clause): the operating system the hardware's kernel derives from; its C
  library headers are the candidate compile-time interface for the runtime shim (D002). The
  pinned revision will be recorded here when chosen.

## Measurements this project stands on

- oops-sdk `docs/hardware/agc-gl-cube-oracle-fw1240.md`: the register, descriptor and packet
  facts measured on firmware 12.40, with fence, GPU clock and pixel hash.
- obSCEne's hardware census (`data/hardware/ps5-imports.txt`, 2026-08-30): which C-library
  names bind on the hardware.
