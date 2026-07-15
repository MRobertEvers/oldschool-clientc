#ifndef TRSPK_VBOCHAIN16_H
#define TRSPK_VBOCHAIN16_H

#include "trspk_vbo.h"
#include "trspk_vertex.h"

#include <stdint.h>

/* Maximum vertices in a single page of the chain (16-bit index limit). */
#define TRSPK_VBOCHAIN16_PAGE_CAPACITY 65535u

/*
 * A chain of VBO pages where no individual push crosses a 16-bit vertex
 * index boundary.  The chain grows on demand and can be rewound to the
 * beginning with trspk_vbochain16_reset() for reuse without reallocation.
 */
struct TRSPK_VBOChain16
{
    struct TRSPK_VBO** pages;
    uint32_t page_count;    /* number of pages allocated so far        */
    uint32_t page_capacity; /* capacity of the pages pointer array      */
    uint32_t current_page;  /* index of the page being written into     */
    uint32_t write_cursor;  /* vertex count committed in current_page   */
    enum TRSPK_VertexFormat format;
};

/* Describes where a push landed so the caller can write vertex data. */
struct TRSPK_VBOChain16PushResult
{
    struct TRSPK_VBO* vbo;
    uint32_t base_vertex; /* first vertex index within vbo to write */
};

struct TRSPK_VBOChain16*
trspk_vbochain16_create(enum TRSPK_VertexFormat format);

void
trspk_vbochain16_free(struct TRSPK_VBOChain16* chain);

/*
 * Rewind the chain to the start.  Existing pages are kept and will be
 * overwritten by subsequent pushes — no allocation or deallocation occurs.
 */
void
trspk_vbochain16_reset(struct TRSPK_VBOChain16* chain);

/*
 * Reserve space for `count` contiguous vertices in the chain.
 * If the push would cross a 16-bit boundary the chain advances to the
 * next page first, so the returned range is always within a single page.
 *
 * `count` must be <= TRSPK_VBOCHAIN16_PAGE_CAPACITY.
 */
struct TRSPK_VBOChain16PushResult
trspk_vbochain16_push(
    struct TRSPK_VBOChain16* chain,
    uint32_t count);

#endif
