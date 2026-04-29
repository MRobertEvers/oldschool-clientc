#include "../../tools/trspk_dynamic_pass.h"
#include "../../tools/trspk_lru_model_cache.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_vertex_buffer.h"
#include "../../tools/trspk_vertex_format.h"
#include "opengl3_internal.h"
#include "trspk_opengl3.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static uint32_t*
trspk_opengl3_vbo_ptr_for_usage(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage);
static uint32_t*
trspk_opengl3_ebo_ptr_for_usage(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage);

#define TRSPK_OGL3_DYNAMIC_VERTEX_CAPACITY 4096u
#define TRSPK_OGL3_DYNAMIC_INDEX_CAPACITY_INITIAL 12288u

/** One successfully CPU-baked deferred dynamic entry before merged GPU upload. */
typedef struct TRSPK_Ogl3FlushBakedRow
{
    TRSPK_DynamicBatchFlushRow gpu;
    TRSPK_VertexBuffer baked;
    TRSPK_UsageClass usage;
} TRSPK_Ogl3FlushBakedRow;

static void
ogl3_dynamic_batch_flush_upload(
    void* ctx,
    int is_element_array_buffer,
    uintptr_t buffer_id,
    intptr_t offset,
    size_t size,
    const void* data)
{
    (void)ctx;
    const TRSPK_GLuint buf = (TRSPK_GLuint)buffer_id;
    const TRSPK_GLenum target =
        is_element_array_buffer ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    trspk_glBindBuffer(target, buf);
    trspk_glBufferSubData(target, (TRSPK_GLintptr)offset, (TRSPK_GLsizeiptr)size, data);
    trspk_glBindBuffer(target, 0u);
}

/** Single dynamic mesh upload (immediate store path); two GL uploads total. */
static void
trspk_opengl3_upload_dynamic_interleaved_ranges(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage,
    uint32_t vbo_offset_vertices,
    uint32_t ebo_offset_u32,
    const void* vertices,
    uint32_t vertex_bytes,
    const void* indices,
    uint32_t index_bytes)
{
    trspk_opengl3_dynamic_world_vao_if_needed(r, usage);
    TRSPK_GLuint vbo = (TRSPK_GLuint)*trspk_opengl3_vbo_ptr_for_usage(r, usage);
    TRSPK_GLuint ebo = (TRSPK_GLuint)*trspk_opengl3_ebo_ptr_for_usage(r, usage);
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1);
    trspk_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    trspk_glBufferSubData(
        GL_ARRAY_BUFFER,
        (TRSPK_GLintptr)((size_t)vbo_offset_vertices * stride),
        (TRSPK_GLsizeiptr)vertex_bytes,
        vertices);
    trspk_glBindBuffer(GL_ARRAY_BUFFER, 0u);
    trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    trspk_glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        (TRSPK_GLintptr)((size_t)ebo_offset_u32 * sizeof(uint32_t)),
        (TRSPK_GLsizeiptr)index_bytes,
        indices);
    trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);
}

static uint32_t
trspk_opengl3_slot_table_index(
    TRSPK_ModelId model_id,
    uint32_t pose_index)
{
    return (uint32_t)model_id * TRSPK_MAX_POSES_PER_MODEL + pose_index;
}

static TRSPK_DynamicSlotmap32*
trspk_opengl3_slotmap_for_usage(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage)
{
    if( !r )
        return NULL;
    return usage == TRSPK_USAGE_PROJECTILE ? r->dynamic_projectile_slotmap : r->dynamic_npc_slotmap;
}

static uint32_t*
trspk_opengl3_vbo_ptr_for_usage(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage)
{
    return usage == TRSPK_USAGE_PROJECTILE ? &r->dynamic_projectile_vbo : &r->dynamic_npc_vbo;
}

static uint32_t*
trspk_opengl3_ebo_ptr_for_usage(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage)
{
    return usage == TRSPK_USAGE_PROJECTILE ? &r->dynamic_projectile_ebo : &r->dynamic_npc_ebo;
}

static bool
trspk_opengl3_replace_buffer(
    uint32_t* slot,
    TRSPK_GLenum target,
    size_t required_bytes,
    TRSPK_GLenum usage,
    size_t* allocated_bytes,
    bool* out_created_new_buffer_object)
{
    if( !slot || required_bytes == 0u || !allocated_bytes )
        return false;
    if( out_created_new_buffer_object )
        *out_created_new_buffer_object = false;
    TRSPK_GLuint cur = (TRSPK_GLuint)*slot;
    if( cur != 0u )
    {
        if( *allocated_bytes >= required_bytes )
            return true;
        trspk_glDeleteBuffers(1, &cur);
        *slot = 0u;
        *allocated_bytes = 0u;
    }
    TRSPK_GLuint buf = 0u;
    trspk_glGenBuffers(1, &buf);
    trspk_glBindBuffer(target, buf);
    trspk_glBufferData(target, (TRSPK_GLsizeiptr)required_bytes, NULL, usage);
    trspk_glBindBuffer(target, 0u);
    *slot = (uint32_t)buf;
    *allocated_bytes = required_bytes;
    if( out_created_new_buffer_object )
        *out_created_new_buffer_object = true;
    return buf != 0u;
}

static bool
trspk_opengl3_ensure_dynamic_buffers(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage)
{
    if( !r )
        return false;
    TRSPK_DynamicSlotmap32* map = trspk_opengl3_slotmap_for_usage(r, usage);
    if( !map )
        return false;
    const size_t vbytes = (size_t)trspk_dynamic_slotmap32_vertex_capacity(map) *
                          trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1);
    const size_t ibytes = (size_t)trspk_dynamic_slotmap32_index_capacity(map) * sizeof(uint32_t);
    const TRSPK_GLenum gl_usage =
        usage == TRSPK_USAGE_PROJECTILE ? GL_STREAM_DRAW : GL_DYNAMIC_DRAW;
    size_t* v_alloc = usage == TRSPK_USAGE_PROJECTILE ? &r->dynamic_projectile_vbo_allocated_bytes
                                                      : &r->dynamic_npc_vbo_allocated_bytes;
    size_t* e_alloc = usage == TRSPK_USAGE_PROJECTILE ? &r->dynamic_projectile_ebo_allocated_bytes
                                                      : &r->dynamic_npc_ebo_allocated_bytes;
    bool v_new = false;
    bool e_new = false;
    const bool v_ok = trspk_opengl3_replace_buffer(
        trspk_opengl3_vbo_ptr_for_usage(r, usage),
        GL_ARRAY_BUFFER,
        vbytes,
        gl_usage,
        v_alloc,
        &v_new);
    if( !v_ok )
        return false;
    const bool e_ok = trspk_opengl3_replace_buffer(
        trspk_opengl3_ebo_ptr_for_usage(r, usage),
        GL_ELEMENT_ARRAY_BUFFER,
        ibytes,
        gl_usage,
        e_alloc,
        &e_new);
    if( !e_ok )
        return false;
    if( v_new || e_new )
    {
        if( usage == TRSPK_USAGE_PROJECTILE )
            r->dynamic_projectile_stream_revision++;
        else
            r->dynamic_npc_stream_revision++;
    }
    return true;
}

static void
trspk_opengl3_dynamic_free_pose_slot(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    uint32_t pose_index)
{
    if( !r || !r->dynamic_slot_handles || !r->dynamic_slot_usages ||
        pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return;
    const uint32_t idx = trspk_opengl3_slot_table_index(model_id, pose_index);
    const TRSPK_DynamicSlotHandle handle = r->dynamic_slot_handles[idx];
    if( handle == TRSPK_DYNAMIC_SLOT_INVALID )
        return;
    TRSPK_DynamicSlotmap32* map = trspk_opengl3_slotmap_for_usage(r, r->dynamic_slot_usages[idx]);
    trspk_dynamic_slotmap32_free(map, handle);
    r->dynamic_slot_handles[idx] = TRSPK_DYNAMIC_SLOT_INVALID;
    r->dynamic_slot_usages[idx] = TRSPK_USAGE_SCENERY;
}

bool
trspk_opengl3_dynamic_init(TRSPK_OpenGL3Renderer* r)
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
    r->dynamic_npc_slotmap = trspk_dynamic_slotmap32_create(
        TRSPK_OGL3_DYNAMIC_VERTEX_CAPACITY, TRSPK_OGL3_DYNAMIC_INDEX_CAPACITY_INITIAL);
    r->dynamic_projectile_slotmap = trspk_dynamic_slotmap32_create(
        TRSPK_OGL3_DYNAMIC_VERTEX_CAPACITY, TRSPK_OGL3_DYNAMIC_INDEX_CAPACITY_INITIAL);
    if( !r->dynamic_npc_slotmap || !r->dynamic_projectile_slotmap )
        return false;
    return trspk_opengl3_ensure_dynamic_buffers(r, TRSPK_USAGE_NPC) &&
           trspk_opengl3_ensure_dynamic_buffers(r, TRSPK_USAGE_PROJECTILE);
}

void
trspk_opengl3_pass_flush_pending_dynamic_gpu_uploads(TRSPK_OpenGL3Renderer* r)
{
    if( !r || !r->cache )
    {
        if( r )
            r->deferred_dynamic_bake_count = 0u;
        return;
    }
    const uint32_t n_def = r->deferred_dynamic_bake_count;
    if( n_def == 0u )
        return;

    TRSPK_LruModelCache* lru = trspk_resource_cache_lru_model_cache(r->cache);
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1);

    TRSPK_Ogl3FlushBakedRow* rows =
        (TRSPK_Ogl3FlushBakedRow*)calloc((size_t)n_def, sizeof(TRSPK_Ogl3FlushBakedRow));
    if( !rows )
    {
        r->deferred_dynamic_bake_count = 0u;
        return;
    }
    int n_ok = 0;

    for( uint32_t i = 0u; i < n_def; ++i )
    {
        TRSPK_OpenGL3DeferredDynamicBake* d = &r->deferred_dynamic_bakes[i];
        TRSPK_VertexBuffer baked = { 0 };
        const TRSPK_VertexBuffer* id_mesh =
            lru ? trspk_lru_model_cache_get(lru, d->lru_model_id, d->seg, d->frame_i) : NULL;
        if( !id_mesh || !trspk_vertex_buffer_bake_array_to_interleaved(id_mesh, &d->bake, &baked) ||
            baked.vertex_count != d->vertex_count || baked.index_count != d->index_count ||
            baked.format != TRSPK_VERTEX_FORMAT_WEBGL1 ||
            baked.index_format != TRSPK_INDEX_FORMAT_U32 || !baked.vertices.as_webgl1 ||
            !baked.indices.as_u32 )
        {
            trspk_vertex_buffer_free(&baked);
            trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
            continue;
        }
        if( !trspk_opengl3_ensure_dynamic_buffers(r, d->usage) )
        {
            trspk_vertex_buffer_free(&baked);
            trspk_resource_cache_invalidate_model_pose(r->cache, d->model_id, d->pose_index);
            continue;
        }
        TRSPK_Ogl3FlushBakedRow* row = &rows[n_ok++];
        row->baked = baked;
        row->usage = d->usage;
        row->gpu.vbo_beg_bytes = d->vbo_offset * stride;
        row->gpu.vbo_len_bytes = baked.vertex_count * stride;
        row->gpu.vbo_src = baked.vertices.as_webgl1;
        row->gpu.ebo_beg_bytes = d->ebo_offset * (uint32_t)sizeof(uint32_t);
        row->gpu.ebo_len_bytes = baked.index_count * (uint32_t)sizeof(uint32_t);
        row->gpu.ebo_src = baked.indices.as_u32;
    }

    if( n_ok > 0 )
    {
        bool need_npc = false;
        bool need_prj = false;
        for( int j = 0; j < n_ok; ++j )
        {
            if( rows[j].usage == TRSPK_USAGE_PROJECTILE )
                need_prj = true;
            else if( rows[j].usage == TRSPK_USAGE_NPC )
                need_npc = true;
        }
        if( need_npc )
            trspk_opengl3_dynamic_world_vao_if_needed(r, TRSPK_USAGE_NPC);
        if( need_prj )
            trspk_opengl3_dynamic_world_vao_if_needed(r, TRSPK_USAGE_PROJECTILE);

        if( !trspk_dynamic_batch_scratch_ensure_sort_idx(&r->dynamic_flush_batch, (uint32_t)n_ok) )
        {
            for( int j = 0; j < n_ok; ++j )
            {
                trspk_opengl3_dynamic_world_vao_if_needed(r, rows[j].usage);
                trspk_opengl3_upload_dynamic_interleaved_ranges(
                    r,
                    rows[j].usage,
                    rows[j].gpu.vbo_beg_bytes / stride,
                    rows[j].gpu.ebo_beg_bytes / (uint32_t)sizeof(uint32_t),
                    rows[j].baked.vertices.as_webgl1,
                    rows[j].gpu.vbo_len_bytes,
                    rows[j].baked.indices.as_u32,
                    rows[j].gpu.ebo_len_bytes);
            }
        }
        else
        {
            uint32_t* idx = r->dynamic_flush_batch.sort_idx;
            int c = 0;
            for( int j = 0; j < n_ok; ++j )
            {
                if( rows[j].usage == TRSPK_USAGE_NPC )
                    idx[c++] = (uint32_t)j;
            }
            if( c > 0 )
            {
                const TRSPK_GLuint vbo_gl = (TRSPK_GLuint)*trspk_opengl3_vbo_ptr_for_usage(r, TRSPK_USAGE_NPC);
                const TRSPK_GLuint ebo_gl = (TRSPK_GLuint)*trspk_opengl3_ebo_ptr_for_usage(r, TRSPK_USAGE_NPC);
                if( vbo_gl != 0u && ebo_gl != 0u )
                {
                    const size_t row_stride = sizeof(TRSPK_Ogl3FlushBakedRow);
                    const size_t flush_off = offsetof(TRSPK_Ogl3FlushBakedRow, gpu);
                    trspk_dynamic_batch_sort_indices_by_stream(
                        rows, row_stride, flush_off, idx, c, TRSPK_DYNAMIC_BATCH_STREAM_VBO);
                    trspk_dynamic_batch_upload_merged_subdata_runs(
                        &r->dynamic_flush_batch,
                        idx,
                        c,
                        rows,
                        row_stride,
                        flush_off,
                        TRSPK_DYNAMIC_BATCH_STREAM_VBO,
                        (uintptr_t)vbo_gl,
                        ogl3_dynamic_batch_flush_upload,
                        NULL);
                    trspk_dynamic_batch_sort_indices_by_stream(
                        rows, row_stride, flush_off, idx, c, TRSPK_DYNAMIC_BATCH_STREAM_EBO);
                    trspk_dynamic_batch_upload_merged_subdata_runs(
                        &r->dynamic_flush_batch,
                        idx,
                        c,
                        rows,
                        row_stride,
                        flush_off,
                        TRSPK_DYNAMIC_BATCH_STREAM_EBO,
                        (uintptr_t)ebo_gl,
                        ogl3_dynamic_batch_flush_upload,
                        NULL);
                }
            }
            c = 0;
            for( int j = 0; j < n_ok; ++j )
            {
                if( rows[j].usage == TRSPK_USAGE_PROJECTILE )
                    idx[c++] = (uint32_t)j;
            }
            if( c > 0 )
            {
                const TRSPK_GLuint vbo_gl =
                    (TRSPK_GLuint)*trspk_opengl3_vbo_ptr_for_usage(r, TRSPK_USAGE_PROJECTILE);
                const TRSPK_GLuint ebo_gl =
                    (TRSPK_GLuint)*trspk_opengl3_ebo_ptr_for_usage(r, TRSPK_USAGE_PROJECTILE);
                if( vbo_gl != 0u && ebo_gl != 0u )
                {
                    const size_t row_stride = sizeof(TRSPK_Ogl3FlushBakedRow);
                    const size_t flush_off = offsetof(TRSPK_Ogl3FlushBakedRow, gpu);
                    trspk_dynamic_batch_sort_indices_by_stream(
                        rows, row_stride, flush_off, idx, c, TRSPK_DYNAMIC_BATCH_STREAM_VBO);
                    trspk_dynamic_batch_upload_merged_subdata_runs(
                        &r->dynamic_flush_batch,
                        idx,
                        c,
                        rows,
                        row_stride,
                        flush_off,
                        TRSPK_DYNAMIC_BATCH_STREAM_VBO,
                        (uintptr_t)vbo_gl,
                        ogl3_dynamic_batch_flush_upload,
                        NULL);
                    trspk_dynamic_batch_sort_indices_by_stream(
                        rows, row_stride, flush_off, idx, c, TRSPK_DYNAMIC_BATCH_STREAM_EBO);
                    trspk_dynamic_batch_upload_merged_subdata_runs(
                        &r->dynamic_flush_batch,
                        idx,
                        c,
                        rows,
                        row_stride,
                        flush_off,
                        TRSPK_DYNAMIC_BATCH_STREAM_EBO,
                        (uintptr_t)ebo_gl,
                        ogl3_dynamic_batch_flush_upload,
                        NULL);
                }
            }
        }
    }

    for( int j = 0; j < n_ok; ++j )
        trspk_vertex_buffer_free(&rows[j].baked);
    free(rows);
    r->deferred_dynamic_bake_count = 0u;
}

void
trspk_opengl3_dynamic_reset_pass(TRSPK_OpenGL3Renderer* r)
{
    if( !r )
        return;
    trspk_opengl3_pass_flush_pending_dynamic_gpu_uploads(r);
    if( r->dynamic_slot_handles && r->cache )
    {
        const uint32_t table_count = TRSPK_MAX_MODELS * TRSPK_MAX_POSES_PER_MODEL;
        for( uint32_t i = 0u; i < table_count; ++i )
        {
            if( r->dynamic_slot_handles[i] == TRSPK_DYNAMIC_SLOT_INVALID )
                continue;
            const TRSPK_ModelId model_id = (TRSPK_ModelId)(i / TRSPK_MAX_POSES_PER_MODEL);
            const uint32_t pose_index = i % TRSPK_MAX_POSES_PER_MODEL;
            trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
            r->dynamic_slot_handles[i] = TRSPK_DYNAMIC_SLOT_INVALID;
        }
    }
    if( r->dynamic_npc_slotmap )
        trspk_dynamic_slotmap32_reset(r->dynamic_npc_slotmap);
    if( r->dynamic_projectile_slotmap )
        trspk_dynamic_slotmap32_reset(r->dynamic_projectile_slotmap);
}

static bool
trspk_opengl3_deferred_reserve(
    TRSPK_OpenGL3Renderer* r,
    uint32_t need)
{
    if( need <= r->deferred_dynamic_bake_cap )
        return true;
    uint32_t cap = r->deferred_dynamic_bake_cap ? r->deferred_dynamic_bake_cap : 8u;
    while( cap < need )
        cap *= 2u;
    TRSPK_OpenGL3DeferredDynamicBake* n = (TRSPK_OpenGL3DeferredDynamicBake*)realloc(
        r->deferred_dynamic_bakes, cap * sizeof(TRSPK_OpenGL3DeferredDynamicBake));
    if( !n )
        return false;
    r->deferred_dynamic_bakes = n;
    r->deferred_dynamic_bake_cap = cap;
    return true;
}

bool
trspk_opengl3_dynamic_enqueue_draw_mesh_deferred(
    TRSPK_OpenGL3Renderer* r,
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

    const uint32_t idx = trspk_opengl3_slot_table_index(pose_storage_model_id, pose_index);
    const bool had_dynamic_slot =
        r->dynamic_slot_handles && r->dynamic_slot_handles[idx] != TRSPK_DYNAMIC_SLOT_INVALID;

    trspk_opengl3_dynamic_free_pose_slot(r, pose_storage_model_id, pose_index);

    TRSPK_DynamicSlotmap32* map = trspk_opengl3_slotmap_for_usage(r, usage);
    if( !map )
        return false;
    TRSPK_DynamicSlotHandle handle = TRSPK_DYNAMIC_SLOT_INVALID;
    uint32_t vbo_offset = 0u;
    uint32_t ebo_offset = 0u;
    if( !trspk_dynamic_slotmap32_alloc(
            map, array_vertex_count, array_index_count, &handle, &vbo_offset, &ebo_offset) )
    {
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, pose_storage_model_id, pose_index);
        return false;
    }
    if( !trspk_opengl3_ensure_dynamic_buffers(r, usage) )
    {
        trspk_dynamic_slotmap32_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, pose_storage_model_id, pose_index);
        return false;
    }
    trspk_opengl3_dynamic_world_vao_if_needed(r, usage);

    TRSPK_GLuint vbo_gl = (TRSPK_GLuint)*trspk_opengl3_vbo_ptr_for_usage(r, usage);
    TRSPK_GLuint ebo_gl = (TRSPK_GLuint)*trspk_opengl3_ebo_ptr_for_usage(r, usage);
    TRSPK_ModelPose pose;
    memset(&pose, 0, sizeof(pose));
    pose.vbo = (TRSPK_GPUHandle)(uintptr_t)vbo_gl;
    pose.ebo = (TRSPK_GPUHandle)(uintptr_t)ebo_gl;
    pose.vbo_offset = vbo_offset;
    pose.ebo_offset = ebo_offset;
    pose.element_count = array_index_count;
    pose.batch_id = TRSPK_SCENE_BATCH_NONE;
    pose.chunk_index = 0u;
    pose.usage_class = (uint8_t)usage;
    pose.dynamic = true;
    pose.valid = true;
    if( !trspk_resource_cache_set_model_pose(r->cache, pose_storage_model_id, pose_index, &pose) )
    {
        trspk_dynamic_slotmap32_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, pose_storage_model_id, pose_index);
        return false;
    }

    r->dynamic_slot_handles[idx] = handle;
    r->dynamic_slot_usages[idx] = usage;

    if( !trspk_opengl3_deferred_reserve(r, r->deferred_dynamic_bake_count + 1u) )
    {
        trspk_dynamic_slotmap32_free(map, handle);
        r->dynamic_slot_handles[idx] = TRSPK_DYNAMIC_SLOT_INVALID;
        r->dynamic_slot_usages[idx] = TRSPK_USAGE_SCENERY;
        trspk_resource_cache_invalidate_model_pose(r->cache, pose_storage_model_id, pose_index);
        return false;
    }
    TRSPK_OpenGL3DeferredDynamicBake* q =
        &r->deferred_dynamic_bakes[r->deferred_dynamic_bake_count++];
    q->usage = usage;
    q->model_id = pose_storage_model_id;
    q->lru_model_id = lru_model_id;
    q->pose_index = pose_index;
    q->seg = gpu_segment_slot;
    q->frame_i = frame_index;
    q->bake = *bake;
    q->vbo_offset = vbo_offset;
    q->ebo_offset = ebo_offset;
    q->vertex_count = array_vertex_count;
    q->index_count = array_index_count;
    return true;
}

void
trspk_opengl3_dynamic_shutdown(TRSPK_OpenGL3Renderer* r)
{
    if( !r )
        return;
    free(r->deferred_dynamic_bakes);
    r->deferred_dynamic_bakes = NULL;
    r->deferred_dynamic_bake_count = 0u;
    r->deferred_dynamic_bake_cap = 0u;
    if( r->dynamic_npc_vbo )
    {
        TRSPK_GLuint b = (TRSPK_GLuint)r->dynamic_npc_vbo;
        trspk_glDeleteBuffers(1, &b);
    }
    if( r->dynamic_npc_ebo )
    {
        TRSPK_GLuint b = (TRSPK_GLuint)r->dynamic_npc_ebo;
        trspk_glDeleteBuffers(1, &b);
    }
    if( r->dynamic_projectile_vbo )
    {
        TRSPK_GLuint b = (TRSPK_GLuint)r->dynamic_projectile_vbo;
        trspk_glDeleteBuffers(1, &b);
    }
    if( r->dynamic_projectile_ebo )
    {
        TRSPK_GLuint b = (TRSPK_GLuint)r->dynamic_projectile_ebo;
        trspk_glDeleteBuffers(1, &b);
    }
    trspk_dynamic_slotmap32_destroy(r->dynamic_npc_slotmap);
    trspk_dynamic_slotmap32_destroy(r->dynamic_projectile_slotmap);
    free(r->dynamic_slot_handles);
    free(r->dynamic_slot_usages);
    r->dynamic_npc_vbo = 0u;
    r->dynamic_npc_ebo = 0u;
    r->dynamic_projectile_vbo = 0u;
    r->dynamic_projectile_ebo = 0u;
    r->dynamic_npc_vbo_allocated_bytes = 0;
    r->dynamic_npc_ebo_allocated_bytes = 0;
    r->dynamic_projectile_vbo_allocated_bytes = 0;
    r->dynamic_projectile_ebo_allocated_bytes = 0;
    r->dynamic_npc_stream_revision = 0u;
    r->dynamic_projectile_stream_revision = 0u;
    r->dynamic_vao_applied_revision_npc = 0u;
    r->dynamic_vao_applied_revision_projectile = 0u;
    r->dynamic_npc_slotmap = NULL;
    r->dynamic_projectile_slotmap = NULL;
    r->dynamic_slot_handles = NULL;
    r->dynamic_slot_usages = NULL;
    trspk_dynamic_batch_scratch_destroy(&r->dynamic_flush_batch);
}

static bool
trspk_opengl3_dynamic_upload_interleaved(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    uint32_t vertex_count,
    uint32_t index_count,
    const void* vertices,
    uint32_t vertex_bytes,
    const void* indices,
    uint32_t index_bytes)
{
    if( !r || !r->cache || !vertices || !indices || pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return false;
    if( vertex_count == 0u || index_count == 0u || vertex_bytes == 0u || index_bytes == 0u )
        return false;

    const uint32_t idx = trspk_opengl3_slot_table_index(model_id, pose_index);
    const bool had_dynamic_slot =
        r->dynamic_slot_handles && r->dynamic_slot_handles[idx] != TRSPK_DYNAMIC_SLOT_INVALID;

    trspk_opengl3_dynamic_free_pose_slot(r, model_id, pose_index);

    TRSPK_DynamicSlotmap32* map = trspk_opengl3_slotmap_for_usage(r, usage);
    if( !map )
        return false;
    TRSPK_DynamicSlotHandle handle = TRSPK_DYNAMIC_SLOT_INVALID;
    uint32_t vbo_offset = 0u;
    uint32_t ebo_offset = 0u;
    if( !trspk_dynamic_slotmap32_alloc(
            map, vertex_count, index_count, &handle, &vbo_offset, &ebo_offset) )
    {
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
        return false;
    }
    if( !trspk_opengl3_ensure_dynamic_buffers(r, usage) )
    {
        trspk_dynamic_slotmap32_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
        return false;
    }

    trspk_opengl3_upload_dynamic_interleaved_ranges(
        r, usage, vbo_offset, ebo_offset, vertices, vertex_bytes, indices, index_bytes);

    TRSPK_ModelPose pose;
    memset(&pose, 0, sizeof(pose));
    const TRSPK_GLuint vbo = (TRSPK_GLuint)*trspk_opengl3_vbo_ptr_for_usage(r, usage);
    const TRSPK_GLuint ebo = (TRSPK_GLuint)*trspk_opengl3_ebo_ptr_for_usage(r, usage);
    pose.vbo_offset = vbo_offset;
    pose.ebo_offset = ebo_offset;
    pose.element_count = index_count;
    pose.batch_id = TRSPK_SCENE_BATCH_NONE;
    pose.chunk_index = 0u;
    pose.usage_class = (uint8_t)usage;
    pose.dynamic = true;
    pose.valid = true;
    if( !trspk_resource_cache_set_model_pose(r->cache, model_id, pose_index, &pose) )
    {
        trspk_dynamic_slotmap32_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
        return false;
    }

    r->dynamic_slot_handles[idx] = handle;
    r->dynamic_slot_usages[idx] = usage;
    return true;
}

static bool
trspk_opengl3_dynamic_store_mesh(
    TRSPK_OpenGL3Renderer* r,
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

    const bool ok = trspk_opengl3_dynamic_upload_interleaved(
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
trspk_opengl3_dynamic_store_vertex_buffer(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const TRSPK_VertexBuffer* vb)
{
    if( !vb || vb->format != TRSPK_VERTEX_FORMAT_WEBGL1 ||
        vb->index_format != TRSPK_INDEX_FORMAT_U32 || !vb->vertices.as_webgl1 ||
        !vb->indices.as_u32 )
        return false;
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1);
    return trspk_opengl3_dynamic_upload_interleaved(
        r,
        model_id,
        usage,
        pose_index,
        vb->vertex_count,
        vb->index_count,
        vb->vertices.as_webgl1,
        vb->vertex_count * stride,
        vb->indices.as_u32,
        vb->index_count * (uint32_t)sizeof(uint32_t));
}

bool
trspk_opengl3_dynamic_store_dynamic_mesh(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    TRSPK_DynamicMesh* mesh)
{
    if( !mesh )
        return false;
    const bool ok = trspk_opengl3_dynamic_upload_interleaved(
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
trspk_opengl3_dynamic_load_model(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake)
{
    return trspk_opengl3_dynamic_store_mesh(r, model_id, model, usage_class, 0u, bake);
}

bool
trspk_opengl3_dynamic_load_anim(
    TRSPK_OpenGL3Renderer* r,
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
    return trspk_opengl3_dynamic_store_mesh(r, model_id, model, usage_class, pose_index, bake);
}

void
trspk_opengl3_dynamic_unload_model(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id)
{
    if( !r || !r->cache )
        return;
    for( uint32_t pose = 0; pose < TRSPK_MAX_POSES_PER_MODEL; ++pose )
        trspk_opengl3_dynamic_free_pose_slot(r, model_id, pose);
    trspk_resource_cache_clear_model(r->cache, model_id);
}
