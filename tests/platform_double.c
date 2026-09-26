/*
 * Host stand-ins for the platform's memory calls, so the winsys suite has live buffers.
 *
 * oops-sdk binds these as weak symbols, which off the console resolve to nothing and
 * refuse every allocation. Here allocation is host memory, mapping returns the right
 * pages and a release invalidates; the tests assert on the winsys's own bookkeeping.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t sce_off_t;

struct obs_batch_map_entry {
    void *vaddr;
    sce_off_t paddr;
    size_t len;
    uint8_t prot;
    uint8_t flags;
};

/* Physical offsets are indices into this table, so a mapping finds the host allocation
 * behind an offset. Offset zero is a legitimate value, as it is on the console. */
#define MAX_ALLOCS 64
static struct {
    void *base;
    size_t len;
    int live;
} s_allocs[MAX_ALLOCS];

int sceKernelAllocateDirectMemory(sce_off_t searchStart, sce_off_t searchEnd,
                                  size_t len, size_t alignment, int memoryType,
                                  sce_off_t *paddr) {
    (void)searchStart;
    (void)searchEnd;
    (void)alignment;
    (void)memoryType;
    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (!s_allocs[i].live) {
            s_allocs[i].base = calloc(1, len);
            if (!s_allocs[i].base)
                return -1;
            s_allocs[i].len = len;
            s_allocs[i].live = 1;
            *paddr = (sce_off_t)i; /* index zero is a real answer */
            return 0;
        }
    }
    return -1;
}

int sceKernelReleaseDirectMemory(sce_off_t paddr, size_t len) {
    (void)len;
    if (paddr < 0 || paddr >= MAX_ALLOCS || !s_allocs[paddr].live)
        return -1;
    free(s_allocs[paddr].base);
    memset(&s_allocs[paddr], 0, sizeof(s_allocs[paddr]));
    return 0;
}

int sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags,
                             sce_off_t directMemoryStart, size_t alignment) {
    (void)len;
    (void)prot;
    (void)flags;
    (void)alignment;
    if (directMemoryStart < 0 || directMemoryStart >= MAX_ALLOCS ||
        !s_allocs[directMemoryStart].live) {
        return -1;
    }
    *addr = s_allocs[directMemoryStart].base;
    return 0;
}

int sceKernelBatchMap(struct obs_batch_map_entry *entries, int num_entries,
                      int *completed) {
    /* A host cannot map at a chosen address, so this reports success without mapping;
     * callers assert on bookkeeping, not memory contents. */
    (void)entries;
    *completed = num_entries;
    return 0;
}

int sceKernelMunmap(void *addr, size_t len) {
    (void)addr;
    (void)len;
    return 0;
}

size_t sceKernelGetDirectMemorySize(void) {
    return (size_t)1 << 32;
}
