# 020. The next wall is syncobj, and it comes before everything

**2026-09-17** - roadmap unit 5

## What this entry is

No code changed. This is the trace worklog 019 said was next: what radeonsi demands after the
option cache, read before it costs a launch rather than after.

The answer moves the next failure earlier than anything in worklogs 017 to 019 assumed.

## `DRM_IOCTL_SYNCOBJ_CREATE`, and where it sits

`amdgpu_winsys_create` does this, in this order:

```c
aws->info.drm_major = drm_major;
aws->info.drm_minor = drm_minor;

if (ac_drm_cs_create_syncobj2(aws->dev, 0, &aws->vm_timeline_syncobj))
   goto fail_alloc;                     /* fatal */
simple_mtx_init(&aws->vm_ioctl_lock, mtx_plain);
...
if (!do_winsys_init(aws, config, fd))   /* ac_query_gpu_info is inside here */
   goto fail_alloc;
```

`ac_drm_cs_create_syncobj2` is `dev->p->create`, which is `drm_syncobj_create`, which is
`drmSyncobjCreate` - `DRM_IOCTL_SYNCOBJ_CREATE`. The winsys shim has **no SYNCOBJ case at all**;
its 22 commands are the AMDGPU set plus `DRM_IOCTL_VERSION`.

So a timeline syncobj is created **before `do_winsys_init` runs**, and its failure is fatal. Every
answer added to `drm_device.c` over the last two days - the derived `GB_ADDR_CONFIG`, DRM 3.54,
`HW_IP_INFO`, `FW_VERSION` - lives inside `ac_query_gpu_info`, which is inside `do_winsys_init`,
which is *after* this call. None of it is reachable until syncobj creation succeeds.

**This corrects something filed.** `REQ-20260916T2208Z-7e29` argued the syncobj surface was "not
touched during device query - they are touched when something waits on a fence - so this is the
submission path, not startup", and called itself "filed ahead of need". That was wrong in the most
consequential direction: it is the first kernel object radeonsi asks for, before it asks anything
about the device. The request was more urgent than the request said.

## What the fence surface actually supports

`-7e29` came back on sweep `20260917-001421` (eboot), and the answer is unusually clean:

| | |
|---|---|
| `sceAgcDriverAddEqEvent` and the other five Eq / wait-rendering symbols | **absent** (`0x0`) |
| controls `sceAgcDriverCreateQueue`, `DestroyQueue`, `SubmitDcb` | resolved (`0x1`) |
| fence width | **64-bit**: `fence-val-lo 0xbeefcafe`, `fence-val-hi 0x12345678`, `fence-bytes-landed 0x8` |

So:

- **A timeline is representable.** The hardware writes a full 64-bit monotonic value, which is what
  a timeline semaphore needs. `has_timeline_syncobj` need not be false forever.
- **There is no blocking wait.** No event-queue registration exists on retail 12.40, so a wait is
  a poll. That is what `166-agc/driver-submit-fence` has always done - a 10,000-iteration loop with
  `sceKernelUsleep(100)` - and it is now known to be the only option rather than the probe's
  shortcut.
- **There is no GPU-side wait packet**, so one submit cannot be made to wait on another's fence
  without the CPU in between.

The resolution's `logs:` line points at `20260916-235024-payload.obs.log`; the rows are in
`20260917-001421-eboot.obs.log`. The `sweep:` field names both, and the data is real - checked row
by row, including the controls.

## What this implies for the shim

Creating and destroying a syncobj needs no hardware: a syncobj is a handle onto a fence, and the
handle table is ours. That is enough to get past `amdgpu_winsys_create` and into
`ac_query_gpu_info`, which is where two days of answers are waiting to be exercised for the first
time.

Waiting and signalling are the part that needs the fence, and the shape is now known rather than
guessed: 64-bit monotonic values, compared by polling. Whether the first implementation offers a
timeline or only a binary syncobj is a decision to write down before the code, because
`has_timeline_syncobj` changes what radeonsi does with it.

## State

Unchanged from 019 - host suite 70 of 70, the title builds, nothing deployed. What is different is
that the next failure is named and sits earlier than expected, and the data needed to fix it
properly is in hand rather than outstanding.
