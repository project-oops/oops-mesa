/*
 * What this console answers when radeonsi asks what the GPU is.
 *
 * On Linux this comes from the kernel. Here it is assembled, and the point of this file being
 * its own is that every field can say how it is known. Three tiers, and they are not mixed:
 *
 *   measured    a run on this console produced this number, and the record is named.
 *   classifier  it is not a property of the hardware at all. It is the value that makes Mesa's
 *               own chip identification reach the part we have established this is, and the
 *               constant is Mesa's, quoted back to it.
 *   assumed     nothing here has measured it. The reason it is plausible is stated, and
 *               REQ-20260914T1558Z-7d41 on the obSCEne bus is the request that would replace it.
 *
 * A caller gets a log line saying how many fields are still assumed, so a frame rendered on this
 * description is never mistaken for a frame rendered on measurements.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "drm-uapi/amdgpu_drm.h"
#include "oops_winsys.h"

/* Mesa's own constants, from src/amd/addrlib/src/amdgpu_asic_addr.h at the pin. Quoted rather
 * than included so that this file compiles without Mesa's private headers, and so a drift in
 * either is a visible difference rather than a silent one. */
#define OOPS_FAMILY_NV          0x8F  /* Navi. GFX1013 is classified inside it. */
#define OOPS_GFX1013_REV_FIRST  0x82  /* AMDGPU_GFX1013_RANGE is [0x82, 0x86). */

unsigned oops_winsys_device_info(struct drm_amdgpu_info_device *out)
{
    unsigned assumed = 0;

    memset(out, 0, sizeof(*out));

    /* --- classifier ---------------------------------------------------------------------
     * D003's measurement and the preamble work established this is GFX10 with ray tracing,
     * which is what Mesa calls GFX1013 (its ac_gpu_info.c says so in as many words). These two
     * values are what its identify_chip walk needs to arrive there. */
    out->family      = OOPS_FAMILY_NV;
    out->external_rev = OOPS_GFX1013_REV_FIRST;

    /* --- measured -----------------------------------------------------------------------
     * The GPU clock, in kHz. oops-gl's end-of-pipe RELEASE_MEM writes the GPU's own counter
     * every frame and two consecutive frames put it at 100 MHz
     * (oops-sdk docs/hardware/agc-gl-cube-oracle-fw1240, orbistoun worklog 539). */
    out->gpu_counter_freq = 100000;

    /* The page size the kernel reports, 16 KiB. obSCEne read hw.pagesize = 0x4000 on firmware
     * 12.40 in its 135-sysctl section, and again in sweep 20260915-192617. */
    out->gart_page_size = 0x4000;
    out->virtual_address_alignment = 0x4000;

    /* --- assumed ------------------------------------------------------------------------
     * Everything below is REQ-20260914T1558Z-7d41's subject. Each line says why it is the
     * plausible value rather than merely being one.
     *
     * Two of that request's routes have now been tried on hardware and refuse, so these are
     * assumptions the platform will not confirm to a userland title, not assumptions nobody has
     * looked into:
     *   - Route 1, a vendor device-info call: sceAgcGetDeviceInfo is unresolvable in every sweep
     *     (166-agc/gpu-device-info skips).
     *   - Route 2, GPU sysctls: hw.gpu.*, hw.agc.*, machdep.gpu and machdep.agc all return ENOENT
     *     (sweep 20260915-192617 lines 2295-2310). The kernel answers hw.model = "100-000000189",
     *     hw.ncpu = 16 and machdep.tsc_freq, but exposes no GPU topology.
     * What is left is a public source for this exact part (hw.model may be the lead) or deriving
     * the counts from observed surface behaviour. Until then these carry 7d41's identifier. */

    /* The GPU virtual-address ranges radeonsi allocates from. Every GPU mapping oops-gl has made
     * sits at 0x2_0000_0000, and it is the only address this collection has measured the platform
     * accept (oops-sdk docs/hardware/agc-gl-cube-oracle-fw1240, orbistoun worklog 539). That one
     * fact anchors both ranges near that base; their extents are assumed and are the subject of
     * REQ-20260919T1708Z-5af3.
     *
     * radeonsi asks libdrm for every renderbuffer's VA with AMDGPU_VA_RANGE_HIGH set
     * unconditionally (mesa/src/gallium/winsys/amdgpu/drm/amdgpu_bo.c:690 at the pin), so the high
     * range is where essentially every allocation lands - including the command buffer (IB) built
     * during context creation. It is therefore placed just above the measured-mappable base, as
     * close to it as an ordered layout allows, so those allocations get an address near the one the
     * platform is known to bind: the winsys binds each buffer at exactly the VA libdrm picks
     * (buffers.c), so an unmappable range fails the map rather than merely mislabelling it. The
     * general range - which radeonsi reaches only for its rare non-HIGH allocations - keeps the
     * proven base itself and is shrunk so the two are disjoint and numerically ordered the way real
     * amdgpu reports them (low range below high range).
     *
     * Leaving high_va at 0/0 - as this file did through worklog 026, when nothing had yet created a
     * context - left that manager empty and made the first IB allocation fail outright, which is the
     * wall captured in docs/hardware/context-fails-va-alloc-fw1240.log. */
    out->virtual_address_offset = 0x0000000200000000ull;  /* general: the proven base */
    out->virtual_address_max    = 0x0000000400000000ull;   /* shrunk so high can sit disjoint above */
    out->high_va_offset         = 0x0000000400000000ull;   /* high: just above the base; radeonsi lands here */
    out->high_va_max            = 0x0000002400000000ull;
    assumed++;

    /* The Navi 10 shape the family shares. oops-gl programs CU_EN masks of 0xffff and a
     * SPI_SHADER_PGM_RSRC3 of 0x003fffff and draws correctly with them, which is consistent
     * with every compute unit enabled but does not measure the counts. */
    out->num_shader_engines = 2;
    out->num_shader_arrays_per_engine = 2;
    out->cu_active_number = 0;   /* derived by Mesa from cu_bitmap below */
    out->cu_ao_mask = 0xffff;
    for (unsigned se = 0; se < 2; se++) {
        for (unsigned sa = 0; sa < 2; sa++) {
            out->cu_bitmap[se][sa] = 0xffff;
        }
    }
    assumed++;

    /* Sixteen render backends, all enabled, from the same family shape. The preamble generated
     * for D003's measurement used this and the compositor tolerated the result, which is weak
     * evidence that it is not wrong rather than evidence that it is right. */
    out->num_rb_pipes = 16;
    out->enabled_rb_pipes_mask = 0xffff;
    assumed++;

    /* Graphics context count: eight is the GFX10 hardware figure and nothing here varies it. */
    out->num_hw_gfx_contexts = 8;
    assumed++;

    /* Memory. The console's unified memory is GDDR6 and the bus width is a published figure for
     * the part, not something read from this machine. Clocks are left at zero, which Mesa
     * treats as unknown rather than as zero. */
    out->vram_type = AMDGPU_VRAM_TYPE_GDDR6;
    out->vram_bit_width = 256;
    assumed++;

    /* Page-table fragment size, the usual GFX10 value. */
    out->pte_fragment_size = 0x200000;
    assumed++;

    /* --- decided ------------------------------------------------------------------------
     * `ids_flags` carries four bits Mesa reads, and leaving the field zeroed is a claim about
     * all four rather than an absence of one. D008 is the whole argument; in short:
     *
     *   FUSION                 set.    Mesa reads it as `has_dedicated_vram = !FUSION`, and
     *                                  `oops_winsys_memory_info` below already answers vram,
     *                                  cpu_accessible_vram and gtt as one pool. Zero here would
     *                                  contradict that, and the memory answer is the one with a
     *                                  measurement behind it. `caps->uma` is the visible effect.
     *   PREEMPTION             clear.  On this part it is the only gate on register shadowing,
     *                                  and submission here has no preemption notion at all.
     *   TMZ                    clear.  Nothing in this collection does trusted memory.
     *   CONFORMANT_TRUNC_COORD clear.  Unmeasured, and clear is the side that keeps Mesa's
     *                                  texture-gather workarounds on. Slower beats wrong.
     *
     * This is not counted as assumed. Three of the bits are statements about what this shim
     * does, which it is entitled to make, and the fourth is tied to the memory answer rather
     * than standing on its own - if that answer is wrong both are wrong together (D008). */
    out->ids_flags = AMDGPU_IDS_FLAGS_FUSION;

    /* --- left zero, on purpose ----------------------------------------------------------
     * `ids_flags` above was zero by `memset` rather than by choice, and that turned out to be
     * four undeclared claims (D008). Twenty-one other fields Mesa reads are still zero here, so
     * the same question was asked of each of them: what does Mesa make of zero, and is that
     * acceptable? The audit is worklog 026; the result is that none of them reaches anything
     * that acts on it, on this part, on this path. Recorded so that is a finding rather than a
     * gap, and so it is not re-derived.
     *
     * Inert - Mesa either guards the zero, or only reads the field on a generation this is not:
     *   num_cu_per_sh                only perfcounters, behind MAX2(1, ...)
     *   num_shader_visible_vgprs     guarded: `if (device_info && device_info->...)`
     *   gl1c_cache_size, gl2c_cache_size, tcp_cache_size, mall_size, num_sqc_per_wgp,
     *   sqc_inst_cache_size, sqc_data_cache_size
     *                                GFX11+ branches; on GFX10 Mesa uses its own constants
     *   chip_rev                     read once, behind `gfx_level == GFX12`
     *   pa_sc_tile_steering_override GFX9
     *   vce_harvest_config           video, and no video engine is answered at all
     *   tcc_disabled_mask            subtracted from max_tcc_blocks, which is also zero
     *   enabled_rb_pipes_mask_hi     the high half of a mask whose sixteen bits all fit low
     *   pci_rev                      only the RGP trace header
     *
     * Zero has an effect, and the effect is the right one:
     *   num_tcc_blocks   gives l2_cache_size = 0 and tcc_rb_non_coherent = false. Nothing
     *                    divides by either - checked - and false is what a sixteen-TCC part
     *                    would also produce, because the flag needs a non-power-of-two count.
     *   device_id        gives pci_id = 0, which reaches `caps->device_id`. No PCI identifier
     *                    for this part has been measured, so zero is the honest answer rather
     *                    than a missing one.
     *
     * (high_va_offset / high_va_max were once here, left zero on the reasoning that nothing handed
     * out a high virtual address. Context creation now does - radeonsi requests AMDGPU_VA_RANGE_HIGH
     * for the command buffer - so they carry an assumed range above, not a deliberate zero.)
     *
     * None of these is counted as assumed. An assumed field is one where a value was chosen and
     * could be wrong; these are fields where zero is what this shim means. */

    oops_winsys_log("device description: %u of its groups are assumed, not measured; "
                    "REQ-20260914T1558Z-7d41 is the request that would settle them", assumed);
    return assumed;
}

/* Bound by oops-sdk's memory calls as weak, and declared the same way here: off the console it
 * resolves to nothing and the memory description refuses. */
__attribute__((weak)) size_t sceKernelGetDirectMemorySize(void);

/*
 * How much memory the heaps hold, which radeonsi asks for before it identifies the chip.
 *
 * The size is not a constant in this file. It is the kernel's answer at the moment of asking,
 * and obSCEne's 020-memory/direct-size read that answer as 0x3_0000_0000 (12 GiB) on firmware
 * 12.40 (sweep 20260915-125124). Mesa uses it to size caches and to cap one allocation; it never
 * lays anything out from it, so a figure that is too large costs an allocation that comes back
 * ENOMEM rather than a corrupt surface.
 *
 * What is assumed is which pool the figure describes. buffers.c allocates through oops-sdk, and
 * on the console the SDK takes the main direct-memory call first (020-memory/allocate-main passes
 * there). Whether that pool is the one this size bounds is orbistoun's REQ-20260915T0030Z-5d1c,
 * and the resolution the bus carries for it is not in that sweep's log, so it is still open.
 *
 * All three heaps are the same pool. The platform has one, and buffers.c maps any buffer for the
 * CPU whatever domain it was created in, so every byte of "VRAM" is CPU-visible.
 */
int oops_winsys_memory_info(struct drm_amdgpu_memory_info *out)
{
    memset(out, 0, sizeof(*out));

    if (!sceKernelGetDirectMemorySize) {
        oops_winsys_log("memory description refused: the direct-memory size call is not bound");
        return -ENOSYS;
    }
    uint64_t size = (uint64_t)sceKernelGetDirectMemorySize();
    if (size == 0) {
        oops_winsys_log("memory description refused: the kernel reports no direct memory");
        return -ENODEV;
    }

    /* What this shim has allocated itself. The kernel's figure would also count other
     * allocations in the process, so this is a floor rather than the whole of it. */
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

    oops_winsys_log("memory description: 0x%llx bytes from the kernel; the pool it bounds is "
                    "assumed (REQ-20260915T0030Z-5d1c)", (unsigned long long)size);
    return 0;
}
