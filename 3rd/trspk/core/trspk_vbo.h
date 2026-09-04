#ifndef TRSPK_VBO_H
#define TRSPK_VBO_H

#include "trspk_flags.h"
#include "trspk_indices.h"
#include "trspk_vertex.h"

// clang-format off
#include "../opengl3/opengl3_vertex.h"
#include "../webgl1/webgl1_vertex.h"
#include "../d3d9/d3d9_vertex.h"
#include "../gles2/gles2_vertex.h"
// clang-format on

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define TRSPK_VBO_FLAG_DIRTY TRSPK_FLAG(0)
/* The vertex writers are aimed at memory this VBO does not own -- see
 * `d3d9_write` below. */
#define TRSPK_VBO_FLAG_MAPPED TRSPK_FLAG(1)

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

    union
    {
        struct TRSPK_VertexWebGL1* as_webgl1;
        struct TRSPK_VertexOpenGl3* as_opengl3;
        struct TRSPK_VertexD3D9* as_d3d9;
        struct TRSPK_VertexGLES2* as_gles2;
    } vertices;

    /*
     * Where the D3D9 vertex writers actually put bytes.
     *
     * Normally this is `vertices.as_d3d9` -- the array this VBO owns. A
     * backend able to map its GPU vertex buffer points it at that mapping
     * instead, and then a bake writes each vertex ONCE, into driver memory,
     * rather than filling a system-memory copy that is memcpy'd across in full
     * immediately afterwards. See trspk_vbo_d3d9_map.
     *
     * Held apart from the union on purpose: the mapping is not owned here, so
     * it must never be realloc'd or freed. The reallocating paths keep this in
     * step with `vertices` whenever it is NOT mapped.
     */
    struct TRSPK_VertexD3D9* d3d9_write;
};

struct TRSPK_VBO*
trspk_vbo_create(
    uint32_t index_count_hint,
    enum TRSPK_VertexFormat format);

void
trspk_vbo_growby(
    struct TRSPK_VBO* vbo,
    uint32_t amount);

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
    struct TRSPK_VertexWebGL1* vertex = &vbo->vertices.as_webgl1[index];

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
trspk_vbo_write_vertex_opengl3(
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
    struct TRSPK_VertexOpenGl3* vertex = &vbo->vertices.as_opengl3[index];

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
/*
 * Aim the D3D9 vertex writers at `mapped`, memory this VBO does not own --
 * in practice a locked D3D9 vertex buffer.
 *
 * While mapped, the VBO's own array is NOT written, so it holds stale data and
 * must not be uploaded from; the caller is promising that everything baked in
 * this window goes straight to the GPU instead. How many vertices the mapping
 * holds is the caller's to know and to enforce -- this buffer's own capacity
 * says nothing about it.
 */
static inline void
trspk_vbo_d3d9_map(struct TRSPK_VBO* vbo, struct TRSPK_VertexD3D9* mapped)
{
    assert(vbo);
    assert(mapped);
    assert(vbo->format == TRSPK_VERTEX_FORMAT_D3D9);
    vbo->d3d9_write = mapped;
    vbo->flags |= TRSPK_VBO_FLAG_MAPPED;
}

static inline void
trspk_vbo_d3d9_unmap(struct TRSPK_VBO* vbo)
{
    assert(vbo);
    vbo->d3d9_write = vbo->vertices.as_d3d9;
    vbo->flags &= ~(uint32_t)TRSPK_VBO_FLAG_MAPPED;
}

static inline bool
trspk_vbo_is_mapped(const struct TRSPK_VBO* vbo)
{
    assert(vbo);
    return (vbo->flags & TRSPK_VBO_FLAG_MAPPED) != 0u;
}

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
    struct TRSPK_VertexD3D9* vertex = &vbo->d3d9_write[index];

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
    struct TRSPK_VertexD3D9* vertex = &vbo->d3d9_write[index];

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
 * `u`/`v` are the face's LOCAL tile coordinates (see gles2_vertex.h); `slot`
 * is the atlas slot (col = slot % 16, row = slot / 16) and `anim_u`/`anim_v`
 * the biased scroll speeds.
 *
 * Like the D3D9 packed writer this does NOT touch the dirty flag: a bake
 * writes a whole model and marks its own range once afterwards
 * (trspk_vbo_mark_dirty_range), which is what keeps a retained upload the
 * size of the model that changed rather than the buffer it lives in. */
static inline void
trspk_vbo_write_vertex_gles2(
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
    struct TRSPK_VertexGLES2* vertex = &vbo->vertices.as_gles2[index];

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

#endif
