#include "trspk_vbochain16.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_PAGE_ARRAY_CAPACITY 4u

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void
ensure_page_array_capacity(struct TRSPK_VBOChain16* chain, uint32_t needed)
{
    if( needed <= chain->page_capacity )
        return;

    uint32_t new_cap = chain->page_capacity == 0 ? INITIAL_PAGE_ARRAY_CAPACITY
                                                  : chain->page_capacity * 2u;
    while( new_cap < needed )
        new_cap *= 2u;

    struct TRSPK_VBO** grown =
        (struct TRSPK_VBO**)realloc(chain->pages, sizeof(struct TRSPK_VBO*) * new_cap);
    assert(grown != NULL);
    chain->pages = grown;
    chain->page_capacity = new_cap;
}

/* Advance to the next page, allocating it if it does not yet exist. */
static void
advance_page(struct TRSPK_VBOChain16* chain)
{
    chain->current_page += 1u;
    chain->write_cursor = 0u;

    if( chain->current_page < chain->page_count )
        return; /* reuse an existing page that survived a reset */

    ensure_page_array_capacity(chain, chain->current_page + 1u);
    chain->pages[chain->current_page] =
        trspk_vbo_create(TRSPK_VBOCHAIN16_PAGE_CAPACITY, chain->format);
    chain->page_count = chain->current_page + 1u;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

struct TRSPK_VBOChain16*
trspk_vbochain16_create(enum TRSPK_VertexFormat format)
{
    struct TRSPK_VBOChain16* chain =
        (struct TRSPK_VBOChain16*)malloc(sizeof(struct TRSPK_VBOChain16));
    assert(chain != NULL);
    memset(chain, 0, sizeof(struct TRSPK_VBOChain16));

    chain->format = format;

    /* Pre-allocate the pointer array and the first page. */
    ensure_page_array_capacity(chain, 1u);
    chain->pages[0] = trspk_vbo_create(TRSPK_VBOCHAIN16_PAGE_CAPACITY, format);
    chain->page_count = 1u;

    return chain;
}

void
trspk_vbochain16_free(struct TRSPK_VBOChain16* chain)
{
    if( chain == NULL )
        return;

    for( uint32_t i = 0u; i < chain->page_count; ++i )
        trspk_vbo_free(chain->pages[i]);

    free(chain->pages);
    free(chain);
}

void
trspk_vbochain16_reset(struct TRSPK_VBOChain16* chain)
{
    chain->current_page = 0u;
    chain->write_cursor = 0u;
    /* pages and their allocations are intentionally kept for reuse */
}

struct TRSPK_VBOChain16PushResult
trspk_vbochain16_push(struct TRSPK_VBOChain16* chain, uint32_t count)
{
    assert(count > 0u);
    assert(count <= TRSPK_VBOCHAIN16_PAGE_CAPACITY);

    /* Roll to the next page if this push would cross the 16-bit boundary. */
    if( chain->write_cursor + count > TRSPK_VBOCHAIN16_PAGE_CAPACITY )
        advance_page(chain);

    struct TRSPK_VBO* vbo = chain->pages[chain->current_page];

    /* Grow the VBO if it does not yet have room for all requested vertices. */
    trspk_vbo_ensure_capacity(vbo, chain->write_cursor + count);

    struct TRSPK_VBOChain16PushResult result;
    result.vbo = vbo;
    result.base_vertex = chain->write_cursor;

    chain->write_cursor += count;

    return result;
}
