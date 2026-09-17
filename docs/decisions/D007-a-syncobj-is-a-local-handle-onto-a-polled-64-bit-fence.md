# D007 - A syncobj is a local handle onto a polled 64-bit fence, and it starts binary

**decided** · 2026-09-17 (measured before deciding; the measurements are named below)

`amdgpu_winsys_create` creates a timeline syncobj **before** it asks the device anything:

```c
if (ac_drm_cs_create_syncobj2(aws->dev, 0, &aws->vm_timeline_syncobj))
   goto fail_alloc;                     /* fatal */
...
if (!do_winsys_init(aws, config, fd))   /* ac_query_gpu_info is inside here */
```

So `DRM_IOCTL_SYNCOBJ_CREATE` gates everything this repository has built. `GB_ADDR_CONFIG`, the
DRM version, `HW_IP_INFO` and `FW_VERSION` all live inside `ac_query_gpu_info`, which is
downstream of that call (worklog 020).

## What the platform actually offers

`REQ-20260916T2208Z-7e29`, sweep `20260917-001421`:

| | |
|---|---|
| `sceAgcDriverAddEqEvent` and the five other Eq / wait-rendering symbols | **absent** |
| controls `sceAgcDriverCreateQueue`, `DestroyQueue`, `SubmitDcb` | resolved |
| fence width | **64-bit**: `fence-val-lo 0xbeefcafe`, `fence-val-hi 0x12345678`, `fence-bytes-landed 0x8` |

A submitted command buffer writes a full 64-bit value to memory when it retires. Nothing exists to
block on that write, and no packet exists to make one submit wait on another's fence.

## The decision

**A syncobj is a handle in this repository's own table, holding a 64-bit fence value and the
address the GPU will write it to. Creating and destroying one touches no hardware. Waiting polls.
Timeline support stays off until something needs it.**

Three parts, each with its own reason.

**Create and destroy are local, and that is not a shortcut.** A syncobj on Linux is a kernel
object wrapping a fence; the handle namespace is the kernel's because the kernel owns the fence.
Here the fence is a value in memory this shim allocated, so the namespace is already ours. There
is nothing to ask the platform for, and asking would be inventing an interface rather than
shimming one. `buffers.c` keeps its buffer handles the same way (D005's division: libdrm's
bookkeeping is upstream's, the objects underneath it are ours).

**Waiting polls, because the measurement says there is no alternative.** The Eq event family does
not exist on retail 12.40, so a blocking wait cannot be built - not "has not been built yet". The
probe's own `166-agc/driver-submit-fence` has always polled, and that is now known to be the only
option rather than the harness taking a shortcut. A wait is therefore a loop over the fence value
with a sleep, and its cost is a real cost to be written down, not hidden.

**Timeline stays off, for now, and the reason is a capability the shim declines to claim.**
`has_timeline_syncobj` is set from `->timeline_wait != NULL`, which `util_sync_provider_drm` sets
only when `drmGetCap(DRM_CAP_SYNCOBJ_TIMELINE)` succeeds *and* reports a non-zero value. So Mesa
concludes there is no timeline and uses binary syncobjs. That conclusion is currently *true* and
costs nothing: radeonsi works without timelines.

*(Amended 2026-09-17, worklog 021.* This paragraph originally said the shim refuses `GET_CAP`,
which was true when it was written and is no longer. `GET_CAP` is now answered, and answers
`DRM_CAP_SYNCOBJ_TIMELINE` with zero - the same conclusion by a more deliberate route, so that a
capability this repository has decided about is distinguishable in the log from one it has never
heard of. The decision below is unchanged, and this is still the one lever that carries it.)*

It is worth saying what makes this reversible, because the 64-bit measurement is exactly what a
timeline needs. A timeline semaphore is a monotonically increasing 64-bit counter with waits on
"reaches at least N", and the hardware writes a monotonic 64-bit value. So the day a timeline is
wanted, the fence supports it and the change is to answer `GET_CAP` and implement the timeline
half - not to discover the hardware cannot do it.

## What is deliberately not decided here

**Wait, signal and reset.** They are reachable only once something submits work, and nothing does
yet. Implementing them now would be writing against a submission path that does not exist, and
the first version would be guesswork about how radeonsi threads fences through it. Create and
destroy are what `amdgpu_winsys_create` needs, and they are what this decision covers; the rest
refuse loudly and name themselves, as every other unimplemented command does.

**Whether a syncobj should hold a GPU address at all.** The table below reserves room for one, but
until a submit attaches a fence to a syncobj there is no evidence about which address, written by
what, at which point. That is a measurement waiting on unit 5's gate, not a design question.
