#ifndef RSCACHE_ALLOC_H
#define RSCACHE_ALLOC_H

#include <stddef.h>

/*
 * Every heap allocation RSCache makes goes through one replaceable allocator.
 *
 * The default is libc. A host that wants to account for, or move, the memory
 * this library returns installs its own with RSCache_SetAllocator BEFORE the
 * first call into the library: a block allocated by one allocator and freed by
 * another is heap corruption.
 *
 * Decoded arrays can cross library boundaries -- toridraw_rscache moves a
 * decoded model's arrays into a ToriDraw_Model -- so a host that installs an
 * allocator here installs the same one in each library those arrays reach,
 * or none. A host that takes a decoded array for itself frees it with
 * RSCACHE_FREE, or copies it out and frees the original that way.
 */
struct RSCache_Allocator
{
    void* (*malloc_fn)(size_t size, void* user);
    void* (*calloc_fn)(size_t count, size_t size, void* user);
    void* (*realloc_fn)(void* ptr, size_t size, void* user);
    void (*free_fn)(void* ptr, void* user);
    void* user;
};

extern struct RSCache_Allocator g_rscache_allocator;

/** Replace the allocator; every field but `user` is required. */
void
RSCache_SetAllocator(const struct RSCache_Allocator* allocator);

/** strdup through the installed allocator. */
char*
RSCache_Strdup(const char* str);

#define RSCACHE_MALLOC(size) g_rscache_allocator.malloc_fn((size), g_rscache_allocator.user)
#define RSCACHE_CALLOC(count, size)                                                                \
    g_rscache_allocator.calloc_fn((count), (size), g_rscache_allocator.user)
#define RSCACHE_REALLOC(ptr, size)                                                                 \
    g_rscache_allocator.realloc_fn((ptr), (size), g_rscache_allocator.user)
#define RSCACHE_FREE(ptr) g_rscache_allocator.free_fn((ptr), g_rscache_allocator.user)

#endif /* RSCACHE_ALLOC_H */
