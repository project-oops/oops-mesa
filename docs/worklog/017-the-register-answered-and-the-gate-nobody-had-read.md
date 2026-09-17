# 017. The register answered, and the gate nobody had read

**2026-09-16** - roadmap unit 5

## What changed

`GB_ADDR_CONFIG` (0x263e) is answered, and with it three more of radeonsi's demands. Every step
worklog 010 enumerated is now satisfied, and one it did not enumerate turned out to sit in front
of all of them.

## The register, derived rather than read

The route to reading it is closed and stays closed: obSCEne's sweep `20260915-150825` submitted a
userspace COPY_DATA of 0x263e and the CP rejected it as privileged, faulting the process
(`GPU_FAULT_BAD_COMMAND_ASYNC`). That sweep's log carries a control fence that retired first, so
the negative is a controlled one and not a broken harness. Two things are worth recording about
it. The console's own fault line names the packet and the operand, so it is hardware output and
not something the probe could have invented. And the probe that produced it was never committed -
`git log -S` finds it in no revision - so the measurement is real but not reproducible from the
tree. The rows later sweeps carry are a citation of `150825`, correctly tagged `assumed`, not a
fresh measurement.

So the value was derived instead. The collection already contains the layout that register
describes: `oops-sdk/src/agc/agc_tiler.c` carries 64KB 32bpp basis vectors that oops-gl draws
through on hardware. addrlib turns a `GB_ADDR_CONFIG` into exactly such a swizzle, so the
register can be recovered by inverting it - enumerate the eleven bits addrlib reads
(`gfx10_gb_reg.h`) and keep the candidates whose computed swizzle reproduces the tiler's byte
offset for all 16,384 pixels of the block.

Of the 224 candidates addrlib accepts under this shim's chip identity, 32 match, and all 32 agree:

| field | result |
|---|---|
| `NUM_PIPES` | 4, sixteen pipes - pinned; the other 192 candidates fail |
| `PIPE_INTERLEAVE_SIZE` | 0, 256 B - excluded rather than derived, see below |
| `MAX_COMPRESSED_FRAGS` | free; all four values reproduce the tiler identically |
| `NUM_PKRS` | free; read only under RB+ |

`OOPS_GB_ADDR_CONFIG` is therefore `0x00000004`, and the two free fields are left zero. They are
not unknowns waiting on a measurement - nothing in Mesa's GFX10 path lets them reach a colour
surface's addresses here, so no value of them changes a layout. The interleave was not derived:
addrlib says outright it has no software pattern for anything but 256 B, and candidates that set
it do not refuse, they loop. That is an exclusion on addrlib's own authority and is labelled as
one rather than counted as a result.

The one swizzle mode that matches, under any identity tried, is `64KB_R_X`.

This is not the register's value on the console and must not be quoted as one. Real registers
carry bits addrlib ignores - Mesa's own navi10 dump sets bit 20 - and a derivation recovers only
the bits addrlib reads. It is the value that makes addrlib produce the layout the hardware is
observed to use, which is the whole of what this register is consumed for here.

## What that settles about the gfx level

Under an RB+ (GFX10.3) identity, nothing matches. 68 valid candidates evaluated, none reproducing
the tiler; the 4 that will not initialise are 64-pipe configurations that produce a six-bit XOR
where the tiler has four, so they could not have matched either. The silicon addresses like
GFX10.1.

That answers the question `drm_device.c` had been leaving open, and it answers it by
falsification rather than by assertion. It also makes the value dependent on the chip identity
in `device_info.c`: reclassifying this part as GFX10.3 would not change the number, it would
invalidate it and leave nothing to put in its place.

It also means `agc_tiler.c`'s own comment was wrong. It said "RDNA2 GFX10.3 basis vectors". The
vectors were right; the label on them was not, and it has been corrected in oops-sdk.

Mesa reaches the same conclusion independently a few lines later: with `ip_discovery_version`
zero it overrides the GFX minor version to 1 for GFX1013, so the device describes itself as
GFX10.1 whatever this repository says.

## The gate nobody had read

With the register answered, the startup path should have reached new ground. It would not have.

Worklog 010 traced `amdgpu_device_initialize` and `amdgpu_query_gpu_info_init` - libdrm's
functions - and found `READ_MMR_REG 0x263e` the single blocked step. But libdrm is not radeonsi's
only reader. `ac_query_gpu_info` in Mesa's own `ac_gpu_info.c` wraps all of it, and before it
asks for anything at all:

```c
if (info->drm_minor < 54) {
   fprintf(stderr, "amdgpu: DRM version is %u.%u.%u, but this driver is "
                   "only compatible with 3.54.0 (kernel 6.6+) or later.\n", ...);
   return AC_QUERY_GPU_INFO_FAIL;
}
```

The shim answered 3.49. So the startup path had been stopping on the *version* call, and would
have gone on stopping there with the register answered perfectly - the register read is inside
`ac_drm_query_gpu_info`, which sits after this check. Nothing had read far enough up the call
graph to see it.

The version is now 3.54, and that claims nothing. Every capability Mesa gates on the minor sits
above it: the GPUVM fault query at 55, default zerovram at 59, the two gfx12 DCC flags at 58 and
60. The two workaround branches keyed below 63 stay active, which is the conservative side of
each. The only gate at or under 54 is a buffer path at `>= 47`, already true at 3.49.

The lesson is narrower than "trace further". It is that a trace records where it stopped looking,
and worklog 010 stopped at libdrm's edge without saying so. This entry names its own edge below.

## Three more answers, and why each is the conservative one

Reading the rest of `ac_query_gpu_info` turned the list of unimplemented commands into an order,
because it makes clear which refusals are fatal and which are merely skipped.

**`HW_IP_INFO`** is fatal, indirectly. Mesa asks every IP type, skips the ones that refuse, and
then fails the device unless GFX or COMPUTE came back with a ring. Only GFX is answered - and
that is not a judgement call, because Mesa discards a compute queue on this exact part by name:

```c
/* GFX1013 is known to have broken compute queue */
if (ip_type == AMD_IP_COMPUTE && device_info->family == FAMILY_NV &&
    ASICREV_IS(device_info->external_rev, GFX1013))
   return false;
```

One graphics ring is claimed because one is what has been seen to work: obSCEne's
`166-agc/driver-submit-fence` creates a queue, submits and sees the fence retire, and D003's route
measurement put radeonsi's own initial state through that path on hardware (worklog 002). Nothing
has seen a second, so one is claimed. Alignments are left zero: Mesa raises whatever arrives to at
least 256 bytes, which is already what oops-sdk asks of a command buffer, so zero and 256 describe
the same requirement.

**`FW_VERSION`** is fatal directly - a failed ME, PFP or MEC query ends the device. Nothing here
has measured a firmware version, so zero is answered, and zero is the safe end of every gate Mesa
keys off these on this part. The ME and PFP comparisons in radeonsi are GFX6/7/8 only; the PFP one
behind the ZPASS event needs GFX11; the MEC one needs GFX10_3, which the derivation above rules
out, and a low value there turns the workaround *on* rather than off. A zero enables no fast path.

**`GET_CAP` for timeline syncobj** is not fatal and is left refusing. Mesa reads the failure as
"no timeline support" and carries on without it, which is true and is the conservative answer.

## Where this stops looking

Named, so the next entry does not have to rediscover it.

`ac_query_gpu_info` should now run to the end: the two remaining calls in it that could fail are
`ac_drm_query_sw_info(address32_hi)`, which libdrm answers from the VA range `DEV_INFO` already
supplied rather than from an ioctl, and `AMDGPU_INFO_MEMORY`, answered since worklog 016. What
happens *after* it - the rest of `amdgpu_winsys_create`, then `si_create_screen` - has not been
traced and is not predicted anywhere.

The syncobj surface is the visible next wall. `util_sync_provider_drm` wires up
`drmSyncobjCreate`, `Wait`, `Signal` and the rest, and none of those `DRM_IOCTL_SYNCOBJ_*`
commands exist in this shim. They are not reached during device query; they are reached when
something waits on a fence. That is a submission-path problem, not a startup one, and it belongs
to the unit-5 gate rather than to this entry.

## What was not changed

The six assumed groups in `device_info.c`. `REQ-20260914T1558Z-7d41` is closed as unanswerable,
and the closure is right but its stated reason is not: obSCEne recorded that no userland query
surface exists, which is true, but Mesa reads none of those fields from registers on this family
in any case. They come from `DEV_INFO`, which on this platform is this repository's own code. No
probe can settle them; an oracle or a public source could.

## State

The host suite is 70 checks and passes. The new ones check what the derivation pins rather than
the literal it produced, so a different literal describing the same layout passes and one
describing a different layout does not - verified by making it fail on purpose. `AMDGPU_INFO` is
exported as `oops_winsys_info` so the suite can reach it at all, since opening the device needs
the platform graphics driver bound and the build machine cannot do that.

Nothing was deployed. `oops-apps/src/mesa-probe` is the title that would say where the path now
stops, and its narration has been corrected: it used to state that a failure was expected until
`GB_ADDR_CONFIG` was measured, which would have been a confident wrong reason attached to any new
failure. It now names no cause and says the winsys lines are the result.
