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
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
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
