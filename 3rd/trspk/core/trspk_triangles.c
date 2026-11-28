#include "trspk_triangles.h"
#include "../trspk_alloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TRSPK_TRI_PAGE_BYTES ((size_t)TRSPK_TRI_PAGE_ENTRIES * sizeof(int))

void
trspk_triangles_free(struct TRSPK_Triangles* triangles)
{
    assert(triangles);
    if( triangles->pages )
    {
        for( uint32_t i = 0u; i < triangles->page_count; i++ )
            TRSPK_PageFree(triangles->pages[i]);
        TRSPK_FREE(triangles->pages);
    }
    TRSPK_FREE(triangles->small);
    memset(triangles, 0, sizeof(*triangles));
}

static void
trspk_triangles_table_ensure(
    struct TRSPK_Triangles* triangles,
    uint32_t pages)
{
    uint32_t capacity;
    int** grown;
    if( pages <= triangles->page_table_capacity )
        return;
    capacity = triangles->page_table_capacity ? triangles->page_table_capacity : 16u;
    while( capacity < pages )
        capacity *= 2u;
    grown = (int**)TRSPK_REALLOC(triangles->pages, (size_t)capacity * sizeof(*grown));
    assert(grown != NULL);
    triangles->pages = grown;
    triangles->page_table_capacity = capacity;
}

static int*
trspk_triangles_new_page(void)
{
    int* page = (int*)TRSPK_PageAlloc(TRSPK_TRI_PAGE_BYTES);
    memset(page, 0xFF, TRSPK_TRI_PAGE_BYTES);
    return page;
}

void
trspk_triangles_ensure(
    struct TRSPK_Triangles* triangles,
    uint32_t tri_count)
{
    assert(triangles);
    if( tri_count <= triangles->cap )
        return;

    if( !triangles->pages && tri_count <= TRSPK_TRI_SMALL_ENTRIES )
    {
        uint32_t new_cap = triangles->cap ? triangles->cap * 2u : TRSPK_TRIANGLES_INIT_CAP;
        while( new_cap < tri_count )
            new_cap *= 2u;
        if( new_cap > TRSPK_TRI_SMALL_ENTRIES )
            new_cap = TRSPK_TRI_SMALL_ENTRIES;

        int* grown = (int*)TRSPK_REALLOC(triangles->small, new_cap * sizeof(int));
        assert(grown != NULL);
        memset(grown + triangles->cap, 0xFF, (new_cap - triangles->cap) * sizeof(int));
        triangles->small = grown;
        triangles->cap = new_cap;
        return;
    }

    if( !triangles->pages )
    {
        /* The small table becomes page 0's first entries. */
        int* page = trspk_triangles_new_page();
        trspk_triangles_table_ensure(triangles, 1u);
        if( triangles->small && triangles->cap > 0u )
            memcpy(page, triangles->small, (size_t)triangles->cap * sizeof(int));
        TRSPK_FREE(triangles->small);
        triangles->small = NULL;
        triangles->pages[0] = page;
        triangles->page_count = 1u;
        triangles->cap = TRSPK_TRI_PAGE_ENTRIES;
    }

    {
        uint32_t const pages_needed =
            (uint32_t)(((uint64_t)tri_count + TRSPK_TRI_PAGE_MASK) >> TRSPK_TRI_PAGE_SHIFT);
        trspk_triangles_table_ensure(triangles, pages_needed);
        while( triangles->page_count < pages_needed )
            triangles->pages[triangles->page_count++] = trspk_triangles_new_page();
        triangles->cap = triangles->page_count << TRSPK_TRI_PAGE_SHIFT;
    }
}

void
trspk_triangles_move(
    struct TRSPK_Triangles* triangles,
    uint32_t dst,
    uint32_t src,
    uint32_t count)
{
    assert(triangles);
    if( count == 0u || dst == src )
        return;
    assert(dst + count <= triangles->cap);
    assert(src + count <= triangles->cap);
    if( !triangles->pages )
    {
        memmove(&triangles->small[dst], &triangles->small[src], (size_t)count * sizeof(int));
        return;
    }
    /* Entry by entry in the direction that never reads what it overwrote,
     * as memmove does: ranges here can cross pages. The common case -- both
     * inside one page each -- is one memmove. */
    if( (dst >> TRSPK_TRI_PAGE_SHIFT) == ((dst + count - 1u) >> TRSPK_TRI_PAGE_SHIFT) &&
        (src >> TRSPK_TRI_PAGE_SHIFT) == ((src + count - 1u) >> TRSPK_TRI_PAGE_SHIFT) )
    {
        memmove(
            trspk_triangles_entry(triangles, dst),
            trspk_triangles_entry(triangles, src),
            (size_t)count * sizeof(int));
        return;
    }
    if( dst < src )
    {
        for( uint32_t i = 0u; i < count; i++ )
            *trspk_triangles_entry(triangles, dst + i) = *trspk_triangles_entry(triangles, src + i);
    }
    else
    {
        for( uint32_t i = count; i-- > 0u; )
            *trspk_triangles_entry(triangles, dst + i) = *trspk_triangles_entry(triangles, src + i);
    }
}
