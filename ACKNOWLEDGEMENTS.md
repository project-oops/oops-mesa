# Acknowledgements

oops-mesa is a shim under an unmodified upstream Mesa. It contains no code from the projects
below other than Mesa itself, which is a pinned dependency and stays a submodule. Everything
else is a reference that was read: a fact taken from it is recorded with its source and
confirmed on the hardware before the shims rely on it (the shared
[OOPS conventions, section 1](../docs/CONVENTIONS.md#1-provenance-is-a-hard-boundary)).

## Dependencies

- Mesa 3D (MIT and per-file licences), pinned at the commit in `dependencies.mk`. The GL
  API, GLSL, NIR, the AMD driver, the address library and the ACO shader backend are Mesa's.
- FreeBSD (BSD-2-Clause), the operating system the hardware's kernel derives from. Its C
  library headers are the compile-time interface the runtime shim builds against (D002,
  D004), staged from `freebsd-src` at the revision `dependencies.mk` pins. Its `lib/msun`
  and locale table are compiled for the target at build time (D011); neither is tracked here.

## Prior art read for candidates

Read in orbistoun's prior-art audit. All are GPL-3.0-or-later and none of their code is here.

- `mpereiraesaa/ps5-agc-gears` at `3b9b69f601939449faf20d32b80e680cf9ff441f`: the presentation
  model (tiled scanout buffers, a flip packet in the command stream, retirement on fence plus
  flip token), the depth register block, the shader-package header layout, and reading the
  vendor library's own register defaults instead of deriving them.
- `mpereiraesaa/ps5-vulkan` at `43e287e09591a094bc6591bcb73e192466782ec1`: descriptor encoding,
  the existence of the 64KB_R_X de-tiling equations, the suspend point after a submit.
- `mpereiraesaa/ps5-xash3d-halflife` at `be0731b4fcc4233349bba5e0d3ff33c79428f695`: the linear
  image descriptor including the pitch field, the cache-acquire contract after CPU writes.
- `mpereiraesaa/ps5-homebrew-lab` at `843c40511e0b79b7e50054f77e40b40512e08712`: the two-quad
  depth litmus and the lab's own provenance notes.
- `blackbearreloaded/ps5-opengl` at `7f9bfabdddb187a11e4401058eba8c9e55194d0a`: the shape of a
  gallium driver and native backend on this hardware, the scanout buffer sizing, render-target
  barrier events, and the direct-memory type it allocates with.

## Measurements this project stands on

- oops-sdk `docs/hardware/agc-gl-cube-oracle-fw1240.md`: the register, descriptor and packet
  facts measured on firmware 12.40, with fence, GPU clock and pixel hash.
- obSCEne's hardware census (`data/hardware/ps5-imports.txt`): which C-library names bind on
  the hardware.
