/*
 * The thread surface Mesa needs, mapped onto the vendor's.
 *
 * Mesa touches `pthread_*` from exactly one file, its C11 threads layer, so this
 * surface is closed at 26 functions. Which 26, and what evidence stands behind each
 * vendor twin, is `docs/hardware/mesa-thread-surface-fw1240.md`; this file is that
 * table turned into code.
 *
 * # Why the mapping is nearly one to one
 *
 * The platform's C library is FreeBSD-derived and every one of its thread types is a
 * pointer: `pthread_t`, `pthread_mutex_t`, `pthread_cond_t` and `pthread_mutexattr_t`
 * are all pointers to opaque structures, and the vendor calls take pointer-sized
 * handles. So the handles pass through unchanged and nothing here allocates, copies or
 * wraps them. Two exceptions are handled below: `pthread_key_t` is an `int`, and
 * `pthread_once_t` is a structure rather than a pointer.
 *
 * # Two conventions that do not line up, and what is done about it
 *
 * **Arity.** Three vendor twins take a trailing name the POSIX form has no place for:
 * create, mutex init and cond init. A shim that delegated by a rule rather than per
 * name would read a register the caller never set. orbistoun hit exactly that and
 * recorded it (its D385 and D475), and oops-sdk's own declarations already carry the
 * right arity. Each name here is written out.
 *
 * **Failure.** POSIX answers zero or an errno; the vendor answers `0x8002_0000 |
 * errno`, a scheme measured across five families and seven provoked failures (orbistoun
 * D398, and obSCEne's `posixerr` section relies on it). So a failure can be translated
 * rather than guessed, which matters most for `pthread_cond_timedwait`: Mesa's C11
 * layer turns `ETIMEDOUT` into a timeout and anything else into an error, so a timeout
 * reported as a generic failure would look like a broken condition variable.
 */

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * The vendor thread API, by published name.
 *
 * The first group is bound and working in oops-sdk today, and these declarations match
 * its `src/thread/thread.c` exactly rather than being re-derived. The second group is
 * named `present` in obSCEne's import census or in orbistoun's libkernel inventory but
 * has never been called from this collection, so its arity follows the POSIX form it
 * mirrors and is marked as the assumption it is. `REQ-20260914T1443Z-3ea7` on the
 * obSCEne bus is the measurement that would settle the whole group, including whether
 * the portable names bind directly and make this file unnecessary.
 */

/* Bound and working in oops-sdk. */
__attribute__((weak)) int scePthreadCreate(void *thread, const void *attr,
                                           void *(*entry)(void *), void *arg,
                                           const char *name);
__attribute__((weak)) int scePthreadJoin(void *thread, void **value);
__attribute__((weak)) int scePthreadDetach(void *thread);
/* The attribute trio, for the stack size `pthread_create` below has to ask for. Same
 * group and same arity as the rest: `oops-sdk/src/thread/thread.c` declares and calls
 * all three, and its titles run on hardware, so these are bound rather than assumed.
 * The build's own import manifest agrees - `build/mesa-imports.txt` attributes
 * `scePthreadAttrInit`, `scePthreadAttrSetstacksize` and `scePthreadAttrDestroy` to
 * `libkernel`. */
__attribute__((weak)) int scePthreadAttrInit(void *attr);
__attribute__((weak)) int scePthreadAttrSetstacksize(void *attr, size_t stacksize);
__attribute__((weak)) int scePthreadAttrDestroy(void *attr);
__attribute__((weak)) void *scePthreadSelf(void);
__attribute__((weak)) int scePthreadEqual(void *a, void *b);
__attribute__((weak)) int scePthreadMutexInit(void *mutex, const void *attr,
                                              const char *name);
__attribute__((weak)) int scePthreadMutexLock(void *mutex);
__attribute__((weak)) int scePthreadMutexTrylock(void *mutex);
__attribute__((weak)) int scePthreadMutexUnlock(void *mutex);
__attribute__((weak)) int scePthreadMutexDestroy(void *mutex);
__attribute__((weak)) int scePthreadCondInit(void *cond, const void *attr,
                                             const char *name);
__attribute__((weak)) int scePthreadCondWait(void *cond, void *mutex);
__attribute__((weak)) int scePthreadCondTimedwait(void *cond, void *mutex,
                                                  unsigned int usec);
__attribute__((weak)) int scePthreadCondSignal(void *cond);
__attribute__((weak)) int scePthreadCondBroadcast(void *cond);
__attribute__((weak)) int scePthreadCondDestroy(void *cond);

/* Named present, never called from here. Arity assumed from the POSIX form. */
__attribute__((weak)) void scePthreadExit(void *value);
__attribute__((weak)) int scePthreadKeyCreate(int *key, void (*destructor)(void *));
__attribute__((weak)) int scePthreadKeyDelete(int key);
__attribute__((weak)) void *scePthreadGetspecific(int key);
__attribute__((weak)) int scePthreadSetspecific(int key, const void *value);
__attribute__((weak)) int scePthreadMutexTimedlock(void *mutex, unsigned int usec);
__attribute__((weak)) int scePthreadMutexattrInit(void *attr);
__attribute__((weak)) int scePthreadMutexattrDestroy(void *attr);
__attribute__((weak)) int scePthreadMutexattrSettype(void *attr, int type);

/* Every object this shim creates carries a name, because the vendor calls take one and
 * the platform's own tools show it. One name for all of them would make a thread list
 * useless, so each kind gets its own. */
static const char k_thread_name[] = "oops-mesa";
static const char k_mutex_name[] = "oops-mesa-mtx";
static const char k_cond_name[] = "oops-mesa-cnd";

/*
 * Translate a vendor return into an errno.
 *
 * Zero means success in both conventions, which is the only part that coincides. A
 * vendor failure carries the errno in its low bits under 0x8002_0000. Anything that
 * does not match that shape is reported as EINVAL rather than as a number invented
 * here: a caller switching on specific errno values then falls to its default branch
 * instead of matching the wrong case.
 */
static int to_errno(int rc) {
    if (rc == 0) {
        return 0;
    }
    if (((unsigned)rc & 0xFFFF0000u) == 0x80020000u) {
        return (int)((unsigned)rc & 0xFFFFu);
    }
    return EINVAL;
}

/* Nothing here can work if the platform did not bind the call. Reporting ENOSYS is
 * honest and makes the first use fail with a reason rather than dereferencing a null.
 */
#define NEED(fn)                                                                       \
    do {                                                                               \
        if (!(fn))                                                                     \
            return ENOSYS;                                                             \
    } while (0)

/* --- threads -------------------------------------------------------------------------
 */

/*
 * The stack Mesa's threads need, which is not the one this platform gives them.
 *
 * Mesa's C11 threads layer calls `pthread_create(thr, NULL, ...)` - a **null
 * attribute**, every time, from `mesa/src/c11/impl/threads_posix.c:255`. On a Linux
 * host that means glibc's default of 8 MB. Here it meant the vendor's default, and the
 * vendor's default is small enough that Mesa's shader compiler runs off the end of it.
 *
 * That is measured, not reasoned: on 2026-09-20 `DRIP00001` compiled and linked a GLSL
 * 330 program and took a SIGSEGV on thread `dri-probe:gl0`, fault address `0x7eed80fa8`
 * against `rsp 0x7eed80fb0` - the fault is exactly `rsp - 8`, which is a `call` pushing
 * its return address onto a page that is not there. Not a bad pointer: a stack that
 * ended. The backtrace, resolved against the link map, is `impl_thrd_routine` ->
 * `util_queue_thread_func` -> `glthread_unmarshal_batch` ->
 * `_mesa_unmarshal_LinkProgram` -> `st_link_shader` -> `gl_nir_link_glsl` ->
 * `gl_nir_link_varyings` -> `nir_opt_varyings_bulk` -> `nir_opt_varyings`, which
 * faulted 0x49 bytes in. Ten frames had consumed about 72 KB (`rbp - rsp` = 0x119b0),
 * and the pass that died allocates its own large structure on the heap
 * (`MALLOC_STRUCT(linkage_info)`)
 * - so this is ordinary compiler frame depth against a stack far too small for it, not
 * one runaway frame. See the worklog entry and `docs/hardware/` for the full capture.
 *
 * 8 MB is chosen because it is what upstream Mesa is written and tested against, not
 * because 72 KB needed rounding up. Sizing this to the failure that was seen would
 * leave the next, larger shader to find the new edge on the console; matching the host
 * removes the whole class. The cost is address space rather than memory - these are
 * demand-paged reservations, and only the pages a thread touches are ever committed.
 *
 * This belongs here and not in a Mesa patch (principle 1): Mesa asking for a default
 * stack is correct, and what a default stack *is* on this platform is precisely what
 * the runtime shim exists to answer.
 */
#define MESA_THREAD_STACK_BYTES ((size_t)8u * 1024u * 1024u)

/* One line if the bigger stack could not be asked for, said once. Falling back to the
 * vendor default is not a failure this function can refuse - a thread that is not
 * created is worse than a thread that might overflow - but it is the cause of a crash
 * that would otherwise look unexplained, so it does not pass in silence (principle 4).
 */
extern void oops_winsys_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg) {
    NEED(scePthreadCreate);

    /* A caller that brought its own attribute keeps it. Mesa never does today; if some
     * future caller does, its choice is deliberate and outranks the default set below.
     */
    if (attr != NULL) {
        return to_errno(scePthreadCreate(thread, attr, start, arg, k_thread_name));
    }

    /*
     * The attribute is a pointer to an opaque structure the vendor allocates, so what
     * is held here is room for that pointer and the address of *that* is what the calls
     * take. The buffer is oversized on purpose and zeroed first, which is exactly what
     * oops-sdk's `oops_thread_create` does with the same three calls - this mirrors a
     * working caller rather than inventing a second convention.
     */
    char attr_buf[128];
    void *attr_ptr = NULL;
    bool sized = false;

    if (scePthreadAttrInit != NULL) {
        for (size_t i = 0; i < sizeof attr_buf; i++) {
            attr_buf[i] = 0;
        }
        if (scePthreadAttrInit(attr_buf) == 0) {
            attr_ptr = attr_buf;
            if (scePthreadAttrSetstacksize != NULL &&
                scePthreadAttrSetstacksize(attr_ptr, MESA_THREAD_STACK_BYTES) == 0) {
                sized = true;
            }
        }
    }

    if (!sized) {
        static bool said = false;
        if (!said) {
            said = true;
            oops_winsys_log(
                "pthread_create: could not set a %zu-byte stack; Mesa's threads take "
                "the vendor default, which its shader compiler has been measured to "
                "overflow. Said once, not per thread.",
                MESA_THREAD_STACK_BYTES);
        }
    }

    int rc = scePthreadCreate(thread, attr_ptr, start, arg, k_thread_name);

    /* Destroyed either way: the attribute is the vendor's allocation and the thread has
     * its own copy of what it needed by the time create returns. */
    if (attr_ptr != NULL && scePthreadAttrDestroy != NULL) {
        scePthreadAttrDestroy(attr_ptr);
    }

    return to_errno(rc);
}

int pthread_join(pthread_t thread, void **value) {
    NEED(scePthreadJoin);
    return to_errno(scePthreadJoin(thread, value));
}

int pthread_detach(pthread_t thread) {
    NEED(scePthreadDetach);
    return to_errno(scePthreadDetach(thread));
}

pthread_t pthread_self(void) {
    return scePthreadSelf ? (pthread_t)scePthreadSelf() : (pthread_t)0;
}

int pthread_equal(pthread_t a, pthread_t b) {
    /* This one answers a question rather than reporting a status, so it does not go
     * through to_errno: a non-zero result means equal. Falling back to a pointer
     * comparison when the vendor call is absent is safe, because the handles are
     * pointers. */
    return scePthreadEqual ? scePthreadEqual(a, b) : (a == b);
}

void pthread_exit(void *value) {
    if (scePthreadExit) {
        scePthreadExit(value);
    }
    /* The vendor call does not return. If it was not bound there is nothing sensible
     * left to do and returning would resume a thread that asked to stop, so this spins
     * deliberately rather than pretending the exit happened. */
    for (;;) {
    }
}

/* --- mutexes -------------------------------------------------------------------------
 */

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    NEED(scePthreadMutexInit);
    return to_errno(scePthreadMutexInit(mutex, attr, k_mutex_name));
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    NEED(scePthreadMutexDestroy);
    return to_errno(scePthreadMutexDestroy(mutex));
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    NEED(scePthreadMutexLock);
    return to_errno(scePthreadMutexLock(mutex));
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    NEED(scePthreadMutexTrylock);
    return to_errno(scePthreadMutexTrylock(mutex));
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    NEED(scePthreadMutexUnlock);
    return to_errno(scePthreadMutexUnlock(mutex));
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    NEED(scePthreadMutexattrInit);
    return to_errno(scePthreadMutexattrInit(attr));
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    NEED(scePthreadMutexattrDestroy);
    return to_errno(scePthreadMutexattrDestroy(attr));
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    NEED(scePthreadMutexattrSettype);
    return to_errno(scePthreadMutexattrSettype(attr, type));
}

/* --- condition variables -------------------------------------------------------------
 */

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    NEED(scePthreadCondInit);
    return to_errno(scePthreadCondInit(cond, attr, k_cond_name));
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    NEED(scePthreadCondDestroy);
    return to_errno(scePthreadCondDestroy(cond));
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    NEED(scePthreadCondWait);
    return to_errno(scePthreadCondWait(cond, mutex));
}

int pthread_cond_signal(pthread_cond_t *cond) {
    NEED(scePthreadCondSignal);
    return to_errno(scePthreadCondSignal(cond));
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    NEED(scePthreadCondBroadcast);
    return to_errno(scePthreadCondBroadcast(cond));
}

/*
 * The one call whose failure value is load-bearing.
 *
 * POSIX takes an absolute deadline; the vendor call takes a relative timeout in
 * microseconds. So the deadline is converted against the clock, and a deadline already
 * past becomes a zero wait rather than a negative one, which would otherwise become an
 * enormous unsigned timeout.
 */
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *deadline) {
    struct timespec now;
    int64_t usec;

    NEED(scePthreadCondTimedwait);

    if (!deadline) {
        return EINVAL;
    }
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return EINVAL;
    }

    usec = ((int64_t)deadline->tv_sec - (int64_t)now.tv_sec) * 1000000 +
           ((int64_t)deadline->tv_nsec - (int64_t)now.tv_nsec) / 1000;
    if (usec < 0) {
        usec = 0;
    }
    if (usec > (int64_t)UINT32_MAX) {
        usec = (int64_t)UINT32_MAX;
    }

    return to_errno(scePthreadCondTimedwait(cond, mutex, (unsigned int)usec));
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) {
    struct timespec now;
    int64_t usec;

    NEED(scePthreadMutexTimedlock);

    if (!deadline) {
        return EINVAL;
    }
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return EINVAL;
    }
    usec = ((int64_t)deadline->tv_sec - (int64_t)now.tv_sec) * 1000000 +
           ((int64_t)deadline->tv_nsec - (int64_t)now.tv_nsec) / 1000;
    if (usec < 0) {
        usec = 0;
    }
    if (usec > (int64_t)UINT32_MAX) {
        usec = (int64_t)UINT32_MAX;
    }
    return to_errno(scePthreadMutexTimedlock(mutex, (unsigned int)usec));
}

/* --- thread-local storage ------------------------------------------------------------
 */

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    NEED(scePthreadKeyCreate);
    return to_errno(scePthreadKeyCreate(key, destructor));
}

int pthread_key_delete(pthread_key_t key) {
    NEED(scePthreadKeyDelete);
    return to_errno(scePthreadKeyDelete(key));
}

void *pthread_getspecific(pthread_key_t key) {
    return scePthreadGetspecific ? scePthreadGetspecific(key) : NULL;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    NEED(scePthreadSetspecific);
    return to_errno(scePthreadSetspecific(key, value));
}

/* --- once ----------------------------------------------------------------------------
 */

/*
 * `pthread_once_t` is a structure here rather than a pointer, so unlike everything else
 * in this file it cannot be handed to the vendor call: the two layouts are not the same
 * object and passing one as the other would corrupt whatever sits after it.
 *
 * So this is the one thing implemented rather than delegated, over a single mutex. That
 * is heavier than the platform's own version and it is correct, which is the right
 * trade for a call that runs once per site for the life of the process.
 */
static pthread_mutex_t s_once_lock;
static bool s_once_lock_ready;

int pthread_once(pthread_once_t *once, void (*routine)(void)) {
    if (!once || !routine) {
        return EINVAL;
    }

    if (!s_once_lock_ready) {
        /* A race here would be a thread calling pthread_once before any other thread
         * exists, which is when Mesa does its first initialisation. If that assumption
         * ever stops holding, this needs the platform's own atomic initialisation and a
         * request to go with it. */
        int rc = pthread_mutex_init(&s_once_lock, NULL);
        if (rc != 0) {
            return rc;
        }
        s_once_lock_ready = true;
    }

    pthread_mutex_lock(&s_once_lock);
    if (once->state == 0) {
        once->state = 1;
        pthread_mutex_unlock(&s_once_lock);
        routine();
        return 0;
    }
    pthread_mutex_unlock(&s_once_lock);
    return 0;
}

/* --- scheduling ----------------------------------------------------------------------
 */

int sched_yield(void) {
    extern void scePthreadYield(void) __attribute__((weak));
    if (scePthreadYield) {
        scePthreadYield();
    }
    return 0;
}

/* --- the families beyond Mesa's C11 layer --------------------------------------------
 * *
 *
 * `docs/hardware/mesa-thread-surface-fw1240.md` is the surface Mesa's C11 threads layer
 * needs, and it is closed at 26. These are the rest: read-write locks, barriers,
 * condition-variable attributes and two odds, referenced from Mesa outside that layer
 * and reported missing by `tools/what-is-still-needed.sh` once every archive was built.
 *
 * Evidence is thinner here and it is worth being exact about which. obSCEne's census
 * records `scePthreadRwlockRdlock`, `scePthreadRwlockWrlock`, the `scePthreadCondattr*`
 * family and `scePthreadGetcpuclockid` as present. It does not record the rwlock init,
 * destroy and unlock calls, nor any of the barrier family - while it does record
 * `scePthreadRwlockattr*` and `scePthreadBarrierattr*`, which is the same shape of hole
 * the census showed for `clock_gettime`: the attribute functions are there and the
 * operations they configure are not, which is a gap in the mine rather than a platform
 * without barriers.
 *
 * So every twin below is declared weakly, as the ones above are. One that the platform
 * does not export resolves to nothing and its wrapper returns ENOSYS at the first call,
 * with a name attached, rather than the whole shim failing to link over a function Mesa
 * may never reach. `REQ-20260914T1743Z-2e08` asks for the whole group by name.
 */

__attribute__((weak)) int scePthreadRwlockInit(void *lock, const void *attr,
                                               const char *name);
__attribute__((weak)) int scePthreadRwlockDestroy(void *lock);
__attribute__((weak)) int scePthreadRwlockRdlock(void *lock);
__attribute__((weak)) int scePthreadRwlockWrlock(void *lock);
__attribute__((weak)) int scePthreadRwlockUnlock(void *lock);
__attribute__((weak)) int scePthreadBarrierInit(void *barrier, const void *attr,
                                                unsigned int count, const char *name);
__attribute__((weak)) int scePthreadBarrierDestroy(void *barrier);
__attribute__((weak)) int scePthreadBarrierWait(void *barrier);
__attribute__((weak)) int scePthreadCondattrInit(void *attr);
__attribute__((weak)) int scePthreadCondattrDestroy(void *attr);
__attribute__((weak)) int scePthreadCondattrSetclock(void *attr, int clock);
__attribute__((weak)) int scePthreadGetcpuclockid(void *thread, int *clock);

static const char k_rwlock_name[] = "oops-mesa-rwl";
static const char k_barrier_name[] = "oops-mesa-bar";

int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr) {
    NEED(scePthreadRwlockInit);
    return to_errno(scePthreadRwlockInit(lock, attr, k_rwlock_name));
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock) {
    NEED(scePthreadRwlockDestroy);
    return to_errno(scePthreadRwlockDestroy(lock));
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock) {
    NEED(scePthreadRwlockRdlock);
    return to_errno(scePthreadRwlockRdlock(lock));
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock) {
    NEED(scePthreadRwlockWrlock);
    return to_errno(scePthreadRwlockWrlock(lock));
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock) {
    NEED(scePthreadRwlockUnlock);
    return to_errno(scePthreadRwlockUnlock(lock));
}

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr,
                         unsigned count) {
    NEED(scePthreadBarrierInit);
    return to_errno(scePthreadBarrierInit(barrier, attr, count, k_barrier_name));
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {
    NEED(scePthreadBarrierDestroy);
    return to_errno(scePthreadBarrierDestroy(barrier));
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
    NEED(scePthreadBarrierWait);
    return to_errno(scePthreadBarrierWait(barrier));
}

int pthread_condattr_init(pthread_condattr_t *attr) {
    NEED(scePthreadCondattrInit);
    return to_errno(scePthreadCondattrInit(attr));
}

int pthread_condattr_destroy(pthread_condattr_t *attr) {
    NEED(scePthreadCondattrDestroy);
    return to_errno(scePthreadCondattrDestroy(attr));
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock) {
    NEED(scePthreadCondattrSetclock);
    return to_errno(scePthreadCondattrSetclock(attr, (int)clock));
}

int pthread_getcpuclockid(pthread_t thread, clockid_t *clock) {
    int id = 0;
    int rc;

    NEED(scePthreadGetcpuclockid);
    rc = to_errno(scePthreadGetcpuclockid(thread, &id));
    if (rc == 0 && clock) {
        *clock = (clockid_t)id;
    }
    return rc;
}
