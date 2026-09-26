# Worklog

One entry per milestone. Between milestones, commit messages are the record.

## 2026-09-14 - Mesa cross-compiles for the target

- Upstream Mesa 26.2.2 builds in the container against a FreeBSD sysroot, with libdrm patched
  to call the winsys shim.
- radeonsi's GFX10 preamble runs on the platform's graphics queue, with and without `CLEAR_STATE`.
- On GFX10 radeonsi leaves scissor, blend, depth and rasteriser defaults to `CLEAR_STATE`; on
  GFX11 it writes them explicitly.
- `libScePosix` does not load in the app sandbox, so thread names are bound from `libkernel`.
- Mesa's only thread dependency is `src/c11/impl/threads_posix.c`.
- An installed FreeBSD header set is not a copy of `include/`: `include/Makefile` links headers
  from `sys/sys` and `sys/`, and `math.h` ships with the maths library.
- Against a headers-only sysroot a meson `links()` answer shows the mechanics work, not that the
  platform exports the symbol.

## 2026-09-15 - A title links all of Mesa

- A title links every Mesa archive, both shims, the C++ support and oops-sdk into one module.
- Mesa and libdrm both define `handle_table_remove`. Each archive is partially linked into one
  object and then localized; localizing per object breaks libdrm's own cross-object calls.
- `-ffreestanding` is dropped and `-nostdlib` kept; a hosted title compiles with `--sysroot`.
- `libSceLibcInternal` on firmware 12.40 does not export `getenv`, so it is defined locally and
  answers null for every name.
- An import placed in a library that does not export it links and packages, then dies on the
  first call with `PRX_NOT_RESOLVED_FUNCTION`.
- An export sweep on the eboot leg reads zero even for its positive controls; only the payload
  leg answers export questions.

## 2026-09-17 - radeonsi creates a screen on hardware

- `radeonsi_screen_create` returns a screen on firmware 12.40; the capture is
  [docs/hardware/screen-created-fw1240.md](hardware/screen-created-fw1240.md).
- Every symbol a Mesa title imports resolves to an address on the native leg.
- `GB_ADDR_CONFIG` cannot be read from userland (a COPY_DATA of it faults as privileged). Its
  value `0x00000004` is derived by inverting AddressLib against oops-sdk's tiler.
- libdrm's first statement is `DRM_IOCTL_GET_CLIENT`, and radeonsi creates a syncobj before it
  queries the device at all (D007).
- Creating a screen issues no GPU work and allocates no GPU memory.
- The loader honours `PT_TLS` in a title's own image. Mesa is built `-ftls-model=initial-exec`:
  local-exec fails in a `-shared` title and general-dynamic calls a `__tls_get_addr` that faults.
- `write(1)` and `write(2)` in an eboot title succeed and go nowhere, so Mesa's stdio is routed to
  klog.
- The obSCEne import census was captured in the PS4 compatibility container (GEN=4); presence in
  it does not mean an import binds on native firmware.
- No userland call ends a big-app process: `_exit` raises `SIGSYS` and returning from the entry
  point faults at `rip 0`. A title prints its last line and parks.
- `sysconf` refuses the processor counts, and Mesa then sizes its pools for one CPU.

## 2026-09-19 - The DRI frontend runs radeonsi and the GPU executes its shaders

- The Gallium DRI frontend creates a radeonsi screen and a context, and the first submission
  retires.
- A zeroed `sce_process_param` libc block selects libc's small internal heap; Application Heap
  Mode with an extendable heap is required for Mesa.
- radeonsi places command buffers in the high VA range unconditionally; an empty range fails the
  first context.
- libdrm sends `GEM_VA` as `_IOWR` (`0xc0406448`) although the header macro says `_IOW`.
- `sceAgcDriverCreateQueue` and a submit both return 0 without `sceAgcInit`, but the queue never
  executes. Graphics work needs a type 0 queue and `sceAgcDriverSubmitCommandBuffer`.
- `oops_mem_alloc` memory is write-back, so a command stream is flushed from the CPU cache
  before submission.
- A GPU wavefront fault kills the title and frees the app slot; the console stays healthy.
- A parked title emits one burst of log lines, so the log listener is started before launch.

## 2026-09-20 - A GL context renders and reads back pixel-exact

- `glGetString` reports `4.6 (Compatibility Profile) Mesa 26.2.2` on `gfx1013, ACO, DRM 3.54`.
- A clear reads back 64/128/191/255 and a triangle's centre and corner pixels are exact.
- Two buffers mapped at one CPU address overwrite each other's shader upload. libdrm's
  `drm_munmap` is a no-op, so a buffer keeps its CPU address until it is closed.
- A title's `.init_array` must be run at start-up, or ACO's opcode table is zero and every
  instruction assembles as opcode 0 (which decodes as GFX11 code).
- The same `.init_array` holds GLSL's built-in tables and the compiler runtime's CPU detection.
- `driSwapBuffers` calls a function pointer that only the swrast and kopper loaders set; an image
  loader presents with `dri_flush_drawable`.

## 2026-09-20 - First frame on the panel, and the GLSL pipeline

- A frame rendered by radeonsi appears on the panel through oops-sdk's display.
- A GLSL 330 program with a VBO draws; interpolation and coverage match the analytic values to
  the pixel, and the frame hash is `0x5188ddb7`
  ([docs/hardware/glsl-pipeline-and-frame-hash-fw1240.md](hardware/glsl-pipeline-and-frame-hash-fw1240.md)).
- Mesa creates every thread with a null attribute, and the platform's default stack overflows in
  the GLSL linker; the runtime shim gives such threads 8 MB.
- `glReadPixels` rows are bottom-up and scanout rows are top-down.
- Closing the display releases the scanout and blanks the panel.

## 2026-09-21 - Direct scanout at 60 fps with no CPU pixel work

- The display scans radeonsi's own two colour buffers; `mesa-cube` holds 59.94 fps with no
  readback ([docs/hardware/the-present-path-measured-fw1240.md](hardware/the-present-path-measured-fw1240.md)).
- Through the readback path the GPU drew a frame in 4.2 ms and the CPU spent 32.5 ms delivering it.
- `sceVideoOutRegisterBuffers2` does not constrain a buffer's address, but registration is
  single-shot: a second call returns `SLOT_OCCUPIED` and unregistering is not exported (D009).
- A radeonsi colour buffer carries 49152 bytes of displayable DCC unless it is created for front
  rendering; the display scans DCC as raw pixels.
- A buffer is offered for scanout only when its size equals the plain `64KB_R_X` size.
- radeonsi's 1080p `SW_MODE 27` layout matches oops-sdk's display tiler at every pixel.
- The flip queue is 26 deep; a title that presents faster than 60 Hz fills it and flips are
  refused, so present waits for scanout after each flip.
- A title that presents once measures only the first flip, which costs milliseconds; a
  steady-state flip costs microseconds.
- `dri2_query_image` leaves its output untouched on failure, so an unset modifier reads as linear.

## 2026-09-22 - clang 21 verified on hardware

- Mesa built with clang 21 reproduces the frame hash `0x5188ddb7`, and `mesa-cube` holds 59.94 fps
  (D013; [docs/hardware/the-clang-21-bump-verified-fw1240.md](hardware/the-clang-21-bump-verified-fw1240.md)).
- The pinned libc++ compiles under clang 21 except sources for other platforms, the opt-in
  time-zone database and `charconv.cpp`, which needs llvm-project's `libc/shared/fp_bits.h`.
- `meson-log.txt` keeps the compiler recorded at configure time; `readelf -p .comment` on an
  object shows the compiler that built it.
- The frame hash covers no texture sampling and cannot detect a texturing fault.
- radeonsi advertises 322 extensions, including sparse and interop, which the winsys refuses, and
  compute, which runs on the graphics ring.

## 2026-09-23 - Framebuffer objects animate

- `fbotexture`, cube mapping, shadow mapping, 3D textures and assets read from `/app0/data/`
  render on hardware.
- The command processor reads only the declared range of a submitted command buffer and cannot
  follow radeonsi's `INDIRECT_BUFFER` chain; the winsys walks the chain in software.
- No platform submit entry point takes a wait, fence or dependency argument.

## 2026-09-25 - The Khronos CTS runs on the hardware

- VK-GL-CTS `opengl-cts-4.6.8.1` runs unmodified; `KHR-GL30.info` passes 6 of 6
  ([docs/hardware/the-khronos-cts-runs-fw1240.md](hardware/the-khronos-cts-runs-fw1240.md)).
- `F_DUPFD_CLOEXEC` fails with `EOPNOTSUPP` and `F_DUPFD` works; the frontend's dup falls back on
  that errno.
- Escaping the sandbox to reach `/data` repoints the process root, and `/app0` then names
  nothing, so the device descriptor is claimed from `.init_array`.
- A title's link script keeps `.eh_frame`, or every C++ throw is fatal.
- A descriptor from libc's `open()` on `/data` accepts writes, reports success and stores
  nothing; `oops_fs` writes through `SYS_open` and persists.
- The CTS's GL dispatch is thread-local and is compiled initial-exec, like Mesa's.
