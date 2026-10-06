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

static void winsys_vlog(const char *fmt, va_list ap)
    __attribute__((format(printf, 1, 0)));

void oops_winsys_log(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    winsys_vlog(fmt, ap);
    va_end(ap);
}

void oops_winsys_log_debug(const char *fmt, ...) {
    va_list ap;

#ifndef OOPS_HOST_BUILD
    if (oops_log_channel_level("winsys", OOPS_LOG_INFO) < OOPS_LOG_DEBUG) {
        return;
    }
#endif
    va_start(ap, fmt);
    winsys_vlog(fmt, ap);
    va_end(ap);
}

static void winsys_vlog(const char *fmt, va_list ap) {
    char line[256];

    vsnprintf(line, sizeof(line), fmt, ap);

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

/*
 * Progress marks for a submission that never retires. A hang names a submission but not
 * the packet, and the CP's own position cannot be read from here. What can be read is
 * memory: every packet below writes or polls an address, and after the timeout the
 * value there says whether the CP got that far. Packet fields are PM4's
 * (`mesa/src/amd/common/sid.h`; WRITE_DATA's destinations from Mesa's generated
 * `amd_cp_packets_gfx11.h`, `V_371_TC_L2` 2 and `V_371_MEMORY` 5).
 */
#ifndef OOPS_HOST_BUILD
#define OOPS_MARKS_MAX 48u

struct oops_mark {
    uint64_t va;
    const volatile uint32_t *cpu;
    uint32_t before, ref, mask;
    uint16_t at; /* byte offset in its IB */
    char kind;   /* R release, W write, S filled size, P polled */
    uint8_t ib;
};

static struct oops_mark s_marks[OOPS_MARKS_MAX];
static unsigned s_nmarks, s_nib;

static uint32_t mark_read(const volatile uint32_t *p) {
#if defined(__x86_64__)
    __builtin_ia32_clflush((const void *)p);
#endif
    return *p;
}

static void mark_add(char kind, uint32_t at, uint64_t va, uint32_t ref, uint32_t mask) {
    if (s_nmarks == OOPS_MARKS_MAX) {
        return;
    }
    const volatile uint32_t *p =
        (const volatile uint32_t *)oops_winsys_cpu_for_va(va, 4);
    struct oops_mark *m = &s_marks[s_nmarks++];
    m->va = va;
    m->cpu = p;
    m->before = p ? mark_read(p) : 0;
    m->ref = ref;
    m->mask = mask;
    m->at = (uint16_t)at;
    m->kind = kind;
    m->ib = (uint8_t)s_nib;
}
#endif

void oops_winsys_clear_marks(void) {
#ifndef OOPS_HOST_BUILD
    s_nmarks = s_nib = 0;
#endif
}

void oops_winsys_note_marks(uint64_t va, uint32_t bytes) {
#ifndef OOPS_HOST_BUILD
    if (oops_log_channel_level("winsys", OOPS_LOG_INFO) < OOPS_LOG_TRACE) {
        return;
    }
    const uint32_t *dw = (const uint32_t *)oops_winsys_cpu_for_va(va, bytes);
    const uint32_t n = bytes / 4u;

    for (uint32_t i = 0; dw != NULL && i < n;) {
        const uint32_t h = dw[i];
        if ((h >> 30) != 3u) {
            i++;
            continue;
        }
        const uint32_t op = (h >> 8) & 0xffu, cnt = ((h >> 16) & 0x3fffu) + 1u;
        const uint32_t *b = &dw[i + 1u];
        if (i + 1u + cnt > n) {
            break;
        }
        if (op == 0x49u && cnt >= 5u && ((b[1] >> 29) & 7u) != 0u) { /* RELEASE_MEM */
            mark_add('R', i * 4u, ((uint64_t)b[3] << 32) | b[2], b[4], ~0u);
        } else if (op == 0x37u && cnt >= 3u) { /* WRITE_DATA */
            const uint32_t sel = (b[0] >> 8) & 0xfu;
            if (sel == 2u || sel == 5u) {
                mark_add('W', i * 4u, ((uint64_t)b[2] << 32) | b[1],
                         cnt > 3u ? b[3] : 0u, ~0u);
            }
        } else if (op == 0x34u && cnt >= 3u &&
                   (b[0] & 1u)) { /* STRMOUT_BUFFER_UPDATE */
            mark_add('S', i * 4u, ((uint64_t)b[2] << 32) | b[1], 0u, ~0u);
        } else if (op == 0x3cu && cnt >= 5u &&
                   (b[0] & 0x30u) == 0x10u) { /* WAIT_REG_MEM */
            mark_add('P', i * 4u, ((uint64_t)b[2] << 32) | b[1], b[3], b[4]);
        }
        i += 1u + cnt;
    }
    s_nib++;
#else
    (void)va;
    (void)bytes;
#endif
}

void oops_winsys_report_marks(void) {
#ifndef OOPS_HOST_BUILD
    for (unsigned k = 0; k < s_nmarks; k++) {
        const struct oops_mark *m = &s_marks[k];
        if (m->cpu == NULL) {
            oops_winsys_log("mark %c ib%u+%04x va 0x%llx: not CPU-mapped", m->kind,
                            m->ib, m->at, (unsigned long long)m->va);
            continue;
        }
        oops_winsys_log(
            "mark %c ib%u+%04x va 0x%llx: 0x%08x -> 0x%08x (ref 0x%08x mask 0x%08x)",
            m->kind, m->ib, m->at, (unsigned long long)m->va, m->before,
            mark_read(m->cpu), m->ref, m->mask);
    }
#endif
}
