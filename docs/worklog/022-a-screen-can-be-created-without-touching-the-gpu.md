# 022. A screen can be created without touching the GPU

**2026-09-17** - roadmap unit 5

## What this entry is

No code changed. Worklog 021 closed the startup path as far as the end of `ac_query_gpu_info` and
said the next useful thing was to trace what follows: `ac_addrlib_create`, then `si_create_screen`.
This is that trace.

It ends somewhere more useful than expected. **Creating a radeonsi screen on this part issues no
ioctl beyond the ones worklog 021 tabulated, and allocates no GPU memory at all.** So a non-null
`pipe_screen` is a complete proof of the device-query chain that submits nothing and can hang
nothing - which makes it a far better first hardware milestone than a triangle.

## `ac_addrlib_create` is arithmetic, and it has already been run

It issues nothing. It reads three fields and calls into AddressLib:

```c
regValue.gbAddrConfig      = info->gb_addr_config;
addrCreateInput.chipFamily = info->family_id;
addrCreateInput.chipRevision = info->chip_external_rev;
```

The first of those is worth following, because it is where worklog 017's derivation actually lands.
The chain is: this shim's `READ_MMR_REG 0x263e` answer, into `dev->info.gb_addr_cfg` inside libdrm
(`amdgpu_query_gpu_info_init`), out through `ac_drm_query_gpu_info` as `amdinfo.gb_addr_cfg`, into
`info->gb_addr_config` in `ac_fill_tiling_info`, and from there straight into `regValue`. Nothing
rewrites it on the way - the one branch that would zero it needs `!has_graphics` and a CHIP_GFX940
or later part.

And `AddrCreate` succeeding with those three values is not an assumption. The derivation tool *ran*
AddressLib with exactly this family, revision and register value - that is how it enumerated 224
candidates and kept 32. Had `AddrCreate` refused them, the derivation would have produced nothing
rather than an answer. This is the one link in the chain that has already been executed end to end,
albeit on the build machine.

The two `assert`s here on the pipe interleave are compiled out in release, and would pass anyway:
`PIPE_INTERLEAVE_SIZE` is 0, which is the 256 B the assert demands.

## `si_create_screen`: the whole of it, and not one ioctl

`radeonsi_screen_create_impl` is the callback `amdgpu_winsys_create` invokes once the winsys
exists. Read end to end, with `si_init_gfx_screen` inside it, it does: option-cache reads, a
`memcpy` of `radeon_info`, function-pointer tables, hash tables, mutexes, a slab allocator, thread
pools, and a great deal of arithmetic over `sscreen->info`. There is no `drmIoctl` on the path.

There is exactly one buffer allocation in the file:

```c
if (sscreen->info.gfx_level >= GFX11) {
   sscreen->attribute_pos_prim_ring = si_aligned_buffer_create(...);
}
```

This part is GFX10, so it is skipped. `si_init_mm_screen` allocates nothing either - it walks
`info.video_caps` looking for a supported queue, finds none because `HW_IP_INFO` refuses every
video engine, returns false, and the caller ignores that by design ("Don't fail if the multimedia
support is missing").

So the first `GEM_CREATE` this shim will ever see comes later than screen creation - at context
creation or first use, which is unit 6's territory.

## Four things the read settled

**ACO supports this part, so the crippled-screen path is not taken.** `si_init_gfx_screen` opens
with `has_gfx_compute = support_aco || support_llvm`, and if both are false it **returns `true`** -
a screen that exists and cannot compile a shader. That is exactly the silent degradation this
repository refuses (principle 4), and it is upstream's, so it would have to be detected rather than
prevented. It does not arise: `aco_is_gpu_supported` returns true for `GFX10` unconditionally.

**`hw_ip_version_minor` matters more than worklog 021 said.** That entry pinned it because
`ac_identify_chip` derives `gfx_level` from it and `10.0` matches no branch. The consequence is
wider than one function: `gfx_level` is what `aco_is_gpu_supported` switches on, and what perhaps a
hundred behaviour decisions in `si_init_gfx_screen` alone switch on - NGG, DPBB binning, DCC
stores, MSAA clear-to-reg. A zeroed minor would not have produced a subtly wrong driver; it would
have failed the device several steps away from the cause. The check added in 021 is worth more than
its one line suggests.

**Thread affinity is compiled out, and that is fine.** `util_queue_init` is called with
`UTIL_QUEUE_INIT_SET_FULL_THREAD_AFFINITY`, which sounds like a demand on the platform. It is not:
`HAVE_PTHREAD_SETAFFINITY` is not defined in this build, so `util_set_thread_affinity` compiles to
`return false`, and `util_queue` ignores the result. The same false also skips the APIC-walking
loop in `u_cpu_detect.c` that would otherwise pin the calling thread to each core in turn to learn
the L3 topology. Mesa loses a scheduling hint and nothing else. One fewer symbol this platform has
to export.

**`si_run_tests` is inert, and worklog 019 is why.** The last statement of screen creation runs a
battery of buffer-clear, copy, blit and *deliberate VM-fault* tests, and then calls `exit(0)`. All
of it is gated on the `AMD_TEST` option, which reaches `getenv`, which now answers null honestly.
Every `debug_get_*_option` on this path takes its documented default for the same reason - that is
the quiet dividend of fixing `getenv` properly rather than stubbing it to something plausible.

## What this makes the next hardware run

`mesa-probe` already calls `radeonsi_screen_create`. Given the above, a run of it has an unusually
clean shape:

- **It submits nothing.** No command stream, no buffer, no fence. There is nothing for the GPU to
  fault on and nothing to hang, because the GPU is never asked to do anything.
- **A non-null screen proves the whole of worklog 021's table**, every entry, in one result -
  including entry 20, which is currently only checked by reading.
- **A null screen names its own cause - conditionally.** Every refusal this shim makes logs under
  its own name through `oops_klog`, and that is known to surface. Mesa's own failure messages are
  a different matter, and the next section is about why.

That is a better unit 5 milestone than the one the roadmap carries. Reproducing the oops-sdk oracle
record through the winsys needs submission, buffers and fences - all of which sit behind syncobj
wait and signal, which D007 deliberately left unwritten. A screen needs none of it.

## The diagnostics may not exist, and that is now a filed question

Writing the bullet above is what exposed this. The claim "a failed screen names its cause" was
about to be made on the assumption that Mesa's error messages arrive somewhere. They may not.

**Mesa reports its fatal errors with `fprintf(stderr, ...)`, not through its own logger.** On the
device-query path alone that is 21 calls in `ac_gpu_info.c` and 7 in libdrm's `amdgpu_device.c`,
each immediately before the return that fails the device. And the configurable logger is no escape:
`src/util/log.c:143` sets `mesa_log_file = stderr`, so `mesa_loge` lands on descriptor 2 as well.

oops-sdk writes to descriptor **1**. `oops_klog` does `SYS_klog` and then `SYS_write` to fd 1
(`oops-sdk/src/system/system.c:532-533`). Nothing in the collection says anything about fd 2.

`REQ-20260909T1051Z-8e4a` settled that the *stream* exists - `_Stderr` resolves and its FILE struct
carries fd 2 at offset 4, exactly as `_Stdout` carries fd 1. What nobody has measured is whether
bytes written there surface in a captured log.

The two outcomes are far apart. If fd 2 surfaces, a failed run explains itself. If it does not, a
run produces this shim's own klog lines and nothing else - and those say only what *this shim*
refused, never what Mesa concluded from an answer it accepted. The second is much harder to
diagnose, so it is worth one probe to find out which world this is before spending a run.

Filed as `REQ-20260917T0233Z-5c9d`, which also asks the cheap follow-up: if fd 2 is dead, does
`dup2(1, 2)` revive it? If so the fix is one line in the runtime shim at startup. If not, the fix
is heavier and is better designed knowing that.

## State

No code changed; 90 host checks unchanged; nothing deployed. The trace worklogs 020, 021 and 022
were each asked for is now complete from the title's first instruction to a usable
`pipe_screen`, and the next question is no longer "what does it demand" but "does what we answered
turn out to be true", which only the hardware can say. One thing is outstanding before that is
worth asking it: `-5c9d`, on whether the hardware can answer audibly.
