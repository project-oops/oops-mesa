/* Link-time support for the preamble generator: the register-name tables Mesa's debug
 * code reads, instantiated once, and stand-ins for functions referenced only on paths
 * this tool never takes. Each stand-in aborts with its name, so a Mesa change that
 * starts calling one fails loudly instead of returning zero. */
#include <stdio.h>
#include <stdlib.h>
#include "ac_gpu_info.h"
#include "sid.h"
#define SID_TABLE_IMPLEMENTATION
#include "sid_tables.h"

static void unreachable_here(const char *fn) {
    fprintf(stderr,
            "preamble-dump: %s was called; this path is outside the GFX10 preamble\n",
            fn);
    abort();
}

void ac_get_harvested_configs(const struct radeon_info *info, unsigned raster_config,
                              unsigned *cik_raster_config_1_p,
                              unsigned *raster_config_se) {
    (void)info;
    (void)raster_config;
    (void)cik_raster_config_1_p;
    (void)raster_config_se;
    unreachable_here("ac_get_harvested_configs");
}

uint32_t ac_gfx103_get_cu_mask_ps(const struct radeon_info *info) {
    (void)info;
    unreachable_here("ac_gfx103_get_cu_mask_ps");
    return 0;
}

/* Mesa's field-by-field register printer lives in its command-stream parser, which this
 * tool does not build; ac_debug.c references it from a path the name lookup never
 * reaches. */
void ac_dump_reg(FILE *file, enum amd_gfx_level gfx_level, enum radeon_family family,
                 unsigned offset, uint32_t value, uint32_t field_mask) {
    (void)file;
    (void)gfx_level;
    (void)family;
    (void)offset;
    (void)value;
    (void)field_mask;
    unreachable_here("ac_dump_reg");
}
