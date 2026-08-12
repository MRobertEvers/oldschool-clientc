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
 * There are two ways to find those windows, and which one you can use is a
 * property of the arena rather than of the splitter:
 *
 *   trspk_webgl1_split16        Searches. Takes triangles while the span
 *                               between the lowest and highest index it holds
 *                               stays under 65536. Correct for any arena, and
 *                               only as good as the arena's clustering.
 *
 *   trspk_webgl1_split16_paged  Does not search. Requires an arena paged at
 *                               `page_size`, so a model never straddles a page
 *                               and a triangle's three vertices are always in
 *                               one. The base is then simply the triangle's
 *                               page, and consecutive triangles in that page
 *                               merge into one chunk.
 *
 * The paged form exists because the searching one degenerates badly, and the
 * measurement is worth recording: with the world in a single arena page, a
 * settled osrs239 scene produced **2878 chunks from one draw range** — a draw
 * call and a five-pointer attribute rebind per triangle. Painter order is
 * distance order, arena order is load order, and consecutive triangles are
 * routinely more than 65535 vertices apart in an arena of 1.3M, so the sliding
 * window closes almost every triangle.
 *
 * Paging the arena is what D3D9 has always done here (D3D9_VBO_PAGE = 65536,
 * for the identical 16-bit index limit), and its DrawIndexedPrimitive takes the
 * same (base vertex, local indices) shape. Nothing about either is
 * WebGL-specific beyond which of them a backend is forced into.
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

/*
 * The same rewrite for an arena paged at `page_size` (a power of two, at most
 * 65536). A chunk is a run of consecutive triangles sharing one page, and the
 * base is that page — no search, one pass.
 *
 * `out_straddle` counts triangles whose three vertices did not land in one
 * page. That is not something this function can paper over: the local index
 * would exceed 65535 and silently address the wrong vertex. Such triangles are
 * dropped and counted, so a broken arena invariant is visible as missing
 * geometry plus a number rather than as subtly wrong geometry.
 */
uint32_t
trspk_webgl1_split16_paged(
    struct TRSPK_DrawRangeList const* ranges,
    uint32_t const* src,
    uint16_t* dst,
    struct TRSPK_WebGL1Chunk* chunks,
    uint32_t chunk_capacity,
    uint32_t page_size,
    uint32_t* out_chunk_count,
    int* out_overflow,
    uint32_t* out_straddle);

#endif
