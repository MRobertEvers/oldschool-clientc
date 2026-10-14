#ifndef TORIMEM_RSCACHE_H
#define TORIMEM_RSCACHE_H

/*
 * ToriMem adapted to RSCache's allocator interface (rscache_alloc.h).
 *
 * RSCache is standalone: it allocates through its own struct RSCache_Allocator,
 * which defaults to malloc/free, and knows nothing of ToriMem. A binary that
 * wants RSCache's memory accounted calls ToriMem_InstallRSCache; one that does
 * not (a tool) keeps libc. Host code that hands memory across the boundary
 * goes through this header, so it is right either way:
 *
 *   - memory RSCache will free, or that RSCache allocated: RSCACHE_MALLOC /
 *     RSCACHE_CALLOC / RSCACHE_FREE, the library's own interface;
 *   - a decoded array that becomes a ToriMem group's: ToriMem_TakeFromRSCache.
 *
 *       RSCache (its allocator) --TakeFromRSCache--> ASSET / any group
 *            |                                          ^
 *            +- installed: lib:rscache, Adopt in place -+
 *            +- not installed: libc, copied + RSCACHE_FREE
 */

#include "mem/torirs_mem.h"

#include "rscache_alloc.h" /* -I 3rd/rscache/src, as every RSCache build has */

#include <assert.h>
#include <string.h>

static inline void
ToriMem_InstallRSCache(void)
{
    TORIMEM_INSTALL_LIB(RSCache, RSCache_SetAllocator, ToriMem_LibRSCache);
}

/** Whether RSCache allocates through ToriMem in this binary. */
static inline int
ToriMem_RSCacheInstalled(void)
{
    return g_rscache_allocator.free_fn == ToriMem_LibFree;
}

/** `to` takes a block RSCache allocated (`size` bytes). Installed: the block
 *  changes group in place. Not installed: it is copied into `to` and the
 *  original freed through RSCache's allocator. Returns the block's address
 *  in `to`; the caller drops `ptr`. */
static inline void*
ToriMem_TakeFromRSCacheAt(
    struct ToriMem_Group* to,
    void* ptr,
    size_t size,
    const char* file,
    int line)
{
    assert(to);
    assert(ptr);
    if( ToriMem_RSCacheInstalled() )
        return ToriMem_Adopt(ptr, to);
    void* copy = ToriMem_MemdupAt(to, ptr, size, file, line);
    RSCACHE_FREE(ptr);
    return copy;
}

#define ToriMem_TakeFromRSCache(to, ptr, size)                                                     \
    ToriMem_TakeFromRSCacheAt((to), (ptr), (size), TORIMEM_SITE)
/** A NUL-terminated string RSCache allocated. */
#define ToriMem_TakeStrFromRSCache(to, str)                                                        \
    ((char*)ToriMem_TakeFromRSCacheAt((to), (str), strlen(str) + 1, TORIMEM_SITE))

/** A copy of `size` bytes that RSCache owns (RSCACHE_FREE releases it). */
static inline void*
ToriMem_MemdupIntoRSCache(
    const void* src,
    size_t size)
{
    assert(src);
    void* out = RSCACHE_MALLOC(size);
    assert(out);
    memcpy(out, src, size);
    return out;
}

#endif /* TORIMEM_RSCACHE_H */
