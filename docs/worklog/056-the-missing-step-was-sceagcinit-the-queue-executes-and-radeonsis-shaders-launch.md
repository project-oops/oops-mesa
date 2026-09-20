# 056. The missing step was sceAgcInit: the queue executes, and radeonsi's shaders launch

**2026-09-19** - roadmap unit 6

## Correcting worklog 055

Worklog 055 fixed three real submit-path bugs (type-3 -> type-0 queue, SubmitDcb -> SubmitCommandBuffer,
the DCB flush and the SET_CONTEXT_REG preamble) and then concluded the remaining non-retirement was a
GPU-state / oops-sdk regression that obSCEne's `-a6c2` was tracking. **That conclusion was wrong**, and
the way it was wrong is worth keeping: the GPU was never wedged - the compositor rendered the dashboard
the whole time - and the control experiment had in fact already passed on this exact console that night
(obSCEne `166-agc/graphics-submit`, sweep `20260919-212620`: `rc-submit 0x0`, `fence-val 0xbeefcafe`,
`fence-hit 0x1`). Reaching for an external cause instead of the one measurable control was the error.

## The actual gap: the AGC runtime was never initialised

`ensure_queue` called `sceAgcDriverCreateQueue` without first calling `sceAgcInit`. On this platform the
driver accepts a CreateQueue **and** a submit without it - both return 0 - but the command-processor
microcode never services a queue whose runtime state was never established, so the fence sits armed
forever. That is the whole of 055's "submit_rc=0 but no retire".

The fix is what oops-sdk already does before every queue it creates (`agc_compute.c:30`,
`agc_display.c:382`) and what obSCEne's `166-agc/init` does: zero a 64-byte state and call
`sceAgcInit(state, 0xd)` before the first CreateQueue (version 0xd, `oops-sdk agc/driver.h`). One call,
in `submit.c`'s `ensure_queue`.

## The result: the GPU runs radeonsi

With it, the queue self-test **retires** on hardware:

```
queue self-test: q=80058a8b8 scb=1 dcb=1 submit_rc=0 fence=0xbeefcafe -> RETIRED
```

and radeonsi's own IB, submitted right after, **executes on the GPU** - it launched 48 shader
wavefronts. This is the first time this project has run its own GPU work. The submit path
(type-0 queue, sceAgcInit, SubmitCommandBuffer, SET_CONTEXT_REG preamble, DCB flush) is complete.

## The new wall, which is a real one: the shaders hit ILLEGAL_INST

The 48 wavefronts faulted, all at the same shader PC:

```
## [SE:0 SA:0 WGP:0 SIMD:0 WAVE:0] PC=0x0000000400A00014 {TRAPSTS=0x40000800,...} ILLEGAL_INST
exception: 0xa0d0c012 (GPU_FAULT_WAVEFRONT_ERROR_ASYNC)
```

The process died on the GPU fault; the console stayed healthy (SceShellUI kept rendering, slot freed -
a GPU wavefront fault is contained, not a ring wedge). `PC=0x400A00014` is in radeonsi's high VA range,
so a wavefront was dispatched to a shader at ~`0x4_00A0_0000` and executed bytes that are not valid
RDNA2 code. The leading suspect is the same cache-visibility issue the DCB had, now for radeonsi's own
shader buffers: radeonsi writes the shader through its CPU mapping (write-back `oops_mem_map_direct`
memory) and the GPU fetches stale/garbage from the GPU VA unless the buffer is flushed or allocated
write-combined. Other candidates: the shader-address registers (`SPI_SHADER_PGM_LO_*`) pointing off, or
an ISA/wave-mode mismatch (radeonsi's GFX10.3 output vs the wave setup). Distinguishing them is the next
unit - read back what is actually at `0x4_00A0_0000` versus what radeonsi wrote.

## Credit and lesson

The user found this by running the one control that settles it - obSCEne's own graphics-submit on this
console - rather than trusting a chain of code inspection that "looked identical". The lesson for this
file: when a submission is accepted but does not execute and a reference implementation exists, run the
reference on the same hardware before theorising. Recorded as [[never-touch-the-console-unasked]]'s
positive twin - the control run is cheap and it is the ground truth.
