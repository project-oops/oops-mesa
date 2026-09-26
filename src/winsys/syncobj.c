/*
 * Synchronisation objects, which are this repository's rather than the platform's.
 *
 * `amdgpu_winsys_create` creates a timeline syncobj before it asks the device anything,
 * and a failure there is `goto fail_alloc`. So this file gates every other answer the
 * winsys gives: the derived `GB_ADDR_CONFIG`, the DRM version, `HW_IP_INFO` and
 * `FW_VERSION` all live inside `ac_query_gpu_info`, which runs later (worklog 020).
 *
 * D007 has the reasoning; the short of it is that a syncobj on Linux is a kernel object
 * wrapping a fence, and here the fence is a value in memory this shim allocated, so the
 * handle namespace is already ours. Creating one asks the platform for nothing because
 * there is nothing to ask it for - the same division `buffers.c` already draws, and the
 * one D005 sets out.
 *
 * What the hardware does provide, measured rather than assumed: a submitted command
 * buffer writes a full **64-bit** value when it retires (`fence-bytes-landed 0x8`,
 * `fence-val-lo`, `fence-val-hi`), and there is no way to block on that write - the
 * whole event-queue family is absent on retail 12.40, controls in the same check
 * resolving (`REQ-20260916T2208Z-7e29`, sweep `20260917-001421`). A wait is therefore a
 * poll, whenever waits are implemented.
 *
 * They are not implemented here. Nothing submits work yet, so nothing can signal, and
 * writing a wait against a submission path that does not exist would be guessing at how
 * radeonsi threads fences through it. `SYNCOBJ_WAIT`, `SIGNAL` and `RESET` refuse and
 * name themselves, like every other command with nothing behind it.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "drm-uapi/drm.h"

#include "oops_winsys.h"

/*
 * How many of these can be live at once, and why the answer is a policy rather than a
 * count.
 *
 * This was originally 256, on the stated basis that "radeonsi creates one syncobj per
 * winsys and a handful per context" and that the table therefore catches a leak. That
 * was wrong, and worth saying so rather than quietly changing the number. radeonsi
 * creates a syncobj **per fence**:
 *
 *     static struct pipe_fence_handle *amdgpu_fence_create(struct amdgpu_cs *acs)
 *     {
 *        ...
 *        if (ac_drm_cs_create_syncobj2(ctx->aws->dev, 0, &fence->syncobj)) {
 *
 * and there is one fence per command-stream flush, plus one for every semaphore and
 * every imported fence (`amdgpu_cs.cpp`). Fences are reference counted, so a fence an
 * application holds keeps its syncobj alive - and `glFenceSync` puts no bound on how
 * many an application may hold.
 *
 * So hitting the limit is not evidence of a bug. It is evidence of a busy application,
 * which means the old message accused the caller of something it had not done.
 *
 * The table stays fixed, because a shim should not allocate without bound on a
 * submission path, and it still refuses rather than growing, because a refusal that
 * names itself beats silently succeeding (CLAUDE.md, principle 4). 1024 slots is 24 KiB
 * of static storage and is chosen to be past what ordinary use can reach, not from a
 * measurement. The measurement that would replace it is the live high-water mark under
 * a real workload, which needs a submission path to exist - so this number is revisited
 * with D007's deferred half, not before.
 */
#define OOPS_WINSYS_MAX_SYNCOBJ 1024

struct oops_winsys_syncobj {
    /* Zero means the slot is free. Handles are slot index + 1 so that a caller holding
     * zero cannot address a live object by accident - the same reason libdrm treats 0
     * as no handle. */
    int live;
    /*
     * The value the GPU is expected to write, and where. Both stay zero until a submit
     * attaches a fence to a syncobj, which nothing does yet: which address, written by
     * what, and at which point are measurements waiting on unit 5's gate rather than
     * design questions (D007).
     */
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

        /*
         * `DRM_SYNCOBJ_CREATE_SIGNALED` asks for an object that starts already
         * signalled. Nothing can observe the difference until waiting exists, so
         * honouring it would be recording a state no reader can read. It is noted
         * rather than silently accepted.
         */
        if (arg->flags & DRM_SYNCOBJ_CREATE_SIGNALED) {
            oops_winsys_log(
                "syncobj %u created with CREATE_SIGNALED; nothing waits yet, so the "
                "initial state is not yet meaningful",
                arg->handle);
        }
        return 0;
    }

    /* Not an accusation. radeonsi makes one of these per fence and an application may
     * hold as many fences as it likes, so this is a limit being reached rather than a
     * leak being caught. The number to raise is OOPS_WINSYS_MAX_SYNCOBJ, and seeing
     * this line is the measurement that would justify raising it. */
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
