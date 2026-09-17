/*
 * Buffer objects, and the address space they live in.
 *
 * On Linux the kernel owns both: a GEM handle names an allocation, and a separate call asks the
 * kernel to map that allocation at a virtual address libdrm's own manager picked. This file is
 * the same split over the platform's direct-memory calls, because libdrm expects the split and
 * gets confused by a winsys that folds them together.
 *
 *   GEM_CREATE   reserves physical memory and nothing else. No address yet, exactly as the
 *                kernel behaves: a buffer with no mapping is a legitimate state.
 *   GEM_VA       maps that physical memory at the address libdrm chose, page by page, through
 *                the platform's batch-map call.
 *   GEM_MMAP     hands back a token the shim's own mmap turns into a CPU pointer.
 *
 * # The address space is shared, and that is measured
 *
 * oops-gl passes the pointer from a managed allocation straight to the GPU as a command-buffer
 * address and as a colour-target address, and the GPU reads both (oops-sdk's oracle record for
 * firmware 12.40). So a virtual address means the same thing to both sides here, which is why
 * GEM_VA can map at an address libdrm chose rather than having to relocate anything.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "drm-uapi/amdgpu_drm.h"

#include "oops/memory.h"
#include "oops_winsys.h"

/* The page size the kernel reports and the granularity batch-map works in (obSCEne 135-sysctl,
 * hw.pagesize = 0x4000 on firmware 12.40). */
#define OOPS_WINSYS_PAGE 0x4000u

/* A fixed table rather than a growing one. radeonsi's buffer count for a frame is in the
 * hundreds, and a fixed table cannot fail to allocate while handling an allocation. When it
 * fills, the call refuses and says so instead of overwriting a live entry. */
#define OOPS_WINSYS_MAX_BO 4096

struct oops_winsys_bo {
    /* Liveness is its own flag rather than a sentinel in one of the fields. The first version
     * read "physical offset is not negative" as live, and zero is a perfectly valid physical
     * offset - oops-sdk.s own header says so in as many words - so every handle in an empty
     * table looked live. The host suite caught it on a buffer list naming a stale handle. */
    bool     live;
    uint64_t size;      /* rounded up to a page */
    uint64_t alignment; /* as asked for, so GEM_OP can report the creation back */
    uint64_t domain_flags;
    int64_t  phys;      /* the direct-memory offset backing it */
    uint64_t gpu_va;    /* where GEM_VA mapped it, or 0 */
    void    *cpu_ptr;   /* where the shim's mmap put it, or NULL */
    uint32_t domain;    /* the AMDGPU_GEM_DOMAIN_* it was asked for */
};

static struct oops_winsys_bo s_bo[OOPS_WINSYS_MAX_BO];

/* Handle 0 is "no buffer" to libdrm, so the table is indexed from one. */
/* Whether a handle names a live buffer. The buffer-list check uses it to turn a stale handle
 * into a refusal here rather than a GPU fault in a later frame. */
bool oops_winsys_bo_is_live(uint32_t handle);

static struct oops_winsys_bo *bo_of(uint32_t handle)
{
    if (handle == 0 || handle > OOPS_WINSYS_MAX_BO) {
        return NULL;
    }
    struct oops_winsys_bo *bo = &s_bo[handle - 1];
    return bo->live ? bo : NULL;
}

uint64_t oops_winsys_bo_bytes_live(void)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < OOPS_WINSYS_MAX_BO; i++) {
        if (s_bo[i].live) {
            total += s_bo[i].size;
        }
    }
    return total;
}

static uint64_t round_up_page(uint64_t n)
{
    return (n + OOPS_WINSYS_PAGE - 1u) & ~(uint64_t)(OOPS_WINSYS_PAGE - 1u);
}

int oops_winsys_gem_create(union drm_amdgpu_gem_create *arg)
{
    uint64_t size = round_up_page(arg->in.bo_size);
    uint64_t align = arg->in.alignment < OOPS_WINSYS_PAGE ? OOPS_WINSYS_PAGE : arg->in.alignment;
    oops_mem_type_t type;
    int64_t phys = 0;
    uint32_t handle = 0;

    /* The argument is a union, so anything not overwritten on the way out is still the caller.s
     * input read as an output. A refused create therefore clears the handle explicitly: a caller
     * that forgets to check the return code gets zero, which is "no buffer", rather than the low
     * half of the size it asked for, which would look like a perfectly good handle. */
    if (size == 0) {
        arg->out.handle = 0;
        return -EINVAL;
    }

    /* The domain says where the driver wants it. This platform has one physical pool and the
     * distinction that survives is the cache policy: a buffer the GPU reads hot wants the
     * write-combined path, one the CPU reads back wants the cached one. That mapping is
     * oops-gl's, which measured both on this firmware. */
    if (arg->in.domains & AMDGPU_GEM_DOMAIN_VRAM) {
        type = OOPS_MEM_WC_GARLIC;
    } else {
        type = OOPS_MEM_WB_ONION;
    }

    for (uint32_t i = 0; i < OOPS_WINSYS_MAX_BO; i++) {
        if (!s_bo[i].live) {
            handle = i + 1;
            break;
        }
    }
    if (handle == 0) {
        oops_winsys_log("buffer table is full at %u entries", OOPS_WINSYS_MAX_BO);
        arg->out.handle = 0;
        return -ENOMEM;
    }

    if (oops_mem_alloc_direct((size_t)size, (size_t)align, type, &phys) != 0) {
        oops_winsys_log("direct allocation of %llu bytes refused",
                        (unsigned long long)size);
        arg->out.handle = 0;
        return -ENOMEM;
    }

    struct oops_winsys_bo *bo = &s_bo[handle - 1];
    bo->live = true;
    bo->size = size;
    bo->alignment = align;
    bo->domain_flags = arg->in.domain_flags;
    bo->phys = phys;
    bo->gpu_va = 0;
    bo->cpu_ptr = NULL;
    bo->domain = (uint32_t)arg->in.domains;

    memset(&arg->out, 0, sizeof(arg->out));
    arg->out.handle = handle;
    return 0;
}

int oops_winsys_gem_close(uint32_t handle)
{
    struct oops_winsys_bo *bo = bo_of(handle);
    if (!bo) {
        return -EINVAL;
    }
    if (bo->cpu_ptr) {
        oops_mem_unmap(bo->cpu_ptr, (size_t)bo->size);
    }
    if (bo->gpu_va) {
        oops_mem_unmap((void *)(uintptr_t)bo->gpu_va, (size_t)bo->size);
    }
    oops_mem_free_direct(bo->phys, (size_t)bo->size);
    memset(bo, 0, sizeof(*bo));
    return 0;
}

/*
 * The offset libdrm passes to mmap. On Linux it is a cookie into the device's address space and
 * carries no meaning to userspace; the same is true here, so it carries the handle.
 */
int oops_winsys_gem_mmap(union drm_amdgpu_gem_mmap *arg)
{
    struct oops_winsys_bo *bo = bo_of(arg->in.handle);
    uint32_t handle = arg->in.handle;

    if (!bo) {
        return -EINVAL;
    }
    memset(&arg->out, 0, sizeof(arg->out));
    arg->out.addr_ptr = (uint64_t)handle * OOPS_WINSYS_PAGE;
    return 0;
}

void *oops_winsys_mmap(int fd, size_t length, uint64_t offset)
{
    uint32_t handle = (uint32_t)(offset / OOPS_WINSYS_PAGE);
    struct oops_winsys_bo *bo;

    (void)fd;
    bo = bo_of(handle);
    if (!bo || length > bo->size) {
        return NULL;
    }
    if (!bo->cpu_ptr) {
        void *v = NULL;
        if (oops_mem_map_direct(&v, (size_t)bo->size, OOPS_PROT_CPU_RW, 0,
                                bo->phys, OOPS_WINSYS_PAGE) != 0) {
            oops_winsys_log("cpu mapping of buffer %u refused", handle);
            return NULL;
        }
        bo->cpu_ptr = v;
    }
    return bo->cpu_ptr;
}

/*
 * The CPU address of a buffer this shim created, by handle rather than by the encoded offset
 * `oops_winsys_mmap` takes. Submission needs it: an `AMDGPU_CHUNK_ID_FENCE` chunk names a buffer
 * and a byte offset inside it, and the sequence number has to land there for radeonsi's fence
 * wait to see it (worklog 028).
 *
 * The range is checked rather than trusted. A chunk naming an offset past the end of the buffer
 * it also names is a caller error, and writing there would corrupt whatever follows instead of
 * saying so.
 */
void *oops_winsys_bo_cpu_range(uint32_t handle, uint64_t offset, uint64_t bytes)
{
    struct oops_winsys_bo *bo = bo_of(handle);

    if (!bo) {
        oops_winsys_log("buffer %u is not live, so it has no address to write to", handle);
        return NULL;
    }
    if (offset > bo->size || bytes > bo->size - offset) {
        oops_winsys_log("buffer %u is %llu bytes; %llu at offset %llu does not fit",
                        handle, (unsigned long long)bo->size, (unsigned long long)bytes,
                        (unsigned long long)offset);
        return NULL;
    }
    if (!bo->cpu_ptr) {
        void *v = NULL;
        if (oops_mem_map_direct(&v, (size_t)bo->size, OOPS_PROT_CPU_RW, 0,
                                bo->phys, OOPS_WINSYS_PAGE) != 0) {
            oops_winsys_log("cpu mapping of buffer %u refused", handle);
            return NULL;
        }
        bo->cpu_ptr = v;
    }
    return (char *)bo->cpu_ptr + offset;
}

int oops_winsys_munmap(void *addr, size_t length)
{
    /* The mapping belongs to the buffer and is released when the buffer is closed, so an
     * unmap here would leave a live handle pointing at nothing. libdrm unmaps before it
     * closes, and treating this as a no-op keeps that order safe. */
    (void)addr;
    (void)length;
    return 0;
}

/*
 * Map or unmap a buffer at the address libdrm's own manager chose.
 *
 * This is the one command that would be impossible if the two address spaces were separate. The
 * platform's batch-map call takes a virtual address and a physical offset, which is exactly the
 * page-table operation the kernel performs on Linux.
 */
int oops_winsys_gem_va(struct drm_amdgpu_gem_va *arg)
{
    struct oops_winsys_bo *bo = bo_of(arg->handle);
    uint8_t prot = 0;

    if (!bo) {
        return -EINVAL;
    }
    if (arg->offset_in_bo != 0) {
        /* A partial mapping is legitimate on Linux and nothing here has needed one. Refusing is
         * better than mapping the whole buffer and appearing to have honoured the offset. */
        oops_winsys_log("GEM_VA with a non-zero offset into the buffer is not implemented");
        return -ENOSYS;
    }

    if (arg->flags & AMDGPU_VM_PAGE_READABLE) prot |= OOPS_PROT_GPU_READ;
    if (arg->flags & AMDGPU_VM_PAGE_WRITEABLE) prot |= OOPS_PROT_GPU_WRITE;
    if (prot == 0) prot = OOPS_PROT_GPU_READ;

    switch (arg->operation) {
    case AMDGPU_VA_OP_MAP: {
        uint64_t size = arg->map_size ? round_up_page(arg->map_size) : bo->size;
        if (oops_mem_batch_map((void *)(uintptr_t)arg->va_address, bo->phys,
                               (size_t)size, OOPS_WINSYS_PAGE, prot) != 0) {
            oops_winsys_log("mapping buffer %u at 0x%llx refused", arg->handle,
                            (unsigned long long)arg->va_address);
            return -ENOMEM;
        }
        bo->gpu_va = arg->va_address;
        return 0;
    }
    case AMDGPU_VA_OP_UNMAP:
        if (bo->gpu_va) {
            oops_mem_unmap((void *)(uintptr_t)bo->gpu_va, (size_t)bo->size);
            bo->gpu_va = 0;
        }
        return 0;
    case AMDGPU_VA_OP_CLEAR:
    case AMDGPU_VA_OP_REPLACE:
        /*
         * Sparse buffers, and only sparse buffers. Every call site for these two in Mesa is in
         * `amdgpu_bo_sparse_create`, `_commit` or `_destroy`, so nothing on the path to a first
         * frame reaches them (worklog 029).
         *
         * They are not merely unwritten. `REPLACE` remaps part of a live range and the sparse
         * path also maps with `AMDGPU_VM_PAGE_PRT`, which asks for pages that fault benignly -
         * a page-table property, and oops-sdk's memory surface has nothing that expresses it
         * (`oops_mem_batch_map` maps a contiguous physical range and that is all). So this is a
         * gap in what the platform is known to offer, not a gap in this file.
         *
         * Mesa advertises sparse anyway: `has_sparse` is hardcoded from the chip family and
         * reaches `caps->sparse_buffer_page_size`. An application that believes it gets this
         * refusal, under its own name, rather than a wrong result.
         */
        oops_winsys_log("GEM_VA operation %u is sparse-only and is not implemented; sparse needs "
                        "PRT mappings, which this platform is not known to offer",
                        arg->operation);
        return -ENOSYS;
    default:
        return -EINVAL;
    }
}

bool oops_winsys_bo_is_live(uint32_t handle)
{
    return bo_of(handle) != NULL;
}

/*
 * Has the GPU finished with this buffer?
 *
 * The answer here is always yes, and it is correct rather than convenient: submission waits for
 * its own fence before returning (see submit.c), so every stream handed to the driver has
 * retired by the time anything can ask this. There is no window in which a buffer is busy.
 *
 * That stops being true the moment submission becomes asynchronous, and this is one of the two
 * places that would have to change - the other being WAIT_CS. Both say so, so that neither is
 * quietly left answering from an assumption that no longer holds.
 */
int oops_winsys_gem_wait_idle(union drm_amdgpu_gem_wait_idle *arg)
{
    struct oops_winsys_bo *bo = bo_of(arg->in.handle);
    uint32_t domain;

    if (!bo) {
        return -EINVAL;
    }
    domain = bo->domain;

    memset(&arg->out, 0, sizeof(arg->out));
    arg->out.status = 0;   /* 0 is idle, 1 is busy */
    arg->out.domain = domain;
    return 0;
}

/*
 * Ask about a buffer, or ask to move it.
 *
 * The first is answered from the table. The second cannot be: the domain a buffer was created
 * with chose its cache policy, and on this platform that is fixed when the pages are mapped.
 * Honouring a change would mean reallocating and remapping behind a caller that still holds the
 * old pointer, so it is refused instead. A caller that asked to move a buffer and got success
 * would reasonably believe it had moved.
 */
int oops_winsys_gem_op(struct drm_amdgpu_gem_op *arg)
{
    struct oops_winsys_bo *bo = bo_of(arg->handle);

    if (!bo) {
        return -EINVAL;
    }

    switch (arg->op) {
    case AMDGPU_GEM_OP_GET_GEM_CREATE_INFO: {
        struct drm_amdgpu_gem_create_in *info =
            (struct drm_amdgpu_gem_create_in *)(uintptr_t)arg->value;
        if (!info) {
            return -EINVAL;
        }
        memset(info, 0, sizeof(*info));
        info->bo_size = bo->size;
        info->alignment = bo->alignment;
        info->domains = bo->domain;
        info->domain_flags = bo->domain_flags;
        return 0;
    }

    case AMDGPU_GEM_OP_SET_PLACEMENT:
        oops_winsys_log("buffer %u cannot be moved between domains after it is created",
                        arg->handle);
        return -ENOSYS;

    default:
        oops_winsys_log("buffer operation %u is not one this shim knows", arg->op);
        return -EINVAL;
    }
}

/*
 * The form libdrm's mmap macro wants: a pointer, or MAP_FAILED. Kept here rather than in the
 * patch so the patch stays two hunks of routing with no logic in it.
 */
void *oops_winsys_mmap_or_failed(int fd, size_t length, uint64_t offset)
{
    void *p = oops_winsys_mmap(fd, length, offset);
    return p ? p : (void *)-1;   /* MAP_FAILED */
}
