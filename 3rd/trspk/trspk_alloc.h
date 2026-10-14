#ifndef TRSPK_ALLOC_H
#define TRSPK_ALLOC_H

#include <stddef.h>

/*
 * Every heap allocation trspk makes goes through one replaceable allocator.
 *
 * The default is libc. A host that wants to account for, or move, the memory
 * this library returns installs its own with TRSPK_SetAllocator BEFORE the
 * first call into the library: a block allocated by one allocator and freed by
 * another is heap corruption. Memory crosses library boundaries (a decoded
 * model's arrays move into a ToriDraw_Model; a caller's pixels are adopted by
 * a sprite), so a host installs the SAME allocator in every library it links,
 * or none.
 */
struct TRSPK_Allocator
{
    void* (*malloc_fn)(size_t size, void* user);
    void* (*calloc_fn)(size_t count, size_t size, void* user);
    void* (*realloc_fn)(void* ptr, size_t size, void* user);
    void (*free_fn)(void* ptr, void* user);
    void* user;
};

extern struct TRSPK_Allocator g_trspk_allocator;

/** Replace the allocator; every field but `user` is required. */
void
TRSPK_SetAllocator(const struct TRSPK_Allocator* allocator);

/** strdup through the installed allocator. */
char*
TRSPK_Strdup(const char* str);

#define TRSPK_MALLOC(size) g_trspk_allocator.malloc_fn((size), g_trspk_allocator.user)
#define TRSPK_CALLOC(count, size) g_trspk_allocator.calloc_fn((count), (size), g_trspk_allocator.user)
#define TRSPK_REALLOC(ptr, size) g_trspk_allocator.realloc_fn((ptr), (size), g_trspk_allocator.user)
#define TRSPK_FREE(ptr) g_trspk_allocator.free_fn((ptr), g_trspk_allocator.user)

#endif /* TRSPK_ALLOC_H */
