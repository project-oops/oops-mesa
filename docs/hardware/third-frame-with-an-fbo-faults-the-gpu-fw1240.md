# The third frame with a framebuffer object faults the GPU — RESOLVED 2026-09-23

**2026-09-22** · firmware 12.40 · the same retail unit as the earlier records · `mesa-demos`
`fbotexture`, title `MDEM00001` · follows
[render to texture with depth and stencil](render-to-texture-with-depth-and-stencil-fw1240.md)

> ## Resolved: radeonsi chains its command buffers and the command processor cannot follow
>
> **Fixed in `46303e4`. Measured on hardware 2026-09-23: 29 frames, 397 chains walked, zero
> faults, and `fbotexture` animating — it had never survived a second frame.**
>
> When a command stream outgrows its buffer, `amdgpu_cs_flush` allocates another and ends the
> current one with a type-3 `INDIRECT_BUFFER` naming it. On Linux the kernel is handed the first
> link and the hardware walks the rest. `fbotexture` chains roughly thirteen times a frame, every
> frame, which is why the FBO path met this and `gears` never did.
>
> The CP cannot walk it here, and obSCEne [`REQ-20260922T2230Z-6e81`](../../../obscene) is why
> that is a measurement: its arm 4 put a DCB at the end of a page with the next page explicitly
> unmapped and the submission retired cleanly. The CP reads `gpu_addr` through `+ 4*size` and
> nothing else, so **the declared range is the only range a submission makes valid** and a jump out
> of it lands where the GPU has no translation, whatever our own page tables say. The third
> command buffer chained to `0x400020000` at dword 1980 of 1984, and the fault at that exact
> address was the next line in the log.
>
> `submit_chain` now walks it in software: the dwords before the chain packet as one DCB, then the
> target as another, bounded at 16 links. The CP never executes an `INDIRECT_BUFFER` because it
> never sees one.
>
> **It explains the symptom as well as the crash.** The fence stream is appended after the last
> link, so the CP had been jumping over it — which is why this always read as `submission did not
> retire` rather than as a lost frame.
>
> The narrative below is kept as filed, wrong turns included. Three readings were wrong: a freed
> buffer, a leaked buffer colliding in the address space, and a map larger than its allocation.
> The second was a real defect found on the way and fixed in `b7eeab1`; the other two were
> disproved by instrumenting rather than by argument.

That record ended with an FBO rendering correctly and one frame on screen, and noted that one
frame is `fbotexture`'s specified behaviour because `Anim` defaults false. This is what happens
when it is turned on.

## What the run did

| | |
|---|---|
| frame 1 | `present us: flush=61840 read=0 mirror=0 disp=11610 total=73450` |
| a key arrives | the first keystroke ever delivered to a title here (oops-sdk `dc4927b`) |
| frame 2 | `present us: flush=889 read=0 mirror=0 disp=23 total=912` — correct, and fast |
| frame 3 | **never presents** |

```
GPU Protection fault. hub:0 vmid: system process0 pid:0x0 client:CPG(6) access:Read permission:0x3
reason: Unmapped page access, Protection fault addr(VA): 0x0000000400020000
…
submission 10 did not retire; fence still 0x11111111
context 1 has now failed to retire 1 time(s)
ioctl #219 0xc0186444 answered -60
amdgpu: The CS has cancelled because the context is lost. This context is guilty of a hard recovery.
```

Then `abort()` on thread `mesa-demos:cs0`, a coredump, and a GPU reset that also took SceShellUI
down with it (`No heart beat of Shell UI`, `Killing Shell UI since HP3D has timed out`). The
console recovered on its own.

## Why this has not been seen before

**Every previous run on this console drew one frame.** `fbotexture` has an FBO and, until the
keyboard worked, could never be told to draw a second. So this is the first multi-frame run *with*
one, and the wall was standing behind a dead keyboard the whole time.

### The control, run immediately after

`gears`, built from the same tree and the same SDK, on the same firmware. It animates from its
first frame and has **no framebuffer object**:

```
present us: flush=44498 read=0 mirror=0 disp=18429 total=62927   (first frame, shader compile)
present us: flush=4713  read=0 mirror=0 disp=12    total=4725
present us: flush=4731  read=0 mirror=0 disp=12    total=4743
…
present us: flush=1612  read=0 mirror=0 disp=11    total=1623
```

It ran to over 500 ioctls with no fault, settling to ~1.6 ms a present. So multi-frame
submission is sound, and so is everything `fbotexture` shares with it: the same winsys, the same
fence, the same keyboard and input path per frame, the same scanout. **The FBO is the only
variable left**, which is what makes this worth chasing in `src/winsys/` rather than anywhere
else.

That makes it a new class rather than a regression: the single-frame FBO path is
[measured and correct](render-to-texture-with-depth-and-stencil-fw1240.md), and frame 2 presents
correctly too, with the same attachments. Something between the second present and the third
submission stops being mapped.

## What is known, and what is guessed

**Known.** `0x400020000` is below both scanout buffers (`0x400600000` and `0x401e00000`), so it is
not one of those. The faulting client is `CPG`, the graphics command processor, on a **read** -
so it is a command buffer, an index or descriptor table, or a resource a packet points at, rather
than a shader touching a surface. `GUI_ACTIVE`, `CP`, `CPF`, `CPG`, `DB` and `CB` are all reported
busy; `TA`, `SX`, `SPI`, `PA` and `SC` idle, which is consistent with the fault landing before any
shader ran.

**Guessed, and wrong.** The first reading was a buffer freed or unmapped while a submission still
referenced it. Two instrumented runs disproved it and found something else on the way.

### What the instrumented runs settled

`src/winsys/buffers.c` was made to log every VA map, unmap and close, and `submit.c` to log the
address of every instruction buffer it submits. Three things came out, in order.

**A real bug, found and fixed, that is not this one.** The size fallback used to create GL at the
requested extent, find the display had refused, destroy it and create it again - two Mesa devices
in one process. The second re-initialised with a VA allocator starting from the same base while
the first's teardown had leaked `gem 5` at `0x400600000` and `gem 7` at `0x400700000`, so the
8.8 MB scanout buffer was handed an address a live mapping still held. Fixed in `b7eeab1` by
asking the display before building GL; `e087968` makes any future collision say so by name. **The
fault is unchanged**, the address space is now provably clean, and no collision is reported - so
that was a genuine defect sitting beside this one, not its cause.

**The faulting address is never unmapped.** `0x400020000` is inside buffer 2, mapped at
`0x400000000` for 2 MiB and never released for the life of the process.

**Buffer 2 is the instruction-buffer pool, and the fault is one page past the IB being executed:**

```
submitting IB at 0x400000000, 17088 bytes    <- frame 1, presents
submitting IB at 0x400018000, 16160 bytes    <- frame 2, presents
submitting IB at 0x40001c000,  7936 bytes    <- frame 3, submission 9
Protection fault … client:CPG(6) access:Read … addr(VA): 0x0000000400020000
submission 9 did not retire; fence still 0x11111111
```

The IB runs `0x40001c000`–`0x40001DF00`. The fault is 8.5 KiB past its end and exactly
`OOPS_WINSYS_PAGE` (16 KiB) past its start, page-aligned. `submit_one` passes `bytes / 4` with no
rounding, so the size handed to the driver is exact.

So the command processor read past the end of the buffer it was given, at an address our own
bookkeeping says is mapped. Either the mapping is not reaching the GPU page table for the whole
2 MiB, or the CP reads somewhere we never mapped and the address is a coincidence of layout.
That is a hardware-behaviour question and it is
[`REQ-20260922T2230Z-6e81`](../../../obscene) on the obSCEne bus: what the CP reads past a DCB's
declared size, and what terminates one.

Nothing here is a diagnosis. It is the first frame-3 failure recorded, with the register state
that came with it and the two readings it has since eliminated.

## What this does not affect

Presentation, tiling and scanout are unimplicated: frame 2 went out through the same path as
frame 1, `read=0 mirror=0`, direct scanout, in 912 us. The fault is on the submission after it.
