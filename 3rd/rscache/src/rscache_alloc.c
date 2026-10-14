#include "rscache_alloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void*
libc_malloc(
    size_t size,
    void* user)
{
    (void)user;
    return malloc(size);
}

static void*
libc_calloc(
    size_t count,
    size_t size,
    void* user)
{
    (void)user;
    return calloc(count, size);
}

static void*
libc_realloc(
    void* ptr,
    size_t size,
    void* user)
{
    (void)user;
    return realloc(ptr, size);
}

static void
libc_free(
    void* ptr,
    void* user)
{
    (void)user;
    free(ptr);
}

struct RSCache_Allocator g_rscache_allocator = {
    libc_malloc, libc_calloc, libc_realloc, libc_free, NULL,
};

void
RSCache_SetAllocator(const struct RSCache_Allocator* allocator)
{
    assert(allocator);
    assert(allocator->malloc_fn);
    assert(allocator->calloc_fn);
    assert(allocator->realloc_fn);
    assert(allocator->free_fn);
    g_rscache_allocator = *allocator;
}

char*
RSCache_Strdup(const char* str)
{
    assert(str);
    size_t len = strlen(str);
    char* out = RSCACHE_MALLOC(len + 1);
    assert(out);
    memcpy(out, str, len + 1);
    return out;
}
