#ifndef TORIMEM_TORIDRAW_H
#define TORIMEM_TORIDRAW_H

/*
 * ToriMem adapted to ToriDraw's allocator interface (toridraw_alloc.h).
 *
 * ToriDraw is standalone: it allocates through its own struct
 * ToriDraw_Allocator, which defaults to malloc/free. Host code builds models,
 * sprites and fonts that ToriDraw later frees, so it allocates them through
 * ToriDraw's interface (TORIDRAW_MALLOC / TORIDRAW_CALLOC / TORIDRAW_FREE),
 * and that is right whether or not this binary installed ToriMem there.
 *
 *       ASSET / any group --GiveToToriDraw--> ToriDraw (its allocator)
 *            |                                   ^
 *            +- installed: Move into lib:toridraw
 *            +- not installed: copied into TORIDRAW_MALLOC, original freed
 */

#include "mem/torirs_mem.h"

#include "toridraw_alloc.h" /* -I 3rd/toridraw, as every ToriDraw build has */

#include <assert.h>
#include <string.h>

static inline void
ToriMem_InstallToriDraw(void)
{
    TORIMEM_INSTALL_LIB(ToriDraw, ToriDraw_SetAllocator, ToriMem_LibToriDraw);
}

/** Whether ToriDraw allocates through ToriMem in this binary. */
static inline int
ToriMem_ToriDrawInstalled(void)
{
    return g_toridraw_allocator.free_fn == ToriMem_LibFree;
}

/** ToriDraw takes a block of group `from` (`size` bytes); ToriDraw's free
 *  releases it from then on. Returns its address; the caller drops `ptr`. */
static inline void*
ToriMem_GiveToToriDraw(
    struct ToriMem_Group* from,
    void* ptr,
    size_t size)
{
    assert(from);
    assert(ptr);
    if( ToriMem_ToriDrawInstalled() )
        return ToriMem_Move(ptr, from, ToriMem_LibToriDraw());
    void* copy = TORIDRAW_MALLOC(size);
    assert(copy);
    memcpy(copy, ptr, size);
    ToriMem_Free(from, ptr);
    return copy;
}

#endif /* TORIMEM_TORIDRAW_H */
