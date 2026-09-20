# 054. The VA maps land, the first submission is reached, and it does not retire

**2026-09-19** - roadmap unit 6

## The GEM_VA dispatch fix worked, and the maps succeed

With worklog 053's dispatch fix, every `GEM_VA` (`0xc0406448`) now answers `0` - the shim binds each
buffer at the address libdrm chose and the platform **accepts it**. So the high-VA range worklog 052
handed Mesa is one this platform maps: `-5af3`'s question is answered in the affirmative for the
addresses radeonsi actually used (in the `0x2_0000_0000 .. 0x2_004xxxxx` region). Context creation
ran ~25 ioctls further than any run before it - GEM_CREATE / GEM_MMAP / GEM_VA / CTX in a steady
rhythm - and reached, for the first time in this project, a **command submission**.

Capture: [`docs/hardware/first-submission-then-va-write-fault-fw1240.log`](../hardware/first-submission-then-va-write-fault-fw1240.log).

## The new wall: the submission does not retire

```
command stream chunk kind 6 is not handled          (BO_HANDLES - benign to skip)
submission 1 did not retire; fence still 0x11111111
context 2 has now failed to retire 1 time(s)
ioctl #63 0xc0186444 (AMDGPU_CS) answered -60        (-ETIMEDOUT)
```

`submit.c` does not stub submission - it reuses oops-gl's proven synchronous fence (submit
radeonsi's stream, then a small end-of-pipe stream that writes `0xbeefcafe` over an armed
`0x11111111`, and wait for the word). Here the word **never flipped**: radeonsi's stream was handed
over but the end-of-pipe event never wrote, so the bounded poll timed out and `AMDGPU_CS` returned
`-60`. radeonsi treats that as a lost context, does a few more allocations, and then SIGSEGVs on a
CPU write to `0x200428200` - a GPU virtual address, page not present - which is its reaction to the
failed submission, not an independent fault. The process died cleanly; the console stayed healthy
(SceShellCore/UI/Compositor all normal, slot freed by the crash - a SIGSEGV frees the slot where a
park holds it).

## Why this is the real unit, not a quick fix

oops-gl's fence sequence has retired for `gl-cube`, whose command stream oops-gl hand-builds. What
does not retire here is **radeonsi's own** context-initialisation stream. The likely difference is
that radeonsi's IB expects GPU state (queue/ring registration, shader or register setup) that the
gl-cube path establishes and this path does not, so the GPU never reaches the end-of-pipe event our
fence stream appends. Establishing why radeonsi's first stream does not run - by comparing what it
submits against oops-gl's working submission, and by reading back what the GPU did or did not do -
is the next unit, and it is where a real frame is won or lost.

## State / next

Milestones banked this session: the heap fix carried config enumeration (052), the high-VA range let
the VA allocation and mapping succeed (052/053), and the frontend reached its first GPU submission
(054). What is left on the path to a frame:

1. **Why radeonsi's submission does not retire.** Compare its IB and the surrounding GPU setup
   against oops-gl's retiring `gl-cube` submission; determine what state its stream assumes. This may
   want an obSCEne measurement of its own once the gap is named.
2. The CPU write-fault at the GPU VA is downstream of (1); revisit only if it survives a retiring
   submission (it may be radeonsi CPU-touching a buffer it expects the GPU to have written).

`-5af3` remains open for the full mappable window, but the addresses in play map, so it no longer
blocks. No code changed in this entry - it records the measurement and names the next unit.
