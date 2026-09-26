/*
 * Synchronisation objects, which are this repository's handles rather than the
 * platform's (D007).
 *
 * `amdgpu_winsys_create` creates a syncobj before it asks the device anything and
 * treats failure as fatal, so creation gates every other winsys answer. The fence
 * behind a syncobj is a 64-bit value a retiring command buffer writes into memory this
 * shim allocated, and the platform has no call that blocks on that write, so creating
 * one asks the platform for nothing. `SYNCOBJ_WAIT`, `SIGNAL` and `RESET` refuse and
 * name themselves.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "drm-uapi/drm.h"

#include "oops_winsys.h"

/*
 * The live-syncobj limit, a policy rather than a count. radeonsi creates one syncobj
 * per fence (`amdgpu_fence_create` in amdgpu_cs.cpp), and an application may hold any
 * number of fences, so reaching the limit means a busy application, not a leak. The
 * table is fixed because a submission path does not allocate without bound; 1024 slots
 * is chosen to exceed ordinary use, not measured.
 */
#define OOPS_WINSYS_MAX_SYNCOBJ 1024

struct oops_winsys_syncobj {
    /* Zero means the slot is free. Handles are slot index + 1, because libdrm reads 0
     * as no handle. */
    int live;
    /* The value the GPU is expected to write, and where. Zero while no submission has
     * attached a fence. */
    uint64_t fence_value;
    uint64_t fence_address;
};

static struct oops_winsys_syncobj s_syncobj[OOPS_WINSYS_MAX_SYNCOBJ];

int oops_winsys_syncobj_create(struct drm_syncobj_create *arg) {
    if (arg == NULL) {
        return -EINVAL;
    }

    for (uint32_t i = 0; i < OOPS_WINSYS_MAX_SYNCOBJ; i++) {
        if (s_syncobj[i].live) {
            continue;
        }
        s_syncobj[i].live = 1;
        s_syncobj[i].fence_value = 0;
        s_syncobj[i].fence_address = 0;
        arg->handle = i + 1u;

        /* No wait can observe an initial signalled state, so CREATE_SIGNALED is logged
         * rather than recorded. */
        if (arg->flags & DRM_SYNCOBJ_CREATE_SIGNALED) {
            oops_winsys_log(
                "syncobj %u created with CREATE_SIGNALED; SYNCOBJ_WAIT is refused, so "
                "the initial state has no effect",
                arg->handle);
        }
        return 0;
    }

    /* A limit reached, not a leak caught; this line is the evidence for raising
     * OOPS_WINSYS_MAX_SYNCOBJ. */
    oops_winsys_log(
        "all %d syncobj slots are in use; this shim refuses rather than growing on a "
        "submission path, so the flush that asked for this one fails",
        OOPS_WINSYS_MAX_SYNCOBJ);
    return -ENOSPC;
}

int oops_winsys_syncobj_destroy(struct drm_syncobj_destroy *arg) {
    if (arg == NULL) {
        return -EINVAL;
    }
    if (arg->handle == 0u || arg->handle > OOPS_WINSYS_MAX_SYNCOBJ) {
        oops_winsys_log("syncobj destroy names handle %u, which was never given out",
                        arg->handle);
        return -EINVAL;
    }

    struct oops_winsys_syncobj *obj = &s_syncobj[arg->handle - 1u];
    if (!obj->live) {
        oops_winsys_log("syncobj %u is destroyed twice", arg->handle);
        return -EINVAL;
    }

    memset(obj, 0, sizeof(*obj));
    return 0;
}

bool oops_winsys_syncobj_is_live(uint32_t handle) {
    if (handle == 0u || handle > OOPS_WINSYS_MAX_SYNCOBJ) {
        return false;
    }
    return s_syncobj[handle - 1u].live != 0;
}
