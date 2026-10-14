#include "toridraw_alloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void*
toridraw_libc_malloc(
    size_t size,
    void* user)
{
    (void)user;
    return malloc(size);
}

static void*
toridraw_libc_calloc(
    size_t count,
    size_t size,
    void* user)
{
    (void)user;
    return calloc(count, size);
}

static void*
toridraw_libc_realloc(
    void* ptr,
    size_t size,
    void* user)
{
    (void)user;
    return realloc(ptr, size);
}

static void
toridraw_libc_free(
    void* ptr,
    void* user)
{
    (void)user;
    free(ptr);
}

struct ToriDraw_Allocator g_toridraw_allocator = {
    toridraw_libc_malloc, toridraw_libc_calloc, toridraw_libc_realloc, toridraw_libc_free, NULL,
};

void
ToriDraw_SetAllocator(const struct ToriDraw_Allocator* allocator)
{
    assert(allocator);
    assert(allocator->malloc_fn);
    assert(allocator->calloc_fn);
    assert(allocator->realloc_fn);
    assert(allocator->free_fn);
    g_toridraw_allocator = *allocator;
}

char*
ToriDraw_Strdup(const char* str)
{
    assert(str);
    size_t len = strlen(str);
    char* out = TORIDRAW_MALLOC(len + 1);
    assert(out);
    memcpy(out, str, len + 1);
    return out;
}
