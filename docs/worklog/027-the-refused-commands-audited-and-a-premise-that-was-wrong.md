# 027. The refused commands, audited - and a premise that was wrong

**2026-09-17** - roadmap unit 5

## What this entry is

Eleven commands in `drm_device.c` refuse. That set has never been examined as a set: each was
listed when the dispatch was written, for completeness, without asking who calls it. This asks.

Two results. Two of the eleven have **no caller in Mesa at all** at this pin. And the audit turned
up a premise this repository had written down confidently and got wrong - the one that sized the
syncobj table.

## Who calls the refused commands

| Command | Mesa's caller | Reachable? |
|---|---|---|
| `WAIT_FENCES` | **none** | never - no call site in Mesa |
| `FENCE_TO_HANDLE` | **none** | never - no call site in Mesa |
| `VM` | `ac_drm_vm_reserve_vmid`, `amdgpu_winsys.c:558` | behind `aws->reserve_vmid`, which reads `R600_DEBUG`/`AMD_DEBUG` - no environment, so never |
| `SCHED` | none found | never |
| `GEM_METADATA` | `amdgpu_bo.c:1425`, setting texture metadata | only when a texture is shared between processes |
| `GEM_USERPTR` | `amdgpu_bo.c:1822`, `buffer_from_ptr` | only `GL_AMD_pinned_memory` and its kin |
| `GEM_FLINK`, `GEM_OPEN` | buffer export and import | only cross-process sharing |
| `USERQ`, `USERQ_SIGNAL`, `USERQ_WAIT` | the user-queue submission path | behind `AMD_USERQ`, off |
| `SYNCOBJ_WAIT`, `SIGNAL`, `RESET` | the submission path | **as soon as anything submits** (D007) |

So of eleven refusals, exactly three are on the road to a first frame, and they are the three D007
deliberately deferred. The rest are either unreachable without an environment variable, or belong
to sharing buffers with another process - which nothing on this console does.

`WAIT_FENCES` and `FENCE_TO_HANDLE` are worth singling out: they are in the dispatch because D005's
command list has them, and at this pin Mesa issues neither. They stay listed, because a command
that is present and refuses is better than one that falls to the default branch, but nothing will
ever reach them until the pin moves.

## Three capabilities Mesa claims on this shim's behalf

An unexpected finding, and the reason the audit was worth doing rather than assumed. `ac_gpu_info.c`
hardcodes three capabilities true without asking the driver anything:

```c
info->has_userptr = !info->is_virtio;     /* true here */
info->has_syncobj = true;
info->has_fence_to_handle = true;
```

Each reaches a Gallium capability, which is what an application is told:

| | |
|---|---|
| `caps->resource_from_user_memory` | from `has_userptr` - and `GEM_USERPTR` refuses |
| `caps->fence_signal` | from `has_syncobj` - and `SYNCOBJ_SIGNAL` refuses |
| `caps->native_fence_fd` | from `has_fence_to_handle` - and `FENCE_TO_HANDLE` refuses |

There is no ioctl by which this winsys can say otherwise; Mesa does not ask. So an application
that takes one of these capabilities at its word gets a refusal from the winsys rather than a
clean "unsupported" from the API.

Nothing is done about it today, and that is a choice rather than an oversight. All three are
interop and sharing features, none is reachable by a title drawing into the display oops-sdk opens,
and the only fix is a patch to `ac_gpu_info.c` - which principle 1 says should be a shim if it can
be, and this cannot. What matters is that the mismatch is now written down, so the first
application to trip one is diagnosed in a minute rather than an afternoon.

## The premise that was wrong

`syncobj.c` said this, and it read like a fact:

> 256 is not a measurement and does not pretend to be one. radeonsi creates one syncobj per winsys
> and a handful per context; the table exists to catch a leak loudly rather than to accommodate an
> unbounded number.

The second clause is false. radeonsi creates a syncobj **per fence**:

```c
static struct pipe_fence_handle *amdgpu_fence_create(struct amdgpu_cs *acs)
{
   ...
   if (ac_drm_cs_create_syncobj2(ctx->aws->dev, 0, &fence->syncobj)) {
```

There is one fence per command-stream flush, plus one per semaphore and one per imported fence.
Fences are reference counted, so every fence an application holds keeps a syncobj alive - and
`glFenceSync` puts no bound on how many an application may hold.

Two things follow. The limit is reachable by ordinary use, not only by a bug. And the refusal
message was accusing the caller of leaking when it had done nothing wrong.

Both are fixed. The table stays fixed-size and still refuses rather than growing - a shim should
not allocate without bound on a submission path, and a refusal that names itself beats a silent
success - but it is now 1024 slots, which is 24 KiB of static storage, and the comment says plainly
that the number is a policy chosen to sit past ordinary use rather than a measurement. The
measurement that would replace it is a live high-water mark under a real workload, which needs a
submission path to exist, so it is revisited alongside D007's deferred half rather than guessed at
twice.

The refusal now says what is actually happening: a limit reached, the flush that asked for it
failing, and the constant to raise.

D007 itself needs no amendment - it never stated the size or the rationale. This one lived only in
the code, which is its own small lesson: the reasoning that does not make it into a decision
document is the reasoning nobody re-reads.

## State

96 host checks, 0 failed. The test creates two syncobjs and is indifferent to the table size, which
is correct - it checks handle behaviour, not capacity. All gates pass. Nothing deployed.
