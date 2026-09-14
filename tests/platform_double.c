/*
 * A stand-in for the platform's memory calls, so the winsys can be exercised on the build
 * machine.
 *
 * oops-sdk binds these as weak symbols, so off the console they resolve to nothing and every
 * allocation refuses. That is the right behaviour for the SDK and it makes most of the winsys
 * untestable here: a buffer can never exist, so nothing that takes a live buffer is ever
 * reached.
 *
 * Defining them here gives the suite real buffers backed by ordinary host memory. What is being
 * tested is still the winsys's own logic - the handle table, the liveness rule, what each
 * command reports - and not these. They do the least they can while remaining honest:
 * allocation really allocates, mapping really returns the right pages, and a release really
 * invalidates. Nothing here pretends to be a GPU.
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

/* Physical offsets are handed out as indices into this table, so that a mapping can find the
 * host allocation behind the offset the winsys is carrying. Offset zero is deliberately a
 * legitimate value here, because it is one on the console and the buffer table got that wrong
 * once already (oops-mesa worklog 007). */
#define MAX_ALLOCS 64
static struct { void *base; size_t len; int live; } s_allocs[MAX_ALLOCS];

int sceKernelAllocateDirectMemory(sce_off_t searchStart, sce_off_t searchEnd, size_t len,
                                  size_t alignment, int memoryType, sce_off_t *paddr)
{
    (void)searchStart; (void)searchEnd; (void)alignment; (void)memoryType;
    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (!s_allocs[i].live) {
            s_allocs[i].base = calloc(1, len);
            if (!s_allocs[i].base) return -1;
            s_allocs[i].len = len;
            s_allocs[i].live = 1;
            *paddr = (sce_off_t)i;   /* index zero is a real answer */
            return 0;
        }
    }
    return -1;
}

int sceKernelReleaseDirectMemory(sce_off_t paddr, size_t len)
{
    (void)len;
    if (paddr < 0 || paddr >= MAX_ALLOCS || !s_allocs[paddr].live) return -1;
    free(s_allocs[paddr].base);
    memset(&s_allocs[paddr], 0, sizeof(s_allocs[paddr]));
    return 0;
}

int sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags,
                             sce_off_t directMemoryStart, size_t alignment)
{
    (void)len; (void)prot; (void)flags; (void)alignment;
    if (directMemoryStart < 0 || directMemoryStart >= MAX_ALLOCS ||
        !s_allocs[directMemoryStart].live) {
        return -1;
    }
    *addr = s_allocs[directMemoryStart].base;
    return 0;
}

int sceKernelBatchMap(struct obs_batch_map_entry *entries, int num_entries, int *completed)
{
    /* Mapping at a chosen address is the one thing a host cannot honestly imitate, so this
     * reports the pages as mapped without moving anything. Every test that uses it asserts on
     * the winsys's bookkeeping rather than on memory contents. */
    (void)entries;
    *completed = num_entries;
    return 0;
}

int sceKernelMunmap(void *addr, size_t len)
{
    (void)addr; (void)len;
    return 0;
}

size_t sceKernelGetDirectMemorySize(void)
{
    return (size_t)1 << 32;
}
