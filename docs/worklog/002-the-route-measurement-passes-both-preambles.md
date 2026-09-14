# 002. The route measurement passes: radeonsi's preamble runs on our queue, both ways

**2026-09-14** - roadmap unit 2, the D003 gate, on the PS5 at this desk, firmware 12.40

## What changed

- **`tools/preamble-dump`**, a host tool built against the pinned Mesa with four of its own
  sources (`ac_cmdbuf.c`, `ac_pm4.c`, `ac_shader_util.c`, `ac_debug.c`) and its own header
  generators, no meson and no LLVM. It describes the hardware as Mesa would see it (`GFX10`,
  family `GFX1013`, address bits 39:32 = `0x2`, two shader engines, sixteen render backends)
  and prints the command-stream preamble radeonsi emits at the start of every stream, as C
  data and as a decoded listing with Mesa's register names. Two variants: **A** opens with
  `CONTEXT_CONTROL` and `CLEAR_STATE` as radeonsi does on a graphics queue; **B** is the same
  preamble for a device reported without `CLEAR_STATE`. Both are the same 59 register writes:
  38 packets and 136 dwords, 37 packets and 134 dwords. The two outputs are tracked next to the tool and
  `make check-preamble` regenerates them and fails if they drift from the pin.
- **oops-sdk gains `glSetHardwarePrelude(words, count)`**: words every later frame's command
  stream opens with, ahead of oops-gl's own state. An experiment hook, unvalidated on
  purpose.
- **gl-cube gains two control files**, `preamble-a` and `preamble-b`, which put the
  corresponding variant in front of every frame and say so in the log. The header is picked
  up from the sibling `oops-mesa` checkout when it is there and the build is unchanged when it
  is not.

## The measurement

D003's pass condition, stated before the run: the compositor's own frames keep arriving (no
`HP3D` timeout in the system log, no shell restart), our fence and clock retire on every
frame, and the pixel hash of a known frame is unchanged from the oracle record.

Four runs of GLCB00001, paused at rotation (25, 35, 10) degrees, depth on, texture off, each
ended through its stop file, the system log streamed by Prosperous throughout:

| run | prelude | stream words | frame hash | fence and clock | compositor and shell |
|---|---|---|---|---|---|
| 1 | none (baseline for this build) | 0x41c | `0x9dbfe189`, every logged frame | both retired | quiet |
| 2 | A: `CONTEXT_CONTROL`, `CLEAR_STATE`, 59 writes | 0x4a4 | `0x9dbfe189`, 73 logged frames over 45 s | both retired, frames-confirmed rising, GPU clock advancing at 100 MHz | quiet; home screen drew after exit |
| 3 | B: `CONTEXT_CONTROL`, 59 writes | 0x4a2 | `0x9dbfe189`, 74 logged frames over 45 s | both retired | quiet; clean exit |
| 4 | none (control for the exit line) | 0x41c | not sampled | both retired | quiet |

`0x9dbfe189` is the oracle record's texture-off hash (oops-sdk, `agc-gl-cube-oracle-fw1240`).
No `GPU_FAULT`, no `HP3D` timeout, no `SceShellUI` restart in any run. The line
`### HP3D(pipe1) Agc/Gnm Compositor. proc gen:2 gpu gen:2 vmid:3 / +++ num clients 1` appears
once at every title exit, run 4 included: it is the compositor's client-count report on
teardown, not a timeout, and the shell keeps drawing after it.

**Result: pass, both variants.** The route in D003 is open: radeonsi on a winsys of ours. The
platform serves `CLEAR_STATE` on our queue (variant A retired with the hash unchanged), so the
device description can keep `has_clear_state` on and radeonsi needs no patch there.

## What it does not show

The preamble is what radeonsi writes once at stream start. Its per-draw state, its shader
binaries and its descriptor layouts arrive with unit 6, and each of those is a further
measurement. What this one establishes is the thing D003 named as the risk: a client that
writes `CONTEXT_CONTROL`, `CLEAR_STATE` and radeonsi's initial register set on its own queue,
next to the compositor, for a minute at a time, and the compositor does not mind.

## Surprises

- **radeonsi's GFX10 preamble leans on `CLEAR_STATE` for almost everything.** The 59 writes
  are cache policy, compute defaults, shader-stage resource limits, tessellation and binning
  constants. The scissors, blend, depth and rasteriser defaults that oops-gl writes by hand
  every frame come from `CLEAR_STATE` in radeonsi; on GFX11 Mesa lists them explicitly, on
  GFX10 it does not. Reporting the device without `CLEAR_STATE` changes nothing in the
  preamble: radeonsi then treats every tracked register as unknown and writes it on first
  use, which is why variant B is the same 59 writes minus one packet.
- **Two of radeonsi's constants differ from oops-gl's measured ones and the hash did not
  move**, because oops-gl's own writes follow the prelude and win: `VGT_TESS_DISTRIBUTION`
  (`0xd8181e0c` against `0x88101000`), `PA_SC_BINNER_CNTL_1` (`0x03ff007f` against
  `0x03ff0080`), and `PA_SU_SMALL_PRIM_FILTER_CNTL` (radeonsi enables the filter). Whether
  radeonsi's values also draw the cube is a unit-6 question, not this one.
- **The gl-cube title had stopped packaging.** SELFish's import check refused
  `oops_cosf`, `oops_sinf` and `oops_fs_exists` as imports with no library: gl-cube's source
  list never named `src/math/math.c` and `src/system/fs.c`, and an older link had let the
  references through. Both are in the list now.
- **Prosperous's directory listing stopped answering after the first launch** (`550` for every
  path, root included) while `push`, `sh ps` and `sh ls` without flags kept working, and
  `sh ls -la` fails on this shell for any path. Listing through `sh ls` is the workaround; the
  cause is a Prosperous question, not a hardware one.
- **The stop file is polled slowly.** Removing it a few seconds after pushing it leaves the
  title running; wait for `stop file found, terminating cleanly` in the log before cleaning
  the title directory.

## Next

Unit 3: the container build up to Mesa's first compile error against the runtime shim's
headers, which is unit 4's work list. D002's `pthread_*` census request goes on the obSCEne bus
with unit 4. The register-default comparison above (radeonsi's constants against oops-gl's
measured ones) is worth a line in the unit-6 plan.
