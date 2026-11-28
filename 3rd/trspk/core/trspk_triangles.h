#ifndef TRSPK_TRIANGLES_H
#define TRSPK_TRIANGLES_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/* Per global triangle: atlas/static UVs. */
#define TRSPK_TRIANGLES_ATLAS (-1)

#define TRSPK_TRIANGLES_INIT_CAP 4096u

/*
 * One config per triangle, in pages, for the same reason a VBO's vertices are
 * (core/trspk_vbo.h): a world build's table is millions of entries, and one
 * block per build left holes the next build's did not fit.
 *
 * Triangle t is entry t & TRSPK_TRI_PAGE_MASK of page t >> TRSPK_TRI_PAGE_SHIFT;
 * a page is TRSPK_TRI_PAGE_ENTRIES ints. A table of at most
 * TRSPK_TRI_SMALL_ENTRIES is one ordinary block that doubles (`small`); past
 * that it is pages. A page ends at a triangle that is a multiple of 65536,
 * i.e. at vertex 3 * 65536 * k -- a VBO page boundary -- so a pose's
 * triangles never cross a page either: trspk_triangles_span.
 *
 * New entries read as -1 (TRSPK_TRIANGLES_ATLAS).
 */
#define TRSPK_TRI_PAGE_SHIFT 16u
#define TRSPK_TRI_PAGE_ENTRIES (1u << TRSPK_TRI_PAGE_SHIFT)
#define TRSPK_TRI_PAGE_MASK (TRSPK_TRI_PAGE_ENTRIES - 1u)
#define TRSPK_TRI_SMALL_ENTRIES 16384u

struct TRSPK_Triangles
{
    /* Small: the whole table, `cap` entries. NULL once paged. */
    int* small;
    /* Paged: cap == page_count << TRSPK_TRI_PAGE_SHIFT. NULL while small. */
    int** pages;
    uint32_t page_count;
    uint32_t page_table_capacity;
    uint32_t cap;
};

/* Give every page back and empty the table; it may be used again. */
void
trspk_triangles_free(struct TRSPK_Triangles* triangles);

void
trspk_triangles_ensure(
    struct TRSPK_Triangles* triangles,
    uint32_t tri_count);

static inline uint32_t
trspk_triangles_index_from_vertex(uint32_t vertex_index)
{
    return vertex_index / 3u;
}

static inline int
trspk_triangles_make_config(
    int tex_id,
    bool is_animated)
{
    return is_animated ? tex_id : TRSPK_TRIANGLES_ATLAS;
}

static inline uint32_t
trspk_triangles_encode_draw_config(int cfg)
{
    return cfg < 0 ? 0u : (uint32_t)(cfg + 1);
}

static inline int*
trspk_triangles_entry(
    const struct TRSPK_Triangles* triangles,
    uint32_t tri_index)
{
    assert(tri_index < triangles->cap && "Invalid triangle index");
    if( !triangles->pages )
        return &triangles->small[tri_index];
    return &triangles->pages[tri_index >> TRSPK_TRI_PAGE_SHIFT][tri_index & TRSPK_TRI_PAGE_MASK];
}

/** `count` entries from `first`, contiguous: they must not cross a page. */
static inline int*
trspk_triangles_span(
    const struct TRSPK_Triangles* triangles,
    uint32_t first,
    uint32_t count)
{
    (void)count; /* the asserts alone read it under NDEBUG */
    assert(count > 0u);
    assert(first + count <= triangles->cap);
    assert(!triangles->pages ||
           (first >> TRSPK_TRI_PAGE_SHIFT) == ((first + count - 1u) >> TRSPK_TRI_PAGE_SHIFT));
    return trspk_triangles_entry(triangles, first);
}

/** How many entries from `first` (below `end`) are contiguous with it. */
static inline uint32_t
trspk_triangles_run(
    const struct TRSPK_Triangles* triangles,
    uint32_t first,
    uint32_t end)
{
    uint32_t page_end;
    assert(first < end);
    if( !triangles->pages )
        return end - first;
    page_end = (first & ~TRSPK_TRI_PAGE_MASK) + TRSPK_TRI_PAGE_ENTRIES;
    return (end < page_end ? end : page_end) - first;
}

/* memmove `count` entries from `src` to `dst`, either range crossing pages. */
void
trspk_triangles_move(
    struct TRSPK_Triangles* triangles,
    uint32_t dst,
    uint32_t src,
    uint32_t count);

static inline void
trspk_triangles_set(
    struct TRSPK_Triangles* triangles,
    uint32_t tri_index,
    int value)
{
    *trspk_triangles_entry(triangles, tri_index) = (int16_t)value;
}

static inline int
trspk_triangles_get(
    const struct TRSPK_Triangles* triangles,
    uint32_t tri_index)
{
    return *trspk_triangles_entry(triangles, tri_index);
}

#endif
