/*
 * Host suite for `src/runtime/stderr_to_klog.c`. The subject defines the stdio writers
 * and `write`, so it is its own binary, apart from the winsys suite whose reporting it
 * would capture.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The log sink, recorded so the suite can see what came out. */
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

/* Reports through `write`. The subject defines `write` too, so this text reaches the
 * recording sink rather than the terminal; the exit status carries the result. */
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

    /* A message assembled one character at a time is one line. */
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

    /* stdout is captured as well as stderr. */
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

    /* A line longer than a klog line is split, never truncated. */
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

    /* A bare newline produces no klog write. */
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
