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
 * A write to any other stream goes to its descriptor. This file defines the stdio
 * writers, so no original remains to pass through to, and the platform exports no
 * `write`; oops-sdk's `oops_fs_write` is the system call. `fileno` is the platform's
 * (libSceLibcInternal, present on hardware), so the platform's `FILE` layout stays
 * its own. The stream's buffer is bypassed: every write here reaches the file at once,
 * and a `fflush` of such a stream has nothing to do.
 *
 * A title's own files need this - SuperTuxKart writes its log and its config through
 * `fprintf`, and both were dropped.
 */
extern int64_t oops_fs_write(int fd, const void *buf, size_t count);

static int fd_write_all(int fd, const char *text, size_t len) {
    size_t done = 0;

    while (done < len) {
        const int64_t n = oops_fs_write(fd, text + done, len - done);
        if (n <= 0) {
            errno = EIO;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static int stream_write(FILE *stream, const char *text, size_t len) {
    const int fd = fileno(stream);

    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    return fd_write_all(fd, text, len);
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

    if ((size_t)n < sizeof(buf)) {
        va_end(retry);
        if (!stream_is_captured(stream)) {
            return stream_write(stream, buf, (size_t)n) == 0 ? n : -1;
        }
        line_append(buf, (size_t)n);
        return n;
    }

    char *big = (char *)malloc((size_t)n + 1u);
    if (big == NULL) {
        va_end(retry);
        if (!stream_is_captured(stream)) {
            errno = ENOMEM;
            return -1;
        }
        /* Emit the truncated text and say that it is truncated. */
        line_append(buf, sizeof(buf) - 1u);
        line_flush();
        oops_klog("MESA",
                  "...the line above is truncated; no memory to format the rest");
        return n;
    }

    (void)vsnprintf(big, (size_t)n + 1u, fmt, retry);
    va_end(retry);
    if (!stream_is_captured(stream)) {
        const int rc = stream_write(stream, big, (size_t)n);
        free(big);
        return rc == 0 ? n : -1;
    }
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

    if (size != 0 && bytes / size != nmemb) {
        return 0; /* the multiplication overflowed; write nothing rather than a wrong
                     length */
    }
    if (!stream_is_captured(stream)) {
        return stream_write(stream, (const char *)ptr, bytes) == 0 ? nmemb : 0;
    }
    line_append((const char *)ptr, bytes);
    return nmemb;
}

int fputs(const char *s, FILE *stream) {
    if (!stream_is_captured(stream)) {
        return stream_write(stream, s, strlen(s)) == 0 ? 0 : EOF;
    }
    line_append(s, strlen(s));
    return 0;
}

int fputc(int c, FILE *stream) {
    char ch = (char)c;

    if (!stream_is_captured(stream)) {
        return stream_write(stream, &ch, 1) == 0 ? (unsigned char)c : EOF;
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
        return fd_write_all(fd, (const char *)buf, nbyte) == 0 ? (ssize_t)nbyte : -1;
    }
    line_append((const char *)buf, nbyte);
    return (ssize_t)nbyte;
}
