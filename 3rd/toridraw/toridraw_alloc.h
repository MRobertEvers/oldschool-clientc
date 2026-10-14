#ifndef TORIDRAW_ALLOC_H
#define TORIDRAW_ALLOC_H

#include <stddef.h>

/*
 * Every heap allocation ToriDraw makes goes through one replaceable allocator.
 *
 * The default is libc. A host that wants to account for, or move, the memory
 * this library returns installs its own with ToriDraw_SetAllocator BEFORE the
 * first call into the library: a block allocated by one allocator and freed by
 * another is heap corruption. Memory crosses library boundaries (a decoded
 * model's arrays move into a ToriDraw_Model; a caller's pixels are adopted by
 * a sprite), so a host installs the SAME allocator in every library it links,
 * or none.
 */
struct ToriDraw_Allocator
{
    void* (*malloc_fn)(size_t size, void* user);
    void* (*calloc_fn)(size_t count, size_t size, void* user);
    void* (*realloc_fn)(void* ptr, size_t size, void* user);
    void (*free_fn)(void* ptr, void* user);
    void* user;
};

extern struct ToriDraw_Allocator g_toridraw_allocator;

/** Replace the allocator; every field but `user` is required. */
void
ToriDraw_SetAllocator(const struct ToriDraw_Allocator* allocator);

/** strdup through the installed allocator. */
char*
ToriDraw_Strdup(const char* str);

#define TORIDRAW_MALLOC(size) g_toridraw_allocator.malloc_fn((size), g_toridraw_allocator.user)
#define TORIDRAW_CALLOC(count, size) g_toridraw_allocator.calloc_fn((count), (size), g_toridraw_allocator.user)
#define TORIDRAW_REALLOC(ptr, size) g_toridraw_allocator.realloc_fn((ptr), (size), g_toridraw_allocator.user)
#define TORIDRAW_FREE(ptr) g_toridraw_allocator.free_fn((ptr), g_toridraw_allocator.user)

#endif /* TORIDRAW_ALLOC_H */
