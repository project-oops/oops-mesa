/*
 * The `pthread_*` surface Mesa needs, mapped onto the vendor thread API. The surface
 * and the evidence for each vendor twin are in
 * `docs/hardware/mesa-thread-surface-fw1240.md`.
 *
 * The platform's thread types are pointers to opaque structures, so handles pass
 * through unchanged; `pthread_key_t` is an `int` and `pthread_once_t` is a structure.
 * Create, mutex init and cond init take a trailing name the POSIX form lacks, so each
 * call is written out. The vendor reports failure as `0x8002_0000 | errno`
 * (orbistoun#D398), which `to_errno` translates; Mesa's C11 layer needs `ETIMEDOUT`
 * from a timed wait to tell a timeout from an error.
 */

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * The vendor thread API, by published name. The first group matches the declarations in
 * oops-sdk `src/thread/thread.c`, which calls them on hardware.
 */
__attribute__((weak)) int scePthreadCreate(void *thread, const void *attr,
                                           void *(*entry)(void *), void *arg,
                                           const char *name);
__attribute__((weak)) int scePthreadJoin(void *thread, void **value);
__attribute__((weak)) int scePthreadDetach(void *thread);
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

/* Exported per obSCEne's import census; arity follows the POSIX form they mirror. */
__attribute__((weak)) void scePthreadExit(void *value);
__attribute__((weak)) int scePthreadKeyCreate(int *key, void (*destructor)(void *));
__attribute__((weak)) int scePthreadKeyDelete(int key);
__attribute__((weak)) void *scePthreadGetspecific(int key);
__attribute__((weak)) int scePthreadSetspecific(int key, const void *value);
__attribute__((weak)) int scePthreadMutexTimedlock(void *mutex, unsigned int usec);
__attribute__((weak)) int scePthreadMutexattrInit(void *attr);
__attribute__((weak)) int scePthreadMutexattrDestroy(void *attr);
__attribute__((weak)) int scePthreadMutexattrSettype(void *attr, int type);

/* The names the vendor calls take, shown by the platform's tools; one per kind. */
static const char k_thread_name[] = "oops-mesa";
static const char k_mutex_name[] = "oops-mesa-mtx";
static const char k_cond_name[] = "oops-mesa-cnd";

/*
 * Translates a vendor return into an errno. A value outside the `0x8002_0000 | errno`
 * shape becomes EINVAL, so a caller switching on errno takes its default branch.
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

/* An unbound vendor call fails with ENOSYS rather than a null call. */
#define NEED(fn)                                                                       \
    do {                                                                               \
        if (!(fn))                                                                     \
            return ENOSYS;                                                             \
    } while (0)

/*
 * The stack for a thread created without an attribute, which is every Mesa thread
 * (`mesa/src/c11/impl/threads_posix.c:255`). The vendor default is too small for the
 * GLSL linker's frame depth; 8 MB matches the glibc default upstream Mesa is tested
 * against. The reservation is demand-paged, so the cost is address space.
 */
#define MESA_THREAD_STACK_BYTES ((size_t)8u * 1024u * 1024u)

/* Reports, once, a thread created on the vendor default stack. */
extern void oops_winsys_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg) {
    NEED(scePthreadCreate);

    /* A caller's own attribute outranks the default stack below. */
    if (attr != NULL) {
        return to_errno(scePthreadCreate(thread, attr, start, arg, k_thread_name));
    }

    /*
     * The vendor attribute is a pointer to an opaque structure it allocates; the calls
     * take the address of that pointer. Oversized and zeroed, as oops-sdk's
     * `oops_thread_create` does.
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

    /* The thread holds its own copy of the attribute once create returns. */
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
    /* Not a status, so no to_errno. The handles are pointers, so comparing them is a
     * valid fallback. */
    return scePthreadEqual ? scePthreadEqual(a, b) : (a == b);
}

void pthread_exit(void *value) {
    if (scePthreadExit) {
        scePthreadExit(value);
    }
    /* pthread_exit must not return, so an unbound vendor call spins. */
    for (;;) {
    }
}

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
 * POSIX takes an absolute deadline; the vendor call takes a relative timeout in
 * microseconds. A past deadline becomes a zero wait, not a huge unsigned one.
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

/*
 * `pthread_once_t` is a structure whose layout differs from the vendor's, so this is
 * implemented over one mutex rather than delegated.
 */
static pthread_mutex_t s_once_lock;
static bool s_once_lock_ready;

int pthread_once(pthread_once_t *once, void (*routine)(void)) {
    if (!once || !routine) {
        return EINVAL;
    }

    if (!s_once_lock_ready) {
        /* Unsynchronised: the first pthread_once runs before Mesa starts any thread. */
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

int sched_yield(void) {
    extern void scePthreadYield(void) __attribute__((weak));
    if (scePthreadYield) {
        scePthreadYield();
    }
    return 0;
}

/*
 * Read-write locks, barriers, condition-variable attributes and CPU clocks, which Mesa
 * references outside its C11 threads layer. obSCEne's census records the rwlock lock
 * calls, the condattr family and getcpuclockid, but not the rwlock init, destroy and
 * unlock calls or the barrier family. Weak like the rest, so an unexported one fails
 * with ENOSYS at its first call.
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
