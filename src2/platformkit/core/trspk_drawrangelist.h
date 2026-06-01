#ifndef TRSPK_DRAWRANGELIST_H
#define TRSPK_DRAWRANGELIST_H

#include <stdint.h>

#define TRSPK_DRAWRANGELIST_NULL_IDX UINT32_MAX

struct TRSPK_DrawRange
{
    uint32_t start;
    uint32_t end;

    uint32_t base_offset;
    uint32_t config_idx;

    // Added because D3D9 and some Opengl APIs require this. DrawIndexedPrimitives, and
    // glDrawRangeElements.
    // It requires a start and end index to give the driver hints for vertex
    // cache optimization
    uint32_t min_vertex; /* min local index in [start, end) */
    uint32_t max_vertex; /* max local index in [start, end) */

    uint32_t next_idx;
};

struct TRSPK_DrawRangeList
{
    struct TRSPK_DrawRange* items;
    uint32_t capacity;
    uint32_t count;
    uint32_t head_idx;
    uint32_t tail_idx;
};

struct TRSPK_DrawRangeList*
trspk_drawrangelist_create(uint32_t capacity);

void
trspk_drawrangelist_free(struct TRSPK_DrawRangeList* list);

void
trspk_drawrangelist_clear(struct TRSPK_DrawRangeList* list);

struct TRSPK_DrawRange*
trspk_drawrangelist_push(
    struct TRSPK_DrawRangeList* list,
    uint32_t start,
    uint32_t end,
    uint32_t buffer_idx,
    uint32_t config_idx,
    uint32_t min_vertex,
    uint32_t max_vertex);

struct TRSPK_DrawRange*
trspk_drawrangelist_head(const struct TRSPK_DrawRangeList* list);

struct TRSPK_DrawRange*
trspk_drawrangelist_next(
    const struct TRSPK_DrawRangeList* list,
    const struct TRSPK_DrawRange* item);

#endif
