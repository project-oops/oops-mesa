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
     * 12.40 in its 135-sysctl section. */
    out->gart_page_size = 0x4000;
    out->virtual_address_alignment = 0x4000;

    /* --- assumed ------------------------------------------------------------------------
     * Everything below is REQ-20260914T1558Z-7d41's subject. Each line says why it is the
     * plausible value rather than merely being one. */

    /* Every GPU mapping oops-gl has made sits at 0x2_0000_0000, which is the only part of the
     * address space this collection has seen the driver hand out. The extent above it is a
     * guess at a range, not a measurement of one. */
    out->virtual_address_offset = 0x0000000200000000ull;
    out->virtual_address_max    = 0x0000040000000000ull;
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

    oops_winsys_log("device description: %u of its groups are assumed, not measured; "
                    "REQ-20260914T1558Z-7d41 is the request that would settle them", assumed);
    return assumed;
}
