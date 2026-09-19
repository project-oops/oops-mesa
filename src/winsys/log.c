/*
 * Where the winsys says what happened.
 *
 * On the console this goes to the system log oops-sdk writes, so a refused command appears in
 * `pros logs` beside the title's own lines. On the host it goes to standard error, so the shim's
 * unit tests can be read without a console in the room. Nothing here is conditional on a debug
 * build: a command that refused is a fact worth having in both places, every time.
 */
#include <stdarg.h>
#include <stdio.h>

#include "oops_winsys.h"

#ifndef OOPS_HOST_BUILD
extern void oops_klog(const char *tag, const char *msg);

/* The runtime's stdout/stderr interception holds a partial line until its newline arrives. Both
 * it and this file end at `oops_klog`, so a Mesa line mid-assembly would otherwise be overtaken
 * by a winsys line and the log would read back out of order. Flushing it first costs an
 * occasional split line and buys an order that can be trusted - see the comment on the function
 * itself for what the alternative cost on 2026-09-17. */
extern void oops_stderr_flush_partial(void);
#endif

void oops_winsys_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

#ifdef OOPS_HOST_BUILD
    fprintf(stderr, "[oops-mesa winsys] %s\n", line);
#else
    oops_stderr_flush_partial();
    /* A klog line is dropped silently past about 128 bytes, which cost an afternoon once
     * (orbistoun worklog 539), so this stays well inside that. */
    oops_klog("OOPS-MESA", line);
#endif
}
