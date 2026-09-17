/*
 * tiling-compare: does addrlib place a 32bpp surface where oops-sdk's tiler does, across blocks?
 *
 * # The question
 *
 * Worklog 017 derived GB_ADDR_CONFIG by inverting addrlib against `agc_tiler.c` and matched it
 * across "all 16,384 pixels of the block". 16,384 is 128x128, which is exactly one block, and the
 * tiler's basis vectors cover only within-block coordinates. So the agreement was established for
 * block (0, 0) and quietly extrapolated to whole surfaces (worklog 030).
 *
 * That extrapolation matters, because presentation depends on it. `agc_display.c` registers tiled
 * scanout buffers and oops-gl draws through them on hardware, so the tiler's layout is what the
 * display reads. If radeonsi's colour target has the same layout, presentation is a flip; if it
 * agrees inside a block and diverges between blocks, it is a picture that is almost right.
 *
 * # What this compares
 *
 * Not a restatement of either side. It runs the real `agc_tile_surface` from oops-sdk, and the
 * real addrlib from the pinned Mesa, over a surface several blocks wide, and compares byte
 * offsets pixel by pixel.
 *
 * The oops-sdk side is recovered by tiling rather than by reimplementing: fill a linear surface
 * so that pixel (x, y) holds a unique identifier, tile it with the function the display actually
 * uses, and read back which offset each identifier landed at. Whatever `agc_tile_surface` does,
 * including anything this file's author has misunderstood, is what gets compared.
 *
 * # The control, and why there is one
 *
 * "Zero pixels disagree" is worth nothing on its own: a comparison that cannot detect a difference
 * reports agreement too. So the same comparison is run twice - once with the derived
 * GB_ADDR_CONFIG and once with a deliberately wrong one - and the second is expected to *fail*.
 * If the control also agrees, the tool is measuring nothing and says so.
 *
 * The wrong value changes NUM_PIPES from 16 pipes to 8. That is a field worklog 017's derivation
 * pinned, so it is a difference this comparison must be able to see.
 *
 * # What is fixed, and why
 *
 *   chipFamily / chipRevision   0x8F / 0x82, which is what `device_info.c` reports and what
 *                               `ac_addrlib_create` passes through.
 *   gbAddrConfig                0x00000004, the derived value (drm_device.c).
 *   swizzle mode                ADDR_SW_64KB_R_X, the one mode of the four 64KB modes that
 *                               matched in worklog 017. Selection is not re-litigated here; the
 *                               question is addresses, not which mode addrlib prefers.
 *   pipeBankXor                 0. `ac_surface.c`'s `use_tile_swizzle` returns false when
 *                               `get_display_flag` is set, so a scannable surface carries no
 *                               surface-level swizzle. Passing anything else would compare
 *                               addrlib against a surface the display could never scan out.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "addrinterface.h"

extern "C" {
#include "agc/tiler.h"
}

/* AddressLib keeps this in a core header rather than in its public interface, so `ac_surface.c`
 * defines it itself under the same guard. Copied from there rather than reached for, to keep the
 * tool off AddressLib's internal headers. */
#ifndef CIASICIDGFXENGINE_ARCTICISLAND
#define CIASICIDGFXENGINE_ARCTICISLAND 0x0000000D
#endif

/* The identity this collection reports for the part, and the register it derived. */
static const unsigned kChipFamily = 0x8F;   /* FAMILY_NV */
static const unsigned kChipRevision = 0x82; /* first of AMDGPU_GFX1013_RANGE */
static const unsigned kGbAddrConfig = 0x00000004u;

/* NUM_PIPES is bits [2:0]; 4 is sixteen pipes and 3 is eight. The control uses 3. */
static const unsigned kGbAddrConfigWrong = 0x00000003u;

/*
 * Three blocks across and two down. Not four in a square: with a 2x2 grid the block index
 * `by * tiles_x + bx` runs 0,1,2,3 either way round, so a row-major layout and a column-major one
 * would agree and the comparison could not tell them apart. 3x2 distinguishes them, and adds a
 * third column so that the block index is not merely "0 or 1" in either direction.
 */
static const unsigned kWidth = 384;
static const unsigned kHeight = 256;

static void *alloc_sysmem(const ADDR_ALLOCSYSMEM_INPUT *in)
{
    return malloc(in->sizeInBytes);
}

static ADDR_E_RETURNCODE free_sysmem(const ADDR_FREESYSMEM_INPUT *in)
{
    free(in->pVirtAddr);
    return ADDR_OK;
}

struct Disagreement {
    size_t count;
    unsigned x, y;
    uint64_t sdk, addr;
};

/* Compares addrlib under `gb_addr_config` against the offsets the tiler produced. Returns false
 * if addrlib refused to initialise or to answer, which is a different outcome from disagreeing. */
static bool compare_against_tiler(unsigned gb_addr_config,
                                  const std::vector<uint64_t> &sdk_offset,
                                  Disagreement *out)
{
    ADDR_CREATE_INPUT ci = {};
    ADDR_CREATE_OUTPUT co = {};
    ADDR_REGISTER_VALUE rv = {};

    ci.size = sizeof(ci);
    co.size = sizeof(co);
    rv.gbAddrConfig = gb_addr_config;
    ci.chipFamily = kChipFamily;
    ci.chipRevision = kChipRevision;
    ci.chipEngine = CIASICIDGFXENGINE_ARCTICISLAND;
    ci.regValue = rv;
    ci.callbacks.allocSysMem = alloc_sysmem;
    ci.callbacks.freeSysMem = free_sysmem;
    ci.callbacks.debugPrint = 0;

    if (AddrCreate(&ci, &co) != ADDR_OK) {
        fprintf(stderr, "tiling-compare: AddrCreate refused gbAddrConfig 0x%08X\n",
                gb_addr_config);
        return false;
    }

    memset(out, 0, sizeof(*out));

    for (unsigned y = 0; y < kHeight; y++) {
        for (unsigned x = 0; x < kWidth; x++) {
            ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT in = {};
            ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT aout = {};

            in.size = sizeof(in);
            aout.size = sizeof(aout);
            in.x = x;
            in.y = y;
            in.swizzleMode = ADDR_SW_64KB_R_X;
            in.resourceType = ADDR_RSRC_TEX_2D;
            in.bpp = 32;
            in.unalignedWidth = kWidth;
            in.unalignedHeight = kHeight;
            in.numSlices = 1;
            in.numMipLevels = 1;
            in.numSamples = 1;
            in.numFrags = 1;
            in.pipeBankXor = 0;
            in.pitchInElement = kWidth;
            in.flags.color = 1;
            in.flags.display = 1;

            if (Addr2ComputeSurfaceAddrFromCoord(co.hLib, &in, &aout) != ADDR_OK) {
                fprintf(stderr, "tiling-compare: addrlib refused (%u, %u) under 0x%08X\n",
                        x, y, gb_addr_config);
                AddrDestroy(co.hLib);
                return false;
            }

            const size_t i = (size_t)y * kWidth + x;
            if (aout.addr != sdk_offset[i]) {
                if (!out->count) {
                    out->x = x;
                    out->y = y;
                    out->sdk = sdk_offset[i];
                    out->addr = aout.addr;
                }
                out->count++;
            }
        }
    }

    AddrDestroy(co.hLib);
    return true;
}

int main(void)
{
    /*
     * The oops-sdk side. Pixel (x, y) gets the identifier y * kWidth + x, tiled by the function
     * the display uses; scanning the result gives the offset each pixel landed at.
     *
     * Identifiers start at 1 so that zero means "no pixel claimed this dword", which distinguishes
     * a hole in the mapping from a pixel that legitimately landed at offset 0.
     */
    const size_t pixels = (size_t)kWidth * kHeight;
    const size_t tiled_bytes = agc_tile_surface_bytes(kWidth, kHeight);

    std::vector<uint32_t> linear(pixels);
    std::vector<uint32_t> tiled(tiled_bytes / 4, 0);
    for (size_t i = 0; i < pixels; i++) {
        linear[i] = (uint32_t)(i + 1);
    }
    agc_tile_init();
    agc_tile_surface(tiled.data(), linear.data(), kWidth, kHeight);

    std::vector<uint64_t> sdk_offset(pixels, UINT64_MAX);
    for (size_t dw = 0; dw < tiled.size(); dw++) {
        uint32_t id = tiled[dw];
        if (id == 0 || id > pixels) {
            continue;
        }
        sdk_offset[id - 1] = (uint64_t)dw * 4u;
    }
    for (size_t i = 0; i < pixels; i++) {
        if (sdk_offset[i] == UINT64_MAX) {
            fprintf(stderr, "tiling-compare: agc_tile_surface did not place pixel %zu; the "
                            "comparison would be meaningless\n", i);
            return 1;
        }
    }

    printf("tiling-compare: %ux%u, 32bpp, ADDR_SW_64KB_R_X, pipeBankXor 0\n", kWidth, kHeight);
    printf("  family 0x%02X  revision 0x%02X\n", kChipFamily, kChipRevision);
    printf("  tiled surface is %zu bytes, %ux%u blocks of 64 KiB\n",
           tiled_bytes, kWidth / 128, kHeight / 128);
    printf("  %zu pixels compared per configuration\n\n", pixels);

    Disagreement derived = {}, control = {};

    if (!compare_against_tiler(kGbAddrConfig, sdk_offset, &derived)) {
        return 1;
    }
    printf("  derived  GB_ADDR_CONFIG 0x%08X (16 pipes): %zu disagree\n",
           kGbAddrConfig, derived.count);
    if (derived.count) {
        printf("      first at (%u, %u): tiler 0x%llx, addrlib 0x%llx\n",
               derived.x, derived.y,
               (unsigned long long)derived.sdk, (unsigned long long)derived.addr);
    }

    if (!compare_against_tiler(kGbAddrConfigWrong, sdk_offset, &control)) {
        return 1;
    }
    printf("  control  GB_ADDR_CONFIG 0x%08X (8 pipes):  %zu disagree\n",
           kGbAddrConfigWrong, control.count);
    if (control.count) {
        printf("      first at (%u, %u): tiler 0x%llx, addrlib 0x%llx\n",
               control.x, control.y,
               (unsigned long long)control.sdk, (unsigned long long)control.addr);
    }

    printf("\n");
    if (control.count == 0) {
        printf("  VERDICT: INCONCLUSIVE. The control agrees too, so this comparison cannot\n");
        printf("           detect a difference and its agreement means nothing.\n");
    } else if (derived.count == 0) {
        printf("  VERDICT: the layouts agree, on a surface of %ux%u blocks, with a control\n",
               kWidth / 128, kHeight / 128);
        printf("           that disagrees. A radeonsi 64KB_R_X colour target is already in\n");
        printf("           the layout the display scans out.\n");
    } else {
        printf("  VERDICT: the layouts differ. Presentation cannot be a plain flip from a\n");
        printf("           radeonsi 64KB_R_X surface into a display buffer.\n");
    }

    /* The verdict is the output, not the exit code: any of the three is a finding rather than a
     * failure, and `make check` compares this text against the tracked copy either way. */
    return 0;
}
