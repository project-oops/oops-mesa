/*
 * tiling-compare: does addrlib place a 32bpp surface where oops-sdk's tiler does,
 * across several 64 KiB blocks? The display scans out the tiler's layout, so a match
 * means a radeonsi colour target presents as a flip.
 *
 * Both sides run real code: addrlib from the pinned Mesa, and `agc_tile_surface`
 * recovered by tiling a surface whose pixels hold unique identifiers and reading back
 * where each landed. A control run with a wrong GB_ADDR_CONFIG must disagree, or the
 * comparison cannot detect a difference and says so.
 *
 * pipeBankXor is 0: `use_tile_swizzle` in mesa/src/amd/common/ac_surface.c returns
 * false for a display surface, so a scannable surface carries no swizzle.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "addrinterface.h"

extern "C" {
#include "agc/tiler.h"
}

/* Defined as `ac_surface.c` defines it, to keep the tool off AddressLib's internal
 * headers. */
#ifndef CIASICIDGFXENGINE_ARCTICISLAND
#define CIASICIDGFXENGINE_ARCTICISLAND 0x0000000D
#endif

/* The identity `src/winsys/device_info.c` reports, and the GB_ADDR_CONFIG
 * `src/winsys/drm_device.c` reports. */
static const unsigned kChipFamily = 0x8F;   /* FAMILY_NV */
static const unsigned kChipRevision = 0x82; /* first of AMDGPU_GFX1013_RANGE */
static const unsigned kGbAddrConfig = 0x00000004u;

/* NUM_PIPES is bits [2:0]; 4 is sixteen pipes and 3 is eight. The control uses 3. */
static const unsigned kGbAddrConfigWrong = 0x00000003u;

/* Three blocks across and two down: a 2x2 grid cannot tell row-major block order from
 * column-major. */
static const unsigned kWidth = 384;
static const unsigned kHeight = 256;

static void *alloc_sysmem(const ADDR_ALLOCSYSMEM_INPUT *in) {
    return malloc(in->sizeInBytes);
}

static ADDR_E_RETURNCODE free_sysmem(const ADDR_FREESYSMEM_INPUT *in) {
    free(in->pVirtAddr);
    return ADDR_OK;
}

struct Disagreement {
    size_t count;
    unsigned x, y;
    uint64_t sdk, addr;
};

/* Compares addrlib under `gb_addr_config` against the offsets the tiler produced.
 * Returns false if addrlib refused to initialise or to answer, which is a different
 * outcome from disagreeing. */
static bool compare_against_tiler(unsigned gb_addr_config,
                                  const std::vector<uint64_t> &sdk_offset,
                                  Disagreement *out) {
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
                fprintf(stderr,
                        "tiling-compare: addrlib refused (%u, %u) under 0x%08X\n", x, y,
                        gb_addr_config);
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

int main(void) {
    /* The oops-sdk side: pixel (x, y) holds y * kWidth + x + 1, tiled by the display's
     * function. Identifiers start at 1 so zero marks a dword no pixel claimed. */
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
            fprintf(stderr,
                    "tiling-compare: agc_tile_surface did not place pixel %zu; the "
                    "comparison would be meaningless\n",
                    i);
            return 1;
        }
    }

    printf("tiling-compare: %ux%u, 32bpp, ADDR_SW_64KB_R_X, pipeBankXor 0\n", kWidth,
           kHeight);
    printf("  family 0x%02X  revision 0x%02X\n", kChipFamily, kChipRevision);
    printf("  tiled surface is %zu bytes, %ux%u blocks of 64 KiB\n", tiled_bytes,
           kWidth / 128, kHeight / 128);
    printf("  %zu pixels compared per configuration\n\n", pixels);

    Disagreement derived = {}, control = {};

    if (!compare_against_tiler(kGbAddrConfig, sdk_offset, &derived)) {
        return 1;
    }
    printf("  derived  GB_ADDR_CONFIG 0x%08X (16 pipes): %zu disagree\n", kGbAddrConfig,
           derived.count);
    if (derived.count) {
        printf("      first at (%u, %u): tiler 0x%llx, addrlib 0x%llx\n", derived.x,
               derived.y, (unsigned long long)derived.sdk,
               (unsigned long long)derived.addr);
    }

    if (!compare_against_tiler(kGbAddrConfigWrong, sdk_offset, &control)) {
        return 1;
    }
    printf("  control  GB_ADDR_CONFIG 0x%08X (8 pipes):  %zu disagree\n",
           kGbAddrConfigWrong, control.count);
    if (control.count) {
        printf("      first at (%u, %u): tiler 0x%llx, addrlib 0x%llx\n", control.x,
               control.y, (unsigned long long)control.sdk,
               (unsigned long long)control.addr);
    }

    printf("\n");
    if (control.count == 0) {
        printf("  VERDICT: INCONCLUSIVE. The control agrees too, so this comparison "
               "cannot\n");
        printf("           detect a difference and its agreement means nothing.\n");
    } else if (derived.count == 0) {
        printf("  VERDICT: the layouts agree, on a surface of %ux%u blocks, with a "
               "control\n",
               kWidth / 128, kHeight / 128);
        printf("           that disagrees. A radeonsi 64KB_R_X colour target is "
               "already in\n");
        printf("           the layout the display scans out.\n");
    } else {
        printf("  VERDICT: the layouts differ. Presentation cannot be a plain flip "
               "from a\n");
        printf("           radeonsi 64KB_R_X surface into a display buffer.\n");
    }

    /* The verdict is the output, not the exit code; `make check` compares the text. */
    return 0;
}
