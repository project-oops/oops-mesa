# 021. The wall was earlier again, and this time the whole path is written down

**2026-09-17** - roadmap unit 5

## What this entry is

Worklog 020 found that syncobj creation comes before `do_winsys_init` and called it "the next
wall". It was a wall, and it is now implemented. It was not the next one.

This entry walks the startup path from the first statement libdrm executes to the last line of
`ac_query_gpu_info`, in order, and records for each call whether a refusal is fatal. Two calls
that were falling to the default branch are now answered. The rest of the path is written down so
that the next failure is looked up rather than discovered.

## The first statement is not the version check

`amdgpu_winsys_create` begins with `ac_drm_device_initialize`, which begins with
`_amdgpu_device_initialize`, which begins with this:

```c
r = amdgpu_get_auth(fd, &flag_auth);
if (r) {
        fprintf(stderr, "%s: amdgpu_get_auth (1) failed (%i)\n", __func__, r);
        pthread_mutex_unlock(&dev_mutex);
        return r;
}
```

Nothing precedes it. Not `drmGetVersion`, not `AMDGPU_INFO_ACCEL_WORKING`. And `amdgpu_get_auth`
is two lines:

```c
if (drmGetNodeTypeFromFd(fd) == DRM_NODE_RENDER)
        *auth = 0;
else
        r = drmIoctl(fd, DRM_IOCTL_GET_CLIENT, &client);
```

`DRM_IOCTL_GET_CLIENT` had no case in `drm_device.c`. It fell to the default branch, which logs
"is not one this shim knows" and returns `-EINVAL`. So `_amdgpu_device_initialize` returned on its
first statement, and the failure surfaced two frames up as:

```
amdgpu: amdgpu_device_initialize failed.
```

which says nothing about which of the thirty-odd things that function does went wrong.

## The shortcut is not available to any descriptor

The obvious reading is that the shim hands back the wrong kind of descriptor - take the render-node
branch and the ioctl never happens. That reading is wrong, and it is worth being precise about why,
because it would otherwise look like a one-line fix in `oops_winsys_open`.

`drmGetNodeTypeFromFd` does not ask the driver anything. It `fstat`s the descriptor and then
requires three things of the result: that it is a character device, that its major and minor fall
inside the range Linux reserves for DRM, and that a matching node exists under `/dev/dri`. This
platform has no `/dev/dri` and no DRM major number. There is no descriptor - token, real file, or
anything else - that this shim could return and have that function answer `DRM_NODE_RENDER`.

So the ioctl is the only route, and answering it is the only fix.

## What the answer is, and why it is not a guess

libdrm reads exactly one field of the reply: `client.auth`. It is then stored in `flag_auth`, which
is used only when a second device handle already exists for the same descriptor - which never
happens on the first call, because the device list is empty.

The value is nonetheless not arbitrary, and it is not invented here. It is the value libdrm's own
render-node branch assigns without asking anybody: `*auth = 0`. A render node reports
unauthenticated because there is no DRM master to authenticate against. That is this platform
exactly - no display server, no master, no magic handshake. Answering zero hands libdrm the
conclusion it would have reached itself if `fstat` had been able to see what this device is.

A query for any index other than 0 refuses. There is one client, this process, and a kernel answers
a query past the end of its client table with `-EINVAL` rather than with an empty record.

## `DRM_IOCTL_GET_CAP`, and the lever behind D007

The second unanswered call is `drmGetCap`, issued from `util_sync_provider_drm` while the device
handle is being built:

```c
uint64_t cap;
int err = drmGetCap(drm_fd, DRM_CAP_SYNCOBJ_TIMELINE, &cap);
if (err == 0 && cap != 0) {
   d->base.timeline_signal = drm_syncobj_timeline_signal;
   d->base.timeline_wait = drm_syncobj_timeline_wait;
}
```

This settles something D007 asserted from reasoning rather than from source. `ac_query_gpu_info`
sets `info->has_timeline_syncobj` from `...->timeline_wait != NULL` - a function-pointer test, not
a capability query - and the only thing that installs that pointer is this `drmGetCap`. So D007's
claim that the timeline decision is reversible by answering `GET_CAP` is correct, and this is the
one place it is read.

Answering it zero is that decision being carried, not a capability being denied. The same case
answers `DRM_CAP_ADDFB2_MODIFIERS`, which reaches `has_modifiers`; nothing here shares a buffer
between processes, and `ADDFB2` belongs to a mode-setting interface this shim does not implement at
all. Every other capability refuses and names itself.

Neither caller distinguishes a refusal from a zero, so this changes no behaviour today. It is done
so that a capability this repository has reasoned about cannot be confused, in the log, with one it
has never heard of.

## The whole startup path, in order

Fatal means the call's failure aborts device creation. Everything is `drmIoctl` through the shim
unless the note says otherwise.

| # | Call | Disposition | Answered? |
|---|---|---|---|
| 1 | `DRM_IOCTL_GET_CLIENT` | **fatal** | now |
| 2 | `DRM_IOCTL_VERSION` | **fatal**, and the result is dereferenced without a null check | yes, 3.54 |
| 3 | `AMDGPU_INFO_ACCEL_WORKING` | **fatal**, and a zero answer is also fatal | yes |
| 4 | `AMDGPU_INFO_DEV_INFO` | **fatal** | yes |
| 5 | `READ_MMR_REG 0x263e` (GB_ADDR_CONFIG) | **fatal** | yes, derived (017) |
| 6 | `amdgpu_query_gfx_level_major` | tolerated - only picks a VA-manager flag | yes |
| 7 | `amdgpu_parse_asic_ids` | tolerated, returns void; reads a file that does not exist here | n/a |
| 8 | `DRM_IOCTL_GET_CAP` (SYNCOBJ_TIMELINE) | tolerated - no timeline | now |
| 9 | `DRM_IOCTL_SYNCOBJ_CREATE` | **fatal** | yes (020, D007) |
| 10 | `ac_drm_query_pci_bus_info` | tolerated here: `require_pci_bus_info` is false | no - `pci.valid` goes false |
| 11 | DRM minor `< 54` | **fatal** | yes |
| 12 | sync provider `->wait == NULL` | **fatal** | yes - a pointer test, always non-null |
| 13 | `ac_drm_query_gpu_info` | **fatal** | yes - reads libdrm's cache, no ioctl |
| 14 | `AMDGPU_INFO_DEV_INFO` (again, Mesa's own copy) | **fatal** | yes |
| 15 | `HW_IP_INFO` for each of 12 IP types | **fatal** only if neither GFX nor COMPUTE has a ring | yes, GFX (017) |
| 16 | `FW_VERSION` for ME, MEC, PFP | **fatal**, each of the three | yes, zero (017) |
| 17 | `FW_VERSION` for VCN / VCE / UVD | **fatal** - but reached only if that IP reported queues | n/a, refused at 15 |
| 18 | `amdgpu_query_sw_info(address32_hi)` | **fatal** | yes - reads the VA manager, no ioctl |
| 19 | `AMDGPU_INFO_MEMORY` | **fatal** | yes (016) |
| 20 | `ac_identify_chip` | **fatal** (`UNIMPLEMENTED_HW`) | pure function of #14 and #15; checked by hand below |
| 21 | `AMDGPU_INFO_MAX_IBS` | tolerated, with a documented fallback | no - the fallback is better than a guess |
| 22 | `DRM_IOCTL_GET_CAP` (ADDFB2_MODIFIERS) | tolerated | now |

Three things in that table are worth saying out loud.

**Entry 5 vindicates worklog 017 more strongly than 017 knew.** `GB_ADDR_CONFIG` is not read by
Mesa first - it is read by *libdrm*, inside `amdgpu_query_gpu_info_init`, and the error is
returned. It is on the fatal path of device initialisation, before `ac_query_gpu_info` begins. The
derivation was not an optimisation of a later step; without it nothing starts.

**Entry 21 is a case where refusing is the right answer.** Mesa's fallback for `MAX_IBS` is a table
of per-engine limits with a comment explaining where they come from. That is better sourced than
anything this shim could answer, so the refusal stays.

**Entry 10 costs a line of stderr and nothing else.** `drmGetDevice2` wants sysfs; it will fail,
print `amdgpu: drmGetDevice2 failed.`, and leave `info->pci.valid` false. Worth knowing so the line
is not read as the cause of a later failure.

## Entry 20 has two fatal branches, and only one of them was known about

`ac_identify_chip` is a pure function of answers this repository already gives, so it can be
checked by reading rather than by running. It fails in two separate places.

The first is the family table, which is what worklog 017 reasoned about: `FAMILY_NV` with an
external revision in the GFX1013 range gives `CHIP_GFX1013`. `device_info.c` reports `0x8F` and
`0x82`, and `test_device_is_identifiable_as_gfx1013` already pins both against Mesa's own
constants. That branch passes.

The second was not on anybody's list. Having named the chip, the function derives `gfx_level` from
the GFX IP version - and that if-chain has no default:

```c
else if (info->ip[AMD_IP_GFX].ver_major == 10 && info->ip[AMD_IP_GFX].ver_minor == 3)
   info->gfx_level = GFX10_3;
else if (info->ip[AMD_IP_GFX].ver_major == 10 && info->ip[AMD_IP_GFX].ver_minor == 1)
   info->gfx_level = GFX10;
...
else {
   fprintf(stderr, "amdgpu: Unknown gfx version: %u.%u\n", ...);
   return false;
}
```

`10.0` matches nothing. Only `10.1` and `10.3` exist for this generation, so a `HW_IP_INFO` reply
carrying major 10 and a zeroed minor fails the device with a message about a gfx version, several
steps away from the query that produced it.

The reply does carry `hw_ip_version_minor = 1` and always has, so nothing is broken. But the host
suite was checking the major and not the minor - which is the wrong half, because the major is the
part that is obviously load-bearing and the minor is the part that silently is. It is checked now,
with the reason beside it. This is the same shape as the GFX10.3-or-GFX1013 question worklog 017
settled, arriving from a different direction: the part is GFX10.1, and two separate places in Mesa
now depend on this repository saying so consistently.

## What was done

- `DRM_IOCTL_GET_CLIENT` and `DRM_IOCTL_GET_CAP` answered in `src/winsys/drm_device.c`, both
  exported so the host suite can check them.
- Eight host checks covering both, including that an undecided capability still refuses.
- A ninth pinning `hw_ip_version_minor`, for the reason above.
- Host suite 90 of 90. The title builds; the import manifest is unchanged at 504 placed, 0 unknown,
  because neither answer calls anything new.

## State

Every call on the table above is either answered or deliberately refused with the consequence
known. That is the first time this repository can say the startup path is closed rather than
closed-as-far-as-was-traced - and the previous two entries each said "next wall" about something
that turned out to have an earlier one in front of it, so the table is the deliverable here, not
the two cases.

What it does not say is that the path *succeeds*. Entry 20 is checked by reading, which is worth
more than nothing and less than a run; entries 3 through 5 answer from values this repository
decided rather than measured; and nothing beyond `ac_query_gpu_info` - `ac_addrlib_create`, then
`si_create_screen` - has been traced at all. The next useful thing is either that trace or a run.

Nothing deployed.
