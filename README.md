# oops-mesa

Upstream Mesa on Prospero-generation hardware, for homebrew built on
[oops-sdk](../oops-sdk/). Mesa is an unmodified submodule; this repository owns only what
differs between a Linux host and the hardware (D001):

| path | what it is |
|---|---|
| `mesa/` | upstream Mesa, pinned in `dependencies.mk` |
| `patches/` | numbered patches applied to Mesa at build time, each with a header saying why |
| `src/winsys/` | buffers, mapping, command submission and fences under libdrm (D005, D007) |
| `src/platform/` | a DRI loader that creates GL contexts and presents to the display oops-sdk opens (D009, D010) |
| `src/runtime/` | the C runtime Mesa stands on: thread mapping, absent-libc definitions, C++ support (D002, D006) |
| `toolchain/` | the container build: image, meson cross file, staging scripts |
| `tools/` | `preamble-dump` and `tiling-compare` (host tools whose outputs are tracked data), `generate-imports.sh`, `what-is-still-needed.sh`, `format.sh` |
| `docs/` | [GL surface](docs/GL_SURFACE.md), [decisions](docs/DECISIONS.md), [roadmap](docs/ROADMAP.md), [worklog](docs/WORKLOG.md), hardware records |

radeonsi reports `GL_VERSION` 4.6 on this part; the project claims what the Khronos CTS and
the hardware tests measure (D014), and [GL_SURFACE](docs/GL_SURFACE.md) maps the advertised
surface onto the shims.

## Build

```sh
./bin/oops-mesa check    # pin, patches, documents, formatting, generated tool outputs
./bin/oops-mesa fmt      # format first-party C and C++
./bin/oops-mesa build    # stage the sysroot and sources, then build Mesa in the container
./bin/oops-mesa test     # the shims' host suite
./bin/oops-mesa clean
```

The build needs Docker and the FreeBSD source checkout orbistoun uses, located by
`OOPS_MESA_FREEBSD_SRC` (D004). `build` stages its C-library headers into
`toolchain/sysroot/`, stages libelf, libc++, msun and the locale sources, and runs
`toolchain/build-mesa.sh` in the image `toolchain/Dockerfile` describes (clang 21, D013).
The output is static archives for `x86_64-unknown-freebsd` in `build/`, listed in link
order in `build/link-order.txt`.

## Use

A title in oops-apps sets `USE_MESA=1`; `common/app.mk` includes `oops-mesa.mk`, links the
archives, and packages the title with SELFish. The title creates a context with
`oops_gl_create`, draws with OpenGL, and presents with `oops_gl_present` (declared in
`src/platform/oops_platform.h`), and calls `oops_mesa_run_init_array` first. A title that
links oops-mesa is hosted, not freestanding (D002). Examples are under
`oops-apps/src/oops-mesa/`.

## Provenance

Mesa is MIT licensed. The hardware facts the shims rely on come from oops-sdk's oracle
records and obSCEne's measurements; [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) lists what was
consulted, and the shared [conventions](../docs/CONVENTIONS.md) state the rule.

Licensed under MIT or Apache-2.0, at your option.
