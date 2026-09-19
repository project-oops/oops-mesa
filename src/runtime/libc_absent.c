/*
 * The C library functions Mesa references and this platform does not export.
 *
 * # Why these exist here rather than in the import manifest
 *
 * A title's module names, for every symbol it imports, the library that exports it. Six names a
 * Mesa-linked title references have no such library: obSCEne swept them against `libkernel`,
 * `libSceLibcInternal` and `libScePosix` on the payload leg and every one came back absent, with
 * positive controls in the same sweep proving the leg and the dynamic linker were working -
 * `017-posix/libkernel-pthread-symbols` resolved 27 of 27 and `017-posix/clock-symbols` resolved
 * 11 and 11 (sweep `20260916-235024`, REQ-20260916T2200Z-4d7e).
 *
 * So there is nothing to place them in. An import naming a library that cannot resolve it is not
 * a missing feature, it is a title the loader kills on the first call with
 * `PRX_NOT_RESOLVED_FUNCTION`, which is how this was found.
 *
 * Defining them here removes them from the import list entirely: the linker satisfies them from
 * this object and the module never asks the platform for them.
 *
 * # Why a stub is honest here and would not be elsewhere
 *
 * None of these is on a path this stack executes. Their call sites are Mesa's debug dumps, its
 * syslog wrapper, `uname`, temporary files and time formatting - reached by logging and by code
 * behind options this build does not set. That was established by reading which archive member
 * references each one, not by assuming.
 *
 * That is exactly why they must be loud rather than silent. A stub that quietly returns a
 * plausible value is the lying-stub failure this collection exists to refuse (CLAUDE.md,
 * principle 4): if one of these is ever reached, the interesting fact is *that it was reached*,
 * and a log line naming it is worth more than a correct-looking answer. Each one below says its
 * own name and then fails in the way its caller is already required to handle.
 *
 * `__assert` is the exception and does not return, because its contract is not to. It is also
 * not Mesa's: it comes from oops-sdk's `agc_display.c`, compiled into the title. A title built
 * `-DNDEBUG` would not reference it at all, which is the better fix and belongs to whoever owns
 * that trade.
 */

#include <assert.h>
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
/* After <sys/types.h>, and not alphabetically: it names `cpulevel_t`, `cpuwhich_t` and `id_t`
 * in its prototypes and declares none of them itself. */
#include <sys/cpuset.h>
#include <sys/utsname.h>

#include "oops_winsys.h"

/*
 * The assertion handler. `__dead2` in the platform's header, so returning is not an option and
 * the trap is deliberate: it stops at the fault with the message already logged, rather than
 * unwinding into code whose invariant has just been reported broken.
 */
void __assert(const char *func, const char *file, int line, const char *expr)
{
    oops_winsys_log("assertion failed: %s, %s:%d in %s - this platform exports no __assert, so "
                    "this is the shim in libc_absent.c",
                    expr ? expr : "(null)", file ? file : "(null)", line, func ? func : "(null)");
    __builtin_trap();
}

/*
 * The environment, which this platform does not have.
 *
 * This is the one that was killing the title. `driParseConfigFiles` calls `os_get_option` before
 * anything else, `os_get_option` is one call deep - `getenv` - and obSCEne measured on the payload
 * leg that `libSceLibcInternal` does not export it: eleven candidates swept, ten resolved, and
 * `getenv` alone came back `0x0`, with `libc-controls` and `kernel-controls` both 3 of 3 in the
 * same check (sweep `20260917-013336`, REQ-20260917T0025Z-1f6d). The import manifest placed it
 * plausibly and the loader left its slot pointing at libkernel's unpatched-function trap, which
 * is the `PRX_NOT_RESOLVED_FUNCTION` the title died on.
 *
 * Unlike the rest of this file, **null is the right answer and not a failure**. `getenv` returns
 * null for a name that is not set, and on a platform with no environment no name is set. Mesa
 * reads only optional debug switches through it - `MESA_DRICONF_EXECUTABLE_OVERRIDE`,
 * `AMD_DEBUG`, `R600_DEBUG` and their kin - and every caller already handles null as "not
 * configured". So this one is quiet after saying so once, rather than loud on every call: a
 * correct answer repeated a hundred times is noise, and klog drops lines past about 128 bytes
 * anyway (orbistoun worklog 539).
 */
char *getenv(const char *name)
{
    static int said = 0;
    if (!said) {
        said = 1;
        oops_winsys_log("getenv: this platform exports none, so every name reads as unset. "
                        "Mesa's debug switches are therefore all off. Said once, not per call.");
    }
    (void)name;
    return NULL;
}

/* `uname`'s implementation. radeonsi reaches it through its own device-description logging. */
int __xuname(int size, void *namebuf)
{
    (void)size;
    (void)namebuf;
    oops_winsys_log("__xuname was called; this platform does not export it and the shim has no "
                    "system name to give. Returning failure.");
    return -1;
}

/* libdrm's amdgpu layer parses text files with it. There are none here. */
ssize_t getline(char **linep, size_t *capp, FILE *stream)
{
    (void)linep;
    (void)capp;
    (void)stream;
    oops_winsys_log("getline was called; this platform does not export it. Returning end of "
                    "input, which is what a caller reading a file that is not there expects.");
    return -1;
}

/* Time formatting, reached from Mesa's logging. */
struct tm *localtime_r(const time_t *clock, struct tm *result)
{
    (void)clock;
    (void)result;
    oops_winsys_log("localtime_r was called; this platform does not export it and the shim will "
                    "not invent a broken-down time. Returning null.");
    return NULL;
}

/* Device nodes. libdrm creates them on systems that have a /dev; this one does not. */
int mknod(const char *path, mode_t mode, dev_t dev)
{
    (void)path;
    (void)mode;
    (void)dev;
    oops_winsys_log("mknod was called; there is no device filesystem here to create a node in. "
                    "Returning failure.");
    return -1;
}

/* Temporary files, from Mesa's log helper. */
int mkstemps(char *path, int suffixlen)
{
    (void)path;
    (void)suffixlen;
    oops_winsys_log("mkstemps was called; this platform does not export it and the shim creates "
                    "no file. Returning failure rather than a descriptor that is not one.");
    return -1;
}

/*
 * ---------------------------------------------------------------------------------------------
 * The second set, measured 2026-09-17 (sweep `20260917-160206`, REQ-20260917T1610Z-9c3e).
 *
 * `mesa-probe` reached radeonsi's screen creation and died with `PRX_RUNTIME_ERROR`, which is a
 * call through a slot the loader never resolved - the same way `getenv` presented. The twenty
 * names a title cannot resolve at link time were swept against the libraries its manifest names,
 * with `libc-controls` and `kernel-controls` both 3 of 3 in the same check, so a `0x0` here is an
 * absence and not a blind probe. Eight came back absent.
 *
 * **These differ in kind from the six above, and the difference decides the code.** Those six sit
 * on paths this stack does not execute, so a loud failure is the honest answer and being reached
 * is itself the news. Three of these are on paths Mesa executes constantly. A loud stub for
 * `strcmp` would not be honesty, it would be a title that cannot run.
 *
 * So: where the answer is *defined* - the string functions - it is implemented exactly, and that
 * is not a guess dressed as a value, it is the value. Where there is nothing to answer with - the
 * environment - it fails the way its caller already handles. `getrlimit` needed nothing: it is
 * exported by `libkernel` at `0x800001110` and the manifest was already right about it.
 *
 * # One hazard worth naming
 *
 * A title linking Mesa compiles *with* builtins - `app.mk` filters `-ffreestanding -fno-builtin`
 * out for exactly these titles - so clang is free to recognise the shape of a hand-written
 * `strcmp` and replace its body with a call to `strcmp`, which is this function. Each of the
 * three carries `no_builtin` for that reason. The attribute is only accepted on a definition,
 * which is where it is.
 */

/* Not exported by `libSceLibcInternal` (`0x0`), and on every path Mesa takes. `strlen`, `strchr`,
 * `strdup` and `strstr` next to it *are* exported, so only this one is defined here. */
__attribute__((no_builtin("strcmp")))
int strcmp(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (*p != '\0' && *p == *q) {
        p++;
        q++;
    }
    return (int)*p - (int)*q;
}

/* Not exported (`0x0`). Mesa's option parsing walks separator sets with it. */
__attribute__((no_builtin("strspn")))
size_t strspn(const char *s, const char *accept)
{
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

/* Not exported (`0x0`), though `strdup` beside it is. `malloc` is exported, so this is the
 * ordinary definition and not a stand-in for one. */
__attribute__((no_builtin("strndup")))
char *strndup(const char *s, size_t n)
{
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

/*
 * The environment, the other half of `getenv`.
 *
 * `getenv` above returns null because no name is set; these two are what "cannot be set" looks
 * like. Mesa reaches them through `os_set_option` (`util/os_misc.c:347`), which returns void and
 * inspects nothing, so failing is free - and failing is the truth, because a later `getenv` will
 * certainly not find what was written.
 *
 * Quiet after the first, for the reason `getenv` is: a correct answer repeated is noise.
 */
static void said_no_environment(const char *who)
{
    static int said = 0;
    if (!said) {
        said = 1;
        oops_winsys_log("%s: this platform has no environment to write to, so a name cannot be "
                        "set and a later getenv would not find it. Said once, not per call.",
                        who);
    }
}

int setenv(const char *name, const char *value, int overwrite)
{
    (void)name;
    (void)value;
    (void)overwrite;
    said_no_environment("setenv");
    return -1;
}

int unsetenv(const char *name)
{
    (void)name;
    said_no_environment("unsetenv");
    return -1;
}

/*
 * `pow` **was** here, and is not any more.
 *
 * It was a hand-written one: repeated multiplication, exact for the whole-number exponents the
 * two callers in this link actually use (`util/xmlconfig.c:223` and `aco_statistics.cpp:541`),
 * and loud about refusing a fractional one. That was the right shape while this file was the only
 * place arithmetic could come from.
 *
 * It is gone because msun is now built for the target, and `e_pow.c` in it is the real function -
 * correct for every exponent, not only the two shapes that happened to be called. Keeping both
 * was not an option the linker offers: it reported `pow` defined twice and stopped, which is how
 * this was noticed and is the better failure.
 *
 * The note is left because the reasoning is worth keeping: a shim exact over its callers' actual
 * domain is honest, and it is still second to the real implementation when one can be had.
 */

/*
 * `sysconf`, not exported by `libkernel` (`0x0`).
 *
 * Mesa reaches it through `os_get_total_physical_memory` (`util/os_misc.c:363`), which multiplies
 * `_SC_PHYS_PAGES` by `_SC_PAGESIZE`. That build takes the `HAVE_SYSCONF` branch, so this is the
 * path in use and the `sysctl` branch below it is not.
 *
 * **The product is the measured fact; the two factors are a way of expressing it.** The size
 * comes from `sceKernelGetDirectMemorySize`, which is the same source `device_info.c` answers
 * `AMDGPU_INFO_MEMORY` from - one fact, one source, so the two cannot disagree. The page size is
 * the 16 KiB this title is already linked and mapped at (`-z max-page-size=0x4000` in
 * `app.mk`, and the loader maps its segments at that alignment), and the page count is then
 * derived from it rather than assumed, so their product is exactly the size measured however the
 * page size is read.
 *
 * Anything else this is asked for says so and fails, which is what its callers check for.
 */
__attribute__((weak)) size_t sceKernelGetDirectMemorySize(void);

#define OOPS_PAGE_BYTES 16384

long sysconf(int name)
{
    switch (name) {
    case _SC_PAGESIZE:
        return (long)OOPS_PAGE_BYTES;

    case _SC_PHYS_PAGES: {
        size_t bytes;

        if (!sceKernelGetDirectMemorySize) {
            oops_winsys_log("sysconf(_SC_PHYS_PAGES): the kernel's memory-size call is not bound, "
                            "so there is no count to give. Failing rather than inventing one.");
            return -1;
        }
        bytes = sceKernelGetDirectMemorySize();
        return (long)(bytes / OOPS_PAGE_BYTES);
    }

    /*
     * The processor counts, which Mesa turns into its thread-pool size.
     *
     * These were unanswered until 2026-09-17, and the cost was specific: `u_cpu_detect.c:852`
     * does `nr_cpus = MAX2(1, available_cpus)`, so a `-1` here told Mesa the machine had **one**
     * processor and its shader compiler ran on one thread. obSCEne measured the real numbers
     * (REQ-20260917T1845Z-6b3e): 16 hardware threads, of which a big-app container gets 14,
     * mask `0x3fff` - one physical core held back for the system.
     *
     * **The available count is queried rather than stated**, because the resolution says it
     * varies: 12 and mask `0x0fff` are assigned under some system workloads. A number that is
     * right today and wrong under load is exactly the kind of constant this project should not
     * compile in, and the affinity mask is the thing that actually decides it.
     *
     * `cpuset_getaffinity` is the call. Its address is evidenced twice over - obSCEne's own
     * resolution puts it in `libkernel` at `+0x20f0`, and the mined corpus independently places
     * it there - which matters because `sysctlbyname`, the other route the resolution suggests,
     * is one of the names `-9f41` proved unbindable, so `hw.ncpu` is not available to a title.
     */
    case _SC_NPROCESSORS_CONF:
        /*
         * 16, measured: 8 Zen 2 cores, two-way SMT. This one is stated rather than queried
         * because it is a property of the part rather than of the container, and the call that
         * would report it - `sysctlbyname("hw.ncpu")` - cannot be bound here.
         */
        return 16;

    case _SC_NPROCESSORS_ONLN: {
        cpuset_t mask;
        int count;

        /*
         * `CPU_LEVEL_ROOT`, not `CPU_LEVEL_WHICH`, and the difference is the whole point.
         *
         * This matches FreeBSD's own `sysconf` exactly (`lib/libc/gen/sysconf.c:595` at the D004
         * pin): `cpuset_getaffinity(CPU_LEVEL_ROOT, CPU_WHICH_PID, -1, sizeof(cpus), &cpus)`. The
         * root set is the CPU budget the *container* is allowed, which for a constrained process -
         * a jail, and a big-app sandbox is jail-shaped - is the limited count that actually
         * applies to it, exactly the 14-of-16 the `-6b3e` resolution measured. `CPU_LEVEL_WHICH`
         * asks instead for the current affinity *mask*, which a freshly-launched process may not
         * have set and which the sandbox declined outright on the first hardware run (worklog
         * 048: the call returned failure and the fallback fired). Reading the reference is what
         * found this; the earlier level was a plausible-looking guess and this is the measured
         * idiom.
         */
        CPU_ZERO(&mask);
        if (cpuset_getaffinity(CPU_LEVEL_ROOT, CPU_WHICH_PID, (id_t)-1,
                               sizeof(mask), &mask) == 0) {
            count = CPU_COUNT(&mask);
            if (count > 0) {
                return (long)count;
            }
        }

        /*
         * The measured default for a big-app container, used only when the mask could not be
         * read. It says so, because a thread pool sized from a fallback is worth knowing about -
         * and 14 being wrong in the conservative direction is better than 1.
         */
        oops_winsys_log("sysconf(_SC_NPROCESSORS_ONLN): the affinity mask could not be read, so "
                        "reporting the 14 obSCEne measured for a big-app container rather than "
                        "failing into Mesa's one-thread fallback.");
        return 14;
    }

    default:
        oops_winsys_log("sysconf(%d) is not answered here; this shim knows the page size, the "
                        "page count and the processor counts, and nothing else", name);
        return -1;
    }
}

/*
 * `sysctl`, not exported by `libkernel` (`0x0`).
 *
 * Unlike `sysconf` this is not on a path in use: `os_get_total_physical_memory` takes the
 * `HAVE_SYSCONF` branch above it, and what remains is available-memory reporting
 * (`util/os_misc.c:450`) and a descriptor walk (`util/os_file.c:263`), both of which test the
 * return and carry on without it. So this is a loud stub in the sense the six above are, and
 * being reached is the news.
 *
 * The resolution suggested translating to `sysctlbyname`. That is not done here: the sweep has no
 * row for `sysctlbyname`, so its stated address is unverified, and building a translation on an
 * unmeasured symbol is how the last few days went wrong.
 */
int sysctl(const int *name, unsigned int namelen, void *oldp, size_t *oldlenp,
           const void *newp, size_t newlen)
{
    (void)name;
    (void)namelen;
    (void)oldp;
    (void)oldlenp;
    (void)newp;
    (void)newlen;
    oops_winsys_log("sysctl was called; this platform does not export it and the shim has no "
                    "table to read. Returning failure, which every caller here handles.");
    return -1;
}

/*
 * ---------------------------------------------------------------------------------------------
 * The per-thread locale override, which this platform has no concept of.
 *
 * This is not a stub in either sense above. It is a variable whose correct value here is null,
 * and saying so is what makes `tolower` work.
 *
 * # What it was doing
 *
 * `MESA00001` died in `si_init_renderer_string` on `tolower(name[i])`, with
 * `PRX_RUNTIME_ERROR 0xa0020103` and a General Dynamic TLS call at the fault - resolved from the
 * link map and the instruction at `si_init_renderer_string+0x9c`, which carries the
 * `data16 data16 rex.W` prefixes that mark a call to `__tls_get_addr` (worklog 038, 039).
 *
 * The header explains itself (`toolchain/sysroot/usr/include/runetype.h:91`):
 *
 *     extern _Thread_local const _RuneLocale *_ThreadRuneLocale;
 *     static __inline const _RuneLocale *__getCurrentRuneLocale(void)
 *     {
 *         if (_ThreadRuneLocale)
 *             return _ThreadRuneLocale;
 *         return _CurrentRuneLocale;
 *     }
 *
 * So every `ctype` call reaches a thread-local in another module first.
 *
 * # Why null is the answer rather than a guess
 *
 * obSCEne measured both halves (REQ-20260917T1640Z-5b28, 2026-09-17T16:50Z, verified against the
 * rows): `libkernel` **does** export `__tls_get_addr` (`libkernel-vaddrs.txt:67`, `+0x3b960`), and
 * `libSceLibcInternal` **does not** export `_ThreadRuneLocale` at all - 3,016 of its symbols are
 * captured and the only Rune names among them are `_CurrentRuneLocale` and `_DefaultRuneLocale`.
 * The resolver exists; the variable does not.
 *
 * That is a coherent platform, not a broken one: a libc with no per-thread locale has nothing to
 * put in a per-thread locale override. **Null is what "this thread has no override" means**, and
 * it is what the fall-through above is written to handle - so defining it here does not substitute
 * for the platform's behaviour, it states it. `_CurrentRuneLocale`, which the fall-through
 * returns, *is* exported.
 *
 * Defined in the title's own image, which `TLSP00001` measured the loader to honour. That is a
 * different mechanism from the cross-module resolution that failed, and it is the whole reason
 * this works.
 *
 * # What it costs a title
 *
 * A `PT_TLS` segment, which is what SELFish REQ-20260915T0001Z-8d72 asks `native_eboot.ld` for.
 * `mesa-probe` carries its own `local_tls.ld` today; until that request lands, every title linking
 * Mesa needs the same script.
 */
#include <runetype.h>

_Thread_local const _RuneLocale *_ThreadRuneLocale = NULL;

/*
 * ---------------------------------------------------------------------------------------------
 * System V shared memory, for the software rasteriser this build will never run.
 *
 * These four are the first-kind stub again - unreached paths, where being reached is the news -
 * and the path is named rather than assumed. Every call is in
 * `mesa/src/gallium/winsys/sw/dri/dri_sw_winsys.c`, which allocates its display target in a
 * shared segment so an X server can map it. `libswdri.a` is in a title's link because the DRI
 * target's driver table references it, not because anything selects it: this build creates its
 * screen through radeonsi, and D003 forbids falling back to software at all.
 *
 * So if one of these is ever reached, the interesting fact is that the software winsys ran, which
 * is a larger problem than the call failing. Each says so and then fails the way its caller
 * already tests for - `shmget` and `shmat` are checked against `-1` and `(void *)-1` at
 * `dri_sw_winsys.c:110` and `:116`.
 */
#include <sys/ipc.h>
#include <sys/shm.h>

int shmget(key_t key, size_t size, int shmflg)
{
    (void)key;
    (void)size;
    (void)shmflg;
    oops_winsys_log("shmget was called, so the software winsys is running - which this build does "
                    "not select and D003 forbids falling back to. Refusing.");
    return -1;
}

void *shmat(int shmid, const void *shmaddr, int shmflg)
{
    (void)shmid;
    (void)shmaddr;
    (void)shmflg;
    oops_winsys_log("shmat was called; there is no shared segment here and the caller tests for "
                    "this value.");
    return (void *)-1;
}

int shmdt(const void *shmaddr)
{
    (void)shmaddr;
    oops_winsys_log("shmdt was called; nothing was ever attached.");
    return -1;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    (void)shmid;
    (void)cmd;
    (void)buf;
    oops_winsys_log("shmctl was called; there is no segment to control.");
    return -1;
}

/*
 * ---------------------------------------------------------------------------------------------
 * The 2026-09-17 sweep: what was left after the arithmetic moved into libm.
 *
 * # Where this list comes from
 *
 * The earlier entries in this file each came from a name or two at a time, found by a title dying
 * on the console. This block comes from asking the whole question at once: every symbol the
 * linked title still imports, swept for export on firmware 12.40
 * (REQ-20260917T1640Z-5b28, 139 names, controls 3 of 3 on both legs).
 *
 * 68 came back absent. Of those, 33 are recorded **present** by this collection's own export
 * census, which is a conflict rather than a fact and is filed as REQ-20260917T1818Z-9f41. Two
 * were not symbols at all, but demangled C++ names my own extractor truncated, and one -
 * `_ThreadRuneLocale` - was already handled above.
 *
 * That leaves 32 absent in both measurements. Fourteen were arithmetic and are now compiled in
 * from FreeBSD's own msun rather than written here, which is why there is no `sin` below. The
 * remaining eighteen are these, and `write` is in `stderr_to_klog.c` with the rest of the
 * stream capture rather than here.
 *
 * # The 33 are no longer disputed: none of them binds
 *
 * `-9f41` resolved at 20:45Z and the answer is unambiguous. **String/NID dynamic resolution is
 * authoritative**, all 33 return `0x0`, and an import table referencing any of them fails to
 * link dynamically or dies on the first call with `PRX_NOT_RESOLVED_FUNCTION`.
 *
 * The census was not wrong so much as answering about a different machine. Its rows were captured
 * under **GEN=4** - the PS4 backward-compatibility container, running Orbis userland - where
 * `libSceLibcInternal` did export the C runtime and the maths. Native Prospero replaced it with a
 * stripped PRX that dropped them from the dynamic export table. So census presence never implied
 * bindability here, and the shape of the reconciliation guessed at in the request - an entry in a
 * table that dynamic lookup cannot reach - was right for a reason nobody had proposed.
 *
 * Every one of the 33 is therefore supplied locally. The order that happened in is worth keeping,
 * because it is an argument for a rule: six were done before the answer arrived, on the grounds
 * that **a symbol whose value is fully specified can be defined locally without taking a side** -
 * the IEEE predicates are bit tests with one right answer, so five came from msun and `__isinff`
 * is below. That rule turned out to pick exactly the safe subset, and the eleven it did *not*
 * cover are the ones that needed the answer: a locale table, a filesystem, a clock, a program
 * name. Those follow, and the table is upstream's own rather than a guess at one.
 *
 * # Why some of these are real and most are stubs
 *
 * Unchanged from this file's own rule: a stub is honest only where the path is not executed, and
 * a stub on a live path is the lying-stub failure the collection refuses (CLAUDE.md, principle 4).
 * So the four that Mesa reaches in ordinary operation are implemented, the twelve behind debug
 * dumps, subprocesses and the filesystem are loud, and `exit` does not return because its
 * contract is not to.
 */

/*
 * The one arithmetic name msun does not carry.
 *
 * `__isinff` is libc's, not libm's - FreeBSD keeps the `isinf` family in `lib/libc/gen/isinf.c`,
 * and this build compiles msun only. Its four relatives came from `msun/src/s_isnormal.c` and
 * this one did not, which is the whole reason it is here rather than beside them.
 *
 * Written out rather than left as an import because the answer is not a matter of opinion: a
 * float is an infinity when its exponent is all ones and its significand is zero.
 *
 * It carries its own prototype and no `no_builtin`, unlike the string functions below. Neither is
 * an oversight: this file includes no `<math.h>`, so nothing else declares it, and clang has no
 * `__isinff` builtin to suppress - the attribute names a builtin and is rejected outright for a
 * name that is not one.
 */
int __isinff(float f);
int __isinff(float f)
{
    union {
        float f;
        unsigned int u;
    } v = { .f = f };

    return (v.u & 0x7fffffffu) == 0x7f800000u;
}

/*
 * Three more of the disputed 33, by the same rule as the IEEE predicates above: each has exactly
 * one correct implementation, so defining it takes no side in the conflict.
 *
 * These are the ones worth doing rather than leaving to `-9f41`, because they are not on an
 * obscure path - `bzero` and `bcmp` are reached constantly, by Mesa and by the staged libraries
 * both. Leaving a hot path resting on an unresolved measurement is the trade this file exists to
 * avoid.
 *
 * Their neighbours are deliberately left alone. `opendir`, `readdir` and `closedir` describe a
 * filesystem this shim cannot see, `time` and `getprogname` describe the platform, and
 * `_CurrentRuneLocale` and `__mb_sb_limit` are the platform's own locale data. For those, a local
 * definition would be a different answer rather than the same one, so they stay imports and the
 * request stays open.
 */

/* `memcmp` under its BSD name, and identical to it: the sign of the first difference, zero when
 * the ranges match. */
__attribute__((no_builtin("bcmp")))
int bcmp(const void *a, const void *b, size_t n)
{
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
__attribute__((no_builtin("bzero")))
void bzero(void *s, size_t n)
{
    unsigned char *p = (unsigned char *)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = 0;
    }
}

/* The length of a string, stopping at `n`, and never reading past it. `strlen` beside it *is*
 * exported; only the bounded form is missing. */
/* No `no_builtin` here, unlike its two neighbours: the attribute names a builtin and clang has
 * no `strnlen` one, so asking for it is rejected outright rather than ignored. */
size_t strnlen(const char *s, size_t maxlen)
{
    size_t n = 0;

    while (n < maxlen && s[n] != '\0') {
        n++;
    }
    return n;
}

/* Not exported (`0x0`), and on every path. `strncpy` and `stpcpy` beside it *are* exported, which
 * is why only this one is here. `no_builtin` for the reason the three string functions above
 * carry it: a title linking Mesa compiles with builtins, so clang may rewrite the body of a
 * hand-written `strcpy` into a call to `strcpy`. */
__attribute__((no_builtin("strcpy")))
char *strcpy(char *dst, const char *src)
{
    char *out = dst;

    while ((*out++ = *src++) != '\0') {
    }
    return dst;
}

/* Not exported (`0x0`), though `strncat` beside it is. */
__attribute__((no_builtin("strcat")))
char *strcat(char *dst, const char *src)
{
    char *end = dst;

    while (*end != '\0') {
        end++;
    }
    while ((*end++ = *src++) != '\0') {
    }
    return dst;
}

/* Not exported (`0x0`). `usleep` is a sleep expressed in microseconds and `nanosleep` **is**
 * exported - measured at `0x80000ece0` in `libkernel` in the same sweep - so this is a unit
 * conversion onto a measured call rather than a stand-in for one.
 *
 * The loop is not decoration: `nanosleep` returns early when interrupted, and a caller that
 * asked to wait has not waited. It resumes with the remainder the call reports. */
int usleep(useconds_t usec)
{
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
 * `strtod`, not exported (`0x0`) - and the one name on this list where a hand-written body would
 * have been the wrong answer.
 *
 * Mesa parses GLSL float literals and driconf values through it (`util/strtod.c`), so it is live,
 * and correct rounding is the whole point of it: a decimal literal has one nearest double and an
 * approximation that lands on the neighbour changes a shader's constants. Writing a correctly
 * rounding decimal-to-binary conversion is a project, and writing an almost-correct one here
 * would be exactly the plausible-looking answer principle 4 exists to refuse.
 *
 * So it delegates to `sscanf`, which **is** exported - measured at `0x80010a700` in
 * `libSceLibcInternal` in the same sweep - and which is the same library's own conversion, with
 * whatever rounding it implements applied consistently. `%n` reports how much of the string was
 * consumed, which is what `endptr` is, and a failed conversion leaves `endptr` at the start, as
 * the contract requires.
 *
 * What this inherits rather than decides: hex floats, `inf` and `nan` are handled exactly as far
 * as the platform's `sscanf` handles them. That is the same answer a title would have got had
 * `strtod` been exported, which is the point.
 */
double strtod(const char *nptr, char **endptr)
{
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
 * `exit`, not exported (`0x0`), and the only entry here that cannot return.
 *
 * Mesa calls it where it has decided it cannot continue - an unrecoverable allocation failure in
 * a path with nowhere to report to. Nothing in oops-sdk or in oops-apps' entry path calls it, so
 * defining it here does not stand between a title and its ordinary completion.
 *
 * # It parks, and that is measured rather than chosen
 *
 * This trapped, on the reasoning that there was no exit to delegate to. That was right about the
 * platform and wrong about the remedy, and REQ-20260917T1450Z-2e71 has since settled it:
 *
 *   - `exit`, `_Exit`, `sceKernelExit` and every shell-level kill are **absent**.
 *   - `_exit` is **present** and raises `SIGSYS`, because a `big-app` container's credentials do
 *     not permit syscall 1. The kernel logs `eboot.bin calls exit()` and then kills it.
 *   - returning from the entry point faults at `rip: 0x0`, because the dynamic linker transfers
 *     control with no caller frame to return into.
 *
 * So **no userland call terminates a big-app process**: lifecycle belongs to the shell. The
 * conforming pattern the resolution names is to say the last thing you have to say and then idle,
 * letting the harness close the app over JSON-RPC - which produces no coredump, no crash report
 * and no hung GPU ring, where a trap produces all three.
 *
 * A trap also costs the diagnostic. The log is the only account of why Mesa gave up, and dying
 * inside the logger's own process is the worst moment to do it. Parking keeps the line and hands
 * the ending to the thing that is allowed to perform it.
 *
 * `nanosleep` is used rather than oops-sdk's sleep because this file has no oops-sdk dependency
 * and `nanosleep` is exported, measured at `0x80000ece0` in `libkernel`.
 */
_Noreturn void exit(int status)
{
    oops_winsys_log("exit(%d) was called. This platform exports no exit and a big-app container "
                    "cannot terminate itself (obscene REQ-20260917T1450Z-2e71), so this title is "
                    "now idle and finished - close it from the host.", status);

    for (;;) {
        struct timespec second = { .tv_sec = 1, .tv_nsec = 0 };
        (void)nanosleep(&second, NULL);
    }
}

/*
 * The twelve loud ones.
 *
 * Each was checked for its caller before being written off, the way the six at the top of this
 * file were - not assumed to be unreachable because it looked obscure:
 *
 *   chown, unlink, sync, readlink, stat   Mesa's cache and shader-dump paths (`util/disk_cache*`,
 *                                         `util/os_file.c`). This build sets no cache directory,
 *                                         and a title has no writable filesystem to set one to.
 *   mkstemp, open_memstream               its debug dumps, which write a shader or a state log to
 *                                         a temporary file behind an option this build never sets.
 *   popen, pclose                         the disassembler hand-off, which shells out. There is
 *                                         no shell here and there is no process to start.
 *   openlog                               the syslog wrapper beside `syslog` itself, which is
 *                                         exported; only the setup call is missing.
 *   syscall, sysctlbyname                 the two generic escape hatches. Both are loud and
 *                                         neither is translated - see the note on each.
 *
 * All of them are reached only through code that tests the return, so failing is a path their
 * callers already have. Being reached is the news, and each says its own name so the log says
 * which one.
 */

int chown(const char *path, uid_t owner, gid_t group)
{
    (void)owner;
    (void)group;
    oops_winsys_log("chown(\"%s\") was called; a title has no filesystem to own.",
                    path ? path : "(null)");
    return -1;
}

int unlink(const char *path)
{
    oops_winsys_log("unlink(\"%s\") was called; nothing here created a file to remove.",
                    path ? path : "(null)");
    return -1;
}

void sync(void)
{
    oops_winsys_log("sync was called; there are no buffers here to flush to a disk.");
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)buf;
    (void)bufsiz;
    oops_winsys_log("readlink(\"%s\") was called; there are no symbolic links to read.",
                    path ? path : "(null)");
    return -1;
}

int stat(const char *path, struct stat *sb)
{
    (void)sb;
    oops_winsys_log("stat(\"%s\") was called; this shim has no filesystem to describe. Note the "
                    "census records this name as exported and the sweep did not, so if this line "
                    "appears the conflict in REQ-20260917T1818Z-9f41 is worth re-reading.",
                    path ? path : "(null)");
    return -1;
}

int mkstemp(char *template_name)
{
    oops_winsys_log("mkstemp(\"%s\") was called; a debug dump wants a temporary file and there "
                    "is no writable filesystem to make one in.",
                    template_name ? template_name : "(null)");
    return -1;
}

FILE *open_memstream(char **bufp, size_t *sizep)
{
    (void)bufp;
    (void)sizep;
    oops_winsys_log("open_memstream was called; no in-memory stream is provided here, and the "
                    "caller tests for null.");
    return NULL;
}

FILE *popen(const char *command, const char *type)
{
    (void)type;
    oops_winsys_log("popen(\"%s\") was called; there is no shell and no second process on this "
                    "platform. This is the disassembler hand-off and it cannot run here.",
                    command ? command : "(null)");
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    oops_winsys_log("pclose was called; popen never opened anything.");
    return -1;
}

void openlog(const char *ident, int logopt, int facility)
{
    (void)logopt;
    (void)facility;
    oops_winsys_log("openlog(\"%s\") was called; `syslog` itself is exported but its setup call "
                    "is not, so the identity is dropped and the messages still go out.",
                    ident ? ident : "(null)");
}

/*
 * The generic escape hatches, neither of which is translated into something that would work.
 *
 * `syscall` deliberately does not forward. Making a raw system call by number from here means
 * deciding which numbering this kernel uses, and the platform refuses to name its generation -
 * `kern.osrelease` reads `"0.0-prototype"` (D004, obSCEne `135-sysctl`). A wrong number is not a
 * failed call, it is a different call.
 *
 * `sysctlbyname` keeps the position the `sysctl` stub above it took, and the sweep has now
 * sharpened rather than settled it: the resolution reports the name existing in `libkernel` "as
 * an internal export stub" at `0x12260` while a dynamic lookup for it returns `0x0`. An address
 * that cannot be resolved by the mechanism a title actually uses is not an address a title can
 * call, so this stays a stub until `-9f41` says which reading holds.
 */
int syscall(int number, ...)
{
    oops_winsys_log("syscall(%d, ...) was called; this shim will not guess at a system-call "
                    "numbering the platform declines to name (D004). Failing instead.", number);
    return -1;
}

int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, const void *newp, size_t newlen)
{
    (void)oldp;
    (void)oldlenp;
    (void)newp;
    (void)newlen;
    oops_winsys_log("sysctlbyname(\"%s\") was called; the sweep found no address a title can "
                    "resolve for it, only an internal stub. Returning failure.",
                    name ? name : "(null)");
    return -1;
}

/*
 * ---------------------------------------------------------------------------------------------
 * The eleven that needed `-9f41`'s answer.
 *
 * These were left as imports while the export census and the dynamic sweep disagreed about them,
 * on the reasoning that a local definition of something describing the platform would be a
 * different answer rather than the same one. That reasoning was sound and the premise was wrong:
 * the census rows describe GEN=4, none of these binds natively, so the choice was never
 * "local or the platform's" - it was "local or a crash on first call".
 */

/*
 * The multibyte/single-byte boundary, read by the inlined `ctype` functions themselves
 * (`_ctype.h:106` and `:139` both test `_c >= __mb_sb_limit` before indexing the table).
 *
 * 256 is the C locale's value and this build has no other: `table.c`, staged from the same
 * checkout, sets `__mb_sb_limit` to 256 for `_DefaultRuneLocale` through
 * `__runes_for_locale`, which nothing here calls. Stating it directly is the same number by the
 * shorter route.
 */
int __mb_sb_limit = 256;

/*
 * Two placeholders for a function nobody calls.
 *
 * `librune.a` is one object, staged whole from upstream so that it stays identical to the table
 * it is supposed to be. That object also carries `__runes_for_locale`, which references these
 * two libc-private locale structures - and carving the function out would mean editing the file,
 * which is the one thing staging it was meant to avoid.
 *
 * **Nothing in this link calls it.** Measured rather than assumed: zero references to
 * `__runes_for_locale`, `__xlocale_C_locale` or `__xlocale_global_locale` across every Mesa
 * archive and every staged library. Mesa reaches the table through the inlined
 * `__getCurrentRuneLocale`, never through the locale API.
 *
 * So these exist to satisfy a relocation on a dead path, and their size and contents are
 * deliberately not a claim about `struct _xlocale`. If either is ever actually read, the locale
 * API has become live and this is wrong - replace both with the real structures rather than
 * enlarging these.
 */
void *__xlocale_C_locale[128];
void *__xlocale_global_locale[128];

/*
 * The wall clock, over a call that is exported and measured.
 *
 * `time` is absent; `clock_gettime` is present, at `0x800001210` in `libkernel` in the same
 * sweep. So this is a unit conversion onto a measured call rather than a stand-in for one, in
 * the way `usleep` above is.
 *
 * Mesa reaches it through its shader cache's timestamps and through `util/u_debug`'s elapsed
 * reporting. Seconds since the epoch is what both want and what `CLOCK_REALTIME` gives.
 */
time_t time(time_t *tloc)
{
    struct timespec now = { .tv_sec = 0, .tv_nsec = 0 };

    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        oops_winsys_log("time() could not read CLOCK_REALTIME; reporting 0 rather than a "
                        "plausible date.");
        now.tv_sec = 0;
    }
    if (tloc != NULL) {
        *tloc = now.tv_sec;
    }
    return now.tv_sec;
}

/*
 * Two signal-set operations, which are pure bit manipulation on a structure this build already
 * compiles against.
 *
 * `sigset_t` is `__uint32_t __bits[_SIG_WORDS]` (`sys/_sigset.h:50`), and the numbering is
 * one-based: signal *n* is bit *(n-1)*. Both functions are fully specified by that layout, so
 * they fall under the same rule as the IEEE predicates - one right answer, no side taken.
 *
 * The caveat is worth stating because D004 makes it real. The kernel reads this structure too,
 * and if its layout differs from the staged header's then these agree with Mesa and disagree with
 * the kernel. That risk is not introduced here: Mesa already compiles every `sigaction` and
 * `pthread_sigmask` call against this same header, so the layout is either right for both or
 * wrong for both, and these two functions cannot change which.
 */
int sigfillset(sigset_t *set)
{
    if (set == NULL) {
        return -1;
    }
    for (int i = 0; i < _SIG_WORDS; i++) {
        set->__bits[i] = 0xffffffffu;
    }
    return 0;
}

int sigdelset(sigset_t *set, int signo)
{
    if (set == NULL || signo <= 0 || signo > (int)(_SIG_WORDS * 32)) {
        return -1;
    }
    set->__bits[(signo - 1) / 32] &= ~(1u << (unsigned)((signo - 1) % 32));
    return 0;
}

/*
 * The directory walk, the device name and the program name: six loud ones, for the reason the
 * twelve above are loud.
 *
 * Each was checked for its caller. `opendir`/`readdir`/`closedir` are Mesa's shader cache
 * eviction and its descriptor walk (`util/disk_cache*`, `util/os_file.c`); this build sets no
 * cache directory and a title has no writable filesystem to set one to. `devname_r` is reached
 * from the same descriptor walk. `getprogname` names the process in a debug banner, and
 * `system` is the disassembler hand-off beside `popen`, which is equally impossible here.
 *
 * `getprogname` is the one that returns something rather than failing, because its contract has
 * no failure value - a caller uses the string. It gives the title's own identifier, which this
 * build already knows and which is the true answer to the question being asked.
 */
DIR *opendir(const char *name)
{
    oops_winsys_log("opendir(\"%s\") was called; a title has no filesystem to walk, and the "
                    "caller tests for null.", name ? name : "(null)");
    return NULL;
}

struct dirent *readdir(DIR *dirp)
{
    (void)dirp;
    oops_winsys_log("readdir was called; opendir never opened anything, so this is a caller that "
                    "did not check.");
    return NULL;
}

int closedir(DIR *dirp)
{
    (void)dirp;
    oops_winsys_log("closedir was called; nothing was ever opened.");
    return -1;
}

char *devname_r(dev_t dev, mode_t type, char *buf, int len)
{
    (void)dev;
    (void)type;
    (void)buf;
    (void)len;
    oops_winsys_log("devname_r was called; there is no device namespace to name from here.");
    return NULL;
}

/*
 * `OOPS_APP_NAME` comes from `app.mk` when this file is compiled with a title, which is how it is
 * compiled for real. It is guarded because that is not the only way it gets compiled:
 * `tools/what-is-still-needed.sh` builds these sources on their own to ask which symbols they
 * answer, and a shim that only compiles inside a title makes that tool report a false gap.
 */
#ifndef OOPS_APP_NAME
#define OOPS_APP_NAME "oops-mesa"
#endif

const char *getprogname(void)
{
    /* Not a stub: this is the program's name, and the build knows it. */
    return OOPS_APP_NAME;
}

int system(const char *command)
{
    oops_winsys_log("system(\"%s\") was called; there is no shell and no second process on this "
                    "platform.", command ? command : "(null)");
    return -1;
}

/*
 * ---------------------------------------------------------------------------------------------
 * The last fourteen.
 *
 * `tools/what-is-still-needed.sh`, once it stopped believing the GEN=4 census, could name exactly
 * which imports had never been measured natively: 34 of them. obSCEne swept all 34
 * (REQ-20260917T2045Z-a4f2, sweep `20260917-220500`) and 20 resolve. These are the other 14.
 *
 * After this there is no symbol a Mesa title imports whose status on this platform is unknown.
 *
 * **Four of them were in the list I claimed the hardware had already proved.** The request said
 * `memcpy`, `memset`, `memcmp`, `__cxa_atexit` and the two guards "cannot not be bound", because
 * `MESA00001` answered 35 ioctls and created a screen. That was an inference, not a measurement,
 * and it was wrong for four of the six: `memcpy` and `memset` resolve, `memcmp` and the three C++
 * ABI entries do not. The run survived because clang inlines most small `memcmp` calls and
 * because nothing had yet reached a static-local initialiser. Being right about the conclusion
 * would not have made the reasoning sound.
 *
 * Which are live was checked the same way as before, by reading who references each:
 *
 *   isatty          the GLSL lexer. flex asks whether its input is a terminal, on every compile
 *   __cxa_atexit    the GLSL built-in function table (in `cxx_support.cpp`)
 *   memcmp          the draw module's vertex paths
 *   guards          the ASTC lookup tables (in `cxx_support.cpp`)
 *
 * and the rest are on paths this build does not execute: SPIR-V's error recovery, the HUD, and
 * an OES fixed-point query.
 */

/*
 * Whether the C library should take its threaded paths. `stdio.h` reads it directly from macros -
 * `#define feof(p) (!__isthreaded ? __sfeof(p) : (feof)(p))` at `stdio.h:512` and its kin - so
 * the value chooses between touching `FILE` internals inline and calling the library's own
 * function.
 *
 * 1 is both what the resolution recommends and what is true: Mesa runs its compiler and its
 * driver on many threads. It also routes those macros to `ferror`, `fileno`, `clearerr` and
 * `getc`, all of which the same sweep found present, rather than to inline code reaching into a
 * `FILE` this build did not lay out.
 */
int __isthreaded = 1;

/*
 * `memcmp` is **not** here, and the reason is a gap in the instrument rather than in the platform.
 *
 * The sweep found it absent, which is correct, and `what-is-still-needed.sh` listed it as
 * unprovided, which was not: oops-sdk's `src/system/freestd.c` has defined it all along, and
 * every title links that file. Defining it here produced a duplicate-symbol error, which is how
 * this was found and is the good kind of failure.
 *
 * The tool's "answered by the shims" pile is built by compiling **oops-mesa's** `src/runtime` and
 * `src/winsys` only. A title links more than that - oops-sdk's freestanding helpers among them -
 * so the tool over-reports the work list by whatever oops-sdk already provides. That is the safe
 * direction to be wrong in, unlike the GEN=4 census it was fixed for in worklog 046, but it is
 * still wrong and it is now fixed there too.
 */

/* `strcpy` that returns the end rather than the start. Not exported, though `strcpy` - itself
 * defined above for the same reason - and `strncpy` are. */
__attribute__((no_builtin("stpcpy")))
char *stpcpy(char *dst, const char *src)
{
    while ((*dst = *src++) != '\0') {
        dst++;
    }
    return dst;
}

/*
 * Whether a descriptor is a terminal. Not exported, and **on a live path**: the GLSL lexer is
 * flex-generated, and flex asks this about its input on every compile.
 *
 * 0 is the true answer rather than a refusal. Nothing here is a terminal - the descriptors a
 * title has are not connected to anything, which the same programme of sweeps established for
 * stdout and stderr (`REQ-20260917T0233Z-5c9d`). A flex scanner that is told "not a terminal"
 * reads its buffer, which is what it should do.
 */
int isatty(int fd)
{
    (void)fd;
    return 0;
}

/* `strtod` with the error reporting thrown away, which is its whole definition. `strtod` is
 * defined above, over the platform's own `sscanf`, so this inherits that conversion. Reached from
 * Mesa's HUD, which no environment here can switch on. */
double atof(const char *nptr)
{
    return strtod(nptr, NULL);
}

/*
 * IEEE-754 single-precision classification. Not exported, and fully specified - the same rule the
 * `__isfinite` family above was defined under, and the same one-right-answer property.
 *
 * The return values are `math.h`'s own (`FP_INFINITE` 1, `FP_NAN` 2, `FP_NORMAL` 4,
 * `FP_SUBNORMAL` 8, `FP_ZERO` 16), stated here rather than included because this file pulls in no
 * `math.h` - the same arrangement `__isinff` above uses and for the same reason.
 */
int __fpclassifyf(float f);
int __fpclassifyf(float f)
{
    union {
        float f;
        unsigned int u;
    } v = { .f = f };
    unsigned int exponent = (v.u >> 23) & 0xffu;
    unsigned int mantissa = v.u & 0x7fffffu;

    if (exponent == 0xffu) {
        return mantissa ? 2 : 1;            /* FP_NAN : FP_INFINITE */
    }
    if (exponent == 0u) {
        return mantissa ? 8 : 16;           /* FP_SUBNORMAL : FP_ZERO */
    }
    return 4;                               /* FP_NORMAL */
}

/*
 * Signal disposition, shared-memory objects, and non-local jumps: the four that are not reached.
 *
 * `sigaction` and `atof` both come from Mesa's HUD, which is switched on by an environment
 * variable and this platform has no environment. `shm_open` is `util/anon_file.c`, which wants an
 * anonymous file to back a shared buffer. And `setjmp`/`longjmp` are SPIR-V's error recovery -
 * `spirv_to_nir.c` and `gl_spirv.c` - which a GL 3.3 title reaches only through `glShaderBinary`.
 *
 * # Why `longjmp` traps rather than failing quietly
 *
 * `setjmp` returning 0 is honest: 0 is what it returns on the direct call, and this one genuinely
 * has nothing to restore later. `longjmp` is the other half and it cannot be honest in the same
 * way - it is declared not to return, and there is no saved context to jump to, so every
 * available behaviour is wrong except stopping.
 *
 * Writing the real pair was considered and rejected. It is twenty instructions of x86-64 assembly
 * per side, and it would be twenty instructions nothing in this build executes, protecting a path
 * that would need a working SPIR-V front end before it mattered. A trap that names the situation
 * is worth more than a saved register set nobody restores; if SPIR-V is ever wanted, this is
 * where the work goes and the log line says so.
 */
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    (void)act;
    (void)oact;
    oops_winsys_log("sigaction(%d) was called; this platform does not export it and a title does "
                    "not own signal disposition. Returning failure.", sig);
    return -1;
}

int shm_open(const char *path, int flags, mode_t mode)
{
    (void)flags;
    (void)mode;
    oops_winsys_log("shm_open(\"%s\") was called; there are no shared memory objects here, and "
                    "the caller tests the descriptor.", path ? path : "(null)");
    return -1;
}

int setjmp(jmp_buf env)
{
    (void)env;
    oops_winsys_log("setjmp was called - this is SPIR-V's error recovery, which this build does "
                    "not implement. Returning 0 as a direct call; a longjmp to it will stop.");
    return 0;
}

void longjmp(jmp_buf env, int val)
{
    (void)env;
    oops_winsys_log("longjmp(%d) was called, and there is no saved context to return to: this "
                    "platform exports neither half of the pair and oops-mesa implements only "
                    "setjmp's direct-call return. SPIR-V has hit an error it wanted to unwind "
                    "from. Stopping here rather than jumping somewhere invented.", val);
    __builtin_trap();
}
