/*
 * Thread-local storage for a platform that will not load a title carrying one.
 *
 * # Why this exists
 *
 * The console refuses any title image with a `PT_TLS` program header. That is measured, not
 * inferred: `oops-apps/src/oops-mesa/tls-probe` is 198 KB containing nothing but two `__thread`
 * variables, and the loader rejected it in exactly the same place as the 20 MB Mesa title -
 * `sceSblAuthMgrAuthHeader:readHeader -37`, before a single instruction ran. Every other app in
 * the collection has five program headers and no TLS, and every one of them loads.
 *
 * Mesa cannot simply stop using thread-local storage: eight symbols across `src/util`,
 * `src/mesa/glapi` and ACO are defined `thread_local`, and the GL dispatch pair is reached from
 * hand-written assembly.
 *
 * # What is done instead, and why it is not a patch
 *
 * The compiler already has an answer for platforms without native TLS: `-femulated-tls`. Each
 * thread-local becomes an ordinary `__emutls_v.*` control record in `.data`, and every access
 * becomes a call to `__emutls_get_address`. No segment is emitted, the linker script needs no
 * `PT_TLS` phdr, and upstream Mesa is not modified at all - which is the outcome CLAUDE.md's
 * first principle asks for, a shim rather than a patch.
 *
 * Mesa only disables it for Android (`meson.build`, `with_platform_android`), so nothing in this
 * build was relying on native TLS being kept.
 *
 * What the compiler does not supply is the runtime. `__emutls_get_address` normally comes from
 * compiler-rt's builtins, which this target does not link. It is a small function over
 * `pthread_key_*`, and `src/runtime/threads.c` already maps those onto the vendor's thread API -
 * so it belongs here, in the shim that owns what Mesa asks of a C library.
 *
 * # The contract
 *
 * From the emutls ABI, as compiler-rt implements it. The compiler emits one of these per
 * thread-local object and passes its address:
 *
 *     struct __emutls_control {
 *         size_t size;      the object's size in bytes
 *         size_t align;     its alignment
 *         union { uintptr_t index; void *address; } object;
 *         void  *value;     the initial image, or NULL meaning zero
 *     };
 *
 * `object.index` starts at zero and this file assigns it on first use. Index `n` means slot
 * `n - 1` of the calling thread's array, so zero can keep meaning "not yet assigned".
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

struct emutls_control {
    size_t size;
    size_t align;
    union {
        uintptr_t index;
        void *address;
    } object;
    void *value;
};

/* The per-thread array of object pointers. Element 0 holds the array's length, so a thread that
 * has only ever touched one thread-local carries two words rather than a fixed maximum. */
static pthread_key_t s_key;
static pthread_once_t s_key_once = PTHREAD_ONCE_INIT;

/* Index assignment is global and must not race. The mutex is held only while handing out an
 * index, never while allocating an object. */
static pthread_mutex_t s_index_lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t s_next_index;

/*
 * Freeing a thread's objects when it exits. Without this every thread that ever touched a
 * thread-local would leak its objects for the life of the process, and Mesa starts a compiler
 * thread per core.
 */
static void emutls_thread_cleanup(void *arg)
{
    uintptr_t *array = (uintptr_t *)arg;
    if (array == NULL) {
        return;
    }
    uintptr_t count = array[0];
    for (uintptr_t i = 1; i <= count; i++) {
        free((void *)array[i]);
    }
    free(array);
}

static void emutls_key_init(void)
{
    (void)pthread_key_create(&s_key, emutls_thread_cleanup);
}

/* The calling thread's array, grown to hold at least `index` slots. NULL on allocation failure,
 * which the caller turns into a null return rather than a wild pointer. */
static uintptr_t *emutls_array_for(uintptr_t index)
{
    uintptr_t *array = (uintptr_t *)pthread_getspecific(s_key);
    uintptr_t have = (array != NULL) ? array[0] : 0;

    if (have >= index) {
        return array;
    }

    /* One word for the count, then one per object. */
    uintptr_t *grown = (uintptr_t *)realloc(array, (index + 1u) * sizeof(uintptr_t));
    if (grown == NULL) {
        return NULL;
    }
    /* New slots start empty; the count word is not a pointer and is set last. */
    memset(&grown[have + 1u], 0, (index - have) * sizeof(uintptr_t));
    grown[0] = index;

    if (pthread_setspecific(s_key, grown) != 0) {
        /* The array is not reachable from the key, so nothing else will free it. */
        free(grown);
        return NULL;
    }
    return grown;
}

/* Allocate one object, honouring its alignment, and give it its initial value. */
static void *emutls_allocate(const struct emutls_control *control)
{
    size_t align = control->align;
    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }

    void *object = NULL;
    /* `posix_memalign` is imported and wants a power-of-two multiple of sizeof(void *), which
     * the adjustment above guarantees for every alignment a compiler emits. */
    if (posix_memalign(&object, align, control->size != 0 ? control->size : 1u) != 0) {
        return NULL;
    }

    if (control->value != NULL) {
        memcpy(object, control->value, control->size);
    } else {
        memset(object, 0, control->size);
    }
    return object;
}

void *__emutls_get_address(void *control_ptr);

void *__emutls_get_address(void *control_ptr)
{
    struct emutls_control *control = (struct emutls_control *)control_ptr;

    if (control == NULL) {
        return NULL;
    }

    (void)pthread_once(&s_key_once, emutls_key_init);

    /*
     * Assign this object an index the first time anybody asks for it. Read once outside the lock
     * for the common case, then re-check inside it, because two threads can arrive together on
     * the very first use.
     */
    uintptr_t index = control->object.index;
    if (index == 0) {
        (void)pthread_mutex_lock(&s_index_lock);
        index = control->object.index;
        if (index == 0) {
            index = ++s_next_index;
            control->object.index = index;
        }
        (void)pthread_mutex_unlock(&s_index_lock);
    }

    uintptr_t *array = emutls_array_for(index);
    if (array == NULL) {
        return NULL;
    }

    if (array[index] == 0) {
        void *object = emutls_allocate(control);
        if (object == NULL) {
            return NULL;
        }
        array[index] = (uintptr_t)object;
    }
    return (void *)array[index];
}
