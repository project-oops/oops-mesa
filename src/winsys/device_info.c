/*
 * The device description radeonsi reads through AMDGPU_INFO_DEV_INFO and
 * AMDGPU_INFO_MEMORY.
 *
 * Each field is one of three kinds. Classifier values are Mesa's own constants, chosen
 * so its chip identification reaches GFX1013. Measured values name the record they come
 * from. Assumed values are ones this platform does not expose to a userland title; each
 * states why it is plausible, and the count of assumed groups is returned and logged so
 * a frame rendered on this description is not mistaken for one rendered on
 * measurements.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "drm-uapi/amdgpu_drm.h"
#include "oops_winsys.h"

/* Mesa's constants from mesa/src/amd/addrlib/src/amdgpu_asic_addr.h, quoted rather than
 * included so this file needs no Mesa private headers; tests/winsys_test.c checks them
 * against the header. */
#define OOPS_FAMILY_NV 0x8F         /* Navi. GFX1013 is classified inside it. */
#define OOPS_GFX1013_REV_FIRST 0x82 /* AMDGPU_GFX1013_RANGE is [0x82, 0x86). */

unsigned oops_winsys_device_info(struct drm_amdgpu_info_device *out) {
    unsigned assumed = 0;

    memset(out, 0, sizeof(*out));

    /* Classifier. The part is GFX10 with ray tracing, which Mesa calls GFX1013
     * (mesa/src/amd/common/ac_gpu_info.c); these two values lead its identify_chip
     * walk there. */
    out->family = OOPS_FAMILY_NV;
    out->external_rev = OOPS_GFX1013_REV_FIRST;

    /* Measured. GPU clock in kHz: oops-gl's end-of-pipe RELEASE_MEM writes the GPU
     * counter each frame, and consecutive frames put it at 100 MHz
     * (oops-sdk docs/hardware/agc-gl-cube-oracle-fw1240). */
    out->gpu_counter_freq = 100000;

    /* Measured. obSCEne's 135-sysctl read hw.pagesize = 0x4000 on firmware 12.40. */
    out->gart_page_size = 0x4000;
    out->virtual_address_alignment = 0x4000;

    /* Assumed from here on. No vendor device-info call resolves and the kernel exposes
     * no GPU sysctls, so nothing on this platform tells a userland title these values.
     */

    /* GPU virtual-address ranges. 0x2_0000_0000 is the one base the platform is
     * measured to accept (oops-sdk docs/hardware/agc-gl-cube-oracle-fw1240); the
     * extents are assumed. radeonsi requests AMDGPU_VA_RANGE_HIGH for nearly every
     * allocation (mesa/src/gallium/winsys/amdgpu/drm/amdgpu_bo.c:690), including the
     * first IB, so the high range sits just above that base and an empty one fails
     * context creation (docs/hardware/context-fails-va-alloc-fw1240.log). The general
     * range keeps the base and ends where high begins, low below high as amdgpu reports
     * them. */
    out->virtual_address_offset = 0x0000000200000000ull; /* general: the proven base */
    out->virtual_address_max =
        0x0000000400000000ull; /* ends where the high range begins */
    out->high_va_offset =
        0x0000000400000000ull; /* high: just above the base; radeonsi lands here */
    out->high_va_max = 0x0000002400000000ull;
    assumed++;

    /* The Navi 10 shape of the family. oops-gl draws correctly with CU_EN masks of
     * 0xffff and SPI_SHADER_PGM_RSRC3 of 0x003fffff, which is consistent with every
     * compute unit enabled but does not measure the counts. */
    out->num_shader_engines = 2;
    out->num_shader_arrays_per_engine = 2;
    out->cu_active_number = 0; /* derived by Mesa from cu_bitmap below */
    out->cu_ao_mask = 0xffff;
    for (unsigned se = 0; se < 2; se++) {
        for (unsigned sa = 0; sa < 2; sa++) {
            out->cu_bitmap[se][sa] = 0xffff;
        }
    }
    assumed++;

    /* Sixteen render backends, all enabled, from the same family shape. */
    out->num_rb_pipes = 16;
    out->enabled_rb_pipes_mask = 0xffff;
    assumed++;

    /* Eight graphics contexts, the GFX10 hardware figure. */
    out->num_hw_gfx_contexts = 8;
    assumed++;

    /* GDDR6 with the part's published bus width. Clocks stay zero, which Mesa reads as
     * unknown. */
    out->vram_type = AMDGPU_VRAM_TYPE_GDDR6;
    out->vram_bit_width = 256;
    assumed++;

    /* Page-table fragment size, the usual GFX10 value. */
    out->pte_fragment_size = 0x200000;
    assumed++;

    /* Four bits Mesa reads, each a statement about this winsys rather than an
     * assumption. FUSION set: the memory answer below reports one CPU-visible pool as
     * VRAM, GTT and visible VRAM. PREEMPTION clear: submission has no preemption, so
     * register shadowing stays off. TMZ clear: no protected memory.
     * CONFORMANT_TRUNC_COORD clear, so Mesa keeps its workaround lowering: an unneeded
     * workaround costs instructions, a missing one costs pixels. */
    out->ids_flags = AMDGPU_IDS_FLAGS_FUSION;

    /* The other fields Mesa reads stay zero on purpose; on GFX10 Mesa either guards the
     * zero or reads them only on other generations:
     *   num_cu_per_sh (perfcounters, behind MAX2(1, ...)), num_shader_visible_vgprs
     *   (guarded), gl1c/gl2c/tcp cache sizes, mall_size, num_sqc_per_wgp and the sqc
     *   cache sizes (GFX11+), chip_rev (GFX12), pa_sc_tile_steering_override (GFX9),
     *   vce_harvest_config (no video engine), tcc_disabled_mask,
     * enabled_rb_pipes_mask_hi, pci_rev (RGP trace header only). num_tcc_blocks zero
     * gives l2_cache_size 0 and tcc_rb_non_coherent false, the value a sixteen-TCC part
     * also gives, and nothing divides by either. device_id zero gives caps->device_id
     * 0, since no PCI identifier is measured for this part. */

    oops_winsys_log(
        "device description: %u groups assumed; no userland surface exposes them",
        assumed);
    return assumed;
}

/* Declared weak as oops-sdk binds it: off the console it resolves to nothing and the
 * memory description refuses. */
__attribute__((weak)) size_t sceKernelGetDirectMemorySize(void);

/*
 * How much memory the heaps hold, asked by radeonsi before it identifies the chip.
 *
 * The size is the kernel's answer at the time of asking (0x3_0000_0000, 12 GiB, on
 * firmware 12.40 per obSCEne 020-memory/direct-size). Mesa sizes caches and caps one
 * allocation from it and lays nothing out from it. obSCEne's
 * 020-memory/direct-pools-sequence shows sceKernelAllocateDirectMemory and
 * sceKernelAllocateMainDirectMemory allocating contiguously from one pool whose size
 * query is a constant capacity, so this figure bounds what buffers.c allocates. All
 * three heaps are that one pool, and buffers.c maps every buffer for the CPU, so all of
 * it is CPU-visible.
 */
int oops_winsys_memory_info(struct drm_amdgpu_memory_info *out) {
    memset(out, 0, sizeof(*out));

    if (!sceKernelGetDirectMemorySize) {
        oops_winsys_log(
            "memory description refused: the direct-memory size call is not bound");
        return -ENOSYS;
    }
    uint64_t size = (uint64_t)sceKernelGetDirectMemorySize();
    if (size == 0) {
        oops_winsys_log(
            "memory description refused: the kernel reports no direct memory");
        return -ENODEV;
    }

    /* Only this shim's own allocations: a floor on what the process uses. */
    uint64_t used = oops_winsys_bo_bytes_live();

    struct drm_amdgpu_heap_info heap = {
        .total_heap_size = size,
        .usable_heap_size = size - (used < size ? used : size),
        .heap_usage = used,
        .max_allocation = size,
    };
    out->vram = heap;
    out->cpu_accessible_vram = heap;
    out->gtt = heap;

    oops_winsys_log(
        "memory description: 0x%llx bytes from the kernel, bounding the one pool "
        "both direct-memory calls allocate from",
        (unsigned long long)size);
    return 0;
}
