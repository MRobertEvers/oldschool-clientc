#include "../../tools/trspk_dynamic_pass.h"
#include "../../tools/trspk_lru_model_cache.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_vertex_buffer.h"
#include "../../tools/trspk_vertex_format.h"
#include "trspk_webgl1.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#endif

#define TRSPK_WEBGL1_DYNAMIC_CHUNK_VERTEX_CAPACITY 65535u
#define TRSPK_WEBGL1_DYNAMIC_CHUNK_INDEX_CAPACITY 65535u

static uint32_t
trspk_webgl1_slot_table_index(
    TRSPK_ModelId model_id,
    uint32_t pose_index)
{
    return (uint32_t)model_id * TRSPK_MAX_POSES_PER_MODEL + pose_index;
}

static TRSPK_DynamicSlotmap16*
trspk_webgl1_slotmap_for_usage(
    TRSPK_WebGL1Renderer* r,
    TRSPK_UsageClass usage)
{
    if( !r )
        return NULL;
    return usage == TRSPK_USAGE_PROJECTILE ? r->dynamic_projectile_slotmap : r->dynamic_npc_slotmap;
}

static GLuint*
trspk_webgl1_vbos_for_usage(
    TRSPK_WebGL1Renderer* r,
    TRSPK_UsageClass usage)
{
    return usage == TRSPK_USAGE_PROJECTILE ? r->dynamic_projectile_vbos : r->dynamic_npc_vbos;
}

static GLuint*
trspk_webgl1_ebos_for_usage(
    TRSPK_WebGL1Renderer* r,
    TRSPK_UsageClass usage)
{
    return usage == TRSPK_USAGE_PROJECTILE ? r->dynamic_projectile_ebos : r->dynamic_npc_ebos;
}

static bool
trspk_webgl1_ensure_chunk(
    TRSPK_WebGL1Renderer* r,
    TRSPK_UsageClass usage,
    uint8_t chunk)
{
    if( !r || chunk >= TRSPK_MAX_WEBGL1_CHUNKS )
        return false;
#ifdef __EMSCRIPTEN__
    GLuint* vbos = trspk_webgl1_vbos_for_usage(r, usage);
    GLuint* ebos = trspk_webgl1_ebos_for_usage(r, usage);
    const GLenum gl_usage = usage == TRSPK_USAGE_PROJECTILE ? GL_STREAM_DRAW : GL_DYNAMIC_DRAW;
    if( vbos[chunk] == 0u )
    {
        glGenBuffers(1, &vbos[chunk]);
        glBindBuffer(GL_ARRAY_BUFFER, vbos[chunk]);
        glBufferData(
            GL_ARRAY_BUFFER,
            (GLsizeiptr)(TRSPK_WEBGL1_DYNAMIC_CHUNK_VERTEX_CAPACITY *
                         trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1)),
            NULL,
            gl_usage);
    }
    if( ebos[chunk] == 0u )
    {
        glGenBuffers(1, &ebos[chunk]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebos[chunk]);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            (GLsizeiptr)(TRSPK_WEBGL1_DYNAMIC_CHUNK_INDEX_CAPACITY * sizeof(uint16_t)),
            NULL,
            gl_usage);
    }
    return vbos[chunk] != 0u && ebos[chunk] != 0u;
#else
    (void)usage;
    return true;
#endif
}

#ifdef __EMSCRIPTEN__

typedef struct TRSPK_W1FlushBakedRow
{
    TRSPK_VertexBuffer baked;
    TRSPK_UsageClass usage;
    uint8_t chunk;
    uint32_t vbo_beg_bytes;
    uint32_t vbo_len_bytes;
    uint32_t ebo_beg_bytes;
    uint32_t ebo_len_bytes;
    uint16_t* ib_u16;
} TRSPK_W1FlushBakedRow;

static void
trspk_w1_flush_sort_indices_by_vbo(
    uint32_t* idx,
    int n,
    const TRSPK_W1FlushBakedRow* rows)
{
    for( int i = 1; i < n; ++i )
    {
        const uint32_t key = idx[i];
        const uint32_t kb = rows[key].vbo_beg_bytes;
        int j = i - 1;
        while( j >= 0 && rows[idx[j]].vbo_beg_bytes > kb )
        {
            idx[j + 1] = idx[j];
            --j;
        }
        idx[j + 1] = key;
    }
}

static void
trspk_w1_flush_sort_indices_by_ebo(
    uint32_t* idx,
    int n,
    const TRSPK_W1FlushBakedRow* rows)
{
    for( int i = 1; i < n; ++i )
    {
        const uint32_t key = idx[i];
        const uint32_t kb = rows[key].ebo_beg_bytes;
        int j = i - 1;
        while( j >= 0 && rows[idx[j]].ebo_beg_bytes > kb )
        {
            idx[j + 1] = idx[j];
            --j;
        }
        idx[j + 1] = key;
    }
}

static void
trspk_w1_flush_upload_merged_vbo_runs(
    TRSPK_WebGL1Renderer* r,
    TRSPK_UsageClass usage,
    uint8_t chunk,
    TRSPK_W1FlushBakedRow* rows,
    uint32_t* idx,
    int n)
{
    if( n <= 0 || chunk >= TRSPK_MAX_WEBGL1_CHUNKS )
        return;
    const GLuint buf = trspk_webgl1_vbos_for_usage(r, usage)[chunk];
    if( buf == 0u )
        return;
    int p = 0;
    while( p < n )
    {
        const uint32_t i0 = idx[p];
        uint32_t run_lo = rows[i0].vbo_beg_bytes;
        uint32_t run_hi = run_lo + rows[i0].vbo_len_bytes;
        int q = p + 1;
        while( q < n )
        {
            const uint32_t ij = idx[q];
            const uint32_t lo = rows[ij].vbo_beg_bytes;
            const uint32_t hi = lo + rows[ij].vbo_len_bytes;
            if( lo <= run_hi )
            {
                if( hi > run_hi )
                    run_hi = hi;
                ++q;
            }
            else
                break;
        }
        const size_t total = (size_t)run_hi - (size_t)run_lo;
        uint8_t* scratch = (uint8_t*)malloc(total);
        if( !scratch )
            break;
        for( int t = p; t < q; ++t )
        {
            const uint32_t ri = idx[t];
            memcpy(
                scratch + ((size_t)rows[ri].vbo_beg_bytes - (size_t)run_lo),
                rows[ri].baked.vertices.as_webgl1,
                (size_t)rows[ri].vbo_len_bytes);
        }
        glBindBuffer(GL_ARRAY_BUFFER, buf);
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)run_lo, (GLsizeiptr)total, scratch);
        glBindBuffer(GL_ARRAY_BUFFER, 0u);
        free(scratch);
        p = q;
    }
}

static void
trspk_w1_flush_upload_merged_ebo_runs(
    TRSPK_WebGL1Renderer* r,
    TRSPK_UsageClass usage,
    uint8_t chunk,
    TRSPK_W1FlushBakedRow* rows,
    uint32_t* idx,
    int n)
{
    if( n <= 0 || chunk >= TRSPK_MAX_WEBGL1_CHUNKS )
        return;
    const GLuint buf = trspk_webgl1_ebos_for_usage(r, usage)[chunk];
    if( buf == 0u )
        return;
    int p = 0;
    while( p < n )
    {
        const uint32_t i0 = idx[p];
        uint32_t run_lo = rows[i0].ebo_beg_bytes;
        uint32_t run_hi = run_lo + rows[i0].ebo_len_bytes;
        int q = p + 1;
        while( q < n )
        {
            const uint32_t ij = idx[q];
            const uint32_t lo = rows[ij].ebo_beg_bytes;
            const uint32_t hi = lo + rows[ij].ebo_len_bytes;
            if( lo <= run_hi )
            {
                if( hi > run_hi )
                    run_hi = hi;
                ++q;
            }
            else
                break;
        }
        const size_t total = (size_t)run_hi - (size_t)run_lo;
        uint8_t* scratch = (uint8_t*)malloc(total);
        if( !scratch )
            break;
        for( int t = p; t < q; ++t )
        {
            const uint32_t ri = idx[t];
            memcpy(
                scratch + ((size_t)rows[ri].ebo_beg_bytes - (size_t)run_lo),
                rows[ri].ib_u16,
                (size_t)rows[ri].ebo_len_bytes);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)run_lo, (GLsizeiptr)total, scratch);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);
        free(scratch);
        p = q;
    }
}

static void
trspk_webgl1_upload_dynamic_interleaved_ranges(
    TRSPK_WebGL1Renderer* r,
    TRSPK_UsageClass usage,
    uint8_t chunk,
    uint32_t vbo_offset_vertices,
    uint32_t ebo_offset_u16,
    const void* vertices,
    uint32_t vertex_bytes,
    const void* indices_u16,
    uint32_t index_bytes)
{
    if( chunk >= TRSPK_MAX_WEBGL1_CHUNKS || !vertices || !indices_u16 )
        return;
    GLuint* upload_vbos = trspk_webgl1_vbos_for_usage(r, usage);
    GLuint* upload_ebos = trspk_webgl1_ebos_for_usage(r, usage);
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1);
    const uint32_t vbo_byte_off = vbo_offset_vertices * stride;
    const uint32_t ebo_byte_off = (uint32_t)((size_t)ebo_offset_u16 * sizeof(uint16_t));
    glBindBuffer(GL_ARRAY_BUFFER, upload_vbos[chunk]);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        (GLintptr)(size_t)vbo_byte_off,
        (GLsizeiptr)vertex_bytes,
        vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0u);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, upload_ebos[chunk]);
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        (GLintptr)(size_t)ebo_byte_off,
        (GLsizeiptr)index_bytes,
        indices_u16);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);
}

#endif /* __EMSCRIPTEN__ */

static void
trspk_webgl1_dynamic_free_pose_slot(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    uint32_t pose_index)
{
    if( !r || !r->dynamic_slot_handles || !r->dynamic_slot_usages ||
        pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return;
    const uint32_t idx = trspk_webgl1_slot_table_index(model_id, pose_index);
    const TRSPK_DynamicSlotHandle handle = r->dynamic_slot_handles[idx];
    if( handle == TRSPK_DYNAMIC_SLOT_INVALID )
        return;
    TRSPK_DynamicSlotmap16* map = trspk_webgl1_slotmap_for_usage(r, r->dynamic_slot_usages[idx]);
    trspk_dynamic_slotmap16_free(map, handle);
    r->dynamic_slot_handles[idx] = TRSPK_DYNAMIC_SLOT_INVALID;
    r->dynamic_slot_usages[idx] = TRSPK_USAGE_SCENERY;
}

bool
trspk_webgl1_dynamic_init(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return false;
    const uint32_t table_count = TRSPK_MAX_MODELS * TRSPK_MAX_POSES_PER_MODEL;
    r->dynamic_slot_handles =
        (TRSPK_DynamicSlotHandle*)malloc(sizeof(TRSPK_DynamicSlotHandle) * table_count);
    r->dynamic_slot_usages = (TRSPK_UsageClass*)calloc(table_count, sizeof(TRSPK_UsageClass));
    if( !r->dynamic_slot_handles || !r->dynamic_slot_usages )
        return false;
    for( uint32_t i = 0; i < table_count; ++i )
        r->dynamic_slot_handles[i] = TRSPK_DYNAMIC_SLOT_INVALID;
    r->dynamic_npc_slotmap = trspk_dynamic_slotmap16_create();
    r->dynamic_projectile_slotmap = trspk_dynamic_slotmap16_create();
    return r->dynamic_npc_slotmap && r->dynamic_projectile_slotmap;
}

void
trspk_webgl1_pass_free_pending_dynamic_uploads(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return;
    r->deferred_dynamic_bake_count = 0u;
}

void
trspk_webgl1_pass_flush_pending_dynamic_gpu_uploads(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return;
    if( !r->cache )
    {
        r->deferred_dynamic_bake_count = 0u;
        return;
    }
#ifdef __EMSCRIPTEN__
    TRSPK_LruModelCache* lru = trspk_resource_cache_lru_model_cache(r->cache);
    const uint32_t n_def = r->deferred_dynamic_bake_count;
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1);

    TRSPK_W1FlushBakedRow* rows =
        (TRSPK_W1FlushBakedRow*)calloc((size_t)n_def, sizeof(TRSPK_W1FlushBakedRow));
    if( !rows )
    {
        r->deferred_dynamic_bake_count = 0u;
        return;
    }
    int n_ok = 0;

    for( uint32_t i = 0u; i < n_def; ++i )
    {
        TRSPK_WebGL1DeferredDynamicBake* d = &r->deferred_dynamic_bakes[i];
        TRSPK_VertexBuffer baked = { 0 };
        const TRSPK_VertexBuffer* id_mesh =
            lru ? trspk_lru_model_cache_get(lru, d->lru_model_id, d->seg, d->frame_i) : NULL;
        if( !id_mesh ||
            !trspk_vertex_buffer_bake_array_to_interleaved(id_mesh, &d->bake, &baked) ||
            baked.vertex_count != d->vertex_count || baked.index_count != d->index_count ||
            baked.format != TRSPK_VERTEX_FORMAT_WEBGL1 ||
            baked.index_format != TRSPK_INDEX_FORMAT_U32 || !baked.vertices.as_webgl1 ||
            !baked.indices.as_u32 )
        {
            trspk_vertex_buffer_free(&baked);
            trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
            continue;
        }
        if( d->chunk >= TRSPK_MAX_WEBGL1_CHUNKS )
        {
            trspk_vertex_buffer_free(&baked);
            trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
            continue;
        }
        if( !trspk_webgl1_ensure_chunk(r, d->usage, d->chunk) )
        {
            trspk_vertex_buffer_free(&baked);
            trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
            continue;
        }
        const uint32_t ic = baked.index_count;
        if( ic > r->u16_index_scratch_cap )
        {
            uint32_t cap = r->u16_index_scratch_cap ? r->u16_index_scratch_cap : 256u;
            while( cap < ic )
                cap *= 2u;
            uint16_t* n = (uint16_t*)realloc(r->u16_index_scratch, (size_t)cap * sizeof(uint16_t));
            if( !n )
            {
                trspk_vertex_buffer_free(&baked);
                trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
                continue;
            }
            r->u16_index_scratch = n;
            r->u16_index_scratch_cap = cap;
        }
        const uint32_t* const src = baked.indices.as_u32;
        uint16_t* const u16i = r->u16_index_scratch;
        bool index_ok = true;
        for( uint32_t j = 0u; j < ic; ++j )
        {
            const uint32_t k = src[j];
            if( k > 0xFFFFu )
            {
                index_ok = false;
                break;
            }
            u16i[j] = (uint16_t)k;
        }
        if( !index_ok )
        {
            trspk_vertex_buffer_free(&baked);
            trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
            continue;
        }
        uint16_t* ib_copy = (uint16_t*)malloc((size_t)ic * sizeof(uint16_t));
        if( !ib_copy )
        {
            trspk_vertex_buffer_free(&baked);
            trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
            continue;
        }
        memcpy(ib_copy, u16i, (size_t)ic * sizeof(uint16_t));

        TRSPK_W1FlushBakedRow* row = &rows[n_ok++];
        row->baked = baked;
        row->usage = d->usage;
        row->chunk = d->chunk;
        row->vbo_beg_bytes = d->vbo_offset * stride;
        row->vbo_len_bytes = baked.vertex_count * stride;
        row->ebo_beg_bytes = (uint32_t)((size_t)d->ebo_offset * sizeof(uint16_t));
        row->ebo_len_bytes = ic * (uint32_t)sizeof(uint16_t);
        row->ib_u16 = ib_copy;
    }

    if( n_ok > 0 )
    {
        uint32_t* idx = (uint32_t*)malloc((size_t)n_ok * sizeof(uint32_t));
        if( !idx )
        {
            for( int j = 0; j < n_ok; ++j )
            {
                TRSPK_W1FlushBakedRow* row = &rows[j];
                trspk_webgl1_upload_dynamic_interleaved_ranges(
                    r,
                    row->usage,
                    row->chunk,
                    row->vbo_beg_bytes / stride,
                    row->ebo_beg_bytes / (uint32_t)sizeof(uint16_t),
                    row->baked.vertices.as_webgl1,
                    row->vbo_len_bytes,
                    row->ib_u16,
                    row->ebo_len_bytes);
            }
        }
        else
        {
            for( int ustep = 0; ustep < 2; ++ustep )
            {
                const TRSPK_UsageClass usage =
                    ustep == 0 ? TRSPK_USAGE_NPC : TRSPK_USAGE_PROJECTILE;
                for( uint32_t ch = 0u; ch < TRSPK_MAX_WEBGL1_CHUNKS; ++ch )
                {
                    int c = 0;
                    for( int j = 0; j < n_ok; ++j )
                    {
                        if( rows[j].usage == usage && rows[j].chunk == (uint8_t)ch )
                            idx[c++] = (uint32_t)j;
                    }
                    if( c <= 0 )
                        continue;
                    if( !trspk_webgl1_ensure_chunk(r, usage, (uint8_t)ch) )
                        continue;
                    trspk_w1_flush_sort_indices_by_vbo(idx, c, rows);
                    trspk_w1_flush_upload_merged_vbo_runs(r, usage, (uint8_t)ch, rows, idx, c);
                    trspk_w1_flush_sort_indices_by_ebo(idx, c, rows);
                    trspk_w1_flush_upload_merged_ebo_runs(r, usage, (uint8_t)ch, rows, idx, c);
                }
            }
            free(idx);
        }
    }

    for( int j = 0; j < n_ok; ++j )
    {
        free(rows[j].ib_u16);
        trspk_vertex_buffer_free(&rows[j].baked);
    }
    free(rows);
#else
    for( uint32_t i = 0u; i < r->deferred_dynamic_bake_count; ++i )
    {
        TRSPK_WebGL1DeferredDynamicBake* d = &r->deferred_dynamic_bakes[i];
        trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
    }
#endif
    r->deferred_dynamic_bake_count = 0u;
}

static bool
trspk_webgl1_deferred_reserve(
    TRSPK_WebGL1Renderer* r,
    uint32_t need)
{
    if( need <= r->deferred_dynamic_bake_cap )
        return true;
    uint32_t cap = r->deferred_dynamic_bake_cap ? r->deferred_dynamic_bake_cap : 8u;
    while( cap < need )
        cap *= 2u;
    TRSPK_WebGL1DeferredDynamicBake* n = (TRSPK_WebGL1DeferredDynamicBake*)realloc(
        r->deferred_dynamic_bakes, cap * sizeof(TRSPK_WebGL1DeferredDynamicBake));
    if( !n )
        return false;
    r->deferred_dynamic_bakes = n;
    r->deferred_dynamic_bake_cap = cap;
    return true;
}

static bool
trspk_webgl1_dynamic_upload_interleaved(
    TRSPK_WebGL1Renderer* r,
    TRSPK_UsageClass usage,
    uint8_t chunk,
    uint32_t vbo_offset,
    uint32_t ebo_offset,
    const void* vertices,
    uint32_t vertex_bytes,
    const void* indices_u16,
    uint32_t index_bytes)
{
#ifdef __EMSCRIPTEN__
    if( chunk >= TRSPK_MAX_WEBGL1_CHUNKS || !vertices || !indices_u16 )
        return false;
    trspk_webgl1_upload_dynamic_interleaved_ranges(
        r, usage, chunk, vbo_offset, ebo_offset, vertices, vertex_bytes, indices_u16, index_bytes);
#else
    (void)r;
    (void)usage;
    (void)chunk;
    (void)vbo_offset;
    (void)ebo_offset;
    (void)vertices;
    (void)vertex_bytes;
    (void)indices_u16;
    (void)index_bytes;
#endif
    return true;
}

bool
trspk_webgl1_dynamic_enqueue_draw_mesh_deferred(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId pose_storage_model_id,
    TRSPK_ModelId lru_model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    uint32_t array_vertex_count,
    uint32_t array_index_count,
    const TRSPK_BakeTransform* bake)
{
    if( !r || !r->cache || !bake || pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return false;
    if( array_vertex_count == 0u || array_index_count == 0u )
        return false;

    const uint32_t idx = trspk_webgl1_slot_table_index(pose_storage_model_id, pose_index);
    const bool had_dynamic_slot =
        r->dynamic_slot_handles && r->dynamic_slot_handles[idx] != TRSPK_DYNAMIC_SLOT_INVALID;

    trspk_webgl1_dynamic_free_pose_slot(r, pose_storage_model_id, pose_index);

    TRSPK_DynamicSlotmap16* map = trspk_webgl1_slotmap_for_usage(r, usage);
    if( !map )
        return false;
    TRSPK_DynamicSlotHandle handle = TRSPK_DYNAMIC_SLOT_INVALID;
    uint8_t chunk = 0u;
    uint32_t vbo_offset = 0u;
    uint32_t ebo_offset = 0u;
    if( !trspk_dynamic_slotmap16_alloc(
            map, array_vertex_count, array_index_count, &handle, &chunk, &vbo_offset, &ebo_offset) )
    {
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, pose_storage_model_id, pose_index);
        return false;
    }
    if( !trspk_webgl1_ensure_chunk(r, usage, chunk) )
    {
        trspk_dynamic_slotmap16_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, pose_storage_model_id, pose_index);
        return false;
    }

    GLuint* vbos = trspk_webgl1_vbos_for_usage(r, usage);
    GLuint* ebos = trspk_webgl1_ebos_for_usage(r, usage);
    TRSPK_ModelPose pose;
    memset(&pose, 0, sizeof(pose));
    pose.vbo = (TRSPK_GPUHandle)vbos[chunk];
    pose.ebo = (TRSPK_GPUHandle)ebos[chunk];
    pose.vbo_offset = vbo_offset;
    pose.ebo_offset = ebo_offset;
    pose.element_count = array_index_count;
    pose.batch_id = TRSPK_SCENE_BATCH_NONE;
    pose.chunk_index = chunk;
    pose.usage_class = (uint8_t)usage;
    pose.dynamic = true;
    pose.valid = true;
    if( !trspk_resource_cache_set_model_pose(r->cache, pose_storage_model_id, pose_index, &pose) )
    {
        trspk_dynamic_slotmap16_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, pose_storage_model_id, pose_index);
        return false;
    }

    r->dynamic_slot_handles[idx] = handle;
    r->dynamic_slot_usages[idx] = usage;

    if( !trspk_webgl1_deferred_reserve(r, r->deferred_dynamic_bake_count + 1u) )
    {
        trspk_dynamic_slotmap16_free(map, handle);
        r->dynamic_slot_handles[idx] = TRSPK_DYNAMIC_SLOT_INVALID;
        r->dynamic_slot_usages[idx] = TRSPK_USAGE_SCENERY;
        trspk_resource_cache_invalidate_model_pose(r->cache, pose_storage_model_id, pose_index);
        return false;
    }
    TRSPK_WebGL1DeferredDynamicBake* q =
        &r->deferred_dynamic_bakes[r->deferred_dynamic_bake_count++];
    q->usage = usage;
    q->model_id = pose_storage_model_id;
    q->lru_model_id = lru_model_id;
    q->pose_index = pose_index;
    q->seg = gpu_segment_slot;
    q->frame_i = frame_index;
    q->bake = *bake;
    q->chunk = chunk;
    q->vbo_offset = vbo_offset;
    q->ebo_offset = ebo_offset;
    q->vertex_count = array_vertex_count;
    q->index_count = array_index_count;
    return true;
}

void
trspk_webgl1_dynamic_shutdown(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return;
    free(r->deferred_dynamic_bakes);
    r->deferred_dynamic_bakes = NULL;
    r->deferred_dynamic_bake_count = 0u;
    r->deferred_dynamic_bake_cap = 0u;
#ifdef __EMSCRIPTEN__
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
    {
        if( r->dynamic_npc_vbos[i] )
            glDeleteBuffers(1, &r->dynamic_npc_vbos[i]);
        if( r->dynamic_npc_ebos[i] )
            glDeleteBuffers(1, &r->dynamic_npc_ebos[i]);
        if( r->dynamic_projectile_vbos[i] )
            glDeleteBuffers(1, &r->dynamic_projectile_vbos[i]);
        if( r->dynamic_projectile_ebos[i] )
            glDeleteBuffers(1, &r->dynamic_projectile_ebos[i]);
    }
#endif
    trspk_dynamic_slotmap16_destroy(r->dynamic_npc_slotmap);
    trspk_dynamic_slotmap16_destroy(r->dynamic_projectile_slotmap);
    free(r->dynamic_slot_handles);
    free(r->dynamic_slot_usages);
    free(r->u16_index_scratch);
    r->dynamic_npc_slotmap = NULL;
    r->dynamic_projectile_slotmap = NULL;
    r->dynamic_slot_handles = NULL;
    r->dynamic_slot_usages = NULL;
    r->u16_index_scratch = NULL;
    r->u16_index_scratch_cap = 0u;
}

static bool
trspk_webgl1_dynamic_queue_interleaved(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    uint32_t vertex_count,
    uint32_t index_count,
    const void* vertices,
    uint32_t vertex_bytes,
    const void* indices_u16,
    uint32_t index_bytes)
{
    if( !r || !r->cache || !vertices || !indices_u16 || pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return false;
    if( vertex_count == 0u || index_count == 0u || vertex_bytes == 0u || index_bytes == 0u )
        return false;

    const uint32_t idx = trspk_webgl1_slot_table_index(model_id, pose_index);
    const bool had_dynamic_slot =
        r->dynamic_slot_handles && r->dynamic_slot_handles[idx] != TRSPK_DYNAMIC_SLOT_INVALID;

    trspk_webgl1_dynamic_free_pose_slot(r, model_id, pose_index);

    TRSPK_DynamicSlotmap16* map = trspk_webgl1_slotmap_for_usage(r, usage);
    if( !map )
        return false;
    TRSPK_DynamicSlotHandle handle = TRSPK_DYNAMIC_SLOT_INVALID;
    uint8_t chunk = 0u;
    uint32_t vbo_offset = 0u;
    uint32_t ebo_offset = 0u;
    if( !trspk_dynamic_slotmap16_alloc(
            map, vertex_count, index_count, &handle, &chunk, &vbo_offset, &ebo_offset) )
    {
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
        return false;
    }
    if( !trspk_webgl1_ensure_chunk(r, usage, chunk) )
    {
        trspk_dynamic_slotmap16_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
        return false;
    }

    GLuint* vbos = trspk_webgl1_vbos_for_usage(r, usage);
    GLuint* ebos = trspk_webgl1_ebos_for_usage(r, usage);
    TRSPK_ModelPose pose;
    memset(&pose, 0, sizeof(pose));
    pose.vbo = (TRSPK_GPUHandle)vbos[chunk];
    pose.ebo = (TRSPK_GPUHandle)ebos[chunk];
    pose.vbo_offset = vbo_offset;
    pose.ebo_offset = ebo_offset;
    pose.element_count = index_count;
    pose.batch_id = TRSPK_SCENE_BATCH_NONE;
    pose.chunk_index = chunk;
    pose.usage_class = (uint8_t)usage;
    pose.dynamic = true;
    pose.valid = true;
    if( !trspk_resource_cache_set_model_pose(r->cache, model_id, pose_index, &pose) )
    {
        trspk_dynamic_slotmap16_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
        return false;
    }

    if( !trspk_webgl1_dynamic_upload_interleaved(
            r,
            usage,
            chunk,
            vbo_offset,
            ebo_offset,
            vertices,
            vertex_bytes,
            indices_u16,
            index_bytes) )
    {
        trspk_dynamic_slotmap16_free(map, handle);
        trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
        return false;
    }

    r->dynamic_slot_handles[idx] = handle;
    r->dynamic_slot_usages[idx] = usage;
    return true;
}

static bool
trspk_webgl1_dynamic_store_mesh(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const TRSPK_BakeTransform* bake)
{
    if( !r || !model || !r->cache || pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return false;
    TRSPK_DynamicMesh mesh;
    if( !trspk_dynamic_mesh_build(model, TRSPK_VERTEX_FORMAT_WEBGL1, bake, r->cache, &mesh) )
        return false;

    const bool ok = trspk_webgl1_dynamic_queue_interleaved(
        r,
        model_id,
        usage,
        pose_index,
        mesh.vertex_count,
        mesh.index_count,
        mesh.vertices,
        mesh.vertex_bytes,
        mesh.indices,
        mesh.index_bytes);
    trspk_dynamic_mesh_clear(&mesh);
    return ok;
}

bool
trspk_webgl1_dynamic_store_vertex_buffer(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const TRSPK_VertexBuffer* vb)
{
    if( !vb || vb->format != TRSPK_VERTEX_FORMAT_WEBGL1 ||
        vb->index_format != TRSPK_INDEX_FORMAT_U32 || !vb->vertices.as_webgl1 ||
        !vb->indices.as_u32 )
        return false;
    const uint32_t ic = vb->index_count;
    if( ic > r->u16_index_scratch_cap )
    {
        uint32_t cap = r->u16_index_scratch_cap ? r->u16_index_scratch_cap : 256u;
        while( cap < ic )
            cap *= 2u;
        uint16_t* n = (uint16_t*)realloc(r->u16_index_scratch, (size_t)cap * sizeof(uint16_t));
        if( !n )
            return false;
        r->u16_index_scratch = n;
        r->u16_index_scratch_cap = cap;
    }
    uint16_t* const u16i = r->u16_index_scratch;
    const uint32_t* const src = vb->indices.as_u32;
    for( uint32_t i = 0; i < ic; ++i )
    {
        const uint32_t k = src[i];
        if( k > 0xFFFFu )
            return false;
        u16i[i] = (uint16_t)k;
    }
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1);
    return trspk_webgl1_dynamic_queue_interleaved(
        r,
        model_id,
        usage,
        pose_index,
        vb->vertex_count,
        ic,
        vb->vertices.as_webgl1,
        vb->vertex_count * stride,
        u16i,
        ic * (uint32_t)sizeof(uint16_t));
}

bool
trspk_webgl1_dynamic_store_dynamic_mesh(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    TRSPK_DynamicMesh* mesh)
{
    if( !mesh )
        return false;
    const bool ok = trspk_webgl1_dynamic_queue_interleaved(
        r,
        model_id,
        usage,
        pose_index,
        mesh->vertex_count,
        mesh->index_count,
        mesh->vertices,
        mesh->vertex_bytes,
        mesh->indices,
        mesh->index_bytes);
    trspk_dynamic_mesh_clear(mesh);
    return ok;
}

bool
trspk_webgl1_dynamic_load_model(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake)
{
    return trspk_webgl1_dynamic_store_mesh(r, model_id, model, usage_class, 0u, bake);
}

bool
trspk_webgl1_dynamic_load_anim(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    uint8_t segment,
    uint16_t frame_index,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake)
{
    if( !r || !r->cache )
        return false;
    const uint32_t pose_index =
        trspk_resource_cache_allocate_pose_slot(r->cache, model_id, segment, frame_index);
    return trspk_webgl1_dynamic_store_mesh(r, model_id, model, usage_class, pose_index, bake);
}

void
trspk_webgl1_dynamic_unload_model(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id)
{
    if( !r || !r->cache )
        return;
    for( uint32_t pose = 0; pose < TRSPK_MAX_POSES_PER_MODEL; ++pose )
        trspk_webgl1_dynamic_free_pose_slot(r, model_id, pose);
    trspk_resource_cache_clear_model(r->cache, model_id);
}
