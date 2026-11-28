#include "trspk_vbo.h"
#include "../trspk_alloc.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

size_t
trspk_vbo_vertex_size(enum TRSPK_VertexFormat format)
{
    switch( format )
    {
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        return sizeof(struct TRSPK_VertexWebGL1);
    case TRSPK_VERTEX_FORMAT_OPENGL3_OS1:
        return sizeof(struct TRSPK_VertexOpenGl3Os1);
    case TRSPK_VERTEX_FORMAT_D3D9:
        return sizeof(struct TRSPK_VertexD3D9);
    case TRSPK_VERTEX_FORMAT_GLES2_OS1:
        return sizeof(struct TRSPK_VertexGLES2Os1);
    case TRSPK_VERTEX_FORMAT_ES3_OS235SAILING:
        return sizeof(struct TRSPK_VertexES3Os235Sailing);
    case TRSPK_VERTEX_FORMAT_ES3_OS241TFA:
        return sizeof(struct TRSPK_VertexES3Os241Tfa);
    case TRSPK_VERTEX_FORMAT_OPENGL3_OS241TFA:
        return sizeof(struct TRSPK_VertexOpenGl3Os241Tfa);
    default:
        assert(false);
        return 0;
    }
}

/* ---- storage ---------------------------------------------------------------- */

static TRSPK_StorageHook s_storage_hook;

void
TRSPK_SetStorageHook(TRSPK_StorageHook hook)
{
    s_storage_hook = hook;
}

static size_t
trspk_vbo_page_bytes(const struct TRSPK_VBO* vbo)
{
    return (size_t)TRSPK_PAGE_VERTICES * vbo->stride;
}

static void
trspk_vbo_table_ensure(
    struct TRSPK_VBO* vbo,
    uint32_t pages)
{
    uint32_t capacity;
    uint8_t** grown;
    if( pages <= vbo->page_table_capacity )
        return;
    capacity = vbo->page_table_capacity ? vbo->page_table_capacity : 16u;
    while( capacity < pages )
        capacity *= 2u;
    grown = (uint8_t**)TRSPK_REALLOC(vbo->pages, (size_t)capacity * sizeof(*grown));
    assert(grown != NULL);
    vbo->pages = grown;
    vbo->page_table_capacity = capacity;
}

/* Small -> paged: the small block's vertices become page 0's first ones. */
static void
trspk_vbo_go_paged(struct TRSPK_VBO* vbo)
{
    uint8_t* page;
    assert(!vbo->pages);
    trspk_vbo_table_ensure(vbo, 1u);
    page = (uint8_t*)TRSPK_PageAlloc(trspk_vbo_page_bytes(vbo));
    if( vbo->small && vbo->vertex_count > 0u )
        memcpy(page, vbo->small, (size_t)vbo->vertex_count * vbo->stride);
    TRSPK_FREE(vbo->small);
    vbo->small = NULL;
    vbo->pages[0] = page;
    vbo->page_count = 1u;
    vbo->capacity = TRSPK_PAGE_VERTICES;
}

/* Storage for `needed` vertices. Never moves a page that exists: growing a
 * paged VBO adds pages; only the small block is ever copied (once, into
 * page 0, or as it doubles below TRSPK_VBO_SMALL_BYTES). */
static void
trspk_vbo_storage_ensure(
    struct TRSPK_VBO* vbo,
    uint32_t needed)
{
    uint32_t pages_needed;
    if( needed <= vbo->capacity )
        return;
    if( !vbo->pages )
    {
        if( (size_t)needed * vbo->stride <= TRSPK_VBO_SMALL_BYTES )
        {
            uint32_t const most = (uint32_t)(TRSPK_VBO_SMALL_BYTES / vbo->stride);
            uint32_t capacity = vbo->capacity ? vbo->capacity * 2u : needed;
            uint8_t* grown;
            if( capacity < needed )
                capacity = needed;
            if( capacity > most )
                capacity = most;
            grown = (uint8_t*)TRSPK_REALLOC(vbo->small, (size_t)capacity * vbo->stride);
            assert(grown != NULL);
            vbo->small = grown;
            vbo->capacity = capacity;
            return;
        }
        trspk_vbo_go_paged(vbo);
    }
    pages_needed = (uint32_t)(((uint64_t)needed + TRSPK_PAGE_MASK) >> TRSPK_PAGE_SHIFT);
    trspk_vbo_table_ensure(vbo, pages_needed);
    {
        uint32_t const before = vbo->page_count;
        while( vbo->page_count < pages_needed )
            vbo->pages[vbo->page_count++] = (uint8_t*)TRSPK_PageAlloc(trspk_vbo_page_bytes(vbo));
        assert(vbo->page_count <= (UINT32_MAX >> TRSPK_PAGE_SHIFT));
        vbo->capacity = vbo->page_count << TRSPK_PAGE_SHIFT;
        if( s_storage_hook &&
            vbo->page_count / TRSPK_VBO_REPORT_PAGES != before / TRSPK_VBO_REPORT_PAGES )
            s_storage_hook(
                vbo->label ? vbo->label : "vbo",
                vbo->format,
                vbo->page_count,
                (size_t)vbo->page_count * trspk_vbo_page_bytes(vbo));
    }
}

/* Zero [first, end), a run at a time. */
static void
trspk_vbo_zero(
    struct TRSPK_VBO* vbo,
    uint32_t first,
    uint32_t end)
{
    while( first < end )
    {
        uint32_t const run = trspk_vbo_run(vbo, first, end);
        memset(trspk_vbo_at(vbo, first), 0, (size_t)run * vbo->stride);
        first += run;
    }
}

struct TRSPK_VBO*
trspk_vbo_create(
    uint32_t index_count_hint,
    enum TRSPK_VertexFormat format)
{
    struct TRSPK_VBO* vbo = (struct TRSPK_VBO*)TRSPK_MALLOC(sizeof(struct TRSPK_VBO));
    assert(vbo != NULL);
    memset(vbo, 0, sizeof(struct TRSPK_VBO));

    vbo->format = format;
    vbo->stride = (uint32_t)trspk_vbo_vertex_size(format);
    assert(vbo->stride > 0u);

    if( index_count_hint > 0 )
        trspk_vbo_growby(vbo, index_count_hint);

    return vbo;
}

void
trspk_vbo_growby(
    struct TRSPK_VBO* vbo,
    uint32_t amount)
{
    uint32_t old_count;
    uint32_t new_count;
    assert(vbo);
    if( amount == 0 )
        return;

    old_count = vbo->vertex_count;
    assert(old_count <= UINT32_MAX - amount);
    new_count = old_count + amount;

    trspk_vbo_storage_ensure(vbo, new_count);
    trspk_vbo_zero(vbo, old_count, new_count);

    vbo->vertex_count = new_count;
    /* Only what was added. Marking the whole buffer here widened every
     * caller's dirty span to all of it -- an arena load goes through this --
     * so a renderer's "upload what changed" re-sent its whole retained arena
     * on every model it baked. A GPU buffer that has to grow to hold the new
     * count is the uploader's to re-send whole, and every one does. */
    trspk_vbo_mark_dirty_range(vbo, old_count, amount);
}

uint32_t
trspk_vbo_place(
    struct TRSPK_VBO* vbo,
    uint32_t cursor,
    uint32_t count)
{
    uint32_t first = cursor;
    uint32_t end;
    assert(vbo);
    assert(count > 0u);
    assert(count <= TRSPK_POSE_MAX_VERTICES);
    /* Decided on the index alone, small or paged: a small VBO's vertices
     * become page 0's when it pages, so a pose placed before then must
     * already keep to page 0. */
    if( (first >> TRSPK_PAGE_SHIFT) != ((first + count - 1u) >> TRSPK_PAGE_SHIFT) )
        first = (first & ~TRSPK_PAGE_MASK) + TRSPK_PAGE_VERTICES;
    assert(first <= UINT32_MAX - count);
    end = first + count;
    trspk_vbo_storage_ensure(vbo, end);
    if( end > vbo->vertex_count )
    {
        /* The page tail a pose skipped is zeroed too: an upload of the span
         * sends it, and sends defined bytes. */
        trspk_vbo_zero(vbo, vbo->vertex_count, end);
        vbo->vertex_count = end;
    }
    trspk_vbo_mark_dirty_range(vbo, first, count);
    return first;
}

uint32_t
trspk_vbo_append(
    struct TRSPK_VBO* vbo,
    uint32_t count)
{
    assert(vbo);
    return trspk_vbo_place(vbo, vbo->vertex_count, count);
}

void
trspk_vbo_reserve(
    struct TRSPK_VBO* vbo,
    uint32_t capacity)
{
    assert(vbo);
    trspk_vbo_storage_ensure(vbo, capacity);
}

void
trspk_vbo_ensure_capacity(
    struct TRSPK_VBO* vbo,
    uint32_t min_vertex_count)
{
    if( vbo->vertex_count >= min_vertex_count )
        return;

    trspk_vbo_growby(vbo, min_vertex_count - vbo->vertex_count);
}

void
trspk_vbo_move(
    struct TRSPK_VBO* vbo,
    uint32_t dst,
    uint32_t src,
    uint32_t count)
{
    assert(vbo);
    if( count == 0u || dst == src )
        return;
    memmove(
        trspk_vbo_span(vbo, dst, count),
        trspk_vbo_span(vbo, src, count),
        (size_t)count * vbo->stride);
}

void
trspk_vbo_release(struct TRSPK_VBO* vbo)
{
    assert(vbo);
    if( vbo->pages )
    {
        for( uint32_t i = 0u; i < vbo->page_count; i++ )
            TRSPK_PageFree(vbo->pages[i]);
        TRSPK_FREE(vbo->pages);
    }
    TRSPK_FREE(vbo->small);
    vbo->pages = NULL;
    vbo->page_count = 0u;
    vbo->page_table_capacity = 0u;
    vbo->small = NULL;
    vbo->vertex_count = 0u;
    vbo->capacity = 0u;
    trspk_vbo_clear_dirty(vbo);
}

void
trspk_vbo_free(struct TRSPK_VBO* vbo)
{
    if( vbo == NULL )
        return;
    trspk_vbo_release(vbo);
    TRSPK_FREE(vbo);
}

void
trspk_vbo_reset(struct TRSPK_VBO* vbo)
{
    if( vbo == NULL )
        return;

    vbo->vertex_count = 0u;
    trspk_vbo_set_dirty(vbo);
}
