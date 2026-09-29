/*
 * The winsys shim's host suite.
 *
 * It runs on the build machine without a GPU and checks what is decided here: the
 * device description Mesa's chip identification needs, the measured fields against
 * their records, the shim's handle bookkeeping, and that an unimplemented command
 * refuses rather than succeeding with nothing done.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "drm-uapi/amdgpu_drm.h"
#include "drm-uapi/drm.h"
#include "oops_winsys.h"

/* Mesa's chip-identification macros, used rather than copied. This header includes
 * nothing, so it costs the suite no dependency on the rest of AddressLib. */
#include "amd/addrlib/src/amdgpu_asic_addr.h"

static int failures;
static int checks;

static void check(int ok, const char *what) {
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

static void test_device_is_identifiable_as_gfx1013(void) {
    /* Mesa's own `FAMILY_NV` and `ASICREV_IS`, as `ac_identify_chip` applies them, pick
     * GFX1013 from the description, so a pin bump that moves the range fails here. */
    struct drm_amdgpu_info_device dev;
    unsigned assumed = oops_winsys_device_info(&dev);

    check(dev.family == FAMILY_NV,
          "device family is the one GFX1013 is classified inside");
    check(ASICREV_IS(dev.external_rev, GFX1013) != 0,
          "Mesa's own ASICREV_IS picks GFX1013 out of that family for this revision");
    check(assumed > 0, "the description reports how much of itself is assumed");
}

/* Ask AMDGPU_INFO one query, into a caller-sized reply. */
static int query_info(uint32_t query, void *out, unsigned long size,
                      void (*fill)(struct drm_amdgpu_info *)) {
    struct drm_amdgpu_info info;

    memset(&info, 0, sizeof(info));
    info.query = query;
    info.return_pointer = (uint64_t)(uintptr_t)out;
    info.return_size = (uint32_t)size;
    if (fill) {
        fill(&info);
    }
    return oops_winsys_info(&info);
}

static void ask_gfx(struct drm_amdgpu_info *info) {
    info->query_hw_ip.type = AMDGPU_HW_IP_GFX;
}
static void ask_compute(struct drm_amdgpu_info *info) {
    info->query_hw_ip.type = AMDGPU_HW_IP_COMPUTE;
}
static void ask_me(struct drm_amdgpu_info *info) {
    info->query_fw.fw_type = AMDGPU_INFO_FW_GFX_ME;
}

static void test_syncobjs_are_handles_this_repository_owns(void) {
    /* Syncobj handles are allocated, distinct, non-zero and refused after destruction;
     * they touch no hardware (D007). */
    struct drm_syncobj_create a, b;
    struct drm_syncobj_destroy d;

    memset(&a, 0, sizeof(a));
    check(oops_winsys_syncobj_create(&a) == 0, "a syncobj can be created");
    check(a.handle != 0, "its handle is not zero, which libdrm reads as no handle");
    check(oops_winsys_syncobj_is_live(a.handle), "and the table says it is live");

    memset(&b, 0, sizeof(b));
    check(oops_winsys_syncobj_create(&b) == 0, "a second one can be created");
    check(b.handle != a.handle, "and gets a different handle");

    memset(&d, 0, sizeof(d));
    d.handle = a.handle;
    check(oops_winsys_syncobj_destroy(&d) == 0, "a live syncobj can be destroyed");
    check(!oops_winsys_syncobj_is_live(a.handle), "and stops being live");
    check(oops_winsys_syncobj_destroy(&d) == -EINVAL, "destroying it twice is refused");
    check(oops_winsys_syncobj_is_live(b.handle), "the other one is untouched");

    memset(&d, 0, sizeof(d));
    d.handle = 0;
    check(oops_winsys_syncobj_destroy(&d) == -EINVAL,
          "handle zero is refused, not accepted");

    memset(&d, 0, sizeof(d));
    d.handle = b.handle;
    check(oops_winsys_syncobj_destroy(&d) == 0, "and the second one closes cleanly");
}

static void test_a_fence_slot_is_bounds_checked(void) {
    /* A caller-supplied `AMDGPU_CHUNK_ID_FENCE` offset is range-checked against the
     * real end of the buffer, so a fence write cannot run past it. */
    union drm_amdgpu_gem_create c;
    struct drm_gem_close close_arg;
    uint32_t handle;

    /* Exactly one page, since the winsys rounds every buffer up to a page. */
    const uint64_t page = 0x4000;

    memset(&c, 0, sizeof(c));
    c.in.bo_size = page;
    c.in.alignment = 256;
    c.in.domains = AMDGPU_GEM_DOMAIN_VRAM;
    check(oops_winsys_gem_create(&c) == 0,
          "a buffer to hold a fence slot can be created");
    handle = c.out.handle;

    check(oops_winsys_bo_cpu_range(handle, 0, 8) != NULL,
          "eight bytes at the start of it have an address");
    check(oops_winsys_bo_cpu_range(handle, page - 8, 8) != NULL,
          "and so do the last eight");
    check(oops_winsys_bo_cpu_range(handle, page - 7, 8) == NULL,
          "eight bytes that would run one past the end are refused");
    check(oops_winsys_bo_cpu_range(handle, page * 2, 8) == NULL,
          "an offset beyond the buffer entirely is refused");
    check(oops_winsys_bo_cpu_range(handle + 1000, 0, 8) == NULL,
          "and a handle that was never given out is refused");

    memset(&close_arg, 0, sizeof(close_arg));
    close_arg.handle = handle;
    check(oops_winsys_gem_close(close_arg.handle) == 0, "the buffer closes");
    check(oops_winsys_bo_cpu_range(handle, 0, 8) == NULL,
          "and a closed buffer has no address any more");
}

static void test_the_part_reports_itself_as_one_memory_pool(void) {
    /* FUSION set and the three heaps equal are one statement about this console, so the
     * ids_flags bits and the memory answer are checked together. */
    struct drm_amdgpu_info_device dev;
    struct drm_amdgpu_memory_info mem;

    (void)oops_winsys_device_info(&dev);
    check((dev.ids_flags & AMDGPU_IDS_FLAGS_FUSION) != 0,
          "the part reports FUSION, so Mesa reads has_dedicated_vram as false");
    check((dev.ids_flags & AMDGPU_IDS_FLAGS_TMZ) == 0,
          "and no trusted memory, which nothing here implements");
    check(
        (dev.ids_flags & AMDGPU_IDS_FLAGS_PREEMPTION) == 0,
        "and no preemption, which is the only gate on register shadowing on this part");
    check((dev.ids_flags & AMDGPU_IDS_FLAGS_CONFORMANT_TRUNC_COORD) == 0,
          "and no conformant truncation, which keeps Mesa's gather workarounds on");

    memset(&mem, 0, sizeof(mem));
    check(oops_winsys_memory_info(&mem) == 0, "the memory description answers");
    check(mem.vram.total_heap_size == mem.gtt.total_heap_size &&
              mem.vram.total_heap_size == mem.cpu_accessible_vram.total_heap_size,
          "and reports one pool as all three heaps, which is what FUSION means");
}

static void test_the_first_question_libdrm_asks_is_answered(void) {
    /* GET_CLIENT, which `amdgpu_device_initialize` asks first, and the decided
     * capabilities answer rather than refuse. */
    struct drm_client c;
    struct drm_get_cap cap;

    memset(&c, 0, sizeof(c));
    c.idx = 0;
    c.auth = 1;
    check(oops_winsys_get_client(&c) == 0, "the client query is answered, not refused");
    check(c.auth == 0,
          "unauthenticated, the same answer a render node gives without asking");

    memset(&c, 0, sizeof(c));
    c.idx = 1;
    check(oops_winsys_get_client(&c) == -EINVAL,
          "there is no second client to describe");

    /* Mesa installs `timeline_wait` only on non-zero, so zero keeps syncobjs binary
     * (D007). */
    memset(&cap, 0, sizeof(cap));
    cap.capability = DRM_CAP_SYNCOBJ_TIMELINE;
    cap.value = 0xdeadbeef;
    check(oops_winsys_get_cap(&cap) == 0, "the timeline capability is answered");
    check(cap.value == 0, "and says no timeline, which is what D007 decided");

    memset(&cap, 0, sizeof(cap));
    cap.capability = DRM_CAP_ADDFB2_MODIFIERS;
    cap.value = 0xdeadbeef;
    check(oops_winsys_get_cap(&cap) == 0, "the modifier capability is answered");
    check(cap.value == 0,
          "and says none: nothing here shares a buffer between processes");

    memset(&cap, 0, sizeof(cap));
    cap.capability = DRM_CAP_PRIME;
    cap.value = 0xdeadbeef;
    check(oops_winsys_get_cap(&cap) == 0,
          "PRIME is answered, not refused - D009 settled it");
    check(cap.value == 0, "and says no cross-process buffer sharing exists here");

    memset(&cap, 0, sizeof(cap));
    cap.capability = 0xffff; /* a capability this shim has no opinion on */
    check(oops_winsys_get_cap(&cap) == -EINVAL,
          "a capability nothing has decided is refused");
}

static void test_the_version_mesa_will_accept(void) {
    /* The version is at or above 3.54, the floor `ac_query_gpu_info` refuses below. */
    struct drm_version v;

    memset(&v, 0, sizeof(v));
    check(oops_winsys_version(&v) == 0, "the version query answers");
    check(v.version_major == 3, "the major is the one Mesa asserts");
    check(v.version_minor >= 54,
          "the minor is at or above the floor Mesa refuses below");
}

static void test_the_graphics_engine_is_the_one_that_answers(void) {
    /* GFX answers with a ring and COMPUTE refuses: Mesa discards a compute queue on
     * this part, so GFX is the only route to a usable device. */
    struct drm_amdgpu_info_hw_ip ip;

    memset(&ip, 0, sizeof(ip));
    check(query_info(AMDGPU_INFO_HW_IP_INFO, &ip, sizeof(ip), ask_gfx) == 0,
          "the graphics engine answers");
    check(ip.available_rings != 0,
          "it has at least one ring, or Mesa fails the device");
    check(ip.hw_ip_version_major == 10, "it reports the GFX10 generation");
    /* `ac_identify_chip` maps 10.1 to GFX10 and matches nothing for 10.0, which fails
     * the device. */
    check(ip.hw_ip_version_minor == 1,
          "and the 10.1 minor, which is what selects GFX10");

    memset(&ip, 0, sizeof(ip));
    check(query_info(AMDGPU_INFO_HW_IP_INFO, &ip, sizeof(ip), ask_compute) == -ENOSYS,
          "the compute engine refuses, as an absent engine should");
    check(ip.available_rings == 0, "and writes nothing into the reply when it refuses");
}

static void test_firmware_versions_answer_conservatively(void) {
    /* Firmware versions answer zero, the safe end of every gate Mesa keys off them,
     * since a refusal is fatal to Mesa. */
    struct drm_amdgpu_info_firmware fw;

    memset(&fw, 0, sizeof(fw));
    fw.ver = 0xdeadbeef;
    check(query_info(AMDGPU_INFO_FW_VERSION, &fw, sizeof(fw), ask_me) == 0,
          "the firmware query answers rather than failing the device");
    check(fw.ver == 0, "it answers zero, which claims nothing");
    check(fw.feature == 0, "and no feature bits");
}

static void test_gb_addr_config_is_the_derived_layout(void) {
    /* The two fields the derivation in drm_device.c pins, not the literal, so any value
     * describing the same layout passes. */
    uint32_t gb = oops_winsys_gb_addr_config();

    check(gb != 0,
          "the register has a value to answer with, so the read does not refuse");
    check((gb & 0x7u) == 0x4u, "NUM_PIPES is the derived 16 pipes");
    check(((gb >> 3) & 0x7u) == 0u,
          "PIPE_INTERLEAVE_SIZE is the 256 B addrlib has a pattern for");
}

static void test_measured_fields_match_their_records(void) {
    /* The measured device fields hold the values their records give. */
    struct drm_amdgpu_info_device dev;
    oops_winsys_device_info(&dev);

    /* oops-sdk's oracle record, from the end-of-pipe GPU clock: 100 MHz, reported in
     * kHz. */
    check(dev.gpu_counter_freq == 100000,
          "GPU counter frequency is the measured 100 MHz");
    /* obSCEne 135-sysctl on firmware 12.40: hw.pagesize = 0x4000. */
    check(dev.gart_page_size == 0x4000, "page size is the measured 16 KiB");
    check(dev.virtual_address_alignment == 0x4000,
          "address alignment matches the page size");
}

static void test_memory_is_the_kernels_answer(void) {
    /* platform_double.c answers 4 GiB, unlike the console's 12 GiB, so a constant in
     * the winsys shows as a mismatch; live buffers count as used. */
    struct drm_amdgpu_memory_info mem;
    union drm_amdgpu_gem_create c;

    check(oops_winsys_memory_info(&mem) == 0,
          "the memory description answers when the kernel can be asked");
    check(mem.vram.total_heap_size == (uint64_t)1 << 32,
          "its size is the kernel's, not a constant");
    check(mem.cpu_accessible_vram.total_heap_size == mem.vram.total_heap_size,
          "all of it is CPU-visible, because any buffer can be mapped for the CPU");
    check(mem.gtt.total_heap_size == mem.vram.total_heap_size,
          "and GTT is the same one pool");
    check(mem.vram.heap_usage == 0,
          "with nothing allocated, nothing is counted as used");

    memset(&c, 0, sizeof(c));
    c.in.bo_size = 0x8000;
    c.in.domains = AMDGPU_GEM_DOMAIN_GTT;
    check(oops_winsys_gem_create(&c) == 0, "a buffer can be created to be counted");
    oops_winsys_memory_info(&mem);
    check(mem.gtt.heap_usage == 0x8000, "a live buffer is counted as used");
    check(mem.gtt.usable_heap_size == mem.gtt.total_heap_size - 0x8000,
          "and is taken off what is left");
    oops_winsys_gem_close(c.out.handle);
    oops_winsys_memory_info(&mem);
    check(mem.gtt.heap_usage == 0, "and stops being counted once it is closed");
}

static void test_unimplemented_commands_refuse(void) {
    /* Open and unimplemented commands refuse rather than succeed with nothing done. */
    int fd = oops_winsys_open();
    char arg[512];

    /* On the host nothing is bound, so open refuses. That is itself the contract. */
    if (fd < 0) {
        check(fd == -ENODEV, "open refuses with ENODEV when the driver is not bound");
        /* The dispatcher is still reachable with a bad descriptor and must say so. */
        check(oops_winsys_ioctl(fd, DRM_IOCTL_AMDGPU_CS, arg) == -EBADF,
              "a command on a descriptor that was never opened is refused");
        return;
    }

    memset(arg, 0, sizeof(arg));
    check(oops_winsys_ioctl(fd, DRM_IOCTL_AMDGPU_GEM_CREATE, arg) == -ENOSYS,
          "GEM_CREATE refuses rather than succeeding with nothing done");
    check(oops_winsys_ioctl(fd, DRM_IOCTL_AMDGPU_CS, arg) == -ENOSYS,
          "CS refuses rather than succeeding with nothing done");
    check(oops_winsys_ioctl(fd, 0xdeadbeef, arg) == -EINVAL,
          "a command this shim does not know is refused, not ignored");
    check(oops_winsys_close(fd) == 0, "close accepts the descriptor open returned");
    check(oops_winsys_close(fd + 1) == -EBADF,
          "close refuses a descriptor it never gave out");
}

static void test_version_identifies_as_amdgpu(void) {
    /* The driver names itself "amdgpu", which libdrm requires, in both call forms. */
    struct drm_version v;
    char name[16], date[16], desc[80];

    /* The length-probe form libdrm calls first: null buffers, lengths returned. */
    memset(&v, 0, sizeof(v));
    check(oops_winsys_version(&v) == 0, "the version query answers a length probe");
    check(v.name_len == 6, "it reports the length of the driver name");
    check(v.version_major == 3, "it reports an interface version libdrm accepts");

    memset(&v, 0, sizeof(v));
    memset(name, 0, sizeof(name));
    v.name = name;
    v.name_len = sizeof(name);
    v.date = date;
    v.date_len = sizeof(date);
    v.desc = desc;
    v.desc_len = sizeof(desc);
    check(oops_winsys_version(&v) == 0, "the version query answers a filled request");
    check(strcmp(name, "amdgpu") == 0,
          "the driver names itself amdgpu, which libdrm requires");
}

static void test_a_buffer_through_its_whole_life(void) {
    /* A buffer's create, query, map, VA, idle, list and close all work, and all refuse
     * once it is closed; platform calls are platform_double.c's. */
    union drm_amdgpu_gem_create c;
    union drm_amdgpu_gem_mmap m;
    union drm_amdgpu_gem_wait_idle w;
    union drm_amdgpu_bo_list l;
    struct drm_amdgpu_gem_va va;
    struct drm_amdgpu_gem_op op;
    struct drm_amdgpu_gem_create_in created;
    struct drm_amdgpu_bo_list_entry entry;
    uint32_t handle;
    void *cpu;

    memset(&c, 0, sizeof(c));
    c.in.bo_size = 5000; /* deliberately not a page multiple */
    c.in.alignment = 256;
    c.in.domains = AMDGPU_GEM_DOMAIN_VRAM;
    check(oops_winsys_gem_create(&c) == 0, "a buffer can be created");
    handle = c.out.handle;
    check(handle != 0, "and its handle is not the none handle");

    memset(&op, 0, sizeof(op));
    op.handle = handle;
    op.op = AMDGPU_GEM_OP_GET_GEM_CREATE_INFO;
    op.value = (uint64_t)(uintptr_t)&created;
    check(oops_winsys_gem_op(&op) == 0, "how it was created can be asked back");
    check(created.bo_size == 0x4000,
          "the size it reports is the page-rounded one it really has");
    check(created.alignment >= 0x4000,
          "the alignment is at least a page, whatever was asked");
    check(created.domains == AMDGPU_GEM_DOMAIN_VRAM,
          "the domain it was created with is kept");

    memset(&op, 0, sizeof(op));
    op.handle = handle;
    op.op = AMDGPU_GEM_OP_SET_PLACEMENT;
    check(oops_winsys_gem_op(&op) == -ENOSYS,
          "moving a buffer between domains is refused, not silently ignored");

    memset(&m, 0, sizeof(m));
    m.in.handle = handle;
    check(oops_winsys_gem_mmap(&m) == 0, "a mapping token can be asked for");
    cpu = oops_winsys_mmap(0, 0x4000, m.out.addr_ptr);
    check(cpu != NULL, "and the token turns into a usable pointer");

    memset(&va, 0, sizeof(va));
    va.handle = handle;
    va.operation = AMDGPU_VA_OP_MAP;
    va.va_address = 0x0000000200000000ull;
    va.map_size = 0x4000;
    va.flags = AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE;
    check(oops_winsys_gem_va(&va) == 0,
          "it can be mapped at an address chosen by the caller");

    va.offset_in_bo = 64;
    check(oops_winsys_gem_va(&va) == -ENOSYS,
          "a partial mapping is refused rather than quietly mapping the whole buffer");

    /* libdrm encodes GEM_VA as _IOWR, not the header's DRM_IOW; the dispatcher routes
     * that encoding, shown by undoing the map above through it. */
    {
        unsigned long iowr =
            DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_VA, struct drm_amdgpu_gem_va);
        int devfd = dup(
            2); /* a real fd is served past the gate, as the dup-fd test establishes */
        check(
            iowr != (unsigned long)DRM_IOCTL_AMDGPU_GEM_VA,
            "libdrm's _IOWR wire encoding of GEM_VA is not the header's DRM_IOW macro");
        va.offset_in_bo = 0;
        va.operation = AMDGPU_VA_OP_UNMAP;
        check(devfd >= 0 && oops_winsys_ioctl(devfd, iowr, &va) == 0,
              "the _IOWR encoding libdrm actually sends is dispatched to gem_va, not "
              "refused");
        if (devfd >= 0)
            close(devfd);
    }

    memset(&w, 0, sizeof(w));
    w.in.handle = handle;
    check(oops_winsys_gem_wait_idle(&w) == 0,
          "whether the GPU has finished with it can be asked");
    check(w.out.status == 0,
          "and it is idle, because submission waits for its own fence");

    memset(&l, 0, sizeof(l));
    entry.bo_handle = handle;
    entry.bo_priority = 0;
    l.in.operation = AMDGPU_BO_LIST_OP_CREATE;
    l.in.bo_number = 1;
    l.in.bo_info_size = sizeof(entry);
    l.in.bo_info_ptr = (uint64_t)(uintptr_t)&entry;
    check(oops_winsys_bo_list(&l) == 0, "a list naming a live buffer is accepted");

    check(oops_winsys_gem_close(handle) == 0, "the buffer can be closed");

    /* Everything now refuses: a stale handle that still works is what a buffer list
     * exists to catch. */
    memset(&w, 0, sizeof(w));
    w.in.handle = handle;
    check(oops_winsys_gem_wait_idle(&w) == -EINVAL,
          "asking about a closed buffer is refused");
    memset(&op, 0, sizeof(op));
    op.handle = handle;
    op.op = AMDGPU_GEM_OP_GET_GEM_CREATE_INFO;
    check(oops_winsys_gem_op(&op) == -EINVAL,
          "an operation on a closed buffer is refused");
    check(oops_winsys_gem_close(handle) == -EINVAL, "and closing it twice is refused");

    memset(&c, 0, sizeof(c));
    check(oops_winsys_gem_create(&c) == -EINVAL, "a zero-sized buffer is refused");
    check(c.out.handle == 0,
          "and hands back no handle even though the argument is a union");
}

static void test_contexts_track_what_they_can(void) {
    /* Contexts allocate, report hangs as resets, refuse clock pinning and free. */
    union drm_amdgpu_ctx c, d;
    uint32_t id;

    memset(&c, 0, sizeof(c));
    c.in.op = AMDGPU_CTX_OP_ALLOC_CTX;
    check(oops_winsys_ctx(&c) == 0, "a context can be allocated");
    id = c.out.alloc.ctx_id;
    check(id != 0, "and it comes back with a handle that is not the none handle");

    /* The reset answer must mean something. With nothing gone wrong it says so; the
     * hang counter is what submission feeds when a stream fails to retire. */
    memset(&d, 0, sizeof(d));
    d.in.op = AMDGPU_CTX_OP_QUERY_STATE;
    d.in.ctx_id = id;
    check(oops_winsys_ctx(&d) == 0, "its state can be queried");
    check(d.out.state.hangs == 0, "a fresh context reports no failed submissions");
    check(d.out.state.reset_status == AMDGPU_CTX_NO_RESET, "and therefore no reset");

    oops_winsys_ctx_note_hang(id);
    memset(&d, 0, sizeof(d));
    d.in.op = AMDGPU_CTX_OP_QUERY_STATE;
    d.in.ctx_id = id;
    oops_winsys_ctx(&d);
    check(d.out.state.hangs == 1, "a failed submission is counted against the context");
    check(d.out.state.reset_status == AMDGPU_CTX_GUILTY_RESET,
          "and the reset answer changes, so radeonsi can act on it");

    /* Pinning clocks is the system's business, and is refused rather than ignored. */
    memset(&d, 0, sizeof(d));
    d.in.op = AMDGPU_CTX_OP_SET_STABLE_PSTATE;
    d.in.ctx_id = id;
    check(oops_winsys_ctx(&d) == -EPERM,
          "pinning a clock state is refused, not ignored");

    memset(&d, 0, sizeof(d));
    d.in.op = AMDGPU_CTX_OP_FREE_CTX;
    d.in.ctx_id = id;
    check(oops_winsys_ctx(&d) == 0, "a context can be freed");
    memset(&d, 0, sizeof(d));
    d.in.op = AMDGPU_CTX_OP_QUERY_STATE;
    d.in.ctx_id = id;
    check(oops_winsys_ctx(&d) == -EINVAL, "and is gone afterwards");
}

static void test_buffer_lists_check_their_handles(void) {
    /* A buffer list refuses a handle that is not a live buffer. */
    union drm_amdgpu_bo_list l;
    struct drm_amdgpu_bo_list_entry entry;

    /* An empty list is legitimate and costs nothing to accept. */
    memset(&l, 0, sizeof(l));
    l.in.operation = AMDGPU_BO_LIST_OP_CREATE;
    check(oops_winsys_bo_list(&l) == 0, "an empty buffer list is accepted");
    check(l.out.list_handle != 0, "and comes back with a handle");

    /* Handle 7 names no buffer, so the list is refused. */
    memset(&l, 0, sizeof(l));
    entry.bo_handle = 7;
    entry.bo_priority = 0;
    l.in.operation = AMDGPU_BO_LIST_OP_CREATE;
    l.in.bo_number = 1;
    l.in.bo_info_size = sizeof(entry);
    l.in.bo_info_ptr = (uint64_t)(uintptr_t)&entry;
    check(oops_winsys_bo_list(&l) == -EINVAL,
          "a list naming a handle that is not a live buffer is refused");

    memset(&l, 0, sizeof(l));
    l.in.operation = AMDGPU_BO_LIST_OP_DESTROY;
    l.in.list_handle = 999;
    check(oops_winsys_bo_list(&l) == -EINVAL,
          "destroying a list that was never made is refused");
}

static void test_a_duplicated_device_fd_is_served(void) {
    /* The ioctl gate serves an open duplicate of the device fd and refuses the same
     * number once closed. An unknown command keeps the check off the vendor driver. */
    char arg[512];
    int dupfd =
        dup(2); /* a real, open descriptor standing in for the frontend's copy */

    memset(arg, 0, sizeof(arg));
    check(dupfd >= 0, "a descriptor can be duplicated on the host");
    if (dupfd < 0) {
        return;
    }

    check(oops_winsys_ioctl(dupfd, 0xdeadbeef, arg) == -EINVAL,
          "an open duplicate of the device fd is served past the gate, not refused as "
          "a bad fd");

    close(dupfd);
    check(oops_winsys_ioctl(dupfd, 0xdeadbeef, arg) == -EBADF,
          "once closed, that number is refused as a descriptor that was never opened");
}

int main(void) {
    printf("oops-mesa winsys host suite\n");
    test_version_identifies_as_amdgpu();
    test_a_buffer_through_its_whole_life();
    test_contexts_track_what_they_can();
    test_buffer_lists_check_their_handles();
    test_device_is_identifiable_as_gfx1013();
    test_syncobjs_are_handles_this_repository_owns();
    test_a_fence_slot_is_bounds_checked();
    test_the_part_reports_itself_as_one_memory_pool();
    test_the_first_question_libdrm_asks_is_answered();
    test_the_version_mesa_will_accept();
    test_the_graphics_engine_is_the_one_that_answers();
    test_firmware_versions_answer_conservatively();
    test_gb_addr_config_is_the_derived_layout();
    test_measured_fields_match_their_records();
    test_memory_is_the_kernels_answer();
    test_unimplemented_commands_refuse();
    test_a_duplicated_device_fd_is_served();

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
