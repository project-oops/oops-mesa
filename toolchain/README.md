# toolchain

The container build and its inputs. Nothing here runs on the machine directly.

| file | state | what it is |
|---|---|---|
| `Dockerfile` | exists | the image: clang 21 with lld, ninja, the Python packages Mesa's generators need, and meson from pip because the distribution's is older than Mesa requires. No LLVM: the shader backend is ACO. Its `FROM` tag is the collection's authoritative compiler pin (D013), and `OOPS/tools/check-toolchain.sh` fails when the other repositories disagree with it. |
| `stage-sysroot.sh` | exists | copies the C-library headers out of the checkout `dependencies.mk` pins, in the layout FreeBSD installs them, into `sysroot/usr/include`. Reads the checkout's object store only, so it cannot disturb the tree orbistoun harvests from. |
| `cross-prospero.ini` | exists | the meson cross file: `clang -target x86_64-unknown-freebsd`, the staged sysroot, and the target flags oops-apps' `app.mk` uses, minus the freestanding ones, because a title linking oops-mesa is hosted (D002). |
| `build-mesa.sh` | exists | applies `../patches/`, configures Mesa for radeonsi and ACO only, static, no LLVM, and builds. Stops with the reason on standard error when a dependency cannot be met. |
| `stage-sources.sh` | exists | stages the upstream libraries the build compiles for the target - libelf (radeonsi will not configure without it) and libc++ (Mesa's ACO backend is C++ and the platform's own C++ library cannot serve it, D006) - out of the checkout `dependencies.mk` pins, with `git archive` so the checkout is never written to. |
| `stage-platform-stubs.sh` | exists | emits `platform-stubs.c`: one empty definition per symbol obSCEne measured `present` on firmware 12.40, so meson's link probes answer "did a measurement find this symbol" rather than yes-to-everything or no-to-everything. |
| `platform-stubs.c` | generated | the probe archive's source, written by `stage-platform-stubs.sh`; linked only into meson probe programs, never into anything that runs. |
| `sysroot/` | ignored, staged | 920 headers, never tracked, re-staged on every build and checked against the pin. |

Run it through the verb, which stages the sysroot before it starts the container:

```sh
./bin/oops-mesa build
```

## What it produces today

Static archives for `x86_64-unknown-freebsd`, including radeonsi, ACO, NIR, GLSL, AddressLib
and libdrm's `libdrm_amdgpu.a`. **How many is not written here**: the build prints the count and
`build/link-order.txt` is the list, so a number in this sentence would be a copy of something the
build already owns, and it was wrong by three before anybody noticed (CONVENTIONS section 5). The
first build to produce them is
[worklog 005](../docs/worklog/005-mesa-compiles-for-the-console.md). libdrm is carried as a
pinned dependency rather than replaced, and `patches/001` routes its `ioctl` and `mmap` into the
winsys shim (D005). What is not yet settled is at run time on the hardware, not at compile time:
the winsys and platform shims of roadmap units 5 and 6. The earlier point, where the configure
stopped at `libdrm` before it was carried, is
[worklog 004](../docs/worklog/004-the-container-build-reaches-mesas-first-real-demand.md).
