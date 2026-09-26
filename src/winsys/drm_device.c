/*
 * The kernel interface libdrm talks to.
 *
 * libdrm's AMD layer reaches the kernel through drmCommandWriteRead, drmCommandWrite
 * and drmIoctl, all ioctl() underneath. This file is the winsys seam: a descriptor
 * standing for the device, an ioctl that dispatches the AMD command set onto the vendor
 * graphics driver oops-sdk binds, and mmap for buffer access. libdrm's own bookkeeping
 * stays upstream's (D005). Every command libdrm's amdgpu layer issues has a case; one
 * with nothing behind it returns -ENOSYS and logs its name rather than succeeding with
 * nothing done.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h> /* STDERR_FILENO, for the device descriptor in `oops_winsys_open` */

/* glibc gates F_DUPFD_CLOEXEC behind a feature macro in the host build. A title never
 * execs a child, so plain F_DUPFD is equivalent here. */
#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC F_DUPFD
#endif

#include "drm-uapi/amdgpu_drm.h"
#include "drm-uapi/drm.h"

#include "oops/memory.h"
#include "agc/driver.h"

#include "oops_winsys.h"

/* The fallback device descriptor, used only when `oops_winsys_claim_fd` finds no real
 * one. A token serves a title that calls radeonsi directly, since every ioctl is
 * patched to `oops_winsys_ioctl`, but not the DRI frontend, which needs a descriptor
 * `fstat` answers for. `s_winsys_fd` holds whichever descriptor is in use. */
#define OOPS_WINSYS_FD 0x0057

static int s_winsys_fd = OOPS_WINSYS_FD;

/* GB_ADDR_CONFIG (dword 0x263e), derived rather than read: the CP rejects a userspace
 * read of it as privileged. The value inverts Mesa's addrlib against oops-sdk's
 * agc_tiler.c, which draws correctly on hardware: of the candidates addrlib accepts
 * under the GFX1013 identity, those reproducing the tiler's 64KB_R_X layout all have
 * NUM_PIPES = 4 (16 pipes) and PIPE_INTERLEAVE_SIZE = 0 (256 B). MAX_COMPRESSED_FRAGS
 * and NUM_PKRS (read only under RB+, gfx10addrlib.cpp:943) reach no colour-surface
 * address and stay 0. It is the value addrlib needs for the observed layout, not the
 * register's contents. `tools/tiling-compare` checks it over a multi-block surface in
 * `make check`. */
#define OOPS_GB_ADDR_CONFIG 0x00000004u

uint32_t oops_winsys_gb_addr_config(void) {
    return (uint32_t)(OOPS_GB_ADDR_CONFIG);
}

/* AMDGPU_INFO carries the destination pointer and its size inside the request, so a
 * reply is a bounded copy rather than a write through the request structure itself. */
int oops_winsys_copy_out(const void *request, const void *src, unsigned long size) {
    const struct drm_amdgpu_info *info = (const struct drm_amdgpu_info *)request;
    unsigned long n = size < info->return_size ? size : info->return_size;

    if (!info->return_pointer || !n) {
        /* Every refusal in this file names itself. */
        oops_winsys_log("query %u: no reply written (pointer %s, want %lu, room %lu)",
                        info->query, info->return_pointer ? "ok" : "null", size,
                        (unsigned long)info->return_size);
        return -EINVAL;
    }
    memcpy((void *)(uintptr_t)info->return_pointer, src, n);
    return 0;
}

static int unimplemented(const char *name) {
    oops_winsys_log("ioctl %s is not implemented yet", name);
    return -ENOSYS;
}

/* One line per distinct AMDGPU_INFO query, the first time it is asked, so one hardware
 * run shows the whole sequence radeonsi asks without loops burying it. Codes past 63
 * are logged every time. */
static void trace_query(uint32_t query) {
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

/* The device description. Each answer is measured, derived, or the kernel's own at run
 * time; anything else refuses rather than borrowing a PC part's value. */
static int info_query(struct drm_amdgpu_info *info) {
    switch (info->query) {
    case AMDGPU_INFO_ACCEL_WORKING: {
        /* libdrm refuses the device on zero. A submitted command buffer retires on this
         * queue beside the compositor (obSCEne 166-agc/driver-submit-fence). */
        uint32_t working = 1;
        return oops_winsys_copy_out(info, &working, sizeof(working));
    }

    case AMDGPU_INFO_READ_MMR_REG:
        /*
         * libdrm's device initialisation reads GB_ADDR_CONFIG (0x263e) unconditionally.
         * No vendor register-read function is exported, and a userspace COPY_DATA of
         * 0x263e raises GPU_FAULT_BAD_COMMAND_ASYNC as a privileged register, so that
         * packet is never submitted. The answer is the derived OOPS_GB_ADDR_CONFIG;
         * with no value this refuses rather than guessing a tiling layout.
         */
        if (info->read_mmr_reg.dword_offset == 0x263e) {
            uint32_t gb = oops_winsys_gb_addr_config();
            if (gb == 0) {
                oops_winsys_log(
                    "GB_ADDR_CONFIG (0x263e) is privileged and faults on a userspace "
                    "read; no value is defined, refusing rather than "
                    "guessing");
                return -EACCES;
            }
            oops_winsys_log(
                "GB_ADDR_CONFIG (0x263e) answered 0x%08x: derived by inverting "
                "addrlib against oops-sdk's tiler, not read from the register",
                gb);
            return oops_winsys_copy_out(info, &gb, sizeof(gb));
        }
        oops_winsys_log("register read 0x%x is not answered; nothing has measured it",
                        info->read_mmr_reg.dword_offset);
        return -ENOSYS;

    case AMDGPU_INFO_DEV_INFO: {
        /* device_info.c says how each field is known and logs the assumed count. */
        struct drm_amdgpu_info_device dev;
        oops_winsys_device_info(&dev);
        return oops_winsys_copy_out(info, &dev, sizeof(dev));
    }
    case AMDGPU_INFO_MEMORY: {
        /* The kernel's direct-memory size, described in device_info.c. */
        struct drm_amdgpu_memory_info mem;
        int r = oops_winsys_memory_info(&mem);
        return r ? r : oops_winsys_copy_out(info, &mem, sizeof(mem));
    }
    case AMDGPU_INFO_FW_VERSION: {
        /*
         * Unmeasured, but `ac_query_gpu_info` treats a failed ME, PFP or MEC query as
         * fatal, so zero is answered. Zero is the conservative side of every gate Mesa
         * keys off these on GFX10.1: the ME/PFP checks are GFX6-8 or GFX11, and the MEC
         * one needs GFX10_3 and turns a workaround on when low.
         */
        struct drm_amdgpu_info_firmware fw;
        memset(&fw, 0, sizeof(fw));
        oops_winsys_log(
            "firmware version for type 0x%x is not measured; answering 0, which is "
            "the conservative side of every gate Mesa keys off it on this part",
            info->query_fw.fw_type);
        return oops_winsys_copy_out(info, &fw, sizeof(fw));
    }
    case AMDGPU_INFO_HW_IP_INFO: {
        /*
         * Mesa fails the device unless GFX or COMPUTE reports a ring, and discards a
         * COMPUTE queue on FAMILY_NV GFX1013 by name ("broken compute queue", in
         * mesa/src/amd/common/ac_gpu_info.c), so only GFX answers. One ring is claimed:
         * obSCEne's 166-agc/driver-submit-fence retires a submission on one queue.
         * Version 10.1 is what Mesa assigns GFX1013 itself. Alignments stay zero
         * because Mesa raises them to at least 256, the alignment oops-sdk already
         * uses.
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
         * Instances of an IP block, not rings. Mesa tolerates a refusal
         * (mesa/src/amd/common/ac_gpu_info.c:1505); one graphics instance matches the
         * HW_IP_INFO answer above. Other types refuse, since a zero would be an
         * unmeasured claim.
         */
        uint32_t count = 1;

        if (info->query_hw_ip.type != AMDGPU_HW_IP_GFX) {
            oops_winsys_log(
                "hardware IP type %u has no instance count; only graphics is known",
                info->query_hw_ip.type);
            return -ENOSYS;
        }
        return oops_winsys_copy_out(info, &count, sizeof(count));
    }

    default:
        /* The number maps to a name in `amdgpu_drm.h`. */
        oops_winsys_log("ioctl AMDGPU_INFO query %u (0x%x) is not implemented yet",
                        info->query, info->query);
        return -ENOSYS;
    }
}

/* Every AMDGPU_INFO query. A success is traced once; a failure is logged every time,
 * because a repeated query can fail after its first answer succeeded. */
int oops_winsys_info(struct drm_amdgpu_info *info) {
    int r;

    trace_query(info->query);
    r = info_query(info);
    if (r != 0) {
        oops_winsys_log("AMDGPU_INFO query %u (0x%x) answered %d", info->query,
                        info->query, r);
    }
    return r;
}

static int ioctl_dispatch(unsigned long request, void *arg);

int oops_winsys_ioctl(int fd, unsigned long request, void *arg) {
    static unsigned s_seq;
    unsigned n = ++s_seq;
    int r;

    if (fd != s_winsys_fd) {
        /*
         * The DRI frontend may duplicate the device fd (`pipe_loader_drm_probe_fd`) and
         * issue ioctls on the copy. A real open descriptor is served as that copy; a
         * number `fcntl(F_GETFD)` does not recognise was never opened and is refused.
         */
        if (fcntl(fd, F_GETFD) == -1) {
            static int s_complained = -1;
            if (fd != s_complained) {
                s_complained = fd;
                oops_winsys_log(
                    "ioctl on fd %d, which is not open and is not the winsys device "
                    "fd %d; refusing",
                    fd, s_winsys_fd);
            }
            return -EBADF;
        }

        static int s_noted = -1;
        if (fd != s_noted) {
            s_noted = fd;
            oops_winsys_log(
                "ioctl on fd %d, an open duplicate of the winsys device fd %d; "
                "serving it (the frontend is entitled to dup the device)",
                fd, s_winsys_fd);
        }
    }
    r = ioctl_dispatch(request, arg);

    /* Every call, numbered, so a repeated command is its own line and asking twice is
     * distinguishable from asking once. The startup path is a few dozen calls. */
    oops_winsys_log("ioctl #%u 0x%lx answered %d", n, request, r);
    return r;
}

static int ioctl_dispatch(unsigned long request, void *arg) {
    switch (request) {
    /* The commands a first frame needs. */
    case DRM_IOCTL_AMDGPU_INFO:
        return oops_winsys_info((struct drm_amdgpu_info *)arg);
    case DRM_IOCTL_AMDGPU_GEM_CREATE:
        return oops_winsys_gem_create((union drm_amdgpu_gem_create *)arg);
    case DRM_IOCTL_AMDGPU_GEM_MMAP:
        return oops_winsys_gem_mmap((union drm_amdgpu_gem_mmap *)arg);
    /* libdrm issues GEM_VA through drmCommandWriteRead
     * (mesa/subprojects/libdrm-2.4.133/amdgpu/amdgpu_bo.c:794,830), which encodes it
     * _IOWR (0xc0406448), while the header's DRM_IOCTL_AMDGPU_GEM_VA is DRM_IOW
     * (0x40406448). Both are matched. */
    case DRM_IOCTL_AMDGPU_GEM_VA:
    case DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_VA, struct drm_amdgpu_gem_va):
        return oops_winsys_gem_va((struct drm_amdgpu_gem_va *)arg);
    case DRM_IOCTL_AMDGPU_CS:
        return oops_winsys_cs((union drm_amdgpu_cs *)arg);
    case DRM_IOCTL_AMDGPU_WAIT_CS:
        return oops_winsys_wait_cs((union drm_amdgpu_wait_cs *)arg);

    /* Contexts, buffer lists, idle waits. */
    case DRM_IOCTL_AMDGPU_CTX:
        return oops_winsys_ctx((union drm_amdgpu_ctx *)arg);
    case DRM_IOCTL_AMDGPU_BO_LIST:
        return oops_winsys_bo_list((union drm_amdgpu_bo_list *)arg);
    case DRM_IOCTL_AMDGPU_GEM_WAIT_IDLE:
        return oops_winsys_gem_wait_idle((union drm_amdgpu_gem_wait_idle *)arg);
    case DRM_IOCTL_AMDGPU_GEM_OP:
        return oops_winsys_gem_op((struct drm_amdgpu_gem_op *)arg);
    case DRM_IOCTL_AMDGPU_WAIT_FENCES:
        return unimplemented("WAIT_FENCES");
    case DRM_IOCTL_AMDGPU_FENCE_TO_HANDLE:
        return unimplemented("FENCE_TO_HANDLE");
    case DRM_IOCTL_AMDGPU_VM:
        return unimplemented("VM");
    case DRM_IOCTL_AMDGPU_SCHED:
        return unimplemented("SCHED");

    /* Sharing and metadata. Nothing on this platform shares buffers between processes
     * (D009). */
    case DRM_IOCTL_AMDGPU_GEM_METADATA:
        return unimplemented("GEM_METADATA");
    case DRM_IOCTL_AMDGPU_GEM_USERPTR:
        return unimplemented("GEM_USERPTR");

    /* User queues, a submission path this hardware's driver does not expose. */
    case DRM_IOCTL_AMDGPU_USERQ:
        return unimplemented("USERQ");
    case DRM_IOCTL_AMDGPU_USERQ_SIGNAL:
        return unimplemented("USERQ_SIGNAL");
    case DRM_IOCTL_AMDGPU_USERQ_WAIT:
        return unimplemented("USERQ_WAIT");

    /* Generic DRM, issued by libdrm itself. GET_CLIENT is the first statement of
     * `amdgpu_device_initialize` and gates everything after it. */
    case DRM_IOCTL_GET_CLIENT:
        return oops_winsys_get_client((struct drm_client *)arg);
    case DRM_IOCTL_GET_CAP:
        return oops_winsys_get_cap((struct drm_get_cap *)arg);
    case DRM_IOCTL_VERSION:
        return oops_winsys_version((struct drm_version *)arg);
    case DRM_IOCTL_GEM_CLOSE:
        return oops_winsys_gem_close(((struct drm_gem_close *)arg)->handle);
    case DRM_IOCTL_GEM_FLINK:
        return unimplemented("GEM_FLINK");
    case DRM_IOCTL_GEM_OPEN:
        return unimplemented("GEM_OPEN");

    /* Synchronisation objects, in syncobj.c. `amdgpu_winsys_create` creates one before
     * it asks the device anything and treats failure as fatal. The rest of the family
     * refuses: the platform exports no way to block on a fence (D007). */
    case DRM_IOCTL_SYNCOBJ_CREATE:
        return oops_winsys_syncobj_create((struct drm_syncobj_create *)arg);
    case DRM_IOCTL_SYNCOBJ_DESTROY:
        return oops_winsys_syncobj_destroy((struct drm_syncobj_destroy *)arg);
    case DRM_IOCTL_SYNCOBJ_WAIT:
        return unimplemented("SYNCOBJ_WAIT");
    case DRM_IOCTL_SYNCOBJ_RESET:
        return unimplemented("SYNCOBJ_RESET");
    case DRM_IOCTL_SYNCOBJ_SIGNAL:
        return unimplemented("SYNCOBJ_SIGNAL");

    default:
        oops_winsys_log("ioctl 0x%lx is not one this shim knows", request);
        return -EINVAL;
    }
}

/*
 * The driver identity. libdrm refuses any name but "amdgpu". The version is the kernel
 * interface version: `ac_query_gpu_info` refuses a minor below 54, and every capability
 * Mesa gates on the minor sits at 55 or above, so 3.54 is the lowest version the stack
 * accepts and it turns on nothing this shim lacks.
 */
int oops_winsys_version(struct drm_version *arg) {
    static const char name[] = "amdgpu";
    static const char date[] = "20150101";
    static const char desc[] = "oops-mesa winsys over the platform graphics driver";

    arg->version_major = 3;
    arg->version_minor = 54;
    arg->version_patchlevel = 0;

    /* libdrm calls this twice: once with null pointers to learn the lengths, then again
     * with buffers it sized from them. Both are answered here. */
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
 * The authentication query `amdgpu_device_initialize` makes first, through
 * `amdgpu_get_auth` (libdrm 2.4.133, amdgpu/amdgpu_device.c). Its render-node shortcut
 * needs `fstat` to see a DRM character device, which this platform has none of, so the
 * ioctl always arrives. auth = 0 is the value libdrm assigns a render node itself:
 * there is no DRM master to authenticate against.
 */
int oops_winsys_get_client(struct drm_client *arg) {
    /* Client 0 is this process, the only one holding the device; a kernel answers any
     * other index with -EINVAL. */
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
 * Optional DRM capabilities. The ones answered are answered zero as a statement about
 * this shim, so a decided capability is distinguishable in the log from an unknown one,
 * which refuses and names itself.
 *   DRM_CAP_SYNCOBJ_TIMELINE: `util_sync_provider_drm` installs timeline waits only on
 *   non-zero; zero keeps syncobjs binary (D007).
 *   DRM_CAP_ADDFB2_MODIFIERS: reaches `has_modifiers`; nothing shares buffers and there
 *   is no mode-setting interface.
 */
int oops_winsys_get_cap(struct drm_get_cap *arg) {
    switch (arg->capability) {
    case DRM_CAP_SYNCOBJ_TIMELINE:
    case DRM_CAP_ADDFB2_MODIFIERS:
        arg->value = 0;
        return 0;

    case DRM_CAP_PRIME:
        /* No cross-process buffer sharing exists here (D009). `u_screen.c:139` reads
         * this into `caps->dmabuf`. */
        arg->value = 0;
        return 0;

    default:
        oops_winsys_log("GET_CAP 0x%llx is not a capability this shim has decided",
                        (unsigned long long)arg->capability);
        return -EINVAL;
    }
}

/*
 * Take the device descriptor once and hold it for the life of the process.
 *
 * It is claimed from `.init_array`: a title that reaches `/data` through
 * `oops_fs_storage_path` (oops-sdk/include/oops/fs.h:88) has its root repointed, after
 * which `/app0` names nothing. `oops_mesa_run_init_array()` runs the constructor before
 * title code.
 *
 * The title's own eboot is used because libdrm keys its device table with `fstat`,
 * which never reaches this shim and which a standard stream here does not answer.
 * stderr is the fallback for a title calling radeonsi directly. The descriptor need not
 * be duplicable: patch 003 makes `os_dupfd_cloexec` pass it through. Nothing reads,
 * writes or maps it.
 */
int oops_winsys_claim_fd(void) {
    if (s_winsys_fd != OOPS_WINSYS_FD) {
        return s_winsys_fd; /* already held */
    }

    int f = open("/app0/eboot.bin", O_RDONLY);
    if (f >= 0) {
        s_winsys_fd = f;
        return s_winsys_fd;
    }

    /* `F_GETFD` is the test the ioctl gate uses and works here, unlike `F_DUPFD`. */
    if (fcntl(STDERR_FILENO, F_GETFD) != -1) {
        s_winsys_fd = STDERR_FILENO;
        oops_winsys_log(
            "could not open /app0/eboot.bin for a device descriptor (errno %d); "
            "using stderr, which answers ioctls but which libdrm cannot fstat - a "
            "direct-radeonsi title will work and the DRI frontend will not",
            errno);
        return s_winsys_fd;
    }

    oops_winsys_log(
        "no descriptor the kernel recognises is available (errno %d); using the "
        "token 0x%x, which serves a direct-radeonsi title but not the DRI frontend",
        errno, OOPS_WINSYS_FD);
    return s_winsys_fd;
}

/* Runs from `.init_array`, before any title code. Opening a file needs no graphics
 * driver, and the driver may not be bound yet, so `oops_winsys_open` checks for it. */
__attribute__((constructor)) static void oops_winsys_claim_fd_early(void) {
    (void)oops_winsys_claim_fd();
}

int oops_winsys_open(void) {
    if (!sceAgcDriverCreateQueue) {
        oops_winsys_log("the platform graphics driver is not bound; nothing to open");
        return -ENODEV;
    }

    /* The descriptor was taken in `.init_array`; this answers whether a device exists.
     */
    return oops_winsys_claim_fd();
}

int oops_winsys_close(int fd) {
    /* The same test as the ioctl gate. Nothing is closed: a title parks rather than
     * exits, and the platform reclaims the descriptor at teardown. */
    if (fd == s_winsys_fd || fcntl(fd, F_GETFD) != -1) {
        return 0;
    }
    return -EBADF;
}
