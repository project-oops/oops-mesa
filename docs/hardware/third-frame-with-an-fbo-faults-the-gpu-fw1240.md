# The third frame with a framebuffer object faults the GPU

**2026-09-22** · firmware 12.40 · the same retail unit as the earlier records · `mesa-demos`
`fbotexture`, title `MDEM00001` · follows
[render to texture with depth and stencil](render-to-texture-with-depth-and-stencil-fw1240.md)

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

**Guessed, and it needs a probe rather than another run.** The obvious shape is a buffer freed or
unmapped while a submission still referenced it - the FBO's attachments are the only allocations
in this program with a lifetime that spans frames and is managed by Mesa rather than by the
winsys. `src/winsys/buffers.c` and `src/winsys/submit.c` are where that would live.

Nothing here is a diagnosis. It is the first frame-3 failure recorded, with the register state
that came with it.

## What this does not affect

Presentation, tiling and scanout are unimplicated: frame 2 went out through the same path as
frame 1, `read=0 mirror=0`, direct scanout, in 912 us. The fault is on the submission after it.
