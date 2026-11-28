#ifndef TRSPK_VBO_H
#define TRSPK_VBO_H

#include "trspk_flags.h"
#include "trspk_indices.h"
#include "trspk_vertex.h"

// clang-format off
#include "../opengl3/opengl3_os1_vertex.h"
#include "../opengl3/opengl3_os241tfa_vertex.h"
#include "../webgl1/webgl1_vertex.h"
#include "../d3d9/d3d9_vertex.h"
#include "../gles2/gles2_os1_vertex.h"
#include "../es3/es3_os235sailing_vertex.h"
#include "../es3/es3_os241tfa_vertex.h"
// clang-format on

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TRSPK_VBO_FLAG_DIRTY TRSPK_FLAG(0)

/*
 * A VBO's CPU vertices, in PAGES.
 *
 * Vertex `i` lives in page i >> TRSPK_PAGE_SHIFT at slot i & TRSPK_PAGE_MASK.
 * Every page holds TRSPK_PAGE_VERTICES vertices of the VBO's format, so a page
 * is one of a few fixed sizes (one per format), and a page given back fits the
 * next request for one exactly. That is what keeps a heap that cannot shrink
 * (the web's) from growing a hole at a time: a world build of 1.5M vertices
 * and the next one of 4.5M take the same pages, where one block each left a
 * 78 MB hole the 235 MB build could not use (2026-11-28, an iPhone killed at a
 * 1.3 GB heap with half of it free). See trspk_alloc.h's page hooks.
 *
 * A pose -- the vertices one reservation hands out -- never crosses a page:
 * trspk_vbo_place and trspk_vbo_append start the next page when the current
 * one cannot hold it whole, leaving the tail unused. So a pose is contiguous
 * (trspk_vbo_span), and the GPU buffer, which holds vertex i at byte
 * i * stride, sees the pages laid end to end. TRSPK_PAGE_VERTICES is the
 * 16-bit lanes' page (ES2_VBO_PAGE, D3D9_VBO_PAGE), and a pose is at most
 * TRSPK_POSE_MAX_VERTICES, their limit.
 *
 * A VBO that never holds more than TRSPK_VBO_SMALL_BYTES is one ordinary block
 * that grows by doubling (`small`, no page table): a frame's UI quad stream
 * should not take a page. Past that it moves into pages and stays there.
 *
 * There is no pointer to "the vertices": walk them with trspk_vbo_at (one
 * vertex), trspk_vbo_span (one pose) or trspk_vbo_run (the contiguous run from
 * an index, for an upload or a copy).
 */
#define TRSPK_PAGE_SHIFT 16u
#define TRSPK_PAGE_VERTICES (1u << TRSPK_PAGE_SHIFT)
#define TRSPK_PAGE_MASK (TRSPK_PAGE_VERTICES - 1u)
#define TRSPK_POSE_MAX_VERTICES 65535u
#define TRSPK_VBO_SMALL_BYTES ((size_t)256u * 1024u)

struct TRSPK_VBO
{
    uint32_t vertex_count;
    uint32_t capacity;
    uint32_t flags;
    enum TRSPK_VertexFormat format;

    /* Which vertices changed since the last upload.
     *
     * The dirty FLAG says the buffer needs uploading; this says how much
     * of it. A retained buffer holds every static model in the scene, so
     * one loc morphing used to re-copy the whole thing -- megabytes to
     * the GPU because a door opened. Half-open [first, end); first >= end
     * means nothing is dirty, and a caller that cannot say which range it
     * touched marks the whole buffer, which is exactly the old behaviour. */
    uint32_t dirty_first;
    uint32_t dirty_end;

    /* Bytes per vertex: trspk_vbo_vertex_size(format). */
    uint32_t stride;
    /* Paged: page i holds vertices [i << TRSPK_PAGE_SHIFT, (i + 1) << ...),
     * and capacity == page_count << TRSPK_PAGE_SHIFT. NULL while small. */
    uint8_t** pages;
    uint32_t page_count;
    uint32_t page_table_capacity;
    /* Small: the whole storage, `capacity` vertices. NULL once paged. */
    uint8_t* small;
    /* What this buffer is ("world build", "static models"), for the storage
     * hook. A string literal; NULL reads as "vbo". */
    char const* label;
};

/*
 * Told when a VBO's pages pass another TRSPK_VBO_REPORT_PAGES: which buffer,
 * how many pages, how many bytes. Paged storage never makes one big block,
 * so ToriMem's big-block census cannot see a buffer that grows by hundreds of
 * megabytes a page at a time (the iPhone's renderer, +268 MB in six seconds,
 * 2026-11-28); this names it. A host sends it where it can be read -- the web
 * page's black box. Called inside the growth: the hook must not touch the VBO.
 * NULL removes it.
 */
#define TRSPK_VBO_REPORT_PAGES 16u

typedef void (*TRSPK_StorageHook)(
    char const* label,
    enum TRSPK_VertexFormat format,
    uint32_t pages,
    size_t bytes);

void
TRSPK_SetStorageHook(TRSPK_StorageHook hook);

static inline void
trspk_vbo_set_label(
    struct TRSPK_VBO* vbo,
    char const* label)
{
    vbo->label = label;
}

static inline bool
trspk_vbo_is_paged(const struct TRSPK_VBO* vbo)
{
    return vbo->pages != NULL;
}

/** Vertex `index`. */
static inline void*
trspk_vbo_at(
    const struct TRSPK_VBO* vbo,
    uint32_t index)
{
    assert(index < vbo->capacity);
    if( !vbo->pages )
        return vbo->small + (size_t)index * vbo->stride;
    return vbo->pages[index >> TRSPK_PAGE_SHIFT] + (size_t)(index & TRSPK_PAGE_MASK) * vbo->stride;
}

/** The `count` vertices from `first`, contiguous: one pose, or any range
 *  inside one page. */
static inline void*
trspk_vbo_span(
    const struct TRSPK_VBO* vbo,
    uint32_t first,
    uint32_t count)
{
    (void)count; /* the asserts alone read it under NDEBUG */
    assert(count > 0u);
    assert(first + count <= vbo->capacity);
    assert(!vbo->pages || (first >> TRSPK_PAGE_SHIFT) == ((first + count - 1u) >> TRSPK_PAGE_SHIFT));
    return trspk_vbo_at(vbo, first);
}

/** How many vertices from `first` (below `end`) are contiguous with it: to
 *  `end`, or to the end of first's page. An upload of [first, end) is one
 *  copy per run. */
static inline uint32_t
trspk_vbo_run(
    const struct TRSPK_VBO* vbo,
    uint32_t first,
    uint32_t end)
{
    uint32_t page_end;
    assert(first < end);
    if( !vbo->pages )
        return end - first;
    page_end = (first & ~TRSPK_PAGE_MASK) + TRSPK_PAGE_VERTICES;
    return (end < page_end ? end : page_end) - first;
}

#define TRSPK_VBO_AT(vbo, Type, index) ((Type*)trspk_vbo_at((vbo), (index)))

/*
 * What a GPU buffer that has to hold `needed` (bytes, or vertices) is made:
 * that and half again, in whole `granule`s, at least `initial`. Never the next
 * power of two. A GL buffer is the GPU's memory, not the wasm heap, but on iOS
 * it is the tab's footprint all the same, and doubling a retained buffer held
 * up to twice what it drew: the iPhone's GL buffers went 268 -> 459 MB in one
 * step before the tab was killed (2026-11-28). Half again rather than a
 * quarter: a retained buffer that grows is uploaded again whole, and a quarter
 * made the static group of a replay regrow (and re-upload) eight times on its
 * way to 49 MB where doubling did it three times. The growth is from what is
 * needed, so one step covers any jump.
 */
static inline uint32_t
trspk_gpu_capacity_for(
    uint32_t initial,
    uint32_t needed,
    uint32_t granule)
{
    uint64_t capacity = (uint64_t)needed + needed / 2u;
    assert(granule > 0u);
    if( capacity < initial )
        capacity = initial;
    capacity = (capacity + granule - 1u) / granule * granule;
    return capacity > UINT32_MAX ? needed : (uint32_t)capacity;
}

/** Bytes per vertex in `format`: a VBO's stride. */
size_t
trspk_vbo_vertex_size(enum TRSPK_VertexFormat format);

struct TRSPK_VBO*
trspk_vbo_create(
    uint32_t index_count_hint,
    enum TRSPK_VertexFormat format);

/* Extend vertex_count by `amount` zeroed vertices, in place: the grown range
 * may not cross a page (padding a page's tail does not). A pose goes through
 * trspk_vbo_append instead, which picks where it starts. */
void
trspk_vbo_growby(
    struct TRSPK_VBO* vbo,
    uint32_t amount);

/* Where `count` vertices (one pose, at most TRSPK_POSE_MAX_VERTICES) go, at
 * `cursor` or later: `cursor` itself, or the start of the next page when the
 * pose would cross one. Storage is made for them, vertex_count covers them
 * (zeroed when new), and their range is marked dirty. Returns the first. */
uint32_t
trspk_vbo_place(
    struct TRSPK_VBO* vbo,
    uint32_t cursor,
    uint32_t count);

/* trspk_vbo_place at the end: the next pose. */
uint32_t
trspk_vbo_append(
    struct TRSPK_VBO* vbo,
    uint32_t count);

/* Move `count` vertices from `src` to `dst` (memmove: they may overlap). Each
 * range must lie inside one page -- a pose does. */
void
trspk_vbo_move(
    struct TRSPK_VBO* vbo,
    uint32_t dst,
    uint32_t src,
    uint32_t count);

/* Give the storage back -- every page to the pool -- and empty the VBO. It
 * may be used again. A build dropped for the next one calls this, so the
 * next one is made of the same pages. */
void
trspk_vbo_release(struct TRSPK_VBO* vbo);

/* Room for `capacity` vertices without changing vertex_count. An empty VBO
 * takes fresh storage instead of copying its old contents. A deferred world
 * build reserves the size it is about to bake with it
 * (trspk_batch32_reserve_vertices). */
void
trspk_vbo_reserve(
    struct TRSPK_VBO* vbo,
    uint32_t capacity);

void
trspk_vbo_ensure_capacity(
    struct TRSPK_VBO* vbo,
    uint32_t min_vertex_count);

void
trspk_vbo_free(struct TRSPK_VBO* vbo);

void
trspk_vbo_reset(struct TRSPK_VBO* vbo);

static inline bool
trspk_vbo_is_dirty(const struct TRSPK_VBO* vbo)
{
    return trspk_flags_test(vbo->flags, TRSPK_VBO_FLAG_DIRTY);
}

/* Mark the WHOLE buffer dirty. The honest answer whenever the caller does
 * not know what it touched -- a device reset, a wholesale rebuild. */
static inline void
trspk_vbo_set_dirty(struct TRSPK_VBO* vbo)
{
    trspk_flags_set(&vbo->flags, TRSPK_VBO_FLAG_DIRTY);
    vbo->dirty_first = 0u;
    vbo->dirty_end = vbo->capacity;
}

/* Mark [first, first + count) dirty, widening whatever is already marked.
 * Several models can bake between two uploads, so the range is a union and
 * never a replacement. */
static inline void
trspk_vbo_mark_dirty_range(
    struct TRSPK_VBO* vbo,
    uint32_t first,
    uint32_t count)
{
    uint32_t end = first + count;

    if( count == 0u )
        return;
    if( !trspk_flags_test(vbo->flags, TRSPK_VBO_FLAG_DIRTY) )
    {
        vbo->dirty_first = first;
        vbo->dirty_end = end;
        trspk_flags_set(&vbo->flags, TRSPK_VBO_FLAG_DIRTY);
        return;
    }
    if( first < vbo->dirty_first )
        vbo->dirty_first = first;
    if( end > vbo->dirty_end )
        vbo->dirty_end = end;
}

static inline void
trspk_vbo_clear_dirty(struct TRSPK_VBO* vbo)
{
    trspk_flags_clear(&vbo->flags, TRSPK_VBO_FLAG_DIRTY);
    vbo->dirty_first = 0u;
    vbo->dirty_end = 0u;
}

static inline void
trspk_vbo_write_vertex_webgl1(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id)
{
    struct TRSPK_VertexWebGL1* vertex = TRSPK_VBO_AT(vbo, struct TRSPK_VertexWebGL1, index);

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->position[3] = 1.0f;
    vertex->color[0] = color[0];
    vertex->color[1] = color[1];
    vertex->color[2] = color[2];
    vertex->color[3] = color[3];
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->tex_id = tex_id;
    vertex->uv_mode = 0.0f;
    trspk_vbo_set_dirty(vbo);
}

static inline void
trspk_vbo_write_vertex_opengl3_os1(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id)
{
    struct TRSPK_VertexOpenGl3Os1* vertex = TRSPK_VBO_AT(vbo, struct TRSPK_VertexOpenGl3Os1, index);

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->position[3] = 1.0f;
    vertex->color[0] = color[0];
    vertex->color[1] = color[1];
    vertex->color[2] = color[2];
    vertex->color[3] = color[3];
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->tex_id = tex_id;
    vertex->uv_mode = 0.0f;
}

/* Pack four float [0,1] RGBA components into a D3D ARGB uint32_t. */
static inline uint32_t
trspk_d3d9_pack_argb(
    float r,
    float g,
    float b,
    float a)
{
    uint32_t ir = (uint32_t)(r * 255.0f + 0.5f);
    uint32_t ig = (uint32_t)(g * 255.0f + 0.5f);
    uint32_t ib = (uint32_t)(b * 255.0f + 0.5f);
    uint32_t ia = (uint32_t)(a * 255.0f + 0.5f);
    if( ir > 255u )
        ir = 255u;
    if( ig > 255u )
        ig = 255u;
    if( ib > 255u )
        ib = 255u;
    if( ia > 255u )
        ia = 255u;
    return (ia << 24u) | (ir << 16u) | (ig << 8u) | ib;
}

/* Write a vertex whose colour is ALREADY the packed D3DCOLOR the format
 * stores. A caller holding one should not have to widen it to four floats
 * only for trspk_d3d9_pack_argb to narrow it straight back.
 *
 * Unlike the float writer this does NOT set the dirty flag. A bake writes
 * hundreds of vertices into one buffer and the flag is idempotent, so the
 * caller sets it once around the loop -- see d3d9_bake_pose_vertices. */
static inline void
trspk_vbo_write_vertex_d3d9_argb(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    uint32_t argb,
    float u,
    float v,
    float tex_id)
{
    struct TRSPK_VertexD3D9* vertex = TRSPK_VBO_AT(vbo, struct TRSPK_VertexD3D9, index);

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->color = argb;
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->texdata[0] = tex_id;
    vertex->texdata[1] = 0.0f;
}

static inline void
trspk_vbo_write_vertex_d3d9(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id)
{
    struct TRSPK_VertexD3D9* vertex = TRSPK_VBO_AT(vbo, struct TRSPK_VertexD3D9, index);

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->color = trspk_d3d9_pack_argb(color[0], color[1], color[2], color[3]);
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->texdata[0] = tex_id;
    vertex->texdata[1] = 0.0f;
    trspk_vbo_set_dirty(vbo);
}

/* Write one GLES2 vertex. `rgba` is already in R,G,B,A memory order and
 * `u`/`v` are the face's LOCAL tile coordinates (see gles2_os1_vertex.h); `slot`
 * is the atlas slot (col = slot % 16, row = slot / 16) and `anim_u`/`anim_v`
 * the biased scroll speeds.
 *
 * Like the D3D9 packed writer this does NOT touch the dirty flag: a bake
 * writes a whole model and marks its own range once afterwards
 * (trspk_vbo_mark_dirty_range), which is what keeps a retained upload the
 * size of the model that changed rather than the buffer it lives in. */
static inline void
trspk_vbo_write_vertex_gles2_os1(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    uint32_t rgba,
    float u,
    float v,
    uint8_t tile_col,
    uint8_t tile_row,
    uint8_t anim_u,
    uint8_t anim_v)
{
    struct TRSPK_VertexGLES2Os1* vertex = TRSPK_VBO_AT(vbo, struct TRSPK_VertexGLES2Os1, index);

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->rgba = rgba;
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->tile_col = tile_col;
    vertex->tile_row = tile_row;
    vertex->anim_u = anim_u;
    vertex->anim_v = anim_v;
}

/** trspk_vbo_write_vertex_gles2_os1's fields and the face's raw depth bias
 *  (es3_os235sailing_vertex.h). Leaves the dirty range alone, as that one
 *  does. */
static inline void
trspk_vbo_write_vertex_es3_os235sailing(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    uint32_t rgba,
    float u,
    float v,
    uint8_t tile_col,
    uint8_t tile_row,
    uint8_t anim_u,
    uint8_t anim_v,
    int8_t depth_bias)
{
    struct TRSPK_VertexES3Os235Sailing* vertex = TRSPK_VBO_AT(vbo, struct TRSPK_VertexES3Os235Sailing, index);

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->rgba = rgba;
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->tile_col = tile_col;
    vertex->tile_row = tile_row;
    vertex->anim_u = anim_u;
    vertex->anim_v = anim_v;
    vertex->depth_bias = depth_bias;
    vertex->pad[0] = 0u;
    vertex->pad[1] = 0u;
    vertex->pad[2] = 0u;
}

/** The Sailing fields and the face's texture projector
 *  (es3_os241tfa_vertex.h): P, U, V in world space, or NULL for a face with
 *  none; P is stored relative to (x, y, z). Leaves the dirty range alone. */
static inline void
trspk_vbo_write_vertex_es3_os241tfa(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    uint32_t rgba,
    float u,
    float v,
    uint8_t tile_col,
    uint8_t tile_row,
    uint8_t anim_u,
    uint8_t anim_v,
    int8_t depth_bias,
    const float* projector_p,
    const float* projector_u,
    const float* projector_v)
{
    struct TRSPK_VertexES3Os241Tfa* vertex = TRSPK_VBO_AT(vbo, struct TRSPK_VertexES3Os241Tfa, index);

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->rgba = rgba;
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->tile_col = tile_col;
    vertex->tile_row = tile_row;
    vertex->anim_u = anim_u;
    vertex->anim_v = anim_v;
    vertex->depth_bias = depth_bias;
    vertex->pad[0] = 0u;
    vertex->pad[1] = 0u;
    vertex->pad[2] = 0u;
    vertex->pad2 = 0u;
    if( !projector_p )
    {
        memset(vertex->projector_p, 0, sizeof(vertex->projector_p));
        memset(vertex->projector_u, 0, sizeof(vertex->projector_u));
        memset(vertex->projector_v, 0, sizeof(vertex->projector_v));
        return;
    }
    vertex->projector_p[0] = trspk_float_to_half(projector_p[0] - x);
    vertex->projector_p[1] = trspk_float_to_half(projector_p[1] - y);
    vertex->projector_p[2] = trspk_float_to_half(projector_p[2] - z);
    for( int i = 0; i < 3; i++ )
    {
        vertex->projector_u[i] = trspk_float_to_half(projector_u[i]);
        vertex->projector_v[i] = trspk_float_to_half(projector_v[i]);
    }
}

#endif
