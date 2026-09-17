# 029. Context creation is covered, and sparse is the one that is not

**2026-09-17** - roadmap unit 5

## What this entry is

Worklog 022 established that creating a screen touches no GPU. The next thing radeonsi does is
create a context, and then draw. This traces both for what they ask of the winsys.

Almost everything is already answered. The exception is sparse buffers, which Mesa advertises on
this shim's behalf and which cannot be shimmed with what the platform is known to offer - the
fourth capability in that position, and the first one a real application is likely to reach for.

## Context creation, call by call

`amdgpu_ctx_create` does five things, in order:

| Step | Reaches | Answered? |
|---|---|---|
| `ac_drm_cs_ctx_create2` | `AMDGPU_CTX`, op `ALLOC` | yes (worklog 007) |
| `ac_drm_bo_alloc`, size `gart_page_size`, heap GTT | `GEM_CREATE` | yes |
| `ac_drm_bo_cpu_map` | `GEM_MMAP`, then `drm_mmap` through patch 001 | yes |
| `memset(base, 0, alloc_size)` | the CPU mapping | - |
| `ac_drm_bo_export(..._kms, ...)` | returns `bo->handle`, no ioctl | - |

The buffer it allocates is the user fence buffer worklog 028 is about, which is why its size is
`info.gart_page_size` - the 16 KiB this winsys reports - and why the mapping has to work before a
single draw happens. Both halves line up: `GEM_MMAP` answers `addr_ptr = handle * 0x4000`, and
`oops_winsys_mmap` recovers the handle by dividing by the same constant.

So context creation needs nothing new. Nor does a first draw: buffers, GPU addresses, buffer
lists, submission and the fence wait are the six commands worklog 006 implemented and worklogs 007
and 028 finished.

## The one thing that is not covered

`AMDGPU_VA_OP_CLEAR` and `AMDGPU_VA_OP_REPLACE` refuse. Checking who calls them:

```
amdgpu_bo.c:1162   AMDGPU_VA_OP_CLEAR      amdgpu_bo_sparse_destroy
amdgpu_bo.c:1228   MAP with VM_PAGE_PRT    amdgpu_bo_sparse_create
amdgpu_bo.c:1304   AMDGPU_VA_OP_REPLACE    amdgpu_bo_sparse_commit
amdgpu_bo.c:1327   AMDGPU_VA_OP_REPLACE    amdgpu_bo_sparse_commit
```

Every one is sparse. Nothing on the road to a first frame touches them, which is the good half.

The bad half is that **Mesa advertises sparse regardless**:

```c
info->has_sparse = info->family >= CHIP_POLARIS10;          /* ac_gpu_info.c:1079 */
...
bool enable_sparse = sscreen->info.gfx_level >= GFX9 && sscreen->info.has_sparse;
caps->sparse_buffer_page_size = enable_sparse ? RADEON_SPARSE_PAGE_SIZE : 0;
```

GFX1013 is well past Polaris10 and well past GFX9, so both are true and the capability is
non-zero. This is the same shape as the three in worklog 027 - `has_userptr`, `has_syncobj`,
`has_fence_to_handle`, all hardcoded, none asked of the driver - but sharper, because sparse
buffers and sparse textures are features an application might actually reach for, where the other
three are interop that needs a second process.

## Why sparse is not merely unwritten

Worth separating from the other refusals, because "not implemented yet" would be the wrong
summary.

`REPLACE` remaps part of a live range, and `amdgpu_bo_sparse_create` maps with
`AMDGPU_VM_PAGE_PRT` - partially resident, meaning pages that fault *benignly* rather than killing
the process. That is a page-table property. oops-sdk's memory surface is
`oops_mem_alloc_direct`, `oops_mem_map_direct`, `oops_mem_batch_map` and `oops_mem_unmap`, and
`batch_map` maps a contiguous physical range at a virtual base. Nothing there expresses "map this
range so that touching it does not fault fatally".

So this is a gap in what the platform is known to offer, not a gap in this file, and writing it
would start with establishing whether PRT-style mappings exist at all - which is a measurement, not
an implementation. That is now in `buffers.c` beside the refusal, so the next reader does not spend
an afternoon assuming it is a missing `case`.

No request is filed for it. Nothing needs sparse to get a triangle on screen, and filing for a
feature nothing on the roadmap reaches would be asking obSCEne to prioritise this repository's
curiosity over its own queue. When unit 6 has a frame and something asks for sparse, the question
will have a reason behind it.

## The four claimed capabilities, collected

Worth having in one place, since this is now four:

| Capability | Set by | Winsys answer |
|---|---|---|
| `caps->resource_from_user_memory` | `has_userptr`, hardcoded | `GEM_USERPTR` refuses |
| `caps->fence_signal` | `has_syncobj`, hardcoded | `SYNCOBJ_SIGNAL` refuses |
| `caps->native_fence_fd` | `has_fence_to_handle`, hardcoded | `FENCE_TO_HANDLE` refuses, and has no caller anyway |
| `caps->sparse_buffer_page_size` | `has_sparse`, from chip family | `VA_OP_REPLACE` / `CLEAR` refuse |

None can be answered through an ioctl, because Mesa does not ask. All four would need a patch to
`ac_gpu_info.c`, which principle 1 says to avoid while a shim is possible - and here no shim is
possible, because there is nothing to shim. The honest position is that the refusals are loud and
the list is written down.

## State

Comments only in this repository; no behaviour changed. 104 host checks, 0 failed. All gates pass.
Nothing deployed, and `-5c9d` is still the outstanding question.
