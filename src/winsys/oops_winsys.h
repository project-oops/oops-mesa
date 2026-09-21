/*
 * The winsys shim's own interface: what libdrm reaches, and what this repository owes it.
 *
 * libdrm calls open, ioctl, mmap and close on a device. On this platform there is no device
 * node and no kernel to call, so those four are provided here and dispatch onto the vendor
 * graphics driver oops-sdk binds by published name (D005).
 */
#ifndef OOPS_WINSYS_H
#define OOPS_WINSYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open the synthetic device. Returns a descriptor libdrm will hand back to every call below,
 * or a negative errno. Fails when the platform graphics driver is not bound, rather than
 * returning a descriptor that cannot do anything. */
int oops_winsys_open(void);
int oops_winsys_close(int fd);

/* The command set. A command with nothing behind it returns -ENOSYS and says which one it was;
 * none of them returns success without having done the work. */
int oops_winsys_ioctl(int fd, unsigned long request, void *arg);

/* The six commands on the path to a first frame. Each refuses rather than succeeding when it
 * cannot do the work; none of them returns 0 having done nothing. */
struct drm_version;
union drm_amdgpu_gem_create;
union drm_amdgpu_gem_mmap;
struct drm_amdgpu_gem_va;
union drm_amdgpu_cs;
union drm_amdgpu_wait_cs;
union drm_amdgpu_ctx;
union drm_amdgpu_bo_list;
union drm_amdgpu_gem_wait_idle;
struct drm_amdgpu_gem_op;
int oops_winsys_version(struct drm_version *arg);

/* The two core DRM questions libdrm asks before it asks anything about a GPU. GET_CLIENT is the
 * first statement of `amdgpu_device_initialize` and gates every other answer in this file;
 * GET_CAP decides one feature flag apiece. Both are exported so the host suite can check the
 * answers, which are decided here rather than measured. */
struct drm_client;
struct drm_get_cap;
int oops_winsys_get_client(struct drm_client *arg);
int oops_winsys_get_cap(struct drm_get_cap *arg);

/* AMDGPU_INFO, the query radeonsi asks everything through. Exported rather than reached only via
 * the dispatcher because opening the device needs the platform graphics driver bound, which the
 * build machine cannot do - and the answers here are decided in this repository, so they are
 * exactly what a host suite should be able to check. */
struct drm_amdgpu_info;
int oops_winsys_info(struct drm_amdgpu_info *arg);
int oops_winsys_gem_create(union drm_amdgpu_gem_create *arg);
int oops_winsys_gem_mmap(union drm_amdgpu_gem_mmap *arg);
int oops_winsys_gem_close(uint32_t handle);
int oops_winsys_gem_va(struct drm_amdgpu_gem_va *arg);
int oops_winsys_cs(union drm_amdgpu_cs *arg);
int oops_winsys_ctx(union drm_amdgpu_ctx *arg);
int oops_winsys_bo_list(union drm_amdgpu_bo_list *arg);
int oops_winsys_gem_wait_idle(union drm_amdgpu_gem_wait_idle *arg);
int oops_winsys_gem_op(struct drm_amdgpu_gem_op *arg);
void oops_winsys_ctx_note_hang(uint32_t ctx_id);
bool oops_winsys_bo_is_live(uint32_t handle);
int oops_winsys_wait_cs(union drm_amdgpu_wait_cs *arg);

/* The device description radeonsi reads through AMDGPU_INFO. Returns how many of its field
 * groups are still assumed rather than measured, and logs that number. */
struct drm_amdgpu_info_device;
unsigned oops_winsys_device_info(struct drm_amdgpu_info_device *out);

/* The heap sizes radeonsi reads through AMDGPU_INFO_MEMORY, from the kernel's direct-memory size
 * at the time of asking. Returns 0, or a negative errno when the kernel cannot be asked. */
struct drm_amdgpu_memory_info;
int oops_winsys_memory_info(struct drm_amdgpu_memory_info *out);

/* Bytes held by the buffers this shim has created and not yet closed. */
uint64_t oops_winsys_bo_bytes_live(void);

/* How many bytes one buffer occupies, as asked for at GEM_CREATE and rounded to a page. Zero for
 * a handle that is not live. The platform shim uses it to tell a tiled colour target from a
 * linear one, which nothing in Mesa's image API will report on a surface created without
 * modifiers - see the note on the definition. */
uint64_t oops_winsys_bo_size(uint32_t handle);

/* Where GEM_VA mapped a buffer, or zero if it is not mapped. The GPU reads through it and so does
 * the CPU - one mapping serves both here - which is what lets the display be handed a radeonsi
 * buffer directly (D012 step 3). Not `cpu_ptr`; see the note on the definition. */
uint64_t oops_winsys_bo_gpu_va(uint32_t handle);

/* Drain the CPU's writes to every mapped buffer to memory, so a submission the GPU is about to
 * read sees fresh bytes rather than stale ones (worklog 057). clflush half; the caller adds an
 * sfence for the write-combined buffers. */
void oops_winsys_flush_cpu_writes(void);

/* The CPU address of a range inside a live buffer, mapping it if it is not mapped yet. Returns
 * NULL for a dead handle or a range that does not fit. Submission uses it to land a sequence
 * number where an `AMDGPU_CHUNK_ID_FENCE` chunk asked for it. */
void *oops_winsys_bo_cpu_range(uint32_t handle, uint64_t offset, uint64_t bytes);

/* Synchronisation objects. A syncobj here is a handle in this repository's own table rather than
 * a kernel object, because the fence it wraps is memory this shim allocated (D007). Creating one
 * is what `amdgpu_winsys_create` needs before it asks the device anything, so these gate every
 * other answer the winsys gives. Waiting and signalling are not implemented: nothing submits work
 * yet, and the platform offers no way to block on a fence in any case. */
struct drm_syncobj_create;
struct drm_syncobj_destroy;
int oops_winsys_syncobj_create(struct drm_syncobj_create *arg);
int oops_winsys_syncobj_destroy(struct drm_syncobj_destroy *arg);
bool oops_winsys_syncobj_is_live(uint32_t handle);

/* GB_ADDR_CONFIG (dword 0x263e) as this shim answers it through AMDGPU_INFO_READ_MMR_REG.
 * Derived by inverting addrlib against oops-sdk's tiler, not read from the register, which
 * faults a userspace read; the derivation and what it does and does not pin are written out
 * beside the definition in drm_device.c. Returns 0 when no value is defined, which is not a
 * legal answer and makes the register read refuse. Exported so the host suite can check the
 * fields addrlib reads rather than a literal. */
uint32_t oops_winsys_gb_addr_config(void);

/* Copy a filled structure back to the caller, honouring the size and pointer an AMDGPU_INFO
 * request carries. */
int oops_winsys_copy_out(const void *request, const void *src, unsigned long size);

/* Map a buffer this shim created into the address space, for the CPU to write. */
void *oops_winsys_mmap(int fd, size_t length, uint64_t offset);
void *oops_winsys_mmap_or_failed(int fd, size_t length, uint64_t offset);
int oops_winsys_munmap(void *addr, size_t length);

/* Where the shim says what happened. Routed to oops-sdk's log on the console and to standard
 * error on the host, so a refused command is visible in both places. */
void oops_winsys_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif
