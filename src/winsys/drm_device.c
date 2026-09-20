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
 * The cases below are the complete set libdrm's amdgpu layer issues, taken from its sources at
 * the pinned version: the 22 AMDGPU commands, the four generic DRM ones its bookkeeping reaches
 * for, and the syncobj family. A command that is not implemented returns -ENOSYS and logs its own
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
#include <fcntl.h>
#include <stdint.h>
#include <string.h>

/* The target sysroot exposes `F_DUPFD_CLOEXEC` directly; the host test compiles this file against
 * glibc, which gates it behind a feature macro. The close-on-exec flag is immaterial here - a
 * title never execs a child - so where the constant is absent, plain `F_DUPFD` is identical for
 * this shim's purpose. The fallback only ever applies to the host build; the shipped target binary
 * uses the real one. */
#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC F_DUPFD
#endif

#include "drm-uapi/amdgpu_drm.h"
#include "drm-uapi/drm.h"

#include "oops/memory.h"
#include "agc/driver.h"

#include "oops_winsys.h"

/* The one device this shim serves. libdrm opens a node and keeps the descriptor; nothing here
 * needs a file system, so historically the descriptor was a fixed token rather than an index
 * into one.
 *
 * That was enough while the only caller was a title calling `radeonsi_screen_create` directly
 * (mesa-probe): the token flowed straight to the ioctl path, every ioctl is patched to
 * `oops_winsys_ioctl`, and the gate below accepted the token. It stopped being enough the moment
 * a title went through the Gallium DRI frontend instead (dri-probe): `pipe_loader_drm_probe_fd`
 * **dups the descriptor** with `os_dupfd_cloexec` before it does anything else, and
 * `fcntl(0x57, F_DUPFD_CLOEXEC)` on a number the kernel never handed out fails - so the probe
 * returned false before a single ioctl, and `driCreateNewScreen3` returned NULL. Measured on
 * hardware 2026-09-19: `getenv` ran, then "the DRI frontend would not create a screen", with no
 * ioctl trace at all (worklog 049).
 *
 * So the descriptor now has to be a real, dup-able fd. `OOPS_WINSYS_FD` remains only as the
 * fallback for when a real one cannot be obtained, which keeps mesa-probe's path working
 * unchanged. `s_winsys_fd` holds whichever it turned out to be. */
#define OOPS_WINSYS_FD 0x0057

static int s_winsys_fd = OOPS_WINSYS_FD;

/* GB_ADDR_CONFIG (dword 0x263e), derived rather than read.
 *
 * The register is unreachable from a title: the CP rejects a userspace COPY_DATA of 0x263e as
 * privileged and faults the process. The READ_MMR_REG case below carries that evidence. What
 * *is* reachable is the layout the register describes, because oops-sdk already implements
 * that layout correctly on this silicon - agc_tiler.c carries 64KB 32bpp basis vectors that
 * oops-gl draws through, on hardware, without artefacts.
 *
 * So the value is recovered by inverting Mesa's own addrlib against that tiler: enumerate the
 * fields addrlib reads from this register (gfx10_gb_reg.h - NUM_PIPES, PIPE_INTERLEAVE_SIZE,
 * MAX_COMPRESSED_FRAGS, NUM_PKRS, eleven bits in all) and keep the candidates whose computed
 * swizzle reproduces the tiler's byte offset for every one of the 16,384 pixels in the block.
 * Of the 224 candidates addrlib accepts under the chip identity device_info.c reports, 32
 * match, and all 32 agree:
 *
 *   NUM_PIPES            = 4 (16 pipes)  pinned; the other 192 candidates all fail
 *   PIPE_INTERLEAVE_SIZE = 0 (256 B)     the only interleave addrlib has a pattern for
 *   MAX_COMPRESSED_FRAGS   left 0        all four values reproduce the tiler identically
 *   NUM_PKRS               left 0        read only under RB+ (gfx10addrlib.cpp:943)
 *
 * The two free fields are not unknowns that happen to be unmeasured. Nothing in Mesa's GFX10
 * path lets them reach a colour surface's addresses here, so no value of them changes a
 * layout. The one swizzle mode that matches is 64KB_R_X, alone among the four 64KB modes.
 *
 * This is therefore not "the register's value on the console", and must not be quoted as one.
 * Real registers carry bits addrlib ignores - Mesa's own navi10 dump sets bit 20 - and a
 * derivation recovers only the bits addrlib reads. It is the value that makes addrlib produce
 * the layout the hardware is observed to use, which is the whole of what this register is
 * consumed for: addrlib, plus num_tile_pipes in ac_gpu_info.c, which reaches WALK_FENCE_SIZE
 * and only distinguishes two pipes from more, plus an assertion that the interleave is 256 B.
 *
 * 16,384 pixels is 128x128, which is exactly one block, so for a while this was an agreement
 * established inside one block and extrapolated to whole surfaces (worklog 030). It no longer is:
 * `tools/tiling-compare` compares addrlib against the tiler over a 3x2 block surface, 98,304
 * pixels, and nothing disagrees - with a control at eight pipes that disagrees on 92,160 of them,
 * so the comparison is known to be able to see a difference. `make check` re-runs it. Worklog 031
 * has the reasoning; the upshot is that this value reproduces the tiler between blocks as well as
 * within one, which is what presentation depends on.
 *
 * It is tied to the chip identity. Under an RB+ (GFX10.3) identity, none of the candidates
 * reproduces the tiler - 68 evaluated, none matching, and the 4 that will not initialise are
 * 64-pipe configurations that produce a six-bit XOR where the tiler has four. So reclassifying
 * this part as GFX10.3 would not change this number, it would invalidate it and leave nothing
 * to replace it with. That is also the answer to the question the READ_MMR_REG comment leaves
 * open: the silicon addresses like GFX10.1, and the "GFX10.3" in agc_tiler.c's own comment is
 * a mislabel of vectors that are themselves correct.
 */
#define OOPS_GB_ADDR_CONFIG 0x00000004u

uint32_t oops_winsys_gb_addr_config(void)
{
#ifdef OOPS_GB_ADDR_CONFIG
    return (uint32_t)(OOPS_GB_ADDR_CONFIG);
#else
    /* Not a legal value: NUM_PIPES of 0 with a 256 B interleave is not a layout anything here
     * has measured or derived, so the register read refuses on it rather than answering. */
    return 0u;
#endif
}

/* AMDGPU_INFO carries the destination pointer and its size inside the request, so a reply is a
 * bounded copy rather than a write through the request structure itself. */
int oops_winsys_copy_out(const void *request, const void *src, unsigned long size)
{
    const struct drm_amdgpu_info *info = (const struct drm_amdgpu_info *)request;
    unsigned long n = size < info->return_size ? size : info->return_size;

    if (!info->return_pointer || !n) {
        /* Refusing without a word is how a query that this file believes it answers can fail
         * anyway and leave nothing in the log to say so. Every refusal in this file names
         * itself. */
        oops_winsys_log("query %u: no reply written (pointer %s, want %lu, room %lu)",
                        info->query, info->return_pointer ? "ok" : "null",
                        size, (unsigned long)info->return_size);
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
 * One line per distinct AMDGPU_INFO query, the first time it is asked.
 *
 * A hardware run is the expensive resource here: build, deploy, launch, read the log. A log that
 * names only the query that failed turns "which queries does radeonsi actually need" into one
 * run per query. This turns it into one run, because the trace is the whole sequence radeonsi
 * asked for in order, and the refusal is wherever it stops.
 *
 * First-time-only because several of these are asked in loops - a per-call line would bury the
 * sequence in repeats. The codes are small and dense (`AMDGPU_INFO_*` runs to about 0x22), so a
 * 64-bit set covers every one defined today, and anything outside it is logged every time rather
 * than silently dropped.
 */
static void trace_query(uint32_t query)
{
    static uint64_t seen;

    if (query < 64u) {
        uint64_t bit = (uint64_t)1 << query;
        if (seen & bit) {
            return;
        }
        seen |= bit;
    }
    oops_winsys_log("AMDGPU_INFO query %u (0x%x) asked", query, query);
}

/*
 * The device description. radeonsi reads this before it programs anything and will not start
 * without plausible answers, so every field here has to say where it came from.
 *
 * Most of this is not measured on this console yet. The request that would settle it is
 * REQ-20260914T1558Z-7d41; until it returns, this refuses rather than inventing, and what it does
 * answer is either something the collection has seen or the kernel's own answer at run time.
 */
static int info_query(struct drm_amdgpu_info *info)
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
         * Every route to it has now been tried and closed (REQ-20260914T2320Z-6f14):
         *   - No vendor register-read function is exported (obSCEne 166-agc/hardware-registers:
         *     driver-fn-absent).
         *   - A userspace COPY_DATA PM4 packet does not merely fail, it FAULTS the GPU: the CP
         *     rejects 0x263e as a privileged register and raises GPU_FAULT_BAD_COMMAND_ASYNC
         *     against the process (sweep 20260915-150825: "GPU Bad packet error:Privilege reg
         *     ... 0x263e"). This is measured, not assumed, and it is why the packet is never to
         *     be resubmitted.
         *
         * So the value cannot be read from a title on retail firmware. It is architectural and
         * fixed for this GPU, so a *public source* naming it for this exact part would be
         * legitimate provenance (CLAUDE.md principle 3) - but none is in hand, and a number
         * carried over from a PC part of the same family is exactly the silent-corruption guess
         * this refuses to make (principle 4).
         *
         * It is answered instead from OOPS_GB_ADDR_CONFIG at the top of this file, which is
         * neither of those things: it is derived from oops-sdk's hardware-validated tiler by
         * inverting addrlib, and the derivation - what it pins, what it leaves free, and what it
         * is not - is written out beside the define. REQ-6f14's suggested 0x00000244 turns out
         * to be one of the 32 values that satisfy that derivation, differing only in the fields
         * nothing reads; but it is not adopted as such, because what makes a value right here is
         * the derivation and not the suggestion.
         *
         * If the define is ever removed, this refuses again rather than inventing, and radeonsi
         * fails init honestly instead of rendering onto a guessed tiling layout.
         */
        if (info->read_mmr_reg.dword_offset == 0x263e) {
            uint32_t gb = oops_winsys_gb_addr_config();
            if (gb == 0) {
                oops_winsys_log("GB_ADDR_CONFIG (0x263e) is privileged and faults on a userspace "
                                "read (REQ-6f14); no value is defined, refusing rather than "
                                "guessing");
                return -EACCES;
            }
            /* Says how it is known, because it is not known the way a measured value is. */
            oops_winsys_log("GB_ADDR_CONFIG (0x263e) answered 0x%08x: derived by inverting "
                            "addrlib against oops-sdk's tiler, not read from the register",
                            gb);
            return oops_winsys_copy_out(info, &gb, sizeof(gb));
        }
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
    case AMDGPU_INFO_MEMORY: {
        /* Answered from the kernel's own direct-memory size; device_info.c says which part of
         * that is measured and which is assumed. */
        struct drm_amdgpu_memory_info mem;
        int r = oops_winsys_memory_info(&mem);
        return r ? r : oops_winsys_copy_out(info, &mem, sizeof(mem));
    }
    case AMDGPU_INFO_FW_VERSION: {
        /*
         * Firmware versions. Nothing here has measured one, and unlike most unanswered things
         * this cannot simply refuse: `ac_query_gpu_info` treats a failed ME, PFP or MEC query
         * as fatal and gives up on the device.
         *
         * Zero is answered instead, and zero is not a guess dressed up as a number - it is the
         * conservative end of every gate Mesa applies to these on this part. The ME and PFP
         * comparisons in radeonsi are GFX6/7/8 only; the PFP one behind the ZPASS event needs
         * GFX11; the MEC one (`has_taskmesh_indirect0_bug`, `mec_fw_version < 100`) needs
         * GFX10_3, which today's GB_ADDR_CONFIG derivation rules out for this part, and in any
         * case a low version there turns the workaround *on*. So a zero enables no fast path
         * and selects the safe side of anything keyed off it.
         *
         * If a version ever matters on this hardware, it is measurable - the vendor driver
         * knows its own firmware - and this is where the measurement would land.
         */
        struct drm_amdgpu_info_firmware fw;
        memset(&fw, 0, sizeof(fw));
        oops_winsys_log("firmware version for type 0x%x is not measured; answering 0, which is "
                        "the conservative side of every gate Mesa keys off it on this part",
                        info->query_fw.fw_type);
        return oops_winsys_copy_out(info, &fw, sizeof(fw));
    }
    case AMDGPU_INFO_HW_IP_INFO: {
        /*
         * What engines exist and how many rings each has. Mesa asks for every IP type in turn,
         * skips the ones that refuse, and then fails the device outright unless GFX or COMPUTE
         * came back with a ring. So this is the call that decides whether there is a GPU at all.
         *
         * Only GFX is answered, and that is not a choice this makes: Mesa refuses a compute
         * queue on this exact part by name -
         *
         *     if (ip_type == AMD_IP_COMPUTE && family == FAMILY_NV &&
         *         ASICREV_IS(external_rev, GFX1013)) return false;   // "broken compute queue"
         *
         * - so GFX is the only route to a usable device here, and a COMPUTE answer would be
         * discarded whatever it said. Every other type refuses, which is what Mesa expects of
         * an engine that is not present.
         *
         * One graphics ring is what has actually been seen to work. obSCEne's
         * 166-agc/driver-submit-fence creates a queue, submits a command buffer and sees the
         * fence retire (rc 0, fence-hit 1), and D003's route measurement put radeonsi's own
         * initial state through that path on hardware and had it retire next to the compositor
         * (worklog 002). Neither says anything about a second ring, so one is claimed.
         *
         * The version fields are left for Mesa to correct. With `ip_discovery_version` zero it
         * takes the major and minor below, then overrides the minor to 1 for GFX1013 - so this
         * part is GFX10.1 by Mesa's own hand, which is the same answer today's GB_ADDR_CONFIG
         * derivation reached from the other direction.
         *
         * Alignments are left zero deliberately. Mesa raises whatever arrives to at least 256
         * bytes (`ib_alignment = MAX3(start, size, 256)`), and 256 is already the alignment
         * oops-sdk asks of a command buffer, so zero and 256 describe the same requirement.
         */
        struct drm_amdgpu_info_hw_ip ip;

        if (info->query_hw_ip.type != AMDGPU_HW_IP_GFX) {
            oops_winsys_log("hardware IP type %u is not present on this device",
                            info->query_hw_ip.type);
            return -ENOSYS;
        }
        memset(&ip, 0, sizeof(ip));
        ip.hw_ip_version_major = 10;
        ip.hw_ip_version_minor = 1;
        ip.available_rings = 0x1;
        return oops_winsys_copy_out(info, &ip, sizeof(ip));
    }

    case AMDGPU_INFO_HW_IP_COUNT: {
        /*
         * How many instances of an IP block there are - not how many rings one instance has,
         * which is `available_rings` above. Mesa asks it per IP type and tolerates a refusal
         * (`ac_gpu_info.c:1505` only stores the answer `if (!ac_drm_query_hw_ip_count(...))`),
         * so this is not known to unblock anything. It is answered because the answer is not a
         * guess: the case above already states that this device presents one graphics IP with
         * one ring, and a count of 1 is that same fact said the other way round. Two places
         * describing one device must not disagree.
         *
         * Anything other than graphics refuses, exactly as `HW_IP_INFO` does, rather than
         * reporting zero instances - a refusal says "not answered here", a zero would be a claim
         * about the hardware that nothing has measured.
         */
        uint32_t count = 1;

        if (info->query_hw_ip.type != AMDGPU_HW_IP_GFX) {
            oops_winsys_log("hardware IP type %u has no instance count; only graphics is known",
                            info->query_hw_ip.type);
            return -ENOSYS;
        }
        return oops_winsys_copy_out(info, &count, sizeof(count));
    }

    default:
        /* Naming the query is not a nicety. A refusal here costs a build, a deployment and a
         * hardware run to observe, and "unrecognised query" spends all of that without saying
         * which one - which is exactly what the 2026-09-17 run cost. The number is what maps to
         * a name in `amdgpu_drm.h`. */
        oops_winsys_log("ioctl AMDGPU_INFO query %u (0x%x) is not implemented yet",
                        info->query, info->query);
        return -ENOSYS;
    }
}

/*
 * Every AMDGPU_INFO query, and what it answered.
 *
 * `trace_query` reports the first time each query is asked, which keeps a loop from burying the
 * sequence. That suppression hid the thing it was built to find: on the 2026-09-17 15:11 run
 * every query was traced exactly once and answered, and radeonsi still gave up - because the
 * query that failed was a *repeat* of one already traced, and the handler that failed it said
 * nothing.
 *
 * So a failure is never suppressed, however many times it has been seen. A success stays quiet
 * after the first, because that is the noise the trace exists to avoid.
 */
int oops_winsys_info(struct drm_amdgpu_info *info)
{
    int r;

    trace_query(info->query);
    r = info_query(info);
    if (r != 0) {
        oops_winsys_log("AMDGPU_INFO query %u (0x%x) answered %d", info->query, info->query, r);
    }
    return r;
}

static int ioctl_dispatch(unsigned long request, void *arg);

int oops_winsys_ioctl(int fd, unsigned long request, void *arg)
{
    static unsigned s_seq;
    unsigned n = ++s_seq;
    int r;

    if (fd != s_winsys_fd) {
        /*
         * A descriptor other than the one this shim handed out. The earlier version refused any
         * such call with -EBADF, on the reasoning that the winsys serves exactly one device - but
         * it also predicted the case that makes a blanket refusal wrong, in as many words: "if
         * Mesa ever duplicates the descriptor - and a loader that takes ownership of a device fd
         * is entitled to - then every call after that point arrives here and fails."
         *
         * That is now the measured, legitimate case: the DRI frontend dups the device fd
         * (`pipe_loader_drm_probe_fd`) and radeonsi issues its ioctls on the dup, so refusing it
         * turns a working driver into a silent -EBADF wall (the dri-probe failure this fixes).
         *
         * But "serve any fd" over-corrects and loses a real safety property the host suite
         * encodes: a command on a descriptor that was never opened should still be refused. The
         * two are distinguishable without tracking dups - **a dup is a real, open descriptor and a
         * never-opened number is not**, which `fcntl(fd, F_GETFD)` reports. So a valid open fd is
         * served as the dup it is, and a bogus one is still refused.
         */
        if (fcntl(fd, F_GETFD) == -1) {
            static int s_complained = -1;
            if (fd != s_complained) {
                s_complained = fd;
                oops_winsys_log("ioctl on fd %d, which is not open and is not the winsys device "
                                "fd %d; refusing", fd, s_winsys_fd);
            }
            return -EBADF;
        }

        static int s_noted = -1;
        if (fd != s_noted) {
            s_noted = fd;
            oops_winsys_log("ioctl on fd %d, an open duplicate of the winsys device fd %d; "
                            "serving it (the frontend is entitled to dup the device)",
                            fd, s_winsys_fd);
        }
    }
    r = ioctl_dispatch(request, arg);

    /*
     * Every call, numbered, with what it answered - and never suppressed as a repeat.
     *
     * The three instruments before this one each answered a narrower question than the one that
     * mattered, and each cost a hardware run. They could all say *which* commands this shim
     * handled; none could say *how many times*. That is the whole difficulty: on the
     * 2026-09-17 15:15 run the device description and the GB_ADDR_CONFIG line each appeared
     * exactly once, which is consistent both with radeonsi asking once and with it asking twice
     * and something between here and there swallowing the second. Those two readings point at
     * completely different faults and the log could not separate them.
     *
     * A sequence number separates them, because a repeat is now a line of its own. The volume is
     * small - the whole startup path is a handful of commands - so nothing here needs rationing.
     */
    oops_winsys_log("ioctl #%u 0x%lx answered %d", n, request, r);
    return r;
}

static int ioctl_dispatch(unsigned long request, void *arg)
{
    switch (request) {
    /* The path to a first frame. These six are what a triangle needs, and they are the order
     * unit 5 implements them in. */
    case DRM_IOCTL_AMDGPU_INFO:          return oops_winsys_info((struct drm_amdgpu_info *)arg);
    case DRM_IOCTL_AMDGPU_GEM_CREATE:    return oops_winsys_gem_create((union drm_amdgpu_gem_create *)arg);
    case DRM_IOCTL_AMDGPU_GEM_MMAP:      return oops_winsys_gem_mmap((union drm_amdgpu_gem_mmap *)arg);
    /* GEM_VA arrives in two encodings, and only one is the header's. libdrm issues it through
     * drmCommandWriteRead (mesa/subprojects/libdrm-2.4.133/amdgpu/amdgpu_bo.c:794,830 at the pin),
     * which builds the request as _IOWR from the struct size - so the value that reaches here is
     * the read/write form (0xc0406448), while the header's DRM_IOCTL_AMDGPU_GEM_VA macro is
     * DRM_IOW (0x40406448) and the plain case would miss every real call. Match both. This is the
     * "one ioctl, two encodings" shape worklog 038 named; GEM_VA is where it bites because its
     * header macro alone in this family declares write-only while the struct now carries a return. */
    case DRM_IOCTL_AMDGPU_GEM_VA:
    case DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_VA, struct drm_amdgpu_gem_va):
        return oops_winsys_gem_va((struct drm_amdgpu_gem_va *)arg);
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

    /* Generic DRM, issued by libdrm rather than by its AMD layer. GET_CLIENT comes before all
     * of them: it is the first statement of `amdgpu_device_initialize` and its failure returns
     * from that function before the version is ever asked for. */
    case DRM_IOCTL_GET_CLIENT:           return oops_winsys_get_client((struct drm_client *)arg);
    case DRM_IOCTL_GET_CAP:              return oops_winsys_get_cap((struct drm_get_cap *)arg);
    case DRM_IOCTL_VERSION:              return oops_winsys_version((struct drm_version *)arg);
    case DRM_IOCTL_GEM_CLOSE:            return oops_winsys_gem_close(((struct drm_gem_close *)arg)->handle);
    case DRM_IOCTL_GEM_FLINK:            return unimplemented("GEM_FLINK");
    case DRM_IOCTL_GEM_OPEN:             return unimplemented("GEM_OPEN");

    /* Synchronisation objects, in syncobj.c. `amdgpu_winsys_create` creates one before it asks
     * the device anything and treats a failure as fatal, so create and destroy gate every answer
     * this file gives. The rest of the family refuses: nothing submits work yet, so nothing can
     * signal, and the platform exports no way to block on a fence either (D007). */
    case DRM_IOCTL_SYNCOBJ_CREATE:
        return oops_winsys_syncobj_create((struct drm_syncobj_create *)arg);
    case DRM_IOCTL_SYNCOBJ_DESTROY:
        return oops_winsys_syncobj_destroy((struct drm_syncobj_destroy *)arg);
    case DRM_IOCTL_SYNCOBJ_WAIT:         return unimplemented("SYNCOBJ_WAIT");
    case DRM_IOCTL_SYNCOBJ_RESET:        return unimplemented("SYNCOBJ_RESET");
    case DRM_IOCTL_SYNCOBJ_SIGNAL:       return unimplemented("SYNCOBJ_SIGNAL");

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
 * The version is libdrm.s interface version, not ours. It was 3.49, chosen as what the pinned
 * libdrm expects from a kernel carrying every command in D005.s list - but libdrm is not the
 * only reader of it. Mesa.s own `ac_query_gpu_info` refuses below 3.54 outright:
 *
 *     if (info->drm_minor < 54) { ... return AC_QUERY_GPU_INFO_FAIL; }
 *
 * and it does that before it asks for anything else at all, so at 3.49 the startup path stopped
 * on this call and no later answer in this file was ever reached - including the register the
 * whole of worklog 010 is about.
 *
 * 3.54 is the minimum Mesa will talk to, and claiming it turns nothing on. Every capability
 * Mesa gates on the minor sits above it: the GPUVM fault query at 55, default zerovram at 59,
 * the two gfx12 DCC flags at 58 and 60. The two workaround branches keyed below 63 stay active,
 * which is the conservative side of each. The only gate at or under 54 is a buffer path at >=
 * 47, already true at 3.49. So this is the smallest version that lets the stack proceed, and it
 * claims no behaviour this shim does not already have.
 */
int oops_winsys_version(struct drm_version *arg)
{
    static const char name[] = "amdgpu";
    static const char date[] = "20150101";
    static const char desc[] = "oops-mesa winsys over the platform graphics driver";

    arg->version_major = 3;
    arg->version_minor = 54;
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

/*
 * The authentication question - and the reason nothing above it was ever reached.
 *
 * `amdgpu_device_initialize` does not begin with the version check. It begins with
 * `amdgpu_get_auth`, and it returns that call.s error without touching anything else:
 *
 *     r = amdgpu_get_auth(fd, &flag_auth);
 *     if (r) { ... return r; }              (libdrm 2.4.133, amdgpu/amdgpu_device.c)
 *
 * `amdgpu_get_auth` takes one of two routes:
 *
 *     if (drmGetNodeTypeFromFd(fd) == DRM_NODE_RENDER)
 *             *auth = 0;
 *     else
 *             r = drmIoctl(fd, DRM_IOCTL_GET_CLIENT, &client);
 *
 * The first route is unreachable here, and not because of which descriptor this shim hands
 * back. `drmGetNodeTypeFromFd` decides by `fstat`: it wants a character device whose major and
 * minor fall in the range reserved for DRM and whose node exists under /dev/dri. This platform
 * has neither that directory nor that major, so the answer is -1 for any descriptor the shim
 * could return. The shortcut cannot be taken.
 *
 * So the ioctl is issued, and until this case existed it fell to the default branch and
 * refused. That refusal stopped `amdgpu_device_initialize` on its first statement - ahead of
 * DRM_IOCTL_VERSION, ahead of ACCEL_WORKING, ahead of the GB_ADDR_CONFIG read that libdrm.s own
 * `amdgpu_query_gpu_info_init` makes, and ahead of every line of `ac_query_gpu_info`. Each of
 * those is answered above, and none of them had ever been asked.
 *
 * libdrm reads one field of the reply. `auth` is not guessed: it is the value libdrm.s own
 * render-node branch assigns without asking anyone. A render node reports unauthenticated
 * because there is no DRM master to authenticate against, and that is this platform exactly -
 * no display server, no master, no magic handshake. Answering 0 hands libdrm the conclusion it
 * would have reached itself had `fstat` been able to see what this device is.
 */
int oops_winsys_get_client(struct drm_client *arg)
{
    /* Client 0 is this process, which is the only one holding the device. A query for any other
     * index asks about a client table this platform does not have, and a kernel answers that
     * with -EINVAL rather than with an empty record. */
    if (arg->idx != 0) {
        return -EINVAL;
    }
    arg->auth = 0;
    arg->pid = 0;
    arg->uid = 0;
    arg->magic = 0;
    arg->iocs = 0;
    return 0;
}

/*
 * Optional DRM capabilities. Two are asked on the radeonsi path, and both are answered zero -
 * which is a statement about this shim, not about the silicon:
 *
 *   DRM_CAP_SYNCOBJ_TIMELINE (0x14) is read once, in `util_sync_provider_drm`, which installs
 *       `timeline_wait` and `timeline_signal` only if it comes back non-zero. `ac_query_gpu_info`
 *       then sets `has_timeline_syncobj` from whether that pointer exists. So this is the single
 *       lever behind D007.s decision to start binary-only, and answering zero is that decision
 *       being carried rather than a capability being denied.
 *   DRM_CAP_ADDFB2_MODIFIERS (0x10) reaches `has_modifiers`, which decides whether radeonsi
 *       offers format modifiers when a buffer is shared. Nothing here shares a buffer with
 *       another process, and ADDFB2 - the call that would consume a modifier - belongs to the
 *       mode-setting interface this shim does not implement at all.
 *
 * Both callers treat a refusal and a zero alike, so answering changes no behaviour today. It is
 * done anyway so that a capability this file has reasoned about cannot be confused, in the log,
 * with one it has never heard of. Every other capability refuses and names itself, for the same
 * reason the AMDGPU_INFO queries do.
 */
int oops_winsys_get_cap(struct drm_get_cap *arg)
{
    switch (arg->capability) {
    case DRM_CAP_SYNCOBJ_TIMELINE:
    case DRM_CAP_ADDFB2_MODIFIERS:
        arg->value = 0;
        return 0;

    case DRM_CAP_PRIME:
        /*
         * Buffer sharing between processes, which this platform does not do and which D009
         * settled from the other side: libdrm refuses `amdgpu_bo_handle_type_kms` imports with
         * `-EPERM`, and every remaining handle type is a cross-process mechanism that does not
         * exist here (worklog 032).
         *
         * So zero is an answer rather than a shrug. `u_screen.c:139` reads it into
         * `caps->dmabuf`, and refusing left that field at its default - the same value by
         * accident. Saying it deliberately is the difference between "no sharing" and "this shim
         * did not know", and only one of those is true.
         */
        arg->value = 0;
        return 0;

    default:
        oops_winsys_log("GET_CAP 0x%llx is not a capability this shim has decided",
                        (unsigned long long)arg->capability);
        return -EINVAL;
    }
}

int oops_winsys_open(void)
{
    if (!sceAgcDriverCreateQueue) {
        oops_winsys_log("the platform graphics driver is not bound; nothing to open");
        return -ENODEV;
    }

    /*
     * A real, dup-able descriptor, because the DRI frontend dups it (see the note on
     * `OOPS_WINSYS_FD`). It is obtained by duplicating an already-open standard descriptor rather
     * than by opening a path: stderr is open on every leg - the same programme of sweeps that
     * found stdout/stderr go nowhere also found them open (`REQ-20260917T0233Z-5c9d`) - and
     * duplicating an open fd is a kernel primitive that needs no filesystem the sandbox might not
     * have. `fcntl(F_DUPFD_CLOEXEC)` is exactly what `os_dupfd_cloexec` will call on the result,
     * so if this succeeds the frontend's dup will too.
     *
     * The fd's *identity* is all that is used. `oops_winsys_mmap` ignores it (`(void)fd`), the
     * ioctl path is patched to this shim regardless of it, and nothing here reads, writes or maps
     * it - so the fact it aliases stderr's sink is immaterial; only `ioctl`, which never reaches
     * the kernel for it, is ever issued.
     *
     * On failure it falls back to the token, which is what mesa-probe has always used and which
     * still works for a title that does not go through the frontend.
     */
    /*
     * A real, open descriptor rather than a bare token. It cannot be *dupped* - measured on
     * hardware, `fcntl(F_DUPFD)` and `F_DUPFD_CLOEXEC` both return EINVAL even on a genuine open
     * regular file, so this platform simply does not implement fcntl-based duplication (worklog
     * 049). The DRI frontend dups the device fd through `os_dupfd_cloexec`, which patch 003 makes
     * pass the fd through unchanged on exactly that EINVAL - so the descriptor the frontend then
     * uses is this same one, and it must be a real fd, because the frontend and libdrm's device
     * probing do incidental non-ioctl things to it (`fstat`, `/proc`-style lookups) that a token
     * would `EBADF`. The title's own eboot is always present and openable (measured: fd came back
     * with errno 0), and nothing here ever reads, writes or mmaps it - `oops_winsys_mmap` ignores
     * the fd - so its being the eboot file is immaterial; only its identity is used, for the gate.
     *
     * Falls back to the token if the open ever fails, which keeps a direct-radeonsi title
     * (mesa-probe) working - that path never dups and never touches the fd but through ioctl.
     */
    int f = open("/app0/eboot.bin", O_RDONLY);
    if (f >= 0) {
        s_winsys_fd = f;
    } else {
        s_winsys_fd = OOPS_WINSYS_FD;
        oops_winsys_log("could not open a real descriptor for the device (errno %d); using the "
                        "token 0x%x, which serves a direct-radeonsi title but not the DRI frontend",
                        errno, OOPS_WINSYS_FD);
    }
    return s_winsys_fd;
}

int oops_winsys_close(int fd)
{
    /* The device fd or an open dup of it, by the same test the ioctl gate uses: a real open
     * descriptor is accepted, a never-opened one is refused. Nothing is actually closed - a real
     * fd handed out by `oops_winsys_open` is left for the platform to reclaim at process teardown,
     * which does not happen because a title parks rather than exits. */
    if (fd == s_winsys_fd || fcntl(fd, F_GETFD) != -1) {
        return 0;
    }
    return -EBADF;
}
