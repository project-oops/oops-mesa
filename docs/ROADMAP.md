# Roadmap

Units of work, in order. Each ends with a worklog entry and, where it touches the hardware, a
record with the fence, GPU clock and pixel hash the oops-gl instrument already produces. The
order is chosen so that the one measurement which decides the driver route (D003) is made
before the code that depends on its answer exists.

| unit | what it delivers | gate | state |
|---|---|---|---|
| 1 | the repository: pin, verbs, decisions, this file | `./bin/oops-mesa check` passes; the collection's decision and link gates pass | done 2026-09-14 |
| 2 | **the route measurement**: radeonsi's initial graphics state emitted through oops-gl's submission path, compositor watched, known frame hashed | pass or fail recorded per D003, with the system-log lines | done 2026-09-14: pass, both variants (worklog 002) |
| 3 | the container build: `toolchain/build-mesa.sh`, the meson cross file, the C-library headers staged from the checkout D004 pins, Mesa configured for the target with radeonsi and ACO only, no LLVM, static, one EGL platform | `./bin/oops-mesa build` reaches Mesa's first demand this platform cannot meet, and that demand is the next unit's work list | done 2026-09-14: 48 archives build for the target (worklog 005) |
| 4 | the runtime shim: the bound-name table, the thread-name mapping over the vendor thread API, Mesa's C11-threads layer over it, a host test for the mapping | `./bin/oops-mesa test` runs the host suite against the 26-row table in [the thread surface record](hardware/mesa-thread-surface-fw1240.md) | threads done 2026-09-14 (`src/runtime/threads.c`, worklog 011); the rest of the C-runtime surface waits for a link to name it |
| 5 | the winsys, standalone (an ioctl under libdrm carrying the 22 AMDGPU commands, the generic DRM ones and the syncobj family, D005; the skeleton is in src/winsys and every command refuses loudly): allocate, map, submit, fence over oops-sdk's bindings, exercised by a title that submits the oracle record's stream and gets the oracle record's hash | the oops-sdk oracle record reproduced through the winsys on firmware 12.40 | the startup path is closed as of 2026-09-17: every call from `amdgpu_get_auth` to the end of `ac_query_gpu_info` is answered or refused with the consequence known, tabulated in worklog 021. None of it has run. The gate as written needs submission, and submission sits behind syncobj wait and signal, which D007 leaves unwritten - so **the useful milestone before it is a non-null `pipe_screen`**, which worklog 022 establishes issues no ioctl past that table and allocates no GPU memory on this part |
| 6 | radeonsi bring-up (unit 2 passed; the fallback driver stays written down in D003): a triangle through EGL and OpenGL 3.3 into the display oops-sdk opens, flipped with the packet flip and retired on fence plus flip token | a title draws and hashes a known frame; the swap costs milliseconds, not the 508 ms the CPU path costs today | |
| 7 | the static SDK: `dist/` with headers, archives and `oops-mesa.mk`; the `USE_MESA` switch in oops-apps' `app.mk`; an example title | a title in oops-apps builds with `make title`, packages with SELFish, deploys with Prosperous, and renders | builds and packages 2026-09-15 (`oops-apps/src/mesa-probe`, worklog 015); deploying and rendering wait on the hardware questions |
| 8 | conformance: a bounded, named subset of the GL 3.3 CTS run on the hardware, results recorded as data | the subset and its pass table are in `docs/hardware/` with the build and firmware named | |

## What is deliberately not on it

- Any OpenGL entry point, shading-language work, or gallium driver code written here (D001).
- Firmware or console support claims beyond the one unit at this desk on firmware 12.40. Every
  hardware statement names its firmware, as the prior art does for 6.02 and 12.02.
- A desktop compatibility profile. Titles adapt their entry point, input and lifecycle to
  oops-sdk; the SDL2 question is a later decision if a port needs it.

## Requests already on the obSCEne bus that this project consumes

`REQ-20260914T1206Z-a1c3` (submit descriptor), `-b7e4` (initialisation gate),
`-1221Z-c5d9` (shader container), `-1245Z-e8a1` (register defaults) and `-1245Z-f3b2`
(linear pitch).

`REQ-20260914T1443Z-3ea7` is filed and is an **optimisation, not a gate**: does `libkernel`
serve the portable thread names on the app-sandbox leg, under either spelling, so the mapping
table can be skipped? Unit 4 no longer waits on it. Its thread surface is closed and every name
in it has a vendor twin, written down in
[the thread surface table](hardware/mesa-thread-surface-fw1240.md) with the arity trap that
comes with delegating. A negative answer costs nothing; a positive one makes unit 4 smaller.
