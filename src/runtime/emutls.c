/*
 * The emulated-TLS runtime. The platform loader rejects a title image with a `PT_TLS`
 * program header (oops-apps `src/oops-mesa/tls-probe`), so Mesa is compiled with
 * `-femulated-tls`: each thread-local becomes an `__emutls_v.*` control record in
 * `.data` and each access a call to `__emutls_get_address`. compiler-rt normally
 * supplies that function; this target does not link it, so it is defined here over
 * `pthread_key_*` (threads.c).
 *
 * The control record follows compiler-rt's emutls ABI. `object.index` is zero until
 * first use; index `n` is slot `n` of the calling thread's array, whose element 0 is
 * the length.
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

/* The per-thread array of object pointers, element 0 holding its length, grown on
 * demand rather than sized to a fixed maximum. */
static pthread_key_t s_key;
static pthread_once_t s_key_once = PTHREAD_ONCE_INIT;

/* Index assignment is global and must not race. The mutex is held only while handing
 * out an index, never while allocating an object. */
static pthread_mutex_t s_index_lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t s_next_index;

/* Frees a thread's objects when it exits; Mesa starts a compiler thread per core. */
static void emutls_thread_cleanup(void *arg) {
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

static void emutls_key_init(void) {
    (void)pthread_key_create(&s_key, emutls_thread_cleanup);
}

/* The calling thread's array, grown to hold at least `index` slots. NULL on allocation
 * failure, which the caller turns into a null return rather than a wild pointer. */
static uintptr_t *emutls_array_for(uintptr_t index) {
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

static void *emutls_allocate(const struct emutls_control *control) {
    size_t align = control->align;
    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }

    void *object = NULL;
    /* `posix_memalign` wants a power-of-two multiple of sizeof(void *), which the
     * adjustment above guarantees for every alignment a compiler emits. */
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

void *__emutls_get_address(void *control_ptr) {
    struct emutls_control *control = (struct emutls_control *)control_ptr;

    if (control == NULL) {
        return NULL;
    }

    (void)pthread_once(&s_key_once, emutls_key_init);

    /* Double-checked: two threads can reach an object's first use together. */
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
