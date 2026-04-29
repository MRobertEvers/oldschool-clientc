#include "../../tools/trspk_dynamic_pass.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_vertex_buffer.h"
#include "../../tools/trspk_vertex_format.h"
#include "trspk_metal.h"
#import <Metal/Metal.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TRSPK_METAL_DYNAMIC_VERTEX_CAPACITY 4096u
#define TRSPK_METAL_DYNAMIC_INDEX_CAPACITY_INITIAL 12288u

static uint32_t
trspk_metal_slot_table_index(
    TRSPK_ModelId model_id,
    uint32_t pose_index)
{
    return (uint32_t)model_id * TRSPK_MAX_POSES_PER_MODEL + pose_index;
}

static TRSPK_DynamicSlotmap32*
trspk_metal_slotmap_for_usage(
    TRSPK_MetalRenderer* r,
    TRSPK_UsageClass usage)
{
    if( !r )
        return NULL;
    return usage == TRSPK_USAGE_PROJECTILE ? r->dynamic_projectile_slotmap : r->dynamic_npc_slotmap;
}

static void**
trspk_metal_vbo_for_usage(
    TRSPK_MetalRenderer* r,
    TRSPK_UsageClass usage)
{
    return usage == TRSPK_USAGE_PROJECTILE ? &r->dynamic_projectile_vbo : &r->dynamic_npc_vbo;
}

static void**
trspk_metal_ebo_for_usage(
    TRSPK_MetalRenderer* r,
    TRSPK_UsageClass usage)
{
    return usage == TRSPK_USAGE_PROJECTILE ? &r->dynamic_projectile_ebo : &r->dynamic_npc_ebo;
}

static bool
trspk_metal_replace_buffer(
    id<MTLDevice> device,
    void** slot,
    size_t required_bytes)
{
    if( !device || !slot || required_bytes == 0u )
        return false;
    id<MTLBuffer> old = (__bridge id<MTLBuffer>)*slot;
    if( old && [old length] >= required_bytes )
        return true;
    id<MTLBuffer> next = [device newBufferWithLength:(NSUInteger)required_bytes
                                             options:MTLResourceStorageModeShared];
    if( !next )
        return false;
    if( old )
    {
        const size_t copy_bytes = [old length] < required_bytes ? [old length] : required_bytes;
        memcpy([next contents], [old contents], copy_bytes);
        CFRelease(*slot);
    }
    *slot = (__bridge_retained void*)next;
    return true;
}

static bool
trspk_metal_ensure_dynamic_buffers(
    TRSPK_MetalRenderer* r,
    TRSPK_UsageClass usage)
{
    if( !r || !r->device )
        return false;
    TRSPK_DynamicSlotmap32* map = trspk_metal_slotmap_for_usage(r, usage);
    if( !map )
        return false;
    id<MTLDevice> device = (__bridge id<MTLDevice>)r->device;
    const size_t vbytes = (size_t)trspk_dynamic_slotmap32_vertex_capacity(map) *
                          trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_METAL);
    const size_t ibytes = (size_t)trspk_dynamic_slotmap32_index_capacity(map) * sizeof(uint32_t);
    return trspk_metal_replace_buffer(device, trspk_metal_vbo_for_usage(r, usage), vbytes) &&
           trspk_metal_replace_buffer(device, trspk_metal_ebo_for_usage(r, usage), ibytes);
}

static void
trspk_metal_dynamic_free_pose_slot(
    TRSPK_MetalRenderer* r,
    TRSPK_ModelId model_id,
    uint32_t pose_index)
{
    if( !r || !r->dynamic_slot_handles || !r->dynamic_slot_usages ||
        pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return;
    const uint32_t idx = trspk_metal_slot_table_index(model_id, pose_index);
    const TRSPK_DynamicSlotHandle handle = r->dynamic_slot_handles[idx];
    if( handle == TRSPK_DYNAMIC_SLOT_INVALID )
        return;
    TRSPK_DynamicSlotmap32* map = trspk_metal_slotmap_for_usage(r, r->dynamic_slot_usages[idx]);
    trspk_dynamic_slotmap32_free(map, handle);
    r->dynamic_slot_handles[idx] = TRSPK_DYNAMIC_SLOT_INVALID;
    r->dynamic_slot_usages[idx] = TRSPK_USAGE_SCENERY;
}

bool
trspk_metal_dynamic_init(TRSPK_MetalRenderer* r)
{
    if( !r || !r->device )
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
        TRSPK_METAL_DYNAMIC_VERTEX_CAPACITY, TRSPK_METAL_DYNAMIC_INDEX_CAPACITY_INITIAL);
    r->dynamic_projectile_slotmap = trspk_dynamic_slotmap32_create(
        TRSPK_METAL_DYNAMIC_VERTEX_CAPACITY, TRSPK_METAL_DYNAMIC_INDEX_CAPACITY_INITIAL);
    if( !r->dynamic_npc_slotmap || !r->dynamic_projectile_slotmap )
        return false;
    return trspk_metal_ensure_dynamic_buffers(r, TRSPK_USAGE_NPC) &&
           trspk_metal_ensure_dynamic_buffers(r, TRSPK_USAGE_PROJECTILE);
}

void
trspk_metal_dynamic_shutdown(TRSPK_MetalRenderer* r)
{
    if( !r )
        return;
    if( r->dynamic_npc_vbo )
        CFRelease(r->dynamic_npc_vbo);
    if( r->dynamic_npc_ebo )
        CFRelease(r->dynamic_npc_ebo);
    if( r->dynamic_projectile_vbo )
        CFRelease(r->dynamic_projectile_vbo);
    if( r->dynamic_projectile_ebo )
        CFRelease(r->dynamic_projectile_ebo);
    trspk_dynamic_slotmap32_destroy(r->dynamic_npc_slotmap);
    trspk_dynamic_slotmap32_destroy(r->dynamic_projectile_slotmap);
    free(r->dynamic_slot_handles);
    free(r->dynamic_slot_usages);
    r->dynamic_npc_vbo = NULL;
    r->dynamic_npc_ebo = NULL;
    r->dynamic_projectile_vbo = NULL;
    r->dynamic_projectile_ebo = NULL;
    r->dynamic_npc_slotmap = NULL;
    r->dynamic_projectile_slotmap = NULL;
    r->dynamic_slot_handles = NULL;
    r->dynamic_slot_usages = NULL;
}

static bool
trspk_metal_dynamic_upload_interleaved(
    TRSPK_MetalRenderer* r,
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

    const uint32_t idx = trspk_metal_slot_table_index(model_id, pose_index);
    const bool had_dynamic_slot =
        r->dynamic_slot_handles && r->dynamic_slot_handles[idx] != TRSPK_DYNAMIC_SLOT_INVALID;

    trspk_metal_dynamic_free_pose_slot(r, model_id, pose_index);

    TRSPK_DynamicSlotmap32* map = trspk_metal_slotmap_for_usage(r, usage);
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
    if( !trspk_metal_ensure_dynamic_buffers(r, usage) )
    {
        trspk_dynamic_slotmap32_free(map, handle);
        if( had_dynamic_slot )
            trspk_resource_cache_invalidate_model_pose(r->cache, model_id, pose_index);
        return false;
    }

    id<MTLBuffer> vbo = (__bridge id<MTLBuffer>)*trspk_metal_vbo_for_usage(r, usage);
    id<MTLBuffer> ebo = (__bridge id<MTLBuffer>)*trspk_metal_ebo_for_usage(r, usage);
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_METAL);
    memcpy((uint8_t*)[vbo contents] + (size_t)vbo_offset * stride, vertices, vertex_bytes);
    memcpy((uint8_t*)[ebo contents] + (size_t)ebo_offset * sizeof(uint32_t), indices, index_bytes);

    TRSPK_ModelPose pose;
    memset(&pose, 0, sizeof(pose));
    pose.vbo = (TRSPK_GPUHandle)*trspk_metal_vbo_for_usage(r, usage);
    pose.ebo = (TRSPK_GPUHandle)*trspk_metal_ebo_for_usage(r, usage);
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
trspk_metal_dynamic_store_mesh(
    TRSPK_MetalRenderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const TRSPK_BakeTransform* bake)
{
    if( !r || !model || !r->cache || pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return false;
    TRSPK_DynamicMesh mesh;
    if( !trspk_dynamic_mesh_build(model, TRSPK_VERTEX_FORMAT_METAL, bake, r->cache, &mesh) )
        return false;

    const bool ok = trspk_metal_dynamic_upload_interleaved(
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
trspk_metal_dynamic_store_vertex_buffer(
    TRSPK_MetalRenderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const TRSPK_VertexBuffer* vb)
{
    if( !vb || vb->format != TRSPK_VERTEX_FORMAT_METAL ||
        vb->index_format != TRSPK_INDEX_FORMAT_U32 || !vb->vertices.as_metal ||
        !vb->indices.as_u32 )
        return false;
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_METAL);
    return trspk_metal_dynamic_upload_interleaved(
        r,
        model_id,
        usage,
        pose_index,
        vb->vertex_count,
        vb->index_count,
        vb->vertices.as_metal,
        vb->vertex_count * stride,
        vb->indices.as_u32,
        vb->index_count * (uint32_t)sizeof(uint32_t));
}

bool
trspk_metal_dynamic_store_dynamic_mesh(
    TRSPK_MetalRenderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    TRSPK_DynamicMesh* mesh)
{
    if( !mesh )
        return false;
    const bool ok = trspk_metal_dynamic_upload_interleaved(
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
trspk_metal_dynamic_load_model(
    TRSPK_MetalRenderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake)
{
    return trspk_metal_dynamic_store_mesh(r, model_id, model, usage_class, 0u, bake);
}

bool
trspk_metal_dynamic_load_anim(
    TRSPK_MetalRenderer* r,
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
    return trspk_metal_dynamic_store_mesh(r, model_id, model, usage_class, pose_index, bake);
}

void
trspk_metal_dynamic_unload_model(
    TRSPK_MetalRenderer* r,
    TRSPK_ModelId model_id)
{
    if( !r || !r->cache )
        return;
    for( uint32_t pose = 0; pose < TRSPK_MAX_POSES_PER_MODEL; ++pose )
        trspk_metal_dynamic_free_pose_slot(r, model_id, pose);
    trspk_resource_cache_clear_model(r->cache, model_id);
}
