/* TRSPK_Batch32: one 32-bit world build, located by vertex base.
 *
 * What a renderer relies on: every pose lands contiguously at the base it was
 * given -- the previous pose's end, or the next page's start when the pose
 * would cross one (core/trspk_vbo.h) -- a key resolves to exactly that entry
 * and nothing else does, a repeat of a key in the same build hands back the
 * original storage, and a new build or a clear leaves nothing of the old one
 * reachable. Past 65535 vertices on purpose: one index space across pages.
 *
 * And the pages: a build's vertices are fixed-size pages from the allocator's
 * page hooks, a clear gives every one back, and the next build is made of the
 * pages the last one gave back -- not one page more.
 *
 *   make test-trspk-batch32
 */
#include "core/trspk_batch32.h"
#include "core/trspk_vbo.h"
#include "trspk_alloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* A page pool of the kind ToriMem keeps: given-back pages are handed out
 * again, per size, and counted. */
#define POOL_MAX 256
static void* g_pool[POOL_MAX];
static size_t g_pool_size[POOL_MAX];
static int g_pool_count;
static int g_pages_live;
static int g_pages_made;

static void*
test_malloc(size_t size, void* user)
{
    (void)user;
    return malloc(size);
}

static void*
test_calloc(size_t count, size_t size, void* user)
{
    (void)user;
    return calloc(count, size);
}

static void*
test_realloc(void* ptr, size_t size, void* user)
{
    (void)user;
    return realloc(ptr, size);
}

static void
test_free(void* ptr, void* user)
{
    (void)user;
    free(ptr);
}

static void*
test_page_alloc(size_t size, void* user)
{
    (void)user;
    g_pages_live++;
    for( int i = 0; i < g_pool_count; i++ )
    {
        if( g_pool_size[i] == size )
        {
            void* page = g_pool[i];
            g_pool[i] = g_pool[--g_pool_count];
            g_pool_size[i] = g_pool_size[g_pool_count];
            return page;
        }
    }
    g_pages_made++;
    /* The size rides in front, as a host's header would. */
    size_t* page = (size_t*)malloc(size + 16u);
    assert(page);
    page[0] = size;
    return (unsigned char*)page + 16u;
}

static void
test_page_free(void* page, void* user)
{
    (void)user;
    assert(g_pool_count < POOL_MAX);
    g_pages_live--;
    g_pool[g_pool_count] = page;
    g_pool_size[g_pool_count] = *(size_t*)((unsigned char*)page - 16u);
    g_pool_count++;
}

/* Where trspk_vbo_place puts a pose that would start at `cursor`. */
static uint32_t
placed(uint32_t cursor, uint32_t count)
{
    if( (cursor >> TRSPK_PAGE_SHIFT) != ((cursor + count - 1u) >> TRSPK_PAGE_SHIFT) )
        return (cursor & ~TRSPK_PAGE_MASK) + TRSPK_PAGE_VERTICES;
    return cursor;
}

#define ELEMENTS 300
#define POSES 4
/* 300 * 4 * 99 = 118,800 vertices: well past one 16-bit page. */
#define POSE_VERTICES 99u

static int g_failures = 0;

static void
check(
    int ok,
    const char* what)
{
    if( ok )
        printf("  ok   %s\n", what);
    else
    {
        printf("  FAIL %s\n", what);
        g_failures++;
    }
}

int
main(void)
{
    struct TRSPK_Batch32* batch;
    struct TRSPK_Batch32Reservation r;
    static uint32_t bases[ELEMENTS][POSES];
    uint32_t expected_base = 0u;
    int bases_ok = 1;
    int crossed = 0;
    int found_ok = 1;
    int e;
    int p;

    printf("trspk-batch32\n");
    {
        struct TRSPK_Allocator a = {
            test_malloc, test_calloc, test_realloc, test_free, NULL, test_page_alloc, test_page_free,
        };
        TRSPK_SetAllocator(&a);
    }

    batch = trspk_batch32_create(TRSPK_VERTEX_FORMAT_OPENGL3_OS1);
    trspk_batch32_begin(batch);
    for( e = 0; e < ELEMENTS; e++ )
    {
        for( p = 0; p < POSES; p++ )
        {
            uint32_t const want = placed(expected_base, POSE_VERTICES);
            trspk_batch32_reserve_pose(batch, e, p & 1, p, POSE_VERTICES, &r);
            if( r.vertex_base != want || r.vbo != trspk_batch32_vbo(batch) )
                bases_ok = 0;
            if( (r.vertex_base >> TRSPK_PAGE_SHIFT) !=
                ((r.vertex_base + POSE_VERTICES - 1u) >> TRSPK_PAGE_SHIFT) )
                crossed = 1;
            bases[e][p] = want;
            expected_base = want + POSE_VERTICES;
        }
    }
    check(bases_ok, "every pose lands contiguously at the base it was given");
    check(!crossed, "no pose crosses a page");
    check(
        trspk_batch32_vbo(batch)->vertex_count == expected_base && expected_base > 65535u,
        "one index space holds the whole build, past a 16-bit page");
    check(
        trspk_batch32_triangles(batch)->cap >= expected_base / 3u,
        "the triangle table covers every reserved triangle");

    trspk_batch32_reserve_pose(batch, 7, 1, 3, POSE_VERTICES, &r);
    check(
        r.vertex_base == bases[7][3] && trspk_batch32_vbo(batch)->vertex_count == expected_base,
        "a repeated key returns its original storage and reserves nothing");
    trspk_batch32_end(batch);

    for( e = 0; e < ELEMENTS && found_ok; e++ )
    {
        for( p = 0; p < POSES; p++ )
        {
            const struct TRSPK_Batch32Entry* entry =
                trspk_batch32_find_entry(batch, e, p & 1, p);
            if( !entry || entry->element_id != e || entry->pose_id != p ||
                entry->vertex_base != bases[e][p] ||
                entry->vertex_count != POSE_VERTICES )
            {
                found_ok = 0;
                break;
            }
        }
    }
    check(found_ok, "every key resolves to its own entry");
    check(
        trspk_batch32_find_entry(batch, ELEMENTS + 1, 0, 0) == NULL &&
            trspk_batch32_find_entry(batch, 0, 1, 0) == NULL,
        "absent keys miss (pose 0 was baked on track 0 only)");
    check(trspk_batch32_entry_count(batch) == ELEMENTS * POSES, "one entry per distinct key");

    /* A rebuild replaces the build; nothing of the old one may resolve. */
    trspk_batch32_begin(batch);
    trspk_batch32_reserve_pose(batch, 1000, 0, 0, 6u, &r);
    trspk_batch32_end(batch);
    check(
        r.vertex_base == 0u && trspk_batch32_vbo(batch)->vertex_count == 6u &&
            trspk_batch32_find_entry(batch, 0, 0, 0) == NULL &&
            trspk_batch32_find_entry(batch, 1000, 0, 0) != NULL,
        "a new build starts at zero and forgets the old one");

    trspk_batch32_clear(batch);
    check(
        trspk_batch32_vbo(batch)->vertex_count == 0u &&
            trspk_batch32_entry_count(batch) == 0u &&
            trspk_batch32_find_entry(batch, 1000, 0, 0) == NULL &&
            !trspk_batch32_building(batch),
        "clear leaves nothing reachable");
    check(g_pages_live == 0, "clear gives every page back");

    /* Build after build of different sizes, as a walk across the map does:
     * once the biggest has been made, no build makes a page. */
    {
        static const uint32_t sizes[] = { 400000u, 90000u, 250000u, 400000u, 12000u, 390000u };
        int made_after_first = 0;
        int max_live = 0;
        for( unsigned b = 0u; b < sizeof(sizes) / sizeof(sizes[0]); b++ )
        {
            int const made_before = g_pages_made;
            trspk_batch32_begin(batch);
            trspk_batch32_reserve_vertices(batch, sizes[b]);
            for( uint32_t v = 0u; v + 3000u <= sizes[b]; v += 3000u )
                trspk_batch32_reserve_pose(batch, (int)(v / 3000u), 0, 0, 3000u, &r);
            trspk_batch32_end(batch);
            if( g_pages_live > max_live )
                max_live = g_pages_live;
            if( b > 0u )
                made_after_first += g_pages_made - made_before;
            trspk_batch32_clear(batch);
        }
        check(made_after_first == 0, "later builds are made of the pages earlier ones gave back");
        check(g_pages_live == 0 && g_pages_made == max_live,
              "the pool holds the biggest build's pages and no more");
    }

    trspk_batch32_destroy(batch);

    if( g_failures )
    {
        printf("%d failures\n", g_failures);
        return 1;
    }
    printf("All trspk batch32 tests passed.\n");
    return 0;
}
