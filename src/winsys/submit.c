/*
 * Command submission, and how this shim knows the work finished.
 *
 * radeonsi builds a command stream in a buffer, tells the kernel where it starts and how long it
 * is, and later asks whether it has retired. On Linux the kernel schedules it and a fence
 * answers. Here the vendor driver takes a descriptor naming the same two things, and the answer
 * has to be built.
 *
 * # Submission is synchronous, and that is a stated choice rather than an oversight
 *
 * The vendor submit call returns once the work is queued, not once it has run, and nothing on
 * this platform has established a protocol by which radeonsi's own end-of-pipe fence could be
 * read back. So after handing over radeonsi's stream this submits a second, tiny stream of its
 * own that ends in an end-of-pipe event writing a known word, and waits for that word. Two
 * streams on one queue retire in order, so the word arriving means radeonsi's work is done.
 *
 * That is oops-gl's proven sequence, reused rather than reinvented: the event, its packet and
 * the flush semantics are the ones in its oracle record for firmware 12.40, which is the only
 * fence on this hardware this collection has ever seen retire.
 *
 * The cost is that a frame cannot overlap the next one. WAIT_CS therefore always answers
 * "signalled", because by the time it is asked the work has already finished. Making submission
 * asynchronous means giving radeonsi's fence a home the shim can read, and that is a later unit
 * with a measurement of its own.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "drm-uapi/amdgpu_drm.h"

#include "agc/driver.h"
#include "oops/memory.h"
#include "oops_winsys.h"

/* The word the end-of-pipe event writes, and the one it is reset to first. Distinct, and
 * neither is a value that could arrive by accident from uninitialised memory. */
#define OOPS_WINSYS_FENCE_ARMED 0x11111111u
#define OOPS_WINSYS_FENCE_FIRED 0xbeefcafeu

/* How long to wait before calling a submission lost. oops-gl uses the same shape: a bounded
 * poll rather than an unbounded one, because a stream that never retires must become a loud
 * failure and not a hang. */
#define OOPS_WINSYS_FENCE_POLLS 100000

extern int sceKernelUsleep(unsigned int microseconds) __attribute__((weak));

static void *s_queue;          /* the AGC queue, created on first use */
static volatile uint32_t *s_fence;
static uint32_t *s_fence_dcb;  /* the little stream that ends in the event */
static uint64_t s_sequence;    /* what the last submission was called */

static bool ensure_queue(void)
{
    if (s_queue) {
        return true;
    }
    if (!sceAgcDriverCreateQueue || !sceAgcDriverSubmitDcb) {
        oops_winsys_log("the platform graphics driver is not bound; cannot submit");
        return false;
    }
    /* Queue type 3 is the direct command queue, as oops-sdk's agc bindings record. */
    if (sceAgcDriverCreateQueue(3, &s_queue, 0) != 0 || !s_queue) {
        oops_winsys_log("creating the graphics queue was refused");
        s_queue = NULL;
        return false;
    }

    s_fence = (volatile uint32_t *)oops_mem_alloc(64, 64, OOPS_MEM_WB_ONION);
    s_fence_dcb = (uint32_t *)oops_mem_alloc(256, 256, OOPS_MEM_WB_ONION);
    if (!s_fence || !s_fence_dcb) {
        oops_winsys_log("allocating the fence and its stream was refused");
        return false;
    }
    return true;
}

/*
 * Build the stream that ends in the end-of-pipe event.
 *
 * The packet is RELEASE_MEM with CACHE_FLUSH_AND_INV_TS and a level-two writeback, writing one
 * 32-bit word. Every constant here is from oops-sdk's `gl_hw_flush`, whose fence has retired on
 * every frame of every run since orbistoun worklog 539; it is copied as a measured recipe rather
 * than re-derived.
 */
static uint32_t build_fence_stream(uint32_t *dw, uint64_t fence_gpu)
{
    uint32_t *start = dw;

    *dw++ = 0xc0064900u;                    /* RELEASE_MEM */
    *dw++ = 0x06603514u;                    /* CACHE_FLUSH_AND_INV_TS, write back through L2 */
    *dw++ = 0x20000000u;                    /* DATA_SEL 1: the 32-bit word below */
    *dw++ = (uint32_t)fence_gpu;
    *dw++ = (uint32_t)(fence_gpu >> 32);
    *dw++ = OOPS_WINSYS_FENCE_FIRED;
    *dw++ = 0u;
    *dw++ = 0u;

    /* The command processor reads ahead, so the stream ends in padding rather than at the last
     * meaningful word. oops-gl pads with the same no-operation for the same reason. */
    for (int i = 0; i < 16; i++) {
        *dw++ = 0xffff1000u;
    }
    return (uint32_t)(dw - start);
}

static int submit_one(uint64_t va, uint32_t bytes)
{
    oops_agc_dcb_desc desc;

    memset(&desc, 0, sizeof(desc));
    desc.gpu_addr = va;
    desc.size = bytes / 4u;   /* the descriptor counts dwords, not bytes */
    desc.flags = 0u;
    desc.pad = 0u;

    return sceAgcDriverSubmitDcb(&desc);
}

int oops_winsys_cs(union drm_amdgpu_cs *arg)
{
    const uint64_t *chunk_ptrs;
    uint32_t submitted = 0;

    if (!ensure_queue()) {
        return -ENODEV;
    }
    if (!arg->in.chunks || arg->in.num_chunks == 0) {
        return -EINVAL;
    }

    chunk_ptrs = (const uint64_t *)(uintptr_t)arg->in.chunks;

    /* Walk the chunks and submit every instruction buffer among them, in order. The other chunk
     * kinds carry dependencies and syncobjs, which a synchronous submission does not need: the
     * previous stream has already retired by the time this one is built. */
    for (uint32_t i = 0; i < arg->in.num_chunks; i++) {
        const struct drm_amdgpu_cs_chunk *chunk =
            (const struct drm_amdgpu_cs_chunk *)(uintptr_t)chunk_ptrs[i];
        if (!chunk) {
            continue;
        }
        switch (chunk->chunk_id) {
        case AMDGPU_CHUNK_ID_IB: {
            const struct drm_amdgpu_cs_chunk_ib *ib =
                (const struct drm_amdgpu_cs_chunk_ib *)(uintptr_t)chunk->chunk_data;
            if (!ib || ib->ib_bytes == 0) {
                break;
            }
            if (submit_one(ib->va_start, ib->ib_bytes) != 0) {
                oops_winsys_log("the driver refused an instruction buffer of %u bytes",
                                ib->ib_bytes);
                return -EIO;
            }
            submitted++;
            break;
        }
        case AMDGPU_CHUNK_ID_DEPENDENCIES:
        case AMDGPU_CHUNK_ID_FENCE:
        case AMDGPU_CHUNK_ID_SYNCOBJ_IN:
        case AMDGPU_CHUNK_ID_SYNCOBJ_OUT:
            /* Nothing to do while submission is synchronous. Named rather than ignored so that
             * making it asynchronous starts by deleting this case. */
            break;
        default:
            oops_winsys_log("command stream chunk kind %u is not handled", chunk->chunk_id);
            break;
        }
    }

    if (submitted == 0) {
        oops_winsys_log("a command stream arrived with no instruction buffer in it");
        return -EINVAL;
    }

    /* Now the fence stream, and the wait. */
    {
        uint64_t fence_gpu = (uint64_t)(uintptr_t)s_fence;
        uint32_t words;
        int fired = 0;

        s_fence[0] = OOPS_WINSYS_FENCE_ARMED;
        words = build_fence_stream(s_fence_dcb, fence_gpu);

        if (submit_one((uint64_t)(uintptr_t)s_fence_dcb, words * 4u) != 0) {
            oops_winsys_log("the driver refused the fence stream");
            return -EIO;
        }

        for (int i = 0; i < OOPS_WINSYS_FENCE_POLLS; i++) {
#if defined(__x86_64__)
            __builtin_ia32_clflush((const void *)s_fence);
#endif
            if (s_fence[0] == OOPS_WINSYS_FENCE_FIRED) {
                fired = 1;
                break;
            }
            if (sceKernelUsleep) {
                sceKernelUsleep(10);
            }
        }
        if (!fired) {
            /* A stream that never retires is a failure, and saying so is the whole point. It is
             * never reported as success with a fence nobody will check. */
            oops_winsys_log("submission %llu did not retire; fence still 0x%08x",
                            (unsigned long long)s_sequence + 1u, s_fence[0]);
            /* Tell the context, so that a later reset query answers from something observed
             * rather than from an assumption that all is well. */
            oops_winsys_ctx_note_hang(arg->in.ctx_id);
            return -ETIMEDOUT;
        }
    }

    memset(&arg->out, 0, sizeof(arg->out));
    arg->out.handle = ++s_sequence;
    return 0;
}

int oops_winsys_wait_cs(union drm_amdgpu_wait_cs *arg)
{
    /* Synchronous submission means the work retired inside the CS call, so anything this shim
     * ever handed out is already done. `status` is zero for signalled and one for timed out,
     * which is the kernel's convention. */
    if (arg->in.handle > s_sequence) {
        oops_winsys_log("asked about submission %llu, which was never handed out",
                        (unsigned long long)arg->in.handle);
        return -EINVAL;
    }
    memset(&arg->out, 0, sizeof(arg->out));
    arg->out.status = 0;
    return 0;
}
