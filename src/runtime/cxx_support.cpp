/*
 * The parts of libc++ that Mesa actually reaches, and nothing else.
 *
 * # Why this file exists rather than a libc++
 *
 * Mesa is a C++ project where it matters most - ACO, its shader backend, is all of it - so a C++
 * standard library has to come from somewhere. Two somewheres were tried first and both are
 * closed (D006):
 *
 *   The platform's own. It has a C++ library: obSCEne's census records 666 mangled symbols in
 *   it. Not one is in libc++'s ABI namespace, and names like `std::_Num_int_base` and
 *   `std::filesystem::_Close_dir` say why - it is a different implementation with a different
 *   ABI, so nothing compiled against libc++'s headers can link against it.
 *
 *   Building libc++ from the pinned checkout, as libelf is built. Its headers are LLVM 21 and
 *   the collection's compiler is clang 18, three major versions behind, and 43 of its 71 sources
 *   refuse to compile. libc++ expects a compiler at least as new as itself.
 *
 * So this provides the handful of definitions Mesa references, written as ordinary C++ against
 * the same headers Mesa is compiled against, which is why the mangled names come out right
 * without anybody spelling one. `tools/what-is-still-needed.sh` names the set, so if Mesa ever
 * reaches further the list grows visibly rather than becoming a link error.
 *
 * If that list grows past a screen, the answer is a newer compiler for the whole collection, not
 * a longer version of this file. That is written down in D006 rather than left to whoever is
 * looking when it happens.
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
 * C++ allocation, over the C allocator the platform provides.
 *
 * These are not libc++'s to supply on most systems either - they belong to the C++ runtime
 * alongside the unwinder - and this platform's runtime is a different implementation whose
 * symbols cannot be resolved against code compiled for libc++ (D006). They are four lines each
 * and there is nothing clever to get wrong.
 *
 * The throwing forms do not throw, because Mesa is compiled with exceptions off. An allocation
 * failure returns null, which is what the `nothrow` forms promise and what Mesa's own allocation
 * wrappers already check for. A caller that assumed the throwing contract would get a null
 * dereference instead of an exception, and that is a worse diagnostic than it sounds, so it is
 * stated here rather than left to be discovered.
 */
void *operator new(__SIZE_TYPE__ size) { return malloc(size ? size : 1); }
void *operator new[](__SIZE_TYPE__ size) { return malloc(size ? size : 1); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, __SIZE_TYPE__) noexcept { free(p); }
void operator delete[](void *p, __SIZE_TYPE__) noexcept { free(p); }

_LIBCPP_BEGIN_NAMESPACE_STD

/*
 * std::mutex, over the platform's threads.
 *
 * libc++ stores a `__libcpp_mutex_t` inside the object and its own source implements these three
 * over it. The layout comes from the headers this is compiled against, so delegating to the same
 * pthread calls the shim already provides keeps both sides agreeing about the object.
 */
void mutex::lock() {
    int rc = pthread_mutex_lock(&__m_);
    if (rc) {
        /*
         * libc++'s own version throws a `system_error` here. Mesa is compiled with exceptions
         * off, so throwing is not available and calling libc++'s thrower would only pull in the
         * machinery this platform cannot resolve (D006).
         *
         * A failed mutex lock is not recoverable in any case: the caller is about to touch data
         * it does not hold. Aborting with the reason is the honest end, and it leaves a line in
         * the system log rather than a corruption to find later.
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
 * The hash table's growth sequence. libc++ asks for the next prime at or above a bound when a
 * table rehashes. Trial division is slower than libc++'s own table and is called once per
 * rehash, which is not a path worth a lookup table here.
 */
size_t __next_prime(size_t n) {
    if (n <= 2) {
        return 2;
    }
    n |= 1;   /* every prime above two is odd, so only odd candidates are worth testing */
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
 * The hash libc++ uses for raw bytes. This is FNV-1a, which is what libc++ itself uses on
 * platforms without a faster one, and it is the same function oops-gl hashes frames with, so the
 * collection has one byte hash rather than two.
 */
size_t __hash_memory(_LIBCPP_NOESCAPE const void* key, size_t len) noexcept {
    const unsigned char* p = static_cast<const unsigned char*>(key);
    size_t h = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/*
 * What libc++ calls when an assertion inside it fails. It must not return: the library has
 * already decided its own state is inconsistent. The message goes to standard error, which the
 * platform routes to the system log, so the reason survives the process.
 */
void __libcpp_verbose_abort(const char* format, ...) noexcept {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    abort();
}

_LIBCPP_END_NAMESPACE_STD

/*
 * The exception constructors are deliberately not here.
 *
 * They were, and they were wrong. `std::logic_error` holds a `__libcpp_refstring`, libc++'s own
 * reference-counted string, and a constructor written here cannot build that member without
 * reimplementing its reference counting. The first version compiled and then failed at link
 * with an undefined `__libcpp_refstring::__libcpp_refstring(char const*)`, which is the right
 * answer to the wrong approach.
 *
 * They come from upstream's `src/stdexcept.cpp` instead, which is one of the libc++ sources that
 * does compile with this compiler. The build adds it to the same archive as this file. See D006
 * for why most of libc++ does not.
 */
