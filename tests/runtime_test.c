/*
 * The runtime shim's host suite. Today that is one file:
 * `src/runtime/stderr_to_klog.c`.
 *
 * # Why this is a separate binary from the winsys suite
 *
 * The thing under test *defines* `fprintf`, `fwrite`, `fputs`, `fputc`, `puts`,
 * `fflush` and `perror`. Linking it into the winsys suite would redirect that suite's
 * own reporting into the log sink and it would appear to pass silently. So it gets its
 * own binary, and this file takes care not to report through anything it has replaced.
 *
 * # Why it reports with write(2) rather than printf
 *
 * Because that care has to be real rather than a convention. clang rewrites
 * `printf("literal\n")` into `puts`, which this file's subject defines - so a reporter
 * written with `printf` would be swallowed by the code it is testing, intermittently,
 * depending on whether a particular call had a format specifier in it. `write` is not
 * intercepted and cannot be rewritten into something that is.
 *
 * That is not a hypothetical: the first version of `stderr_to_klog.c` intercepted two
 * functions because 281 source-level `fprintf` calls were counted, and the archives
 * call seven different things because of exactly this rewriting (worklog 036).
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The log sink `stderr_to_klog.c` writes to. Replaced here so the suite can see what
 * came out. */
static int g_lines;
static char g_seen[512][256];

void oops_klog(const char *tag, const char *msg);
void oops_klog(const char *tag, const char *msg) {
    (void)tag;
    if (g_lines < (int)(sizeof(g_seen) / sizeof(g_seen[0]))) {
        size_t n = strlen(msg);
        if (n > sizeof(g_seen[0]) - 1) {
            n = sizeof(g_seen[0]) - 1;
        }
        memcpy(g_seen[g_lines], msg, n);
        g_seen[g_lines][n] = '\0';
    }
    g_lines++;
}

/* The subject, included rather than linked so that its statics are reachable and so
 * that the suite exercises the same translation unit a title compiles. */
#include "stderr_to_klog.c"

static int failures;
static int checks;

/* Reporting that cannot be intercepted. */
static void say(const char *text) {
    ssize_t ignored = write(1, text, strlen(text));
    (void)ignored;
}

static void check(int ok, const char *what) {
    checks++;
    if (!ok) {
        failures++;
        say("  FAIL  ");
        say(what);
        say("\n");
    }
}

static void reset(void) {
    g_lines = 0;
    s_line_len = 0;
}

int main(void) {
    say("oops-mesa runtime host suite\n");

    /* An ordinary diagnostic, delivered the way clang rewrites a `%s` format. */
    reset();
    fputs("amdgpu: ac_drm_query_info(dev_info) failed.\n", stderr);
    check(g_lines == 1, "an fputs line is one klog write");
    check(strcmp(g_seen[0], "amdgpu: ac_drm_query_info(dev_info) failed.") == 0,
          "and the trailing newline is consumed rather than logged");

    /* The case that forced line buffering: a message assembled one character at a time.
     */
    reset();
    const char *msg = "assembled one character at a time\n";
    for (const char *p = msg; *p; p++) {
        fputc(*p, stderr);
    }
    check(g_lines == 1, "34 fputc calls produce one klog line, not 34");
    check(strcmp(g_seen[0], "assembled one character at a time") == 0,
          "with the text intact");

    /* What a literal fprintf becomes. */
    reset();
    const char *lit = "amdgpu: unknown chip\n";
    fwrite(lit, 1, strlen(lit), stderr);
    check(g_lines == 1, "an fwrite line is one klog write");

    /* stdout is equally dead on this platform, so puts is captured too. */
    reset();
    puts("a line on stdout");
    check(g_lines == 1,
          "puts is captured, because stdout is no more readable than stderr");
    check(strcmp(g_seen[0], "a line on stdout") == 0,
          "and supplies its own line ending");

    /* Several lines in one write must not become one long line. */
    reset();
    const char *three = "first\nsecond\nthird\n";
    fwrite(three, 1, strlen(three), stderr);
    check(g_lines == 3, "three newlines in one write are three klog lines");
    check(strcmp(g_seen[1], "second") == 0, "in order");

    /* Longer than a klog line: split, never truncated. The tail of a diagnostic is
     * usually the half worth having. */
    reset();
    char big[400];
    for (size_t i = 0; i < sizeof(big) - 1; i++) {
        big[i] = (char)('A' + (i % 26));
    }
    big[sizeof(big) - 1] = '\0';
    fputs(big, stderr);
    fputc('\n', stderr);
    size_t total = 0;
    for (int i = 0; i < g_lines; i++) {
        total += strlen(g_seen[i]);
    }
    check(g_lines > 1, "a 399-byte line spans several klog writes");
    check(total == sizeof(big) - 1, "and every byte survives the split");

    /* A message with no newline yet is held, not dropped and not emitted early. */
    reset();
    fputs("no newline yet", stderr);
    check(g_lines == 0, "a partial line is not emitted early");
    fflush(stderr);
    check(g_lines == 1, "and fflush releases it");
    check(strcmp(g_seen[0], "no newline yet") == 0, "intact");

    /* A bare newline is a blank line, which is not worth a klog write. */
    reset();
    fputc('\n', stderr);
    check(g_lines == 0, "a bare newline produces no klog line");

    char tail[64];
    int n = snprintf(tail, sizeof(tail), "%d checks, %d failed\n", checks, failures);
    if (n > 0) {
        say(tail);
    }
    return failures ? 1 : 0;
}
