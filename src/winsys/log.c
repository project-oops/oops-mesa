/*
 * Where the winsys says what happened.
 *
 * On the console this goes to the system log oops-sdk writes, so a refused command
 * appears in `pros logs` beside the title's own lines. On the host it goes to standard
 * error, so the shim's unit tests can be read without a console in the room. Nothing
 * here is conditional on a debug build: a command that refused is a fact worth having
 * in both places, every time.
 */
#include <stdarg.h>
#include <stdio.h>

#include "oops_winsys.h"

#ifndef OOPS_HOST_BUILD
#include "oops/system.h" /* oops_log_channel_level */

extern void oops_klog(const char *tag, const char *msg);

/* The runtime's stdout/stderr interception holds a partial line until its newline
 * arrives. Both it and this file end at `oops_klog`, so a Mesa line mid-assembly would
 * otherwise be overtaken by a winsys line and the log would read back out of order.
 * Flushing it first costs an occasional split line and buys an order that can be
 * trusted - see the comment on the function itself for what the alternative cost on
 * 2026-09-17. */
extern void oops_stderr_flush_partial(void);
#endif

void oops_winsys_log(const char *fmt, ...) {
    char line[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

#ifdef OOPS_HOST_BUILD
    fprintf(stderr, "[oops-mesa winsys] %s\n", line);
#else
    oops_stderr_flush_partial();
    /* A klog line is dropped silently past about 128 bytes, which cost an afternoon
     * once (orbistoun worklog 539), so this stays well inside that. */
    oops_klog("OOPS-MESA", line);
#endif
}

/*
 * Lets a submission that never retires be compared against ones that do. Gated at trace
 * because it is tens of lines per submission. Eight dwords a line keeps each under the
 * ~128 bytes klog truncates at, and the offset makes two dumps diffable by column.
 */
void oops_winsys_dump_ib(uint64_t va, uint32_t bytes) {
#ifndef OOPS_HOST_BUILD
    if (oops_log_channel_level("winsys", OOPS_LOG_INFO) < OOPS_LOG_TRACE) {
        return;
    }

    const uint32_t *dw = (const uint32_t *)oops_winsys_cpu_for_va(va, bytes);
    if (dw == NULL) {
        oops_winsys_log("ib dump: 0x%llx is not in any CPU-mapped buffer",
                        (unsigned long long)va);
        return;
    }

    const uint32_t n = bytes / 4u;
    for (uint32_t i = 0; i < n; i += 8u) {
        char line[128];
        int at = snprintf(line, sizeof(line), "ib+%04x", i * 4u);
        for (uint32_t k = 0; k < 8u && i + k < n; k++) {
            at += snprintf(line + at, sizeof(line) - (size_t)at, " %08x", dw[i + k]);
        }
        oops_stderr_flush_partial();
        oops_klog("OOPS-MESA", line);
    }
#else
    (void)va;
    (void)bytes;
#endif
}
