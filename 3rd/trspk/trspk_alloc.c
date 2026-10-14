#include "trspk_alloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void*
trspk_libc_malloc(
    size_t size,
    void* user)
{
    (void)user;
    return malloc(size);
}

static void*
trspk_libc_calloc(
    size_t count,
    size_t size,
    void* user)
{
    (void)user;
    return calloc(count, size);
}

static void*
trspk_libc_realloc(
    void* ptr,
    size_t size,
    void* user)
{
    (void)user;
    return realloc(ptr, size);
}

static void
trspk_libc_free(
    void* ptr,
    void* user)
{
    (void)user;
    free(ptr);
}

struct TRSPK_Allocator g_trspk_allocator = {
    trspk_libc_malloc, trspk_libc_calloc, trspk_libc_realloc, trspk_libc_free, NULL,
};

void
TRSPK_SetAllocator(const struct TRSPK_Allocator* allocator)
{
    assert(allocator);
    assert(allocator->malloc_fn);
    assert(allocator->calloc_fn);
    assert(allocator->realloc_fn);
    assert(allocator->free_fn);
    g_trspk_allocator = *allocator;
}

char*
TRSPK_Strdup(const char* str)
{
    assert(str);
    size_t len = strlen(str);
    char* out = TRSPK_MALLOC(len + 1);
    assert(out);
    memcpy(out, str, len + 1);
    return out;
}
