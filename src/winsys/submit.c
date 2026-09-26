/*
 * Command submission, and how this shim knows the work finished.
 *
 * radeonsi builds a command stream in a buffer, tells the kernel where it starts and
 * how long it is, and later asks whether it has retired. On Linux the kernel schedules
 * it and a fence answers. Here the vendor driver takes a descriptor naming the same two
 * things, and the answer has to be built.
 *
 * # Submission is synchronous, and that is a stated choice rather than an oversight
 *
 * The vendor submit call returns once the work is queued, not once it has run, and
 * nothing on this platform has established a protocol by which radeonsi's own
 * end-of-pipe fence could be read back. So after handing over radeonsi's stream this
 * submits a second, tiny stream of its own that ends in an end-of-pipe event writing a
 * known word, and waits for that word. Two streams on one queue retire in order, so the
 * word arriving means radeonsi's work is done.
 *
 * That is oops-gl's proven sequence, reused rather than reinvented: the event, its
 * packet and the flush semantics are the ones in its oracle record for firmware 12.40,
 * which is the only fence on this hardware this collection has ever seen retire.
 *
 * The cost is that a frame cannot overlap the next one. WAIT_CS therefore always
 * answers "signalled", because by the time it is asked the work has already finished.
 * Making submission asynchronous means giving radeonsi's fence a home the shim can
 * read, and that is a later unit with a measurement of its own.
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

/* The word the end-of-pipe event writes, and the one it is reset to first. Distinct,
 * and neither is a value that could arrive by accident from uninitialised memory. */
#define OOPS_WINSYS_FENCE_ARMED 0x11111111u
#define OOPS_WINSYS_FENCE_FIRED 0xbeefcafeu

/* How long to wait before calling a submission lost. oops-gl uses the same shape: a
 * bounded poll rather than an unbounded one, because a stream that never retires must
 * become a loud failure and not a hang. */
#define OOPS_WINSYS_FENCE_POLLS 100000

extern int sceKernelUsleep(unsigned int microseconds) __attribute__((weak));

/* The proven submit entry point. oops-gl declares it the same way (oops-sdk
 * gl_context.c) rather than through agc/driver.h, which only carries the queue-less
 * SubmitDcb. It takes the queue the work goes on, which SubmitDcb does not - and the
 * oracle fence (oops-sdk docs/hardware/agc-gl-cube-oracle-fw1240) retired on this call,
 * not on SubmitDcb. */
extern int sceAgcDriverSubmitCommandBuffer(void *queue, const void *dcb)
    __attribute__((weak));

static void *s_queue; /* the AGC queue, created on first use */
static volatile uint32_t *s_fence;
static uint32_t *s_fence_dcb; /* the little stream that ends in the event */
static uint64_t s_sequence;   /* what the last submission was called */
static uint8_t
    s_agc_state[64]; /* the AGC runtime state sceAgcInit fills; kept for its lifetime */

static uint32_t build_fence_stream(uint32_t *dw, uint64_t fence_gpu);
static int submit_one(uint64_t va, uint32_t bytes);

static bool ensure_queue(void) {
    if (s_queue) {
        return true;
    }
    if (!sceAgcInit || !sceAgcDriverCreateQueue ||
        (!sceAgcDriverSubmitCommandBuffer && !sceAgcDriverSubmitDcb)) {
        oops_winsys_log("the platform graphics driver is not bound; cannot submit");
        return false;
    }

    /* Initialise the AGC runtime before creating any queue. This is the step whose
     * absence was the whole of worklog 055: without it the driver still accepts a
     * CreateQueue and a submit (both return 0), but the command-processor microcode
     * never services the queue, so the fence never retires. oops-sdk calls it before
     * every queue it creates (agc_compute.c:30, agc_display.c:382) and obSCEne's
     * 166-agc/init does the same; version 0xd into a zeroed state is the measured
     * convention (oops-sdk agc/driver.h). The state is kept for the runtime's lifetime.
     */
    for (size_t i = 0; i < sizeof(s_agc_state); i++) {
        s_agc_state[i] = 0;
    }
    {
        int arc = sceAgcInit(s_agc_state, 0xd);
        if (arc != 0) {
            oops_winsys_log(
                "sceAgcInit refused (rc %d); the graphics runtime is not up", arc);
            return false;
        }
    }

    /* Queue type 0 is the universal graphics queue - the one oops-gl's proven flush
     * creates (oops-sdk gl_context.c) and the one the oracle fence retired on. The
     * earlier type 3 (the direct-command queue) was a mistake with a subtle cost:
     * SubmitDcb takes no queue argument, so the type-3 queue was created and then never
     * submitted to - the work went to whatever default context SubmitDcb uses, which is
     * why even a fence stream in proven memory did not retire (worklog 054). Creating
     * the graphics queue and submitting onto it is the fix. */
    if (sceAgcDriverCreateQueue(0, &s_queue, 0) != 0 || !s_queue) {
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
 * The packet is RELEASE_MEM with CACHE_FLUSH_AND_INV_TS and a level-two writeback,
 * writing one 32-bit word. Every constant here is from oops-sdk's `gl_hw_flush`, whose
 * fence has retired on every frame of every run since orbistoun worklog 539; it is
 * copied as a measured recipe rather than re-derived.
 */
static uint32_t build_fence_stream(uint32_t *dw, uint64_t fence_gpu) {
    uint32_t *start = dw;

    /* One register before the event, and it is not optional. A fresh graphics queue has
     * no colour target, so the CACHE_FLUSH_AND_INV_TS below - which flushes the CB
     * cache - has nothing valid to flush and the stream never retires (worklog 055).
     * Setting CB_COLOR0_BASE to any mapped address gives it one; nothing reads this
     * target. obSCEne's 166-agc/graphics-submit emits exactly this one SET_CONTEXT_REG
     * before the same RELEASE_MEM and its fence retires on hardware, fence-val
     * 0xbeefcafe (obscene src/probe/sections/agc.c:3932). The fence buffer's own
     * address serves; oops_mem_alloc is 64 KiB-aligned so the >> 8 is exact. */
    *dw++ = 0xc0012800u;                /* SET_CONTEXT_REG, count 1 */
    *dw++ = 0x200u;                     /* reg 0x200: CB_COLOR0_BASE */
    *dw++ = (uint32_t)(fence_gpu >> 8); /* a valid colour-buffer target */

    *dw++ = 0xc0064900u; /* RELEASE_MEM */
    *dw++ = 0x06603514u; /* CACHE_FLUSH_AND_INV_TS, write back through L2 */
    *dw++ = 0x20000000u; /* DATA_SEL 1: the 32-bit word below */
    *dw++ = (uint32_t)fence_gpu;
    *dw++ = (uint32_t)(fence_gpu >> 32);
    *dw++ = OOPS_WINSYS_FENCE_FIRED;
    *dw++ = 0u;
    *dw++ = 0u;

    /* The command processor reads ahead, so the stream ends in padding rather than at
     * the last meaningful word. oops-gl pads with the same no-operation for the same
     * reason. */
    for (int i = 0; i < 16; i++) {
        *dw++ = 0xffff1000u;
    }

    uint32_t words = (uint32_t)(dw - start);

    /* Flush the stream to memory before the GPU fetches it. oops_mem_alloc hands back
     * write-back memory, so these CPU stores sit in the cache until a flush writes them
     * out; without it the command processor reads stale bytes and the stream never runs
     * - which is exactly why a fence stream in proven memory did not retire (worklog
     * 055). oops-gl's gl_hw_flush flushes its whole DCB for the same reason
     * (gl_context.c). */
#if defined(__x86_64__)
    for (size_t p = 0; p < (size_t)words * sizeof(uint32_t); p += 64) {
        __builtin_ia32_clflush((const void *)((const char *)start + p));
    }
#endif
    return words;
}

/*
 * Every address this command buffer sends the command processor to fetch from.
 *
 * obSCEne `REQ-20260922T2230Z-6e81` measured that the CP reads nothing past
 * `desc.size`, does not prefetch across a page boundary, and that the 2 MiB mapping
 * under `fbotexture`'s fault really is established - and the map sizes now log as
 * exactly their buffers'. So when CPG faults reading `0x400020000`, a packet in the
 * stream told it to go there, and the only packet that sends the CP to fetch a *stream*
 * is `INDIRECT_BUFFER`.
 *
 * radeonsi chains: `si_cs_chain` ends one command buffer with an `INDIRECT_BUFFER`
 * pointing at the next, so the hardware walks a list our `AMDGPU_CHUNK_ID_IB` loop
 * never sees - it submits each chunk libdrm hands it and knows nothing about what a
 * chunk's last packet points at. If the chain target is a buffer Mesa allocated but
 * never asked us to map, this is where it shows.
 *
 * A PM4 type-3 header is `11` in bits 31-30, the opcode in bits 15-8, and
 * `INDIRECT_BUFFER_CIK` is 0x3f with the target as the two dwords after it. Scanning
 * for that is a few hundred nanoseconds against a submission that costs milliseconds,
 * and it prints only when it finds one.
 */
#define PM4_TYPE3_INDIRECT_BUFFER 0x3fu
#define PM4_IB_SIZE_MASK                                                               \
    0xfffffu /* the dword count lives in bits 19:0 of the third word */
#define OOPS_MAX_IB_CHAIN 16u

static int submit_one(uint64_t va, uint32_t bytes);

/*
 * Submit a command stream, walking any chain it ends in rather than letting the
 * hardware follow it. **This is the difference between this platform and Linux, and it
 * is the whole of the `fbotexture` fault.**
 *
 * radeonsi does not build one command buffer per flush. When a stream outgrows its
 * buffer, `amdgpu_cs_flush` allocates another and ends the current one with a type-3
 * `INDIRECT_BUFFER` naming it, so the driver hands the kernel the first link and the
 * command processor walks the rest. On Linux that works because the kernel has the
 * whole address space mapped and the CP may fetch anywhere in it.
 *
 * Here it does not, and obSCEne `REQ-20260922T2230Z-6e81` is why we can say that rather
 * than guess it. Its arm 4 put a DCB at the end of a page with the *next page unmapped*
 * and the CP retired without a fault - it reads `desc.gpu_addr` through `+ 4*size` and
 * nothing else. The declared range is the only range the submission makes valid, so a
 * jump out of it lands on a page the GPU has no translation for, whatever our own page
 * tables say. Measured: `fbotexture`'s third command buffer chains to `0x400020000` at
 * dword 1980 of 1984, and the protection fault at that exact address is the next line
 * in the log.
 *
 * So each link is submitted in its own right: the dwords *before* the chain packet as
 * one DCB, then the target as another, repeating for as long as the chain runs. The CP
 * never executes an `INDIRECT_BUFFER`, because it never sees one - which also means the
 * fence stream appended after the last link is reached instead of being jumped over,
 * and that is why the old failure showed up as "submission did not retire" rather than
 * as a lost frame.
 *
 * `OOPS_MAX_IB_CHAIN` bounds it. A corrupt stream that chains to itself would otherwise
 * loop here forever, and a shim that hangs is worse than one that says it gave up.
 */
static int submit_chain(uint64_t va, uint32_t bytes, unsigned int depth) {
    const uint32_t *dw = (const uint32_t *)(uintptr_t)va;
    const uint32_t n = bytes / 4u;

    if (depth >= OOPS_MAX_IB_CHAIN) {
        oops_winsys_log(
            "instruction buffer chain is deeper than %u; refusing to follow further",
            OOPS_MAX_IB_CHAIN);
        return -EIO;
    }

    for (uint32_t i = 0; i + 3u < n; i++) {
        if ((dw[i] >> 30) != 3u) {
            continue;
        }
        if (((dw[i] >> 8) & 0xffu) != PM4_TYPE3_INDIRECT_BUFFER) {
            continue;
        }

        const uint64_t target = (uint64_t)dw[i + 1] | ((uint64_t)dw[i + 2] << 32);
        const uint32_t target_dw = dw[i + 3] & PM4_IB_SIZE_MASK;

        oops_winsys_log(
            "  chains to 0x%llx (%u dwords) at dword %u; submitting it as its own",
            (unsigned long long)target, target_dw, i);

        /* The part before the chain packet is a complete stream in itself. Zero dwords
         * happens when a buffer holds nothing but the chain, and submitting an empty
         * DCB is not useful. */
        if (i > 0u && submit_one(va, i * 4u) != 0) {
            return -EIO;
        }
        if (target_dw == 0u) {
            return 0;
        }
        return submit_chain(target, target_dw * 4u, depth + 1u);
    }

    return submit_one(va, bytes);
}

static int submit_one(uint64_t va, uint32_t bytes) {
    oops_agc_dcb_desc desc;

    memset(&desc, 0, sizeof(desc));
    desc.gpu_addr = va;
    desc.size = bytes / 4u; /* the descriptor counts dwords, not bytes */
    desc.flags = 0u;
    desc.pad = 0u;

    /* Submit onto the queue this shim created, the way oops-gl's proven flush does.
     * SubmitDcb is the fallback for a driver that does not export SubmitCommandBuffer;
     * it submits to a default context rather than s_queue, so it is second choice, not
     * first. */
    if (sceAgcDriverSubmitCommandBuffer) {
        return sceAgcDriverSubmitCommandBuffer(s_queue, &desc);
    }
    return sceAgcDriverSubmitDcb(&desc);
}

int oops_winsys_cs(union drm_amdgpu_cs *arg) {
    const uint64_t *chunk_ptrs;
    uint32_t submitted = 0;
    uint32_t user_fence_handle = 0;
    uint64_t user_fence_offset = 0;
    int have_user_fence = 0;

    if (!ensure_queue()) {
        return -ENODEV;
    }
    if (!arg->in.chunks || arg->in.num_chunks == 0) {
        return -EINVAL;
    }

    /* Drain radeonsi's CPU writes - shaders, this IB, vertex and constant data - to
     * memory before the command processor fetches any of it. radeonsi assumes the
     * winsys does this and flushes nothing itself (worklog 057), so without it the GPU
     * ran stale shader bytes and its wavefronts hit ILLEGAL_INST. clflush drains the
     * write-back buffers; the sfence drains the write-combined ones, which is where
     * shaders live. */
    oops_winsys_flush_cpu_writes();
#if defined(__x86_64__)
    __builtin_ia32_sfence();
#endif

    chunk_ptrs = (const uint64_t *)(uintptr_t)arg->in.chunks;

    /* Walk the chunks and submit every instruction buffer among them, in order. The
     * other chunk kinds carry dependencies and syncobjs, which a synchronous submission
     * does not need: the previous stream has already retired by the time this one is
     * built. */
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
            /*
             * The address the command processor will fetch from, said before it
             * fetches.
             *
             * A GPU protection fault names an address and a client, and when that
             * client is CPG the address is usually either this buffer or something a
             * packet inside it points at. Those two want different fixes and the log
             * could not tell them apart: `fbotexture` faulted at 0x400020000 on
             * 2026-09-22 with no unmap of that address anywhere in the run, which
             * leaves "the IB itself" and "a resource the IB names" as the two live
             * readings. This line settles which.
             */
            oops_winsys_log("submitting IB at 0x%llx, %u bytes",
                            (unsigned long long)ib->va_start, ib->ib_bytes);
            oops_winsys_dump_ib(ib->va_start, ib->ib_bytes);
            if (submit_chain(ib->va_start, ib->ib_bytes, 0u) != 0) {
                oops_winsys_log("the driver refused an instruction buffer of %u bytes",
                                ib->ib_bytes);
                return -EIO;
            }
            submitted++;
            break;
        }
        case AMDGPU_CHUNK_ID_FENCE: {
            /*
             * Where radeonsi will look to decide whether this submission retired. It is
             * not optional and it is not the syncobj path: `amdgpu_fence_wait` reads
             * this memory *first* and returns success without touching a syncobj if the
             * value has landed -
             *
             *     user_fence_cpu = afence->user_fence_cpu_address;
             *     if (user_fence_cpu) {
             *        if (*user_fence_cpu >= afence->seq_no) {
             *           afence->signalled = true;
             *           return true;
             *        }
             *
             * - and `amdgpu_cs_has_user_fence` is true for GFX, which is the only
             * engine this shim answers for. So on this part every fence carries one of
             * these, and leaving it unwritten is what would send radeonsi down to
             * `SYNCOBJ_WAIT` (worklog 028).
             *
             * The chunk is remembered rather than written now, because the value to
             * write is the sequence number this call is about to hand back, and that is
             * only true once the work has actually retired.
             */
            const struct drm_amdgpu_cs_chunk_fence *fc =
                (const struct drm_amdgpu_cs_chunk_fence *)(uintptr_t)chunk->chunk_data;
            if (fc) {
                user_fence_handle = fc->handle;
                user_fence_offset = fc->offset;
                have_user_fence = 1;
            }
            break;
        }
        case AMDGPU_CHUNK_ID_DEPENDENCIES:
        case AMDGPU_CHUNK_ID_SYNCOBJ_IN:
        case AMDGPU_CHUNK_ID_SYNCOBJ_OUT:
            /* Nothing to do while submission is synchronous: a dependency named here
             * has already retired, because the submit that produced it did not return
             * until it had. Named rather than ignored so that making submission
             * asynchronous starts by deleting this case. */
            break;
        case AMDGPU_CHUNK_ID_BO_HANDLES:
            /*
             * The buffers this submission touches, named inline instead of through a
             * pre-created list. The chunk data is a whole `drm_amdgpu_bo_list_in` with
             * `operation` and `list_handle` both `~0` - the same request
             * `DRM_IOCTL_AMDGPU_BO_LIST` takes, posted with the stream rather than
             * before it (`amdgpu_cs.cpp:1319`). radeonsi sends one per submission, so
             * this arrives on every frame.
             *
             * **Residency is what it is for, and there is none to manage here.** On
             * Linux the kernel reads this list to make each buffer resident before the
             * stream runs. In this shim a buffer becomes GPU-reachable at
             * `AMDGPU_VA_OP_MAP`, which maps its pages and leaves them mapped until
             * `AMDGPU_VA_OP_UNMAP` or the buffer is destroyed
             * (`buffers.c`). Nothing evicts, nothing pages out, and
             * `oops_winsys_bo_is_busy` always answers no because submission waits for
             * its own fence. So every buffer named here was already reachable before
             * the stream was built, and making it resident is work that does not exist.
             * `context.c` says the same thing about the ioctl form.
             *
             * Named rather than left to `default:` because it is not unhandled - it is
             * handled by having nothing to do, and it was printing "chunk kind 6 is not
             * handled" about ten times a frame, which reads like a gap and is not one.
             *
             * **What would make it matter.** Two things, and neither is true today. If
             * buffers ever become evictable - a real memory manager, or sparse
             * residency, which `AMDGPU_VA_OP_CLEAR`/`REPLACE` refuse for now - this
             * list becomes the statement of what to bring back, and this case stops
             * being empty. And there is one piece of work available even now that is
             * deliberately not taken: the ioctl path walks the same entries and refuses
             * a handle that is not live, so a stale handle becomes a numbered refusal
             * instead of a GPU fault in a later frame. Doing that here would be
             * consistent - but it can refuse a submission that currently succeeds, and
             * this path renders correctly today (frame hash `0x5188ddb7`, reproduced),
             * so adopting it needs a hardware run behind it rather than a guess.
             */
            break;
        default:
            oops_winsys_log("command stream chunk kind %u is not handled",
                            chunk->chunk_id);
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
#if defined(__x86_64__)
        /* Write the armed word back before submitting, so the poll's flush cannot
         * clobber the GPU's FIRED write with this stale value (worklog 055).
         * build_fence_stream flushes the stream itself. */
        __builtin_ia32_clflush((const void *)s_fence);
#endif
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
            /* A stream that never retires is a failure, and saying so is the whole
             * point. It is never reported as success with a fence nobody will check. */
            oops_winsys_log("submission %llu did not retire; fence still 0x%08x",
                            (unsigned long long)s_sequence + 1u, s_fence[0]);
            /* Tell the context, so that a later reset query answers from something
             * observed rather than from an assumption that all is well. */
            oops_winsys_ctx_note_hang(arg->in.ctx_id);
            return -ETIMEDOUT;
        }
    }

    /*
     * The work has retired, so the sequence number this call is about to return is true
     * now and can be published where radeonsi will look for it.
     *
     * Writing it *after* the wait rather than before is the whole correctness argument.
     * radeonsi treats `*user_fence >= seq_no` as "this submission is done"; publishing
     * the number before the fence fired would make that true while the GPU was still
     * working, which is the silent lie this collection refuses (CLAUDE.md, principle 4;
     * orbistoun worklog 539).
     *
     * A refused write is not fatal. The submission did retire, and `WAIT_CS` answers
     * from the same sequence number, so the work is correctly reported either way -
     * what is lost is the fast path, and radeonsi falls through to `SYNCOBJ_WAIT`,
     * which refuses and names itself. That is a worse outcome than a working fast path
     * and a better one than a fence that reads as signalled without being written.
     */
    uint64_t sequence = s_sequence + 1u;

    if (have_user_fence) {
        uint64_t *slot = (uint64_t *)oops_winsys_bo_cpu_range(
            user_fence_handle, user_fence_offset, sizeof(uint64_t));
        if (slot) {
            *slot = sequence;
        } else {
            oops_winsys_log(
                "submission %llu retired but its fence slot (buffer %u + %llu) could "
                "not be written; radeonsi will fall back to SYNCOBJ_WAIT",
                (unsigned long long)sequence, user_fence_handle,
                (unsigned long long)user_fence_offset);
        }
    }

    memset(&arg->out, 0, sizeof(arg->out));
    s_sequence = sequence;
    arg->out.handle = sequence;
    return 0;
}

int oops_winsys_wait_cs(union drm_amdgpu_wait_cs *arg) {
    /* Synchronous submission means the work retired inside the CS call, so anything
     * this shim ever handed out is already done. `status` is zero for signalled and one
     * for timed out, which is the kernel's convention. */
    if (arg->in.handle > s_sequence) {
        oops_winsys_log("asked about submission %llu, which was never handed out",
                        (unsigned long long)arg->in.handle);
        return -EINVAL;
    }
    memset(&arg->out, 0, sizeof(arg->out));
    arg->out.status = 0;
    return 0;
}
