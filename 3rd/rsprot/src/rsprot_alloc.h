#ifndef RSPROT_ALLOC_H
#define RSPROT_ALLOC_H

#include <stddef.h>

/*
 * Every heap allocation rsprot makes goes through one replaceable allocator.
 * The default is libc. A host that installs its own does so before the first
 * call into the library, and installs the same allocator in every library it
 * links, or none, when blocks cross between them: a block allocated by one allocator
 * and freed by another is heap corruption.
 */
struct RSProt_Allocator
{
    void* (*malloc_fn)(size_t size, void* user);
    void* (*calloc_fn)(size_t count, size_t size, void* user);
    void* (*realloc_fn)(void* ptr, size_t size, void* user);
    void (*free_fn)(void* ptr, void* user);
    void* user;
};

extern struct RSProt_Allocator g_rsprot_allocator;

/** Every field but `user` is required. */
void
rsprot_set_allocator(const struct RSProt_Allocator* allocator);

#define RSPROT_MALLOC(size) g_rsprot_allocator.malloc_fn((size), g_rsprot_allocator.user)
#define RSPROT_CALLOC(count, size) g_rsprot_allocator.calloc_fn((count), (size), g_rsprot_allocator.user)
#define RSPROT_REALLOC(ptr, size) g_rsprot_allocator.realloc_fn((ptr), (size), g_rsprot_allocator.user)
#define RSPROT_FREE(ptr) g_rsprot_allocator.free_fn((ptr), g_rsprot_allocator.user)

#endif /* RSPROT_ALLOC_H */
