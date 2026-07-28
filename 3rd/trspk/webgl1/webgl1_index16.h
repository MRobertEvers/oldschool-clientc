#ifndef TRSPK_WEBGL1_INDEX16_H
#define TRSPK_WEBGL1_INDEX16_H

/*
 * 32-bit draw ranges -> 16-bit draw chunks.
 *
 * WebGL1 draws with GL_UNSIGNED_SHORT indices and nothing else:
 * OES_element_index_uint is an extension, and this backend uses none. A scene's
 * vertex arena runs to hundreds of thousands of vertices, so the indices the
 * world pass produces do not fit.
 *
 * They do not have to. An index is only ever read relative to wherever the
 * attribute pointers were left, so a draw whose vertices all lie inside one
 * 65536-vertex window can be expressed as (window base, 16-bit offsets) —
 * glDrawElementsBaseVertex, which WebGL1 also lacks, done with the base folded
 * into the glVertexAttribPointer offsets instead. That is what a chunk is.
 *
 * The split is a scan, not a sort: a range's indices come from faces walked in
 * painter order over one baked model, so they are already clustered and a
 * chunk usually swallows a whole range. A chunk closes only when the next
 * triangle would put more than 65535 vertices between the lowest and highest
 * index it holds.
 *
 * Nothing here is WebGL-specific beyond the limit, and D3D9's DrawIndexedPrimitive
 * takes the same (base vertex, local indices) shape.
 */

#include <stdint.h>

struct TRSPK_DrawRangeList;

/* One glDrawElements: bind attributes at `base_vertex`, then draw `index_count`
 * 16-bit indices starting `index_start` into the emitted buffer. */
struct TRSPK_WebGL1Chunk
{
    uint32_t index_start;
    uint32_t index_count;
    /* Vertex the chunk's 16-bit indices are relative to. */
    uint32_t base_vertex;
    uint32_t group;
    /* Draw config carried over from the source range (blend/cull state). */
    uint32_t config_idx;
};

/*
 * Rewrite `src` (absolute 32-bit indices, as trspk_drawrangeex_build32 left
 * them) into `dst` as 16-bit indices local to each chunk's base vertex.
 *
 * `dst` needs room for as many indices as the ranges cover; `chunks` bounds how
 * many draw calls may be produced. Returns the number of indices written and
 * sets *out_chunk_count. If the chunk table fills, the remaining ranges are
 * dropped and *out_overflow is set — a dropped triangle is better than a draw
 * whose indices point at the wrong vertices, and the caller reports it.
 */
uint32_t
trspk_webgl1_split16(
    struct TRSPK_DrawRangeList const* ranges,
    uint32_t const* src,
    uint16_t* dst,
    struct TRSPK_WebGL1Chunk* chunks,
    uint32_t chunk_capacity,
    uint32_t* out_chunk_count,
    int* out_overflow);

#endif
