/*
 * The kernel interface libdrm believes it is talking to.
 *
 * libdrm's AMD layer reaches the kernel through three thin wrappers - drmCommandWriteRead,
 * drmCommandWrite and drmIoctl - and all three are ioctl() underneath. So this file is the
 * whole of the winsys seam: a descriptor that stands for the device, an ioctl that dispatches
 * the AMD command set onto the vendor graphics driver oops-sdk binds, and mmap for buffer
 * access. libdrm's own bookkeeping - virtual address ranges, buffer objects, command-stream
 * chunk assembly, fences - is upstream's and is not reimplemented here (D005).
 *
 * # Every command is present, and the ones with nothing behind them say so
 *
 * The 22 cases below are the complete set libdrm's amdgpu layer issues, taken from its sources
 * at the pinned version. A command that is not implemented returns -ENOSYS and logs its own
 * name. It never returns success having done nothing: a winsys that silently succeeds produces
 * a black frame and no reason for it, which is the failure oops-gl's badge taught this
 * collection to refuse (CLAUDE.md, principle 4; orbistoun worklog 539).
 *
 * # What is measured and what is not
 *
 * AMDGPU_INFO is where radeonsi asks what the device is, and this platform has not been asked
 * that question yet. REQ-20260914T1558Z-7d41 is on the obSCEne bus for it. Until it returns,
 * the INFO case answers only what the collection has actually measured and refuses the rest,
 * rather than answering from a PC part of the same family.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "drm-uapi/amdgpu_drm.h"
#include "drm-uapi/drm.h"

#include "oops/memory.h"
#include "agc/driver.h"

#include "oops_winsys.h"

/* The one device this shim serves. libdrm opens a node and keeps the descriptor; nothing here
 * needs a file system, so the descriptor is a token rather than an index into one. */
#define OOPS_WINSYS_FD 0x0057

/* AMDGPU_INFO carries the destination pointer and its size inside the request, so a reply is a
 * bounded copy rather than a write through the request structure itself. */
int oops_winsys_copy_out(const void *request, const void *src, unsigned long size)
{
    const struct drm_amdgpu_info *info = (const struct drm_amdgpu_info *)request;
    unsigned long n = size < info->return_size ? size : info->return_size;

    if (!info->return_pointer || !n) {
        return -EINVAL;
    }
    memcpy((void *)(uintptr_t)info->return_pointer, src, n);
    return 0;
}

static int unimplemented(const char *name)
{
    oops_winsys_log("ioctl %s is not implemented yet", name);
    return -ENOSYS;
}

/*
 * The device description. radeonsi reads this before it programs anything and will not start
 * without plausible answers, so every field here has to say where it came from.
 *
 * Nothing in this function is measured on this console yet. The request that would settle it is
 * REQ-20260914T1558Z-7d41; until it returns, this refuses rather than inventing, and the one
 * value it does answer is the one the collection has actually seen.
 */
static int amdgpu_info(struct drm_amdgpu_info *info)
{
    switch (info->query) {
    case AMDGPU_INFO_ACCEL_WORKING: {
        /* The first thing libdrm asks, and it refuses to initialise the device on a zero. It
         * means "is the graphics engine usable", and on this platform the answer is the one
         * D003's measurement established: a command stream we built ran on this queue next to
         * the compositor and retired (worklog 002). */
        uint32_t working = 1;
        return oops_winsys_copy_out(info, &working, sizeof(working));
    }

    case AMDGPU_INFO_READ_MMR_REG:
        /*
         * Reading a hardware register directly. libdrm's device initialisation needs exactly one
         * of these before it will finish, unconditionally and whatever the chip:
         * `GB_ADDR_CONFIG` at dword offset 0x263e, which describes how memory is banked and
         * interleaved and which the tiling library cannot be correct without.
         *
         * Nothing in this collection has read it. Guessing it would not fail here; it would
         * produce a surface layout that is wrong in a way that looks like corruption much later,
         * which is the worst shape of error this project can produce. So it refuses, and
         * REQ-20260914T1712Z-5b60 asks the console for it.
         */
        oops_winsys_log("register read 0x%x is not answered; nothing has measured it",
                        info->read_mmr_reg.dword_offset);
        return -ENOSYS;

    case AMDGPU_INFO_DEV_INFO: {
        /* Answered from device_info.c, where every field says how it is known. Six groups are
         * still assumed rather than measured; it logs that rather than hiding it. */
        struct drm_amdgpu_info_device dev;
        oops_winsys_device_info(&dev);
        return oops_winsys_copy_out(info, &dev, sizeof(dev));
    }
    case AMDGPU_INFO_MEMORY:
        return unimplemented("AMDGPU_INFO_MEMORY");
    case AMDGPU_INFO_FW_VERSION:
        return unimplemented("AMDGPU_INFO_FW_VERSION");
    case AMDGPU_INFO_HW_IP_INFO:
        return unimplemented("AMDGPU_INFO_HW_IP_INFO");
    default:
        return unimplemented("AMDGPU_INFO (unrecognised query)");
    }
}

int oops_winsys_ioctl(int fd, unsigned long request, void *arg)
{
    if (fd != OOPS_WINSYS_FD) {
        return -EBADF;
    }
    (void)arg;

    switch (request) {
    /* The path to a first frame. These six are what a triangle needs, and they are the order
     * unit 5 implements them in. */
    case DRM_IOCTL_AMDGPU_INFO:          return amdgpu_info((struct drm_amdgpu_info *)arg);
    case DRM_IOCTL_AMDGPU_GEM_CREATE:    return oops_winsys_gem_create((union drm_amdgpu_gem_create *)arg);
    case DRM_IOCTL_AMDGPU_GEM_MMAP:      return oops_winsys_gem_mmap((union drm_amdgpu_gem_mmap *)arg);
    case DRM_IOCTL_AMDGPU_GEM_VA:        return oops_winsys_gem_va((struct drm_amdgpu_gem_va *)arg);
    case DRM_IOCTL_AMDGPU_CS:            return oops_winsys_cs((union drm_amdgpu_cs *)arg);
    case DRM_IOCTL_AMDGPU_WAIT_CS:       return oops_winsys_wait_cs((union drm_amdgpu_wait_cs *)arg);

    /* Needed soon after: contexts, buffer lists, idle waits. */
    case DRM_IOCTL_AMDGPU_CTX:           return oops_winsys_ctx((union drm_amdgpu_ctx *)arg);
    case DRM_IOCTL_AMDGPU_BO_LIST:       return oops_winsys_bo_list((union drm_amdgpu_bo_list *)arg);
    case DRM_IOCTL_AMDGPU_GEM_WAIT_IDLE: return oops_winsys_gem_wait_idle((union drm_amdgpu_gem_wait_idle *)arg);
    case DRM_IOCTL_AMDGPU_GEM_OP:        return oops_winsys_gem_op((struct drm_amdgpu_gem_op *)arg);
    case DRM_IOCTL_AMDGPU_WAIT_FENCES:   return unimplemented("WAIT_FENCES");
    case DRM_IOCTL_AMDGPU_FENCE_TO_HANDLE: return unimplemented("FENCE_TO_HANDLE");
    case DRM_IOCTL_AMDGPU_VM:            return unimplemented("VM");
    case DRM_IOCTL_AMDGPU_SCHED:         return unimplemented("SCHED");

    /* Sharing and metadata. Nothing on this platform shares buffers with another process yet,
     * so these stay refused until something asks. */
    case DRM_IOCTL_AMDGPU_GEM_METADATA:  return unimplemented("GEM_METADATA");
    case DRM_IOCTL_AMDGPU_GEM_USERPTR:   return unimplemented("GEM_USERPTR");

    /* User queues: a newer submission path than the one this hardware's driver exposes. They
     * are listed so the set is complete and so an attempt to use them is loud. */
    case DRM_IOCTL_AMDGPU_USERQ:         return unimplemented("USERQ");
    case DRM_IOCTL_AMDGPU_USERQ_SIGNAL:  return unimplemented("USERQ_SIGNAL");
    case DRM_IOCTL_AMDGPU_USERQ_WAIT:    return unimplemented("USERQ_WAIT");

    /* Generic DRM, issued by libdrm rather than by its AMD layer. */
    case DRM_IOCTL_VERSION:              return oops_winsys_version((struct drm_version *)arg);
    case DRM_IOCTL_GEM_CLOSE:            return oops_winsys_gem_close(((struct drm_gem_close *)arg)->handle);
    case DRM_IOCTL_GEM_FLINK:            return unimplemented("GEM_FLINK");
    case DRM_IOCTL_GEM_OPEN:             return unimplemented("GEM_OPEN");

    default:
        oops_winsys_log("ioctl 0x%lx is not one this shim knows", request);
        return -EINVAL;
    }
}

/*
 * The driver identity libdrm checks before it will talk to a device. It compares the name
 * against "amdgpu" and refuses anything else, so this is the first call in any session and the
 * one that decides whether the rest happens at all.
 *
 * The version is libdrm.s interface version, not ours: 3.49 is what the pinned libdrm expects
 * from a kernel new enough to carry every command in D005.s list.
 */
int oops_winsys_version(struct drm_version *arg)
{
    static const char name[] = "amdgpu";
    static const char date[] = "20150101";
    static const char desc[] = "oops-mesa winsys over the platform graphics driver";

    arg->version_major = 3;
    arg->version_minor = 49;
    arg->version_patchlevel = 0;

    /* libdrm calls this twice: once with null pointers to learn the lengths, then again with
     * buffers it sized from them. Both are answered here. */
    if (arg->name && arg->name_len >= sizeof(name) - 1) {
        memcpy(arg->name, name, sizeof(name) - 1);
    }
    if (arg->date && arg->date_len >= sizeof(date) - 1) {
        memcpy(arg->date, date, sizeof(date) - 1);
    }
    if (arg->desc && arg->desc_len >= sizeof(desc) - 1) {
        memcpy(arg->desc, desc, sizeof(desc) - 1);
    }
    arg->name_len = sizeof(name) - 1;
    arg->date_len = sizeof(date) - 1;
    arg->desc_len = sizeof(desc) - 1;
    return 0;
}

int oops_winsys_open(void)
{
    if (!sceAgcDriverCreateQueue) {
        oops_winsys_log("the platform graphics driver is not bound; nothing to open");
        return -ENODEV;
    }
    return OOPS_WINSYS_FD;
}

int oops_winsys_close(int fd)
{
    return fd == OOPS_WINSYS_FD ? 0 : -EBADF;
}
