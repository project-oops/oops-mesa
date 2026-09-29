/*
 * The C library names a Mesa-linked title references and the platform does not export
 * from `libkernel`, `libSceLibcInternal` or `libScePosix`. Defining them here keeps
 * them out of the import list; an import nothing resolves kills the title on its first
 * call with `PRX_NOT_RESOLVED_FUNCTION`.
 *
 * A name with one correct answer (string functions, IEEE predicates, unit conversions
 * onto exported calls) is implemented exactly. A name on a path this build does not
 * execute logs its own name and fails the way its caller already handles, so being
 * reached shows in the log. `__assert`, `exit` and `longjmp` do not return.
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
/* After <sys/types.h>, and not alphabetically: it names `cpulevel_t`, `cpuwhich_t` and
 * `id_t` in its prototypes and declares none of them itself. */
#include <sys/cpuset.h>
#include <sys/utsname.h>

#include "oops/fs.h"
#include "oops_winsys.h"

/*
 * The assertion handler, `__dead2` in the platform's header. It traps after logging
 * rather than unwinding into code whose invariant has just failed. The reference comes
 * from oops-sdk's `agc_display.c`, not from Mesa.
 */
void __assert(const char *func, const char *file, int line, const char *expr) {
    oops_winsys_log(
        "assertion failed: %s, %s:%d in %s - this platform exports no __assert, so "
        "this is the shim in libc_absent.c",
        expr ? expr : "(null)", file ? file : "(null)", line, func ? func : "(null)");
    __builtin_trap();
}

/*
 * The environment, kept in the process.
 *
 * The platform exports no environment and a title starts without one, so the table
 * starts empty and every name reads as unset until the title sets it: Mesa's debug
 * switches (`os_get_option`, first called from `driParseConfigFiles`) stay off, as
 * before. A title that sets a variable reads it back - SuperTuxKart is pointed at its
 * data by `SUPERTUXKART_DATADIR` and its siblings, and ports read `HOME`.
 *
 * Each entry is one `NAME=VALUE` allocation, as a POSIX `environ` entry is, and
 * `getenv` returns the value inside it. Not thread-safe, which POSIX does not require
 * of `setenv`.
 */
#define ENV_MAX 128
static char *s_env[ENV_MAX];

/* The entry for `name`, or -1. */
static int env_find(const char *name, size_t len) {
    for (int i = 0; i < ENV_MAX; i++) {
        if (s_env[i] && strncmp(s_env[i], name, len) == 0 && s_env[i][len] == '=') {
            return i;
        }
    }
    return -1;
}

/* A name POSIX accepts: not empty and without '='. Its length through `*len`. */
static int env_name_ok(const char *name, size_t *len) {
    if (!name || !name[0] || strchr(name, '=')) {
        return 0;
    }
    *len = strlen(name);
    return 1;
}

char *getenv(const char *name) {
    size_t len;
    int i;

    if (!env_name_ok(name, &len)) {
        return NULL;
    }
    i = env_find(name, len);
    return i < 0 ? NULL : s_env[i] + len + 1;
}

/* `uname`'s implementation, reached from radeonsi's device-description logging. */
int __xuname(int size, void *namebuf) {
    (void)size;
    (void)namebuf;
    oops_winsys_log(
        "__xuname was called; this platform does not export it and the shim has no "
        "system name to give. Returning failure.");
    return -1;
}

/* libdrm's amdgpu layer parses text files with it. There are none here. */
ssize_t getline(char **linep, size_t *capp, FILE *stream) {
    (void)linep;
    (void)capp;
    (void)stream;
    oops_winsys_log(
        "getline was called; this platform does not export it. Returning end of "
        "input, which is what a caller reading a file that is not there expects.");
    return -1;
}

/*
 * Local time, reached from Mesa's logging and from games (SuperTuxKart stamps its
 * saves and dates its news with it).
 *
 * The answer is UTC: a true time, with the zone offset recorded as 0 rather than an
 * invented one. The console's zone is in libSceRtc (`sceRtcConvertUtcToLocalTime`,
 * present on the hardware: obscene `data/hardware/ps5-full.txt:11828`), but no title
 * has loaded that module yet, and an import that does not resolve kills the title on
 * its first call - worse than the wrong zone. The broken-down fields are computed here,
 * by the days-to-civil method, because the platform exports no `gmtime_r` either.
 */
struct tm *localtime_r(const time_t *clock, struct tm *result) {
    int64_t secs, days, rem, era, doe, yoe, doy, mp, y;

    if (clock == NULL || result == NULL) {
        return NULL;
    }
    secs = (int64_t)*clock;

    days = secs / 86400;
    rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    memset(result, 0, sizeof(*result));
    result->tm_hour = (int)(rem / 3600);
    result->tm_min = (int)(rem % 3600 / 60);
    result->tm_sec = (int)(rem % 60);
    result->tm_wday = (int)((days % 7 + 11) % 7); /* 1970-01-01 was a Thursday */

    days += 719468; /* to 0000-03-01 */
    era = (days >= 0 ? days : days - 146096) / 146097;
    doe = days - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    y = yoe + era * 400 + (mp >= 10 ? 1 : 0);
    result->tm_mday = (int)(doy - (153 * mp + 2) / 5 + 1);
    result->tm_mon = (int)(mp < 10 ? mp + 2 : mp - 10);
    result->tm_year = (int)(y - 1900);
    {
        static const int before[12] = {0,   31,  59,  90,  120, 151,
                                       181, 212, 243, 273, 304, 334};
        const int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        result->tm_yday =
            before[result->tm_mon] + result->tm_mday - 1 + (leap && result->tm_mon > 1);
    }
    return result;
}

/* Device nodes. libdrm creates them on systems that have a /dev; this one does not. */
int mknod(const char *path, mode_t mode, dev_t dev) {
    (void)path;
    (void)mode;
    (void)dev;
    oops_winsys_log(
        "mknod was called; there is no device filesystem here to create a node in. "
        "Returning failure.");
    return -1;
}

/* Temporary files, from Mesa's log helper. */
int mkstemps(char *path, int suffixlen) {
    (void)path;
    (void)suffixlen;
    oops_winsys_log(
        "mkstemps was called; this platform does not export it and the shim creates "
        "no file. Returning failure rather than a descriptor that is not one.");
    return -1;
}

/*
 * String functions on paths Mesa executes constantly. A title linking Mesa compiles
 * with builtins (`app.mk` filters out `-fno-builtin`), so each carries `no_builtin` to
 * stop clang turning its body into a call to itself.
 */

/* `strlen`, `strchr`, `strdup` and `strstr` are exported; `strcmp` is not. */
__attribute__((no_builtin("strcmp"))) int strcmp(const char *a, const char *b) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (*p != '\0' && *p == *q) {
        p++;
        q++;
    }
    return (int)*p - (int)*q;
}

/* Mesa's option parsing walks separator sets with it. */
__attribute__((no_builtin("strspn"))) size_t strspn(const char *s, const char *accept) {
    size_t n = 0;

    for (; s[n] != '\0'; n++) {
        const char *a = accept;
        while (*a != '\0' && *a != s[n]) {
            a++;
        }
        if (*a == '\0') {
            break;
        }
    }
    return n;
}

__attribute__((no_builtin("strndup"))) char *strndup(const char *s, size_t n) {
    size_t len = 0;
    char *out;

    while (len < n && s[len] != '\0') {
        len++;
    }
    out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = s[i];
    }
    out[len] = '\0';
    return out;
}

/* Into the table `getenv` reads. Mesa also reaches this through `os_set_option`
 * (`mesa/src/util/os_misc.c:347`). A full table is ENOMEM, and says so. */
int setenv(const char *name, const char *value, int overwrite) {
    size_t len, vlen;
    char *entry;
    int i;

    if (!env_name_ok(name, &len)) {
        errno = EINVAL;
        return -1;
    }
    i = env_find(name, len);
    if (i >= 0 && !overwrite) {
        return 0;
    }
    if (!value) {
        value = "";
    }
    vlen = strlen(value);
    entry = malloc(len + 1 + vlen + 1);
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, len);
    entry[len] = '=';
    memcpy(entry + len + 1, value, vlen + 1);
    if (i < 0) {
        for (i = 0; i < ENV_MAX && s_env[i]; i++) {
        }
        if (i == ENV_MAX) {
            free(entry);
            oops_winsys_log("setenv: the environment is full (%d entries); %s not set",
                            ENV_MAX, name);
            errno = ENOMEM;
            return -1;
        }
    } else {
        free(s_env[i]);
    }
    s_env[i] = entry;
    return 0;
}

int unsetenv(const char *name) {
    size_t len;
    int i;

    if (!env_name_ok(name, &len)) {
        errno = EINVAL;
        return -1;
    }
    i = env_find(name, len);
    if (i >= 0) {
        free(s_env[i]);
        s_env[i] = NULL;
    }
    return 0;
}

/*
 * `sysconf`, reached through `os_get_total_physical_memory`
 * (`mesa/src/util/os_misc.c:363`) and Mesa's CPU detection. The memory size comes from
 * `sceKernelGetDirectMemorySize`, the same source `device_info.c` answers
 * `AMDGPU_INFO_MEMORY` from. The page size is the 16 KiB the title is linked at
 * (`-z max-page-size=0x4000` in `app.mk`), and the page count is derived from it so the
 * product is exact.
 */
__attribute__((weak)) size_t sceKernelGetDirectMemorySize(void);

#define OOPS_PAGE_BYTES 16384

long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:
        return (long)OOPS_PAGE_BYTES;

    case _SC_PHYS_PAGES: {
        size_t bytes;

        if (!sceKernelGetDirectMemorySize) {
            oops_winsys_log(
                "sysconf(_SC_PHYS_PAGES): the kernel's memory-size call is not bound, "
                "so there is no count to give. Failing rather than inventing one.");
            return -1;
        }
        bytes = sceKernelGetDirectMemorySize();
        return (long)(bytes / OOPS_PAGE_BYTES);
    }

    /*
     * The processor counts size Mesa's thread pool; `-1` would make it one thread
     * (`mesa/src/util/u_cpu_detect.c:852`). The part has 16 hardware threads, and a
     * big-app container is given a varying subset of them, so the online count is read
     * from the affinity mask.
     */
    case _SC_NPROCESSORS_CONF:
        /* 8 Zen 2 cores with two-way SMT; `hw.ncpu` cannot be read by a title. */
        return 16;

    case _SC_NPROCESSORS_ONLN: {
        cpuset_t mask;
        int count;

        /*
         * `CPU_LEVEL_ROOT` is the container's CPU budget, as FreeBSD's own `sysconf`
         * reads it (`lib/libc/gen/sysconf.c:595` at the D004 pin). `CPU_LEVEL_WHICH`
         * asks for the process affinity mask, which the sandbox refuses.
         */
        CPU_ZERO(&mask);
        if (cpuset_getaffinity(CPU_LEVEL_ROOT, CPU_WHICH_PID, (id_t)-1, sizeof(mask),
                               &mask) == 0) {
            count = CPU_COUNT(&mask);
            if (count > 0) {
                return (long)count;
            }
        }

        /* The usual big-app budget, logged because a pool sized from it is a guess. */
        oops_winsys_log(
            "sysconf(_SC_NPROCESSORS_ONLN): the affinity mask could not be read, so "
            "reporting the 14 obSCEne measured for a big-app container rather than "
            "failing into Mesa's one-thread fallback.");
        return 14;
    }

    default:
        oops_winsys_log(
            "sysconf(%d) is not answered here; this shim knows the page size, the "
            "page count and the processor counts, and nothing else",
            name);
        return -1;
    }
}

/*
 * `sysctl`. Its callers - available-memory reporting (`mesa/src/util/os_misc.c:450`)
 * and a descriptor walk (`mesa/src/util/os_file.c:263`) - test the return and carry on.
 */
int sysctl(const int *name, unsigned int namelen, void *oldp, size_t *oldlenp,
           const void *newp, size_t newlen) {
    (void)name;
    (void)namelen;
    (void)oldp;
    (void)oldlenp;
    (void)newp;
    (void)newlen;
    oops_winsys_log(
        "sysctl was called; this platform does not export it and the shim has no "
        "table to read. Returning failure, which every caller here handles.");
    return -1;
}

/*
 * The per-thread locale override that every inlined `ctype` call reads first
 * (`toolchain/sysroot/usr/include/runetype.h:91`). The platform's libc exports
 * `_CurrentRuneLocale` but has no per-thread locale, so null - "no override" - is its
 * value, and the lookup falls through to `_CurrentRuneLocale`.
 */
#include <runetype.h>

_Thread_local const _RuneLocale *_ThreadRuneLocale = NULL;

/*
 * System V shared memory, used only by the software winsys
 * (`mesa/src/gallium/winsys/sw/dri/dri_sw_winsys.c`), which is linked through the DRI
 * driver table but never selected. The callers test for `-1` and `(void *)-1`
 * (`dri_sw_winsys.c:110`, `:116`).
 */
#include <sys/ipc.h>
#include <sys/shm.h>

int shmget(key_t key, size_t size, int shmflg) {
    (void)key;
    (void)size;
    (void)shmflg;
    oops_winsys_log(
        "shmget was called, so the software winsys is running, which this build does "
        "not select (D001). Refusing.");
    return -1;
}

void *shmat(int shmid, const void *shmaddr, int shmflg) {
    (void)shmid;
    (void)shmaddr;
    (void)shmflg;
    oops_winsys_log(
        "shmat was called; there is no shared segment here and the caller tests for "
        "this value.");
    return (void *)-1;
}

int shmdt(const void *shmaddr) {
    (void)shmaddr;
    oops_winsys_log("shmdt was called; nothing was ever attached.");
    return -1;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    (void)shmid;
    (void)cmd;
    (void)buf;
    oops_winsys_log("shmctl was called; there is no segment to control.");
    return -1;
}

/*
 * `__isinff` belongs to libc (`lib/libc/gen/isinf.c`), and this build compiles msun
 * only. A float is infinite when its exponent is all ones and its significand zero.
 * No `<math.h>` is included, so it carries its own prototype.
 */
int __isinff(float f);
int __isinff(float f) {
    union {
        float f;
        unsigned int u;
    } v = {.f = f};

    return (v.u & 0x7fffffffu) == 0x7f800000u;
}

/* `memcmp` under its BSD name. */
__attribute__((no_builtin("bcmp"))) int bcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) {
            return (int)p[i] - (int)q[i];
        }
    }
    return 0;
}

/* `memset` to zero under its BSD name. */
__attribute__((no_builtin("bzero"))) void bzero(void *s, size_t n) {
    unsigned char *p = (unsigned char *)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = 0;
    }
}

/* No `no_builtin`: clang has no `strnlen` builtin and rejects the attribute for it. */
size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;

    while (n < maxlen && s[n] != '\0') {
        n++;
    }
    return n;
}

__attribute__((no_builtin("strcpy"))) char *strcpy(char *dst, const char *src) {
    char *out = dst;

    while ((*out++ = *src++) != '\0') {
    }
    return dst;
}

__attribute__((no_builtin("strcat"))) char *strcat(char *dst, const char *src) {
    char *end = dst;

    while (*end != '\0') {
        end++;
    }
    while ((*end++ = *src++) != '\0') {
    }
    return dst;
}

/* Over `nanosleep`, which `libkernel` exports. An interrupted sleep resumes with the
 * remainder. */
int usleep(useconds_t usec) {
    struct timespec want;
    struct timespec left;

    want.tv_sec = (time_t)(usec / 1000000u);
    want.tv_nsec = (long)(usec % 1000000u) * 1000L;

    while (nanosleep(&want, &left) != 0) {
        if (left.tv_sec == 0 && left.tv_nsec == 0) {
            return -1;
        }
        want = left;
    }
    return 0;
}

/*
 * `strtod`, on a live path: GLSL float literals and driconf values
 * (`mesa/src/util/strtod.c`). It delegates to the exported `sscanf` so rounding is the
 * platform library's own; `%n` gives `endptr`, and a failed conversion leaves `endptr`
 * at the start.
 */
double strtod(const char *nptr, char **endptr) {
    double value = 0.0;
    int consumed = 0;

    if (nptr == NULL) {
        if (endptr != NULL) {
            *endptr = NULL;
        }
        return 0.0;
    }

    if (sscanf(nptr, "%lf%n", &value, &consumed) != 1) {
        if (endptr != NULL) {
            *endptr = (char *)nptr;
        }
        return 0.0;
    }

    if (endptr != NULL) {
        *endptr = (char *)nptr + consumed;
    }
    return value;
}

/*
 * `exit` parks rather than returning or trapping. No userland call ends a big-app
 * process: `_exit` raises `SIGSYS` and returning from the entry point faults, so the
 * shell closes the title. Parking keeps the log line and avoids a crash report.
 * `nanosleep` keeps this file free of oops-sdk.
 */
_Noreturn void exit(int status) {
    oops_winsys_log(
        "exit(%d) was called. This platform exports no exit and a big-app container "
        "cannot terminate itself, so this title is now idle and finished - close it "
        "from the host.",
        status);

    for (;;) {
        struct timespec second = {.tv_sec = 1, .tv_nsec = 0};
        (void)nanosleep(&second, NULL);
    }
}

/*
 * Filesystem, subprocess and syslog setup calls. Their callers are Mesa's disk cache
 * and shader dumps (`mesa/src/util/disk_cache*`, `mesa/src/util/os_file.c`), debug
 * dumps behind options this build does not set, and the disassembler hand-off; all
 * test the return. A title has no writable filesystem and no shell.
 */

int chown(const char *path, uid_t owner, gid_t group) {
    (void)owner;
    (void)group;
    oops_winsys_log("chown(\"%s\") was called; a title has no filesystem to own.",
                    path ? path : "(null)");
    return -1;
}

int unlink(const char *path) {
    oops_winsys_log("unlink(\"%s\") was called; nothing here created a file to remove.",
                    path ? path : "(null)");
    return -1;
}

void sync(void) {
    oops_winsys_log("sync was called; there are no buffers here to flush to a disk.");
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    (void)buf;
    (void)bufsiz;
    oops_winsys_log("readlink(\"%s\") was called; there are no symbolic links to read.",
                    path ? path : "(null)");
    return -1;
}

/*
 * `stat` over the SDK's file calls, the way oops-apps' `common/posix/posix.c` answers
 * it for a freestanding title: the path opens (it exists), `<path>/.` opens (it is a
 * directory - a file answers ENOTDIR), and a seek to the end gives a file's size. Mode,
 * size and link count are filled; ownership and times are not known and read as zero.
 * Measured on hardware from SuperTuxKart: libkernel's `lstat` and `access` are refused
 * with EPERM for a title, and `oops_fs_exists` (SYS_open) sees the same file.
 */
/*
 * The working directory, kept in the process.
 *
 * A title has no working directory the platform keeps for it: libkernel's `chdir` is
 * not one a title can use, and libSceLibcInternal's `getcwd` jumps through a pointer
 * the platform's libc start-up fills (SuperTuxKart faulted in it). So the directory is
 * this string, starting at `/app0` where the package is mounted, and every relative
 * path this runtime resolves - `stat`, `opendir`, `realpath` - is taken against it.
 * Irrlicht needs it real: it mounts an asset folder by `chdir`-ing into it, listing it,
 * and `stat`-ing each entry by its bare name.
 */
static char s_cwd[PATH_MAX] = "/app0";

/* `path` made absolute against the working directory and canonicalised by oops-sdk
 * (`.`, `..` and repeated separators removed). */
const char *oops_mesa_absolute_path(const char *path, char *buf, size_t max);
const char *oops_fs_resolve_path(const char *path, char *buf,
                                 size_t max); /* oops-sdk */

const char *oops_mesa_absolute_path(const char *path, char *buf, size_t max) {
    char joined[PATH_MAX];

    if (path[0] != '/') {
        if ((size_t)snprintf(joined, sizeof(joined), "%s/%s", s_cwd, path) >=
            sizeof(joined)) {
            return path; /* too long to join: fails as written, not as another file */
        }
        path = joined;
    }
    return oops_fs_resolve_path(path, buf, max);
}

char *getcwd(char *buf, size_t size) {
    const size_t len = strlen(s_cwd) + 1;
    char *out = buf;

    if (buf == NULL) {
        out = malloc(size > len ? size : len);
        if (out == NULL) {
            errno = ENOMEM;
            return NULL;
        }
    } else if (size == 0) {
        errno = EINVAL;
        return NULL;
    } else if (size < len) {
        errno = ERANGE;
        return NULL;
    }
    memcpy(out, s_cwd, len);
    return out;
}

static int stat_is_directory(const char *path);

int chdir(const char *path) {
    char abs[PATH_MAX];
    const char *canon;

    if (path == NULL || path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    canon = oops_mesa_absolute_path(path, abs, sizeof(abs));
    if (!oops_fs_exists(canon)) {
        errno = ENOENT;
        return -1;
    }
    if (!stat_is_directory(canon)) {
        errno = ENOTDIR;
        return -1;
    }
    if (strlen(canon) >= sizeof(s_cwd)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(s_cwd, canon, strlen(canon) + 1);
    return 0;
}

static int stat_is_directory(const char *path) {
    char dot[PATH_MAX];
    int fd;

    if ((size_t)snprintf(dot, sizeof(dot), "%s/.", path) >= sizeof(dot)) {
        return 0;
    }
    fd = oops_fs_open(dot, OOPS_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    (void)oops_fs_close(fd);
    return 1;
}

int stat(const char *path, struct stat *sb) {
    char abs[PATH_MAX];
    int64_t size;

    if (path == NULL || sb == NULL) {
        errno = EFAULT;
        return -1;
    }
    path = oops_mesa_absolute_path(path, abs, sizeof(abs));
    if (!oops_fs_exists(path)) {
        errno = ENOENT;
        return -1;
    }
    memset(sb, 0, sizeof(*sb));
    sb->st_nlink = 1;
    if (stat_is_directory(path)) {
        sb->st_mode = S_IFDIR | 0755;
        return 0;
    }
    size = oops_fs_file_size(path);
    sb->st_mode = S_IFREG | 0644;
    sb->st_size = size > 0 ? (off_t)size : 0;
    return 0;
}

int mkstemp(char *template_name) {
    oops_winsys_log(
        "mkstemp(\"%s\") was called; a debug dump wants a temporary file and there "
        "is no writable filesystem to make one in.",
        template_name ? template_name : "(null)");
    return -1;
}

FILE *open_memstream(char **bufp, size_t *sizep) {
    (void)bufp;
    (void)sizep;
    oops_winsys_log(
        "open_memstream was called; no in-memory stream is provided here, and the "
        "caller tests for null.");
    return NULL;
}

FILE *popen(const char *command, const char *type) {
    (void)type;
    oops_winsys_log(
        "popen(\"%s\") was called; there is no shell and no second process on this "
        "platform. This is the disassembler hand-off and it cannot run here.",
        command ? command : "(null)");
    return NULL;
}

int pclose(FILE *stream) {
    (void)stream;
    oops_winsys_log("pclose was called; popen never opened anything.");
    return -1;
}

/* `syslog` itself is exported; only its setup call is not. */
void openlog(const char *ident, int logopt, int facility) {
    (void)logopt;
    (void)facility;
    oops_winsys_log(
        "openlog(\"%s\") was called; `syslog` itself is exported but its setup call "
        "is not, so the identity is dropped and the messages still go out.",
        ident ? ident : "(null)");
}

/*
 * `syscall` does not forward: the platform does not name its system-call numbering
 * (D004), and a wrong number is a different call, not a failed one.
 */
int syscall(int number, ...) {
    oops_winsys_log(
        "syscall(%d, ...) was called; this shim will not guess at a system-call "
        "numbering the platform declines to name (D004). Failing instead.",
        number);
    return -1;
}

/* `libkernel` holds only an internal stub for it, which dynamic lookup does not find.
 */
int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, const void *newp,
                 size_t newlen) {
    (void)oldp;
    (void)oldlenp;
    (void)newp;
    (void)newlen;
    oops_winsys_log(
        "sysctlbyname(\"%s\") was called; the sweep found no address a title can "
        "resolve for it, only an internal stub. Returning failure.",
        name ? name : "(null)");
    return -1;
}

/*
 * The multibyte/single-byte boundary the inlined `ctype` functions test before
 * indexing the table (`_ctype.h:106`, `:139`). 256 is the C locale's value, the one
 * `table.c` sets for `_DefaultRuneLocale`.
 */
int __mb_sb_limit = 256;

/*
 * Placeholders for relocations in `librune.a`'s `__runes_for_locale`, which nothing in
 * the link calls; the object is staged whole from upstream. Their size is not a claim
 * about `struct _xlocale`: if the locale API is ever used, these must become the real
 * structures.
 */
void *__xlocale_C_locale[128];
void *__xlocale_global_locale[128];

/*
 * Over `clock_gettime`, which `libkernel` exports. Mesa reaches it through its shader
 * cache timestamps and `util/u_debug`'s elapsed reporting.
 */
time_t time(time_t *tloc) {
    struct timespec now = {.tv_sec = 0, .tv_nsec = 0};

    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        oops_winsys_log(
            "time() could not read CLOCK_REALTIME; reporting 0 rather than a "
            "plausible date.");
        now.tv_sec = 0;
    }
    if (tloc != NULL) {
        *tloc = now.tv_sec;
    }
    return now.tv_sec;
}

/*
 * Signal-set bit operations. `sigset_t` is `__uint32_t __bits[_SIG_WORDS]`
 * (`sys/_sigset.h:50`) and signal n is bit n-1. Mesa's `sigaction` calls compile
 * against the same header, so these agree with whatever layout the kernel reads.
 */
int sigfillset(sigset_t *set) {
    if (set == NULL) {
        return -1;
    }
    for (int i = 0; i < _SIG_WORDS; i++) {
        set->__bits[i] = 0xffffffffu;
    }
    return 0;
}

int sigdelset(sigset_t *set, int signo) {
    if (set == NULL || signo <= 0 || signo > (int)(_SIG_WORDS * 32)) {
        return -1;
    }
    set->__bits[(signo - 1) / 32] &= ~(1u << (unsigned)((signo - 1) % 32));
    return 0;
}

/*
 * The directory walk, over the SDK's reader (`oops_fs_opendir`, which reads through
 * `sceKernelGetdents`). Mesa's shader-cache eviction reaches it
 * (`mesa/src/util/disk_cache*`); a title lists its own data with it - SuperTuxKart
 * enumerates its translations, karts and tracks.
 *
 * `DIR` is the sysroot's incomplete `struct _dirdesc`, defined here. `readdir` returns
 * a `struct dirent` the `DIR` owns and the next call overwrites, as POSIX says. The SDK
 * gives no inode, so `d_fileno` is the entry's position, which is non-zero - a zero
 * `d_fileno` is how BSD marks a deleted entry.
 */
struct _dirdesc {
    oops_dir_t *dir;
    struct dirent ent;
    ino_t next;
};

DIR *opendir(const char *name) {
    DIR *d;

    if (name == NULL) {
        errno = EFAULT;
        return NULL;
    }
    d = calloc(1, sizeof(*d));
    if (d == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    {
        char abs[PATH_MAX];
        d->dir = oops_fs_opendir(oops_mesa_absolute_path(name, abs, sizeof(abs)));
    }
    if (d->dir == NULL) {
        free(d);
        errno = ENOENT;
        return NULL;
    }
    return d;
}

struct dirent *readdir(DIR *dirp) {
    oops_dirent_t e;
    size_t len;
    int rc;

    if (dirp == NULL) {
        errno = EBADF;
        return NULL;
    }
    rc = oops_fs_readdir(dirp->dir, &e);
    if (rc <= 0) {
        if (rc < 0) {
            errno = EIO; /* end of directory leaves errno alone */
        }
        return NULL;
    }
    len = strnlen(e.name, sizeof(dirp->ent.d_name) - 1);
    memset(&dirp->ent, 0, sizeof(dirp->ent));
    dirp->ent.d_fileno = ++dirp->next;
    dirp->ent.d_reclen = (uint16_t)sizeof(dirp->ent);
    dirp->ent.d_type = e.is_directory ? DT_DIR : DT_REG;
    dirp->ent.d_namlen = (uint16_t)len;
    memcpy(dirp->ent.d_name, e.name, len);
    return &dirp->ent;
}

int closedir(DIR *dirp) {
    int rc;

    if (dirp == NULL) {
        errno = EBADF;
        return -1;
    }
    rc = oops_fs_closedir(dirp->dir);
    free(dirp);
    return rc;
}

char *devname_r(dev_t dev, mode_t type, char *buf, int len) {
    (void)dev;
    (void)type;
    (void)buf;
    (void)len;
    oops_winsys_log(
        "devname_r was called; there is no device namespace to name from here.");
    return NULL;
}

/*
 * `app.mk` defines `OOPS_APP_NAME` for a title. The fallback lets
 * `tools/what-is-still-needed.sh` compile this file on its own.
 */
#ifndef OOPS_APP_NAME
#define OOPS_APP_NAME "oops-mesa"
#endif

/* The title's own identifier; the contract has no failure value. */
const char *getprogname(void) {
    return OOPS_APP_NAME;
}

int system(const char *command) {
    oops_winsys_log(
        "system(\"%s\") was called; there is no shell and no second process on this "
        "platform.",
        command ? command : "(null)");
    return -1;
}

/*
 * Selects the threaded paths of `stdio.h`'s macros (`stdio.h:512` and its kin). Mesa is
 * multithreaded, and 1 routes those macros to the exported `ferror`, `fileno`,
 * `clearerr` and `getc` rather than inline code reading `FILE` internals.
 *
 * `memcmp` is not defined here: oops-sdk's `src/system/freestd.c` provides it.
 */
int __isthreaded = 1;

/* `strcpy` returning the end. `strcpy` (above) and `strncpy` are exported. */
__attribute__((no_builtin("stpcpy"))) char *stpcpy(char *dst, const char *src) {
    while ((*dst = *src++) != '\0') {
        dst++;
    }
    return dst;
}

/*
 * On a live path: the flex-generated GLSL lexer asks this about its input on every
 * compile. No descriptor in a title is a terminal, so 0 is the true answer.
 */
int isatty(int fd) {
    (void)fd;
    return 0;
}

/* Reached from Mesa's HUD, which nothing here can switch on. */
double atof(const char *nptr) {
    return strtod(nptr, NULL);
}

/*
 * IEEE-754 single-precision classification. The return values are `math.h`'s
 * (`FP_INFINITE` 1, `FP_NAN` 2, `FP_NORMAL` 4, `FP_SUBNORMAL` 8, `FP_ZERO` 16), stated
 * because this file includes no `math.h`.
 */
int __fpclassifyf(float f);
int __fpclassifyf(float f) {
    union {
        float f;
        unsigned int u;
    } v = {.f = f};
    unsigned int exponent = (v.u >> 23) & 0xffu;
    unsigned int mantissa = v.u & 0x7fffffu;

    if (exponent == 0xffu) {
        return mantissa ? 2 : 1; /* FP_NAN : FP_INFINITE */
    }
    if (exponent == 0u) {
        return mantissa ? 8 : 16; /* FP_SUBNORMAL : FP_ZERO */
    }
    return 4; /* FP_NORMAL */
}

/*
 * Unreached paths: `sigaction` is Mesa's HUD, `shm_open` is `util/anon_file.c`, and
 * `setjmp`/`longjmp` are SPIR-V's error recovery (`spirv_to_nir.c`, `gl_spirv.c`).
 * `setjmp` returns 0 as on a direct call; `longjmp` has no saved context to jump to, so
 * it traps.
 */
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    (void)act;
    (void)oact;
    oops_winsys_log(
        "sigaction(%d) was called; this platform does not export it and a title does "
        "not own signal disposition. Returning failure.",
        sig);
    return -1;
}

int shm_open(const char *path, int flags, mode_t mode) {
    (void)flags;
    (void)mode;
    oops_winsys_log(
        "shm_open(\"%s\") was called; there are no shared memory objects here, and "
        "the caller tests the descriptor.",
        path ? path : "(null)");
    return -1;
}

int setjmp(jmp_buf env) {
    /* Once: every shader compile calls it, and the explanation does not change. The
     * failure that matters is a longjmp, which is logged below every time. */
    static int s_said;
    (void)env;
    if (!s_said) {
        s_said = 1;
        oops_winsys_log(
            "setjmp was called - this is SPIR-V's error recovery, which this build "
            "does "
            "not implement. Returning 0 as a direct call; a longjmp to it will stop.");
    }
    return 0;
}

void longjmp(jmp_buf env, int val) {
    (void)env;
    oops_winsys_log(
        "longjmp(%d) was called, and there is no saved context to return to: this "
        "platform exports neither half of the pair and oops-mesa implements only "
        "setjmp's direct-call return. SPIR-V has hit an error it wanted to unwind "
        "from. Stopping here rather than jumping somewhere invented.",
        val);
    __builtin_trap();
}
