# toolchain

The container build and its inputs. Nothing here runs on the machine directly.

| file | state | what it is |
|---|---|---|
| `Dockerfile` | exists | the image: clang 18 with lld, ninja, the Python packages Mesa's generators need, and meson from pip because the distribution's is older than Mesa requires. No LLVM: the shader backend is ACO. |
| `stage-sysroot.sh` | exists | copies the C-library headers out of the checkout `dependencies.mk` pins, in the layout FreeBSD installs them, into `sysroot/usr/include`. Reads the checkout's object store only, so it cannot disturb the tree orbistoun harvests from. |
| `cross-prospero.ini` | exists | the meson cross file: `clang -target x86_64-unknown-freebsd`, the staged sysroot, and the target flags oops-apps' `app.mk` uses, minus the freestanding ones, because a title linking oops-mesa is hosted (D002). |
| `build-mesa.sh` | exists | applies `../patches/`, configures Mesa for radeonsi and ACO only, static, no LLVM, and builds. Stops with the reason on standard error when a dependency cannot be met. |
| `sysroot/` | ignored, staged | 920 headers, never tracked, re-staged on every build and checked against the pin. |

Run it through the verb, which stages the sysroot before it starts the container:

```sh
./bin/oops-mesa build
```

## Where it stops today

At `libdrm`. radeonsi asks a kernel interface what the device is and submits through it, and on
this platform that interface is the winsys shim of roadmap unit 5. Everything before it - every
compiler feature check, every function check, the whole staged header set - passes. See
[worklog 004](../docs/worklog/004-the-container-build-reaches-mesas-first-real-demand.md).
