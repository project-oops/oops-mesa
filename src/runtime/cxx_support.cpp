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
        __throw_system_error(rc, "mutex lock failed");
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
 * Two exception types whose constructors live in libc++'s own sources. They are referenced by
 * Mesa's C++ even in a build that never throws, because the types appear in headers it includes.
 */
namespace std {

logic_error::logic_error(const char* msg) : __imp_(msg) {}

bad_array_new_length::bad_array_new_length() noexcept {}

}  // namespace std
