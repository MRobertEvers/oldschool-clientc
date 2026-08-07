#ifndef TEST_TRSPK_LEAK_RETAINED_ALLOC_TRACKER_H
#define TEST_TRSPK_LEAK_RETAINED_ALLOC_TRACKER_H

#include <stddef.h>
#include <stdlib.h>

struct RetainedAllocSnapshot
{
    size_t live_blocks;
    size_t live_bytes;
    size_t peak_blocks;
    size_t peak_bytes;
};

void*
retained_alloc_malloc(size_t size);

void*
retained_alloc_calloc(size_t count, size_t size);

void*
retained_alloc_realloc(void* pointer, size_t size);

void
retained_alloc_free(void* pointer);

struct RetainedAllocSnapshot
retained_alloc_snapshot(void);

/* Compile the renderer and its CPU-side dependencies with this header forced
 * in to account for every C-heap allocation owned by the retained path.  The
 * tracker implementation itself is compiled without the define. */
#if defined(TORIRS_RETAINED_ALLOC_TRACKING)
#define malloc(size) retained_alloc_malloc(size)
#define calloc(count, size) retained_alloc_calloc((count), (size))
#define realloc(pointer, size) retained_alloc_realloc((pointer), (size))
#define free(pointer) retained_alloc_free(pointer)
#endif

#endif
