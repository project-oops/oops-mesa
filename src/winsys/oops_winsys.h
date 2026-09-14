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
