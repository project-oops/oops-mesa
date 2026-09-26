/*
 * Contexts and buffer lists, the two objects radeonsi creates before its first draw.
 *
 * The platform exposes one queue, so a context carries no priority; it carries the
 * reset history, from submissions that failed to retire. Every buffer is resident from
 * creation, so a buffer list makes nothing resident; it checks that each handle is
 * live, which turns a stale handle into a refusal here instead of a GPU fault later.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "drm-uapi/amdgpu_drm.h"

#include "oops_winsys.h"

#define OOPS_WINSYS_MAX_CTX 64
#define OOPS_WINSYS_MAX_BO_LIST 256

struct oops_winsys_ctx {
    bool live;
    int32_t priority; /* recorded, and honoured nowhere: there is one queue */
    uint32_t hangs;   /* submissions under this context that did not retire */
};

struct oops_winsys_bo_list {
    bool live;
    uint32_t count;
};

static struct oops_winsys_ctx s_ctx[OOPS_WINSYS_MAX_CTX];
static struct oops_winsys_bo_list s_list[OOPS_WINSYS_MAX_BO_LIST];

/* Handle 0 means "none" to libdrm in both families, so both tables index from one. */
static struct oops_winsys_ctx *ctx_of(uint32_t id) {
    if (id == 0 || id > OOPS_WINSYS_MAX_CTX) {
        return NULL;
    }
    struct oops_winsys_ctx *c = &s_ctx[id - 1];
    return c->live ? c : NULL;
}

/* Called by submission when a stream fails to retire: the only GPU failure this shim
 * observes, and the source of the reset answer below. */
void oops_winsys_ctx_note_hang(uint32_t ctx_id) {
    struct oops_winsys_ctx *c = ctx_of(ctx_id);
    if (c) {
        c->hangs++;
        oops_winsys_log("context %u has now failed to retire %u time(s)", ctx_id,
                        c->hangs);
    }
}

int oops_winsys_ctx(union drm_amdgpu_ctx *arg) {
    uint32_t op = arg->in.op;
    uint32_t id = arg->in.ctx_id;
    int32_t priority = arg->in.priority;

    switch (op) {
    case AMDGPU_CTX_OP_ALLOC_CTX: {
        uint32_t handle = 0;
        for (uint32_t i = 0; i < OOPS_WINSYS_MAX_CTX; i++) {
            if (!s_ctx[i].live) {
                handle = i + 1;
                break;
            }
        }
        if (handle == 0) {
            oops_winsys_log("context table is full at %u entries", OOPS_WINSYS_MAX_CTX);
            memset(&arg->out, 0, sizeof(arg->out));
            return -ENOMEM;
        }
        s_ctx[handle - 1].live = true;
        s_ctx[handle - 1].priority = priority;
        s_ctx[handle - 1].hangs = 0;

        memset(&arg->out, 0, sizeof(arg->out));
        arg->out.alloc.ctx_id = handle;
        return 0;
    }

    case AMDGPU_CTX_OP_FREE_CTX: {
        struct oops_winsys_ctx *c = ctx_of(id);
        if (!c) {
            return -EINVAL;
        }
        memset(c, 0, sizeof(*c));
        memset(&arg->out, 0, sizeof(arg->out));
        return 0;
    }

    case AMDGPU_CTX_OP_QUERY_STATE:
    case AMDGPU_CTX_OP_QUERY_STATE2: {
        /* radeonsi decides from this whether to recreate the context. NO_RESET means
         * this shim saw no failed submission, not that the GPU is known healthy. */
        struct oops_winsys_ctx *c = ctx_of(id);
        if (!c) {
            return -EINVAL;
        }
        memset(&arg->out, 0, sizeof(arg->out));
        arg->out.state.hangs = c->hangs;
        arg->out.state.reset_status =
            c->hangs ? AMDGPU_CTX_GUILTY_RESET : AMDGPU_CTX_NO_RESET;
        return 0;
    }

    case AMDGPU_CTX_OP_GET_STABLE_PSTATE: {
        struct oops_winsys_ctx *c = ctx_of(id);
        if (!c) {
            return -EINVAL;
        }
        /* Clock states belong to the system and a title does not set them, so no stable
         * state is pinned. */
        memset(&arg->out, 0, sizeof(arg->out));
        arg->out.pstate.flags = AMDGPU_CTX_STABLE_PSTATE_NONE;
        return 0;
    }

    case AMDGPU_CTX_OP_SET_STABLE_PSTATE:
        /* Refused rather than ignored: success would tell the caller the clocks were
         * pinned. */
        oops_winsys_log(
            "pinning a stable clock state is not something a title may do here");
        return -EPERM;

    default:
        oops_winsys_log("context operation %u is not one this shim knows", op);
        return -EINVAL;
    }
}

int oops_winsys_bo_list(union drm_amdgpu_bo_list *arg) {
    switch (arg->in.operation) {
    case AMDGPU_BO_LIST_OP_CREATE: {
        const struct drm_amdgpu_bo_list_entry *entries =
            (const struct drm_amdgpu_bo_list_entry *)(uintptr_t)arg->in.bo_info_ptr;
        uint32_t count = arg->in.bo_number;
        uint32_t stride = arg->in.bo_info_size;
        uint32_t handle = 0;

        if (count && (!entries || stride < sizeof(uint32_t))) {
            return -EINVAL;
        }

        /* Nothing needs making resident, so the work is the check: a stale handle is
         * refused here, by number, instead of faulting the GPU in a later frame. */
        for (uint32_t i = 0; i < count; i++) {
            const struct drm_amdgpu_bo_list_entry *e =
                (const struct drm_amdgpu_bo_list_entry *)((const char *)entries +
                                                          (size_t)i * stride);
            if (!oops_winsys_bo_is_live(e->bo_handle)) {
                oops_winsys_log(
                    "buffer list names handle %u, which is not a live buffer",
                    e->bo_handle);
                return -EINVAL;
            }
        }

        for (uint32_t i = 0; i < OOPS_WINSYS_MAX_BO_LIST; i++) {
            if (!s_list[i].live) {
                handle = i + 1;
                break;
            }
        }
        if (handle == 0) {
            oops_winsys_log("buffer-list table is full at %u entries",
                            OOPS_WINSYS_MAX_BO_LIST);
            return -ENOMEM;
        }
        s_list[handle - 1].live = true;
        s_list[handle - 1].count = count;

        memset(&arg->out, 0, sizeof(arg->out));
        arg->out.list_handle = handle;
        return 0;
    }

    case AMDGPU_BO_LIST_OP_DESTROY: {
        uint32_t id = arg->in.list_handle;
        if (id == 0 || id > OOPS_WINSYS_MAX_BO_LIST || !s_list[id - 1].live) {
            return -EINVAL;
        }
        memset(&s_list[id - 1], 0, sizeof(s_list[id - 1]));
        return 0;
    }

    default:
        oops_winsys_log("buffer-list operation %u is not one this shim knows",
                        arg->in.operation);
        return -EINVAL;
    }
}
