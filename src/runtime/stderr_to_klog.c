/*
 * Routes stdout and stderr to the system log. In a title, writes to descriptors 1 and
 * 2 succeed and go nowhere, and Mesa reports its failures on stderr
 * (`mesa/src/util/log.c:143`).
 *
 * The interposed names are the ones the built archives reference (`nm -u`), not the
 * ones in Mesa's sources: clang lowers `fprintf` calls to `fwrite`, `fputs`, `fputc` or
 * `puts`. `printf` and `vprintf` are for ported programs. Output accumulates per line
 * so character-at-a-time writes make one log line, and each line goes out in chunks
 * because the log drops a line past about 128 bytes.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> /* malloc/free, for a formatted line longer than the stack buffer */
#include <string.h>
#include <unistd.h> /* for `write`, at the bottom of this file */

/* oops-sdk's log sink (SYS_klog). It calls nothing this file defines, so the
 * interception cannot recurse. */
extern void oops_klog(const char *tag, const char *msg);

/* Payload per klog line, leaving room for the tag and newline oops_klog adds. */
#define OOPS_KLOG_CHUNK 96

/* Accumulation per line. Longer lines are emitted in chunks as they fill, so this
 * bounds memory rather than message length. */
#define OOPS_LINE_MAX 512

static char s_line[OOPS_LINE_MAX];
static size_t s_line_len;

static int stream_is_captured(FILE *stream) {
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

static void line_flush(void) {
    if (s_line_len > 0) {
        klog_chunks(s_line, s_line_len);
        s_line_len = 0;
    }
}

/*
 * Emits a partial line before another subsystem writes to the same log, so the log
 * keeps the order events happened in. A split line is visible; a reordered one is not.
 */
void oops_stderr_flush_partial(void);
void oops_stderr_flush_partial(void) {
    line_flush();
}

/*
 * Accumulates `len` bytes, emitting at each newline. Carriage returns are dropped
 * because `oops_klog` supplies its own line ending.
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
 * A write to any other stream is dropped and reported once. This file defines the
 * stdio writers, so no original remains to pass through to, and nothing in this stack
 * writes to a file.
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
 * Formats one call into the line buffer. A result longer than the stack buffer is
 * formatted again on the heap rather than cut; `GL_EXTENSIONS` runs to kilobytes.
 */
static int emit_formatted(FILE *stream, const char *fmt, va_list ap) {
    char buf[OOPS_LINE_MAX];
    va_list retry;
    int n;

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

    char *big = (char *)malloc((size_t)n + 1u);
    if (big == NULL) {
        /* Emit the truncated text and say that it is truncated. */
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
 * `printf` and `vprintf`. Mesa never calls them, but ported programs do: a call with a
 * conversion stays `printf` rather than being lowered to `puts`.
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
 * `putc` and `putchar` are macros in this platform's `stdio.h` (expanding to
 * `__sputc`), so they cannot be defined here; the archives reference none of the three.
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
 * `write`, which the platform does not export. Descriptors 1 and 2 share the line
 * buffer, so a direct `write` and an `fprintf` keep their order in the log. Any other
 * descriptor is reported and dropped; the return is the byte count either way.
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
