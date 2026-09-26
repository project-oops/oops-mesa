/*
 * Mesa writes its failures to file descriptors 1 and 2, and in a title nothing reads
 * either.
 *
 * # The measurement
 *
 * `REQ-20260917T0233Z-5c9d`, resolved 2026-09-17, sweeps `20260917-043235` (payload)
 * and `20260917-095820` (eboot). On the eboot leg - which is what a title is -
 * `write(1)`, `write(2)` and `dup2(1, 2)` followed by `write(2)` all return the full
 * byte count with `errno` zero, and
 * **none of their markers appears in the captured log**. `sys_call(SYS_klog, 7, ...)`
 * in the same check surfaces reliably, which is the control proving the capture was
 * working.
 *
 * On the payload leg all three surface. So this is a property of title space, not of
 * the write. The descriptors are open and accept bytes into nothing: no error to
 * notice, no output to read.
 *
 * # Why it matters
 *
 * Mesa reports its fatal errors with `fprintf(stderr, ...)` rather than through its own
 * logger, and its logger defaults to stderr as well (`src/util/log.c:143`). Without
 * this file, a title that fails to create a screen produces oops-mesa's own klog lines
 * and nothing else - and those say what *this shim* refused, never what Mesa concluded
 * from an answer it accepted.
 *
 * # Which functions, and how that was got wrong once
 *
 * The first version of this file intercepted `fprintf` and `vfprintf`, on a count of
 * 281 and 5 calls in Mesa's **sources**. That was the wrong thing to count. clang
 * rewrites `fprintf(f, "literal")` into `fwrite`, `fprintf(f, "%s", s)` into `fputs`, a
 * single-character format into `fputc`, and `printf("...\n")` into `puts` - so the
 * sources are not what the archives call. A host test caught it by having its own
 * `fprintf` bypassed.
 *
 * The set below is what the built archives actually reference, from `nm -u` on the five
 * that matter:
 *
 *     libamd_common.a     21 fprintf  16 fwrite   6 fputc  2 fputs  2 puts  2 vfprintf
 *     libmesa_util.a      10 fprintf   8 fwrite   5 fputc  2 fputs  6 fflush
 *     libradeonsi.a        8 fprintf   8 fwrite   6 fputc  1 fputs  4 puts  2 vfprintf
 *                          3 fflush    1 perror
 *     libdrm_amdgpu.a      1 fprintf
 *     libamdgpuwinsys.a    1 vfprintf
 *
 * `puts` is included because it writes to **stdout**, which the measurement found is
 * equally dead here - a diagnostic is no less lost for having gone to the other
 * descriptor.
 *
 * # Why there is a line buffer
 *
 * `fputc` is why. A message emitted a character at a time would otherwise become one
 * klog line per character. So writes to a captured stream accumulate and are emitted at
 * each newline, or when the buffer fills, or on `fflush`.
 *
 * # The line length, which is a real constraint and not a rounding
 *
 * A klog line is dropped silently past about 128 bytes (orbistoun worklog 539, and the
 * same note sits in `src/winsys/log.c`). Mesa's messages routinely exceed that, so a
 * long line is split across several klog writes rather than truncated. A dropped tail
 * would be the most useful half of a diagnostic going missing, which is the failure
 * this file exists to fix.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> /* malloc/free, for a formatted line longer than the stack buffer */
#include <string.h>
#include <unistd.h> /* for `write`, at the bottom of this file */

/* oops-sdk's log sink. It writes SYS_klog, which is the one route the measurement above
 * found to surface from a title. Checked not to call back into any function this file
 * defines, so the interception cannot recurse. */
extern void oops_klog(const char *tag, const char *msg);

/* Payload per klog line. Well inside the ~128 byte cliff, leaving room for the tag
 * oops_klog prefixes and the newline it appends. */
#define OOPS_KLOG_CHUNK 96

/* One line's worth of accumulation. Longer lines are emitted in chunks as they fill, so
 * this bounds memory rather than message length. */
#define OOPS_LINE_MAX 512

static char s_line[OOPS_LINE_MAX];
static size_t s_line_len;

static int stream_is_captured(FILE *stream) {
    /* Both are dead in title space, per the measurement. Anything else is a real file
     * the caller opened and is passed through untouched. */
    return stream == stderr || stream == stdout;
}

static void klog_chunks(const char *text, size_t len) {
    char line[OOPS_KLOG_CHUNK + 1];
    size_t at = 0;

    while (at < len) {
        size_t n = len - at;
        if (n > OOPS_KLOG_CHUNK) {
            n = OOPS_KLOG_CHUNK;
        }
        memcpy(line, text + at, n);
        line[n] = '\0';
        oops_klog("MESA", line);
        at += n;
    }
}

/* Emit whatever has accumulated, if anything. */
static void line_flush(void) {
    if (s_line_len > 0) {
        klog_chunks(s_line, s_line_len);
        s_line_len = 0;
    }
}

/*
 * Flush a partial line because another subsystem is about to write to the same log.
 *
 * Bytes accumulate here until a newline arrives, so a Mesa line still being assembled
 * sits in this buffer while the winsys writes a line of its own straight to `oops_klog`
 * - and the winsys line lands first. The log then reads back in an order events did not
 * happen in.
 *
 * That is not a cosmetic problem. On the 2026-09-17 mesa-probe run, Mesa's
 * `drmGetDevice2 failed` surfaced *after* winsys lines for calls Mesa had already made,
 * which put the apparent failure point several steps later than the real one and sent a
 * diagnosis down the wrong route twice before the reordering was noticed.
 *
 * Splitting a line is the lesser harm. A long Mesa line is already emitted in chunks,
 * so a fragment is a shape this log's readers recognise; a wrong order is not visible
 * at all.
 */
void oops_stderr_flush_partial(void);
void oops_stderr_flush_partial(void) {
    line_flush();
}

/*
 * Accumulate `len` bytes, emitting at each newline. Carriage returns are dropped: they
 * would otherwise reach the log as stray bytes, since `oops_klog` supplies its own line
 * ending.
 */
static void line_append(const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\n') {
            line_flush();
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (s_line_len == sizeof(s_line)) {
            line_flush();
        }
        s_line[s_line_len++] = c;
    }
}

/*
 * A stream that is not captured is written with the platform's `fwrite`... except that
 * this file *is* `fwrite` now, and there is no second name for the original. So
 * pass-through is done with `fputc`, which is also ours, which would recurse.
 *
 * The way out is that pass-through has exactly one real consumer: nothing in this stack
 * writes to a file. `ac_print_gpu_info(stdout, ...)` and the shader dumps are the only
 * callers that name a stream at all, and both name stdout, which is captured. So an
 * uncaptured stream reaching here is something new, and the honest response is to say
 * so rather than to invent a route for it.
 */
static void passthrough_unsupported(const char *who) {
    static int said;
    if (!said) {
        said = 1;
        oops_klog("MESA",
                  "a write to a stream that is not stdout or stderr was dropped");
        oops_klog("MESA", who);
    }
}

/*
 * Format one call and hand it to the line buffer.
 *
 * **A result longer than the stack buffer is formatted again on the heap rather than
 * cut.** The first version clamped to `sizeof(buf) - 1` and dropped the rest without a
 * word, which is the same silent-loss failure this file exists to prevent - it was just
 * one layer further in.
 *
 * `glinfo` found it on hardware on 2026-09-22. `glGetString(GL_EXTENSIONS)` on a 4.6
 * compatibility context is several kilobytes; 511 characters of it arrived, ending
 * mid-token, and the truncation was visible only because the next line began in the
 * middle of a word. Mesa's own messages are all short, so 512 had been enough for as
 * long as Mesa was the only writer - and a ported program printing a driver's whole
 * capability string is exactly the case nobody had.
 *
 * `line_append` already chunks correctly, so the only limit was this buffer.
 */
static int emit_formatted(FILE *stream, const char *fmt, va_list ap) {
    char buf[OOPS_LINE_MAX];
    va_list retry;
    int n;

    /* `ap` is consumed by the first `vsnprintf`, so the copy is made before it is used.
     */
    va_copy(retry, ap);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);

    if (n <= 0) {
        va_end(retry);
        return n;
    }

    if (!stream_is_captured(stream)) {
        va_end(retry);
        passthrough_unsupported("fprintf");
        return n;
    }

    if ((size_t)n < sizeof(buf)) {
        va_end(retry);
        line_append(buf, (size_t)n);
        return n;
    }

    /* Longer than the stack buffer: format the whole thing once more, at its real size.
     */
    char *big = (char *)malloc((size_t)n + 1u);
    if (big == NULL) {
        /* Out of memory is not a reason to say nothing, but it is a reason to say that
         * what follows is short. The truncated text is still the most useful thing
         * available. */
        va_end(retry);
        line_append(buf, sizeof(buf) - 1u);
        line_flush();
        oops_klog("MESA",
                  "...the line above is truncated; no memory to format the rest");
        return n;
    }

    (void)vsnprintf(big, (size_t)n + 1u, fmt, retry);
    va_end(retry);
    line_append(big, (size_t)n);
    free(big);
    return n;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = emit_formatted(stream, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    return emit_formatted(stream, fmt, ap);
}

/*
 * `printf` and `vprintf`, which this file did without until a ported program ran.
 *
 * Everything above was counted against **Mesa's** archives, and Mesa never calls plain
 * `printf`: it names a stream (`fprintf(stderr, ...)`) or the compiler lowers a
 * no-conversion call to `puts`. So for a year the set above was complete, because Mesa
 * was the only thing writing.
 *
 * A **ported program** is not like that. `mesa-demos`' `glinfo` prints its whole answer
 * with `printf("GL_VERSION: %s\n", ...)` - a `%s` conversion, so clang keeps it as
 * `printf` rather than rewriting it to `puts` - and on 2026-09-22 it ran correctly on
 * hardware and produced
 * **nothing in the log**. The title started, brought up GL, queried the driver,
 * printed, and parked; every line it printed went into the void, because `printf` was
 * the one name not interposed.
 *
 * That is the failure this whole file exists to prevent, arriving through the one door
 * nobody had counted - and it will hit every future port, because printing with
 * `printf` is what a program written for a terminal does.
 */
int printf(const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = emit_formatted(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap) {
    return emit_formatted(stdout, fmt, ap);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t bytes = size * nmemb;

    if (!stream_is_captured(stream)) {
        passthrough_unsupported("fwrite");
        return nmemb;
    }
    if (size != 0 && bytes / size != nmemb) {
        return 0; /* the multiplication overflowed; write nothing rather than a wrong
                     length */
    }
    line_append((const char *)ptr, bytes);
    return nmemb;
}

int fputs(const char *s, FILE *stream) {
    if (!stream_is_captured(stream)) {
        passthrough_unsupported("fputs");
        return 0;
    }
    line_append(s, strlen(s));
    return 0;
}

int fputc(int c, FILE *stream) {
    char ch = (char)c;

    if (!stream_is_captured(stream)) {
        passthrough_unsupported("fputc");
        return c;
    }
    line_append(&ch, 1);
    return c;
}

/*
 * `putc` and `putchar` are deliberately absent. Both are macros in this platform's
 * `stdio.h` - `putc(x, fp)` expands to `__sputc(x, fp)` on the non-threaded path - so
 * defining functions with those names does not compile, and would not be called if it
 * did. Nothing needs them: `nm -u` across `libamd_common.a`, `libmesa_util.a` and
 * `libradeonsi.a` references none of `putc`, `putchar` or `__sputc`. clang's rewrite of
 * a single-character `fprintf` emits `fputc`, which is covered above.
 */

int puts(const char *s) {
    /* stdout, and `puts` appends a newline of its own. */
    line_append(s, strlen(s));
    line_flush();
    return 0;
}

int fflush(FILE *stream) {
    /* A null stream means "every stream" in C, which here is the one line buffer. */
    if (stream == NULL || stream_is_captured(stream)) {
        line_flush();
    }
    return 0;
}

void perror(const char *s) {
    if (s != NULL && s[0] != '\0') {
        line_append(s, strlen(s));
        line_append(": ", 2);
    }
    /* The platform's `strerror` is imported and is not intercepted here. */
    const char *msg = strerror(errno);
    if (msg != NULL) {
        line_append(msg, strlen(msg));
    }
    line_flush();
}

/*
 * `write`, which belongs here rather than with the other absent names.
 *
 * It is on the same list as those: obSCEne swept it on firmware 12.40 and found no
 * export, in neither the census nor a dynamic lookup (REQ-20260917T1640Z-5b28). But it
 * is not a C-library gap to stub - it is the bottom of the stream capture this file
 * *is*, so it belongs beside the `fwrite` and `fputs` above it, where the line buffer
 * they share is in scope.
 *
 * # Why routing it to the log is the right answer and not a convenience
 *
 * The measurement at the top of this file is what decides it. On the eboot leg - which
 * is what a title is - `write(1)` and `write(2)` both **succeed**: they return the full
 * byte count with `errno` zero, and nothing surfaces anywhere (sweep
 * `20260917-095820`). That is the worst available shape for a diagnostic. A caller that
 * checks its return learns nothing is wrong, and the bytes are gone. Sending them
 * through `line_append` puts them where every other stream in this file already goes,
 * so a direct `write` and an `fprintf` to the same descriptor end up in the same log in
 * the same order.
 *
 * Descriptors other than 1 and 2 are not invented for. Nothing in this stack opens a
 * file to write to, so one arriving here is news of the same kind
 * `passthrough_unsupported` reports, and it is reported rather than quietly accepted.
 * The return is the byte count either way, because that is what the platform's own
 * `write` answered for these descriptors - the difference is only that these bytes are
 * now readable.
 */
ssize_t write(int fd, const void *buf, size_t nbyte) {
    if (buf == NULL || nbyte == 0) {
        return 0;
    }
    if (fd != 1 && fd != 2) {
        passthrough_unsupported("write to a descriptor that is not stdout or stderr");
        return (ssize_t)nbyte;
    }
    line_append((const char *)buf, nbyte);
    return (ssize_t)nbyte;
}
