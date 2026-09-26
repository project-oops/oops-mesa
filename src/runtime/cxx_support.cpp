/*
 * The parts of libc++ that Mesa reaches, and nothing else (D006). The platform's own
 * C++ library has a different ABI, so nothing compiled against libc++'s headers links
 * against it.
 *
 * The definitions are ordinary C++ against the headers Mesa compiles against, so the
 * mangled names come out right by construction. `tools/what-is-still-needed.sh` lists
 * any reference that nothing answers.
 */

#include <__config>

#include <mutex>
#include <new>
#include <stdexcept>

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * C++ allocation over the platform's C allocator. The throwing forms return null on
 * failure because Mesa is compiled with exceptions off; Mesa's allocation wrappers
 * check for null.
 */
void *operator new(__SIZE_TYPE__ size) {
    return malloc(size ? size : 1);
}
void *operator new[](__SIZE_TYPE__ size) {
    return malloc(size ? size : 1);
}
void operator delete(void *p) noexcept {
    free(p);
}
void operator delete[](void *p) noexcept {
    free(p);
}
void operator delete(void *p, __SIZE_TYPE__) noexcept {
    free(p);
}
void operator delete[](void *p, __SIZE_TYPE__) noexcept {
    free(p);
}

/*
 * The `nothrow` form (`_ZnwmRKSt9nothrow_t`), which the platform does not export.
 * Returning null on failure is its contract.
 */
namespace std {
struct nothrow_t;
}
void *operator new(__SIZE_TYPE__ size, const std::nothrow_t &) noexcept {
    return malloc(size ? size : 1);
}

_LIBCPP_BEGIN_NAMESPACE_STD

/*
 * std::mutex over pthreads. libc++ stores a `__libcpp_mutex_t` in the object, and the
 * headers this is compiled against fix its layout.
 */
void mutex::lock() {
    int rc = pthread_mutex_lock(&__m_);
    if (rc) {
        /*
         * libc++ throws `system_error` here; exceptions are off. A failed lock is not
         * recoverable, so abort with the reason in the system log.
         */
        __libcpp_verbose_abort("oops-mesa: mutex lock failed with errno %d\n", rc);
    }
}

void mutex::unlock() noexcept {
    pthread_mutex_unlock(&__m_);
}

mutex::~mutex() _NOEXCEPT {
    pthread_mutex_destroy(&__m_);
}

/*
 * The next prime at or above `n`, asked for when a hash table rehashes. Trial division
 * is enough for a once-per-rehash call.
 */
size_t __next_prime(size_t n) {
    if (n <= 2) {
        return 2;
    }
    n |= 1; /* every prime above two is odd, so only odd candidates are worth testing */
    for (;; n += 2) {
        if (n % 3 == 0) {
            continue;
        }
        bool prime = true;
        for (size_t d = 5; d * d <= n; d += 6) {
            if (n % d == 0 || n % (d + 2) == 0) {
                prime = false;
                break;
            }
        }
        if (prime) {
            return n;
        }
    }
}

/*
 * The hash libc++ uses for raw bytes: FNV-1a, as libc++ uses on platforms without a
 * faster one.
 */
size_t __hash_memory(_LIBCPP_NOESCAPE const void *key, size_t len) noexcept {
    const unsigned char *p = static_cast<const unsigned char *>(key);
    size_t h = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/*
 * Called when an assertion inside libc++ fails; it must not return. Standard error is
 * routed to the system log, so the reason survives the process.
 */
void __libcpp_verbose_abort(const char *format, ...) noexcept {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    abort();
}

_LIBCPP_END_NAMESPACE_STD

/*
 * The exception constructors come from upstream libc++'s `src/stdexcept.cpp`, built
 * into the same archive: `std::logic_error` holds libc++'s reference-counted
 * `__libcpp_refstring`, which only libc++'s own source can construct.
 */

/*
 * Exit-time destructor registration. A title never exits - it parks until the shell
 * closes it - so accepting the registration and never running it is the whole
 * contract.
 */
extern "C" int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle) {
    (void)func;
    (void)arg;
    (void)dso_handle;
    return 0;
}

/*
 * Function-local static guards, per the Itanium ABI: byte 0 is "initialised", byte 1
 * is "in progress". Acquire returns 1 to the one caller that runs the initialiser; the
 * others spin until it is done. The guarded initialisers are short one-time table
 * builds, so a spin is enough and keeps this file free of the thread API.
 */
extern "C" int __cxa_guard_acquire(unsigned char *guard) {
    unsigned char *done = guard;
    unsigned char *pending = guard + 1;

    if (__atomic_load_n(done, __ATOMIC_ACQUIRE)) {
        return 0;
    }
    while (__atomic_exchange_n(pending, 1, __ATOMIC_ACQ_REL)) {
        if (__atomic_load_n(done, __ATOMIC_ACQUIRE)) {
            return 0;
        }
        __builtin_ia32_pause();
    }
    if (__atomic_load_n(done, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(pending, 0, __ATOMIC_RELEASE);
        return 0;
    }
    return 1;
}

extern "C" void __cxa_guard_release(unsigned char *guard) {
    __atomic_store_n(guard, 1, __ATOMIC_RELEASE);
    __atomic_store_n(guard + 1, 0, __ATOMIC_RELEASE);
}

/*
 * The vtable entry for a pure virtual slot. Reaching it is a construction- or
 * destruction-order bug in the caller, so it stops with the reason logged.
 */
extern "C" void __cxa_pure_virtual() {
    fprintf(
        stderr,
        "oops-mesa: a pure virtual function was called. The object's vtable slot was "
        "never overridden, which is a lifetime bug in the caller, not a missing "
        "platform symbol. Stopping here.\n");
    abort();
}
