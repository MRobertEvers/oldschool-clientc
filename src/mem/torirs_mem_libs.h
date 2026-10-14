#ifndef TORIRS_MEM_LIBS_H
#define TORIRS_MEM_LIBS_H

/*
 * Point each in-house library's allocator at its lib:* group.
 *
 * Each library is standalone: it defines its own allocator interface
 * (struct <Lib>_Allocator, <Lib>_SetAllocator) and defaults to malloc/free.
 * Installing is opt-in per binary. The client, the server and the io server
 * install, so every library block is accounted for and checked; a tool that
 * never installs keeps libc and still works, because host code crosses each
 * boundary through the library's own macros (RSCACHE_FREE, TORIDRAW_MALLOC,
 * ...) and the adapters in mem/torimem_<lib>.h, which copy where an install
 * would have moved in place.
 *
 * Call right after ToriMem_Init, before the first call into any library: a
 * block allocated through libc and freed through ToriMem (or the other way
 * round) is heap corruption. Install every library the binary links, or none
 * -- memory moves between them (rscache arrays become ToriDraw model arrays).
 */

#include "mem/torirs_mem.h"
#include "mem/torimem_rscache.h"
#include "mem/torimem_toridraw.h"

#include "3rd/bzip/bzip.h"
#include "3rd/hmap/hmap.h"
#include "3rd/rsareabuf/rsareabuf.h"
#include "3rd/rsprot/src/rsprot_alloc.h"
#include "3rd/trspk/trspk_alloc.h"


static inline void
ToriMem_InstallTRSPK(void)
{
    TORIMEM_INSTALL_LIB(TRSPK, TRSPK_SetAllocator, ToriMem_LibTRSPK);
}

/* trspk's blocks belong to the renderer that asked for them: the host points
 * the library at ToriMem_Renderer() when a renderer starts and back at
 * lib:trspk when it stops, so the renderer group's end checks them too. Only
 * where ToriMem_InstallTRSPK ran. A block is freed and reallocated by the
 * group in its header, so moving the default never strands one. */
static inline void
ToriMem_PointTRSPK(struct ToriMem_Group* group)
{
    struct TRSPK_Allocator a = {
        ToriMem_LibMalloc, ToriMem_LibCalloc, ToriMem_LibRealloc, ToriMem_LibFree, group,
    };
    TRSPK_SetAllocator(&a);
}

static inline void
ToriMem_InstallHMap(void)
{
    TORIMEM_INSTALL_LIB(HMap, hmap_set_allocator, ToriMem_LibHMap);
}

static inline void
ToriMem_InstallRSProt(void)
{
    TORIMEM_INSTALL_LIB(RSProt, rsprot_set_allocator, ToriMem_LibRSProt);
}

static inline void
ToriMem_InstallRSAreaBuf(void)
{
    TORIMEM_INSTALL_LIB(RSAreaBuf, rsab_set_allocator, ToriMem_LibRSAreaBuf);
}

static inline void
ToriMem_InstallBZip(void)
{
    TORIMEM_INSTALL_LIB(BZip, bzip_set_allocator, ToriMem_LibBZip);
}

#endif /* TORIRS_MEM_LIBS_H */
