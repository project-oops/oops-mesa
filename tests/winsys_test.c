/*
 * The winsys shim's host suite.
 *
 * It runs on the build machine, so it cannot touch the GPU. What it can check is the part that
 * is decided here rather than by the hardware: that the device description carries the values
 * Mesa's own chip identification needs, that the measured fields hold the numbers the records
 * say, and above all that an unimplemented command refuses rather than quietly succeeding.
 *
 * The last one is the point. A winsys that returns success having done nothing produces a black
 * frame and no reason for it (CLAUDE.md, principle 4).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "drm-uapi/amdgpu_drm.h"
#include "oops_winsys.h"

static int failures;
static int checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

/* Mesa's constants, quoted from src/amd/addrlib/src/amdgpu_asic_addr.h at the pin. If either
 * side moves, this test says so rather than the driver misidentifying the chip at run time. */
#define FAMILY_NV           0x8F
#define GFX1013_RANGE_LOW   0x82
#define GFX1013_RANGE_HIGH  0x86

static void test_device_is_identifiable_as_gfx1013(void)
{
    struct drm_amdgpu_info_device dev;
    unsigned assumed = oops_winsys_device_info(&dev);

    check(dev.family == FAMILY_NV, "device family is the one GFX1013 is classified inside");
    check(dev.external_rev >= GFX1013_RANGE_LOW && dev.external_rev < GFX1013_RANGE_HIGH,
          "external revision falls in Mesa's GFX1013 range");
    check(assumed > 0, "the description reports how much of itself is assumed");
}

static void test_measured_fields_match_their_records(void)
{
    struct drm_amdgpu_info_device dev;
    oops_winsys_device_info(&dev);

    /* oops-sdk's oracle record, from the end-of-pipe GPU clock: 100 MHz, reported in kHz. */
    check(dev.gpu_counter_freq == 100000, "GPU counter frequency is the measured 100 MHz");
    /* obSCEne 135-sysctl on firmware 12.40: hw.pagesize = 0x4000. */
    check(dev.gart_page_size == 0x4000, "page size is the measured 16 KiB");
    check(dev.virtual_address_alignment == 0x4000, "address alignment matches the page size");
}

static void test_unimplemented_commands_refuse(void)
{
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
    check(oops_winsys_close(fd + 1) == -EBADF, "close refuses a descriptor it never gave out");
}

static void test_version_identifies_as_amdgpu(void)
{
    /* libdrm compares this against "amdgpu" and refuses the device otherwise, so it is the
     * single string the whole stack depends on. */
    struct drm_version v;
    char name[16], date[16], desc[80];

    /* The length-probe form libdrm calls first: null buffers, lengths returned. */
    memset(&v, 0, sizeof(v));
    check(oops_winsys_version(&v) == 0, "the version query answers a length probe");
    check(v.name_len == 6, "it reports the length of the driver name");
    check(v.version_major == 3, "it reports an interface version libdrm accepts");

    memset(&v, 0, sizeof(v));
    memset(name, 0, sizeof(name));
    v.name = name; v.name_len = sizeof(name);
    v.date = date; v.date_len = sizeof(date);
    v.desc = desc; v.desc_len = sizeof(desc);
    check(oops_winsys_version(&v) == 0, "the version query answers a filled request");
    check(strcmp(name, "amdgpu") == 0, "the driver names itself amdgpu, which libdrm requires");
}

static void test_a_buffer_through_its_whole_life(void)
{
    /*
     * Create, ask about it, map it, give it an address, ask whether the GPU has finished with
     * it, name it in a list, close it, and find every one of those refuses afterwards. The
     * platform calls underneath are the stand-in in platform_double.c; everything being checked
     * here is the winsys's own bookkeeping.
     */
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
    c.in.bo_size = 5000;                 /* deliberately not a page multiple */
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
    check(created.bo_size == 0x4000, "the size it reports is the page-rounded one it really has");
    check(created.alignment >= 0x4000, "the alignment is at least a page, whatever was asked");
    check(created.domains == AMDGPU_GEM_DOMAIN_VRAM, "the domain it was created with is kept");

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
    check(oops_winsys_gem_va(&va) == 0, "it can be mapped at an address chosen by the caller");

    va.offset_in_bo = 64;
    check(oops_winsys_gem_va(&va) == -ENOSYS,
          "a partial mapping is refused rather than quietly mapping the whole buffer");

    memset(&w, 0, sizeof(w));
    w.in.handle = handle;
    check(oops_winsys_gem_wait_idle(&w) == 0, "whether the GPU has finished with it can be asked");
    check(w.out.status == 0, "and it is idle, because submission waits for its own fence");

    memset(&l, 0, sizeof(l));
    entry.bo_handle = handle;
    entry.bo_priority = 0;
    l.in.operation = AMDGPU_BO_LIST_OP_CREATE;
    l.in.bo_number = 1;
    l.in.bo_info_size = sizeof(entry);
    l.in.bo_info_ptr = (uint64_t)(uintptr_t)&entry;
    check(oops_winsys_bo_list(&l) == 0, "a list naming a live buffer is accepted");

    check(oops_winsys_gem_close(handle) == 0, "the buffer can be closed");

    /* Everything must now refuse. A stale handle that still works is the bug that a buffer list
     * exists to catch, and it was a real one here (worklog 007). */
    memset(&w, 0, sizeof(w));
    w.in.handle = handle;
    check(oops_winsys_gem_wait_idle(&w) == -EINVAL, "asking about a closed buffer is refused");
    memset(&op, 0, sizeof(op));
    op.handle = handle;
    op.op = AMDGPU_GEM_OP_GET_GEM_CREATE_INFO;
    check(oops_winsys_gem_op(&op) == -EINVAL, "an operation on a closed buffer is refused");
    check(oops_winsys_gem_close(handle) == -EINVAL, "and closing it twice is refused");

    memset(&c, 0, sizeof(c));
    check(oops_winsys_gem_create(&c) == -EINVAL, "a zero-sized buffer is refused");
    check(c.out.handle == 0, "and hands back no handle even though the argument is a union");
}

static void test_contexts_track_what_they_can(void)
{
    union drm_amdgpu_ctx c, d;
    uint32_t id;

    memset(&c, 0, sizeof(c));
    c.in.op = AMDGPU_CTX_OP_ALLOC_CTX;
    check(oops_winsys_ctx(&c) == 0, "a context can be allocated");
    id = c.out.alloc.ctx_id;
    check(id != 0, "and it comes back with a handle that is not the none handle");

    /* The reset answer must mean something. With nothing gone wrong it says so; the hang
     * counter is what submission feeds when a stream fails to retire. */
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

    /* Pinning clocks is the system.s business, and is refused rather than ignored. */
    memset(&d, 0, sizeof(d));
    d.in.op = AMDGPU_CTX_OP_SET_STABLE_PSTATE;
    d.in.ctx_id = id;
    check(oops_winsys_ctx(&d) == -EPERM, "pinning a clock state is refused, not ignored");

    memset(&d, 0, sizeof(d));
    d.in.op = AMDGPU_CTX_OP_FREE_CTX;
    d.in.ctx_id = id;
    check(oops_winsys_ctx(&d) == 0, "a context can be freed");
    memset(&d, 0, sizeof(d));
    d.in.op = AMDGPU_CTX_OP_QUERY_STATE;
    d.in.ctx_id = id;
    check(oops_winsys_ctx(&d) == -EINVAL, "and is gone afterwards");
}

static void test_buffer_lists_check_their_handles(void)
{
    union drm_amdgpu_bo_list l;
    struct drm_amdgpu_bo_list_entry entry;

    /* An empty list is legitimate and costs nothing to accept. */
    memset(&l, 0, sizeof(l));
    l.in.operation = AMDGPU_BO_LIST_OP_CREATE;
    check(oops_winsys_bo_list(&l) == 0, "an empty buffer list is accepted");
    check(l.out.list_handle != 0, "and comes back with a handle");

    /* The point of the list here is the check. On the host no buffer can exist, so any handle
     * named in a list is stale, and the refusal is what this asserts. */
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
    check(oops_winsys_bo_list(&l) == -EINVAL, "destroying a list that was never made is refused");
}

int main(void)
{
    printf("oops-mesa winsys host suite\n");
    test_version_identifies_as_amdgpu();
    test_a_buffer_through_its_whole_life();
    test_contexts_track_what_they_can();
    test_buffer_lists_check_their_handles();
    test_device_is_identifiable_as_gfx1013();
    test_measured_fields_match_their_records();
    test_unimplemented_commands_refuse();

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
