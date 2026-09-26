# Chained command buffers and the command processor

Firmware 12.40, retail unit. Title `MDEM00001`, `mesa-demos` `fbotexture` with animation on,
2026-09-22 and 2026-09-23. Follows
[render to texture with depth and stencil](render-to-texture-with-depth-and-stencil-fw1240.md).

When a command stream outgrows its buffer, `amdgpu_cs_flush` allocates another and ends the
current one with a type-3 `INDIRECT_BUFFER` naming it. `fbotexture` chains about thirteen times a
frame; `gears` and `shadowtex` do not chain. The command processor reads `gpu_addr` through
`+ 4*size` and nothing else: obSCEne puts a DCB at the end of a page with the next page unmapped
and the submission retires cleanly. A jump out of the declared range lands where the GPU has no
translation. `submit_chain` walks the chain in software - the dwords before the chain packet as
one DCB, then the target as another, bounded at 16 links - so the CP never executes an
`INDIRECT_BUFFER`. The fence stream follows the last link.

## Without the chain walk

| | |
|---|---|
| frame 1 | `present us: flush=61840 read=0 mirror=0 disp=11610 total=73450` |
| frame 2 | `present us: flush=889 read=0 mirror=0 disp=23 total=912` |
| frame 3 | does not present |

```
GPU Protection fault. hub:0 vmid: system process0 pid:0x0 client:CPG(6) access:Read permission:0x3
reason: Unmapped page access, Protection fault addr(VA): 0x0000000400020000
…
submission 10 did not retire; fence still 0x11111111
context 1 has now failed to retire 1 time(s)
ioctl #219 0xc0186444 answered -60
amdgpu: The CS has cancelled because the context is lost. This context is guilty of a hard recovery.
```

Then `abort()` on thread `mesa-demos:cs0`, a coredump, and a GPU reset that takes SceShellUI down
(`No heart beat of Shell UI`, `Killing Shell UI since HP3D has timed out`); the console recovers
on its own. The faulting client is `CPG` on a read; `GUI_ACTIVE`, `CP`, `CPF`, `CPG`, `DB` and
`CB` report busy, `TA`, `SX`, `SPI`, `PA` and `SC` idle.

With `src/winsys/buffers.c` logging every VA map, unmap and close and `submit.c` logging every
instruction buffer:

```
submitting IB at 0x400000000, 17088 bytes    <- frame 1, presents
submitting IB at 0x400018000, 16160 bytes    <- frame 2, presents
submitting IB at 0x40001c000,  7936 bytes    <- frame 3, submission 9
Protection fault … client:CPG(6) access:Read … addr(VA): 0x0000000400020000
submission 9 did not retire; fence still 0x11111111
```

`0x400020000` lies inside buffer 2, the instruction-buffer pool, mapped at `0x400000000` for 2 MiB
and never unmapped. The third command buffer chains to `0x400020000` at dword 1980 of 1984.

## Control

`gears`, same tree, SDK and firmware, animating from its first frame with no FBO and no chain:

```
present us: flush=44498 read=0 mirror=0 disp=18429 total=62927   (first frame, shader compile)
present us: flush=4713  read=0 mirror=0 disp=12    total=4725
present us: flush=4731  read=0 mirror=0 disp=12    total=4743
…
present us: flush=1612  read=0 mirror=0 disp=11    total=1623
```

Over 500 ioctls, no fault, about 1.6 ms a present.

## With the chain walk

Build at `46303e4`, 2026-09-23: 29 frames, 397 chains walked, zero faults, `fbotexture`
animating.
