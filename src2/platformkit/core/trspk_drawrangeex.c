#include "trspk_drawrangeex.h"

#include "trspk_drawrangelist.h"
#include "trspk_ibo.h"
#include "trspk_triangles.h"

#include <assert.h>
#include <string.h>

static inline void
extent_index_u16(
    uint32_t* min_v,
    uint32_t* max_v,
    uint16_t a,
    uint16_t b,
    uint16_t c)
{
    const uint32_t ua = (uint32_t)a;
    const uint32_t ub = (uint32_t)b;
    const uint32_t uc = (uint32_t)c;

    if( ua < *min_v )
        *min_v = ua;
    if( ub < *min_v )
        *min_v = ub;
    if( uc < *min_v )
        *min_v = uc;

    if( ua > *max_v )
        *max_v = ua;
    if( ub > *max_v )
        *max_v = ub;
    if( uc > *max_v )
        *max_v = uc;
}

static inline void
extent_index_u32(
    uint32_t* min_v,
    uint32_t* max_v,
    uint32_t a,
    uint32_t b,
    uint32_t c)
{
    if( a < *min_v )
        *min_v = a;
    if( b < *min_v )
        *min_v = b;
    if( c < *min_v )
        *min_v = c;

    if( a > *max_v )
        *max_v = a;
    if( b > *max_v )
        *max_v = b;
    if( c > *max_v )
        *max_v = c;
}

static void
build_ranges_for_node(
    struct TRSPK_DrawRangeList* draw_ranges,
    const struct TRSPK_Triangles* triangles,
    uint32_t group,
    uint32_t node_gpu_start,
    uint32_t page_base,
    const uint16_t* src,
    uint32_t count)
{
    if( triangles == NULL )
        return;

    uint32_t i = 0u;
    while( i < count )
    {
        const uint32_t abs_vert = page_base + (uint32_t)src[i];
        const uint32_t tri = abs_vert / 3u;
        const int tri_cfg = trspk_triangles_get(triangles, tri);
        const uint32_t encoded_cfg = trspk_triangles_encode_draw_config(tri_cfg);

        const uint32_t range_start = i;

        uint32_t min_vertex = (uint32_t)src[i];
        uint32_t max_vertex = min_vertex;
        extent_index_u16(&min_vertex, &max_vertex, src[i], src[i + 1u], src[i + 2u]);

        i += 3u;
        while( i < count )
        {
            const uint32_t abs_v = page_base + (uint32_t)src[i];
            const uint32_t t = abs_v / 3u;
            const int cfg = trspk_triangles_get(triangles, t);
            if( trspk_triangles_encode_draw_config(cfg) != encoded_cfg )
                break;

            extent_index_u16(&min_vertex, &max_vertex, src[i], src[i + 1u], src[i + 2u]);
            i += 3u;
        }

        trspk_drawrangelist_push(
            draw_ranges,
            node_gpu_start + range_start,
            node_gpu_start + i,
            page_base,
            encoded_cfg,
            group,
            min_vertex,
            max_vertex);
    }
}

static void
build_ranges_for_node_u32(
    struct TRSPK_DrawRangeList* draw_ranges,
    const struct TRSPK_Triangles* triangles,
    uint32_t group,
    uint32_t node_gpu_start,
    uint32_t page_base,
    const uint32_t* src,
    uint32_t count)
{
    if( triangles == NULL )
        return;

    uint32_t i = 0u;
    while( i < count )
    {
        const uint32_t abs_vert = page_base + src[i];
        const uint32_t tri = abs_vert / 3u;
        const int tri_cfg = trspk_triangles_get(triangles, tri);
        const uint32_t encoded_cfg = trspk_triangles_encode_draw_config(tri_cfg);

        const uint32_t range_start = i;

        uint32_t min_vertex = src[i];
        uint32_t max_vertex = min_vertex;
        extent_index_u32(&min_vertex, &max_vertex, src[i], src[i + 1u], src[i + 2u]);

        i += 3u;
        while( i < count )
        {
            const uint32_t abs_v = page_base + src[i];
            const uint32_t t = abs_v / 3u;
            const int cfg = trspk_triangles_get(triangles, t);
            if( trspk_triangles_encode_draw_config(cfg) != encoded_cfg )
                break;

            extent_index_u32(&min_vertex, &max_vertex, src[i], src[i + 1u], src[i + 2u]);
            i += 3u;
        }

        trspk_drawrangelist_push(
            draw_ranges,
            node_gpu_start + range_start,
            node_gpu_start + i,
            page_base,
            encoded_cfg,
            group,
            min_vertex,
            max_vertex);
    }
}

uint32_t
trspk_drawrangeex_build16(
    struct TRSPK_DrawRangeList* draw_ranges,
    const struct TRSPK_Triangles* const triangles_by_group[TRSPK_VBO_GROUP_COUNT],
    const struct TRSPK_IBOChain* ibo_chain,
    uint16_t* dst)
{
    assert(ibo_chain->index_format == TRSPK_INDEX_FORMAT_U16);
    trspk_drawrangelist_clear(draw_ranges);

    uint32_t gpu_cursor = 0u;

    for( struct TRSPK_IBOChainNode* node = ibo_chain->head; node != NULL; node = node->next )
    {
        if( node->ibo.index_count == 0u )
            continue;

        assert(node->group < TRSPK_VBO_GROUP_COUNT);

        const uint32_t node_gpu_start = gpu_cursor;
        const uint32_t page_base = node->ibo.offset;
        const uint16_t* src = node->ibo.indices.as_u16;
        const uint32_t count = node->ibo.index_count;
        const struct TRSPK_Triangles* triangles = triangles_by_group[node->group];

        memcpy(dst + gpu_cursor, src, count * sizeof(uint16_t));

        build_ranges_for_node(
            draw_ranges,
            triangles,
            node->group,
            node_gpu_start,
            page_base,
            src,
            count);

        gpu_cursor += count;
    }

    return gpu_cursor;
}

uint32_t
trspk_drawrangeex_build32(
    struct TRSPK_DrawRangeList* draw_ranges,
    const struct TRSPK_Triangles* const triangles_by_group[TRSPK_VBO_GROUP_COUNT],
    const struct TRSPK_IBOChain* ibo_chain,
    uint32_t* dst)
{
    assert(ibo_chain->index_format == TRSPK_INDEX_FORMAT_U32);
    trspk_drawrangelist_clear(draw_ranges);

    uint32_t gpu_cursor = 0u;

    for( struct TRSPK_IBOChainNode* node = ibo_chain->head; node != NULL; node = node->next )
    {
        if( node->ibo.index_count == 0u )
            continue;

        assert(node->group < TRSPK_VBO_GROUP_COUNT);

        const uint32_t node_gpu_start = gpu_cursor;
        const uint32_t page_base = node->ibo.offset;
        const uint32_t* src = node->ibo.indices.as_u32;
        const uint32_t count = node->ibo.index_count;
        const struct TRSPK_Triangles* triangles = triangles_by_group[node->group];

        memcpy(dst + gpu_cursor, src, count * sizeof(uint32_t));

        build_ranges_for_node_u32(
            draw_ranges,
            triangles,
            node->group,
            node_gpu_start,
            page_base,
            src,
            count);

        gpu_cursor += count;
    }

    return gpu_cursor;
}
