#include "trspk_webgl1.h"

#include "../../tools/trspk_dynamic_pass.h"
#include "../../tools/trspk_vertex_format.h"

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
    return usage == TRSPK_USAGE_PROJECTILE ? r->dynamic_projectile_slotmap
                                           : r->dynamic_npc_slotmap;
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
trspk_webgl1_dynamic_shutdown(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return;
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
    r->dynamic_npc_slotmap = NULL;
    r->dynamic_projectile_slotmap = NULL;
    r->dynamic_slot_handles = NULL;
    r->dynamic_slot_usages = NULL;
}

static void
trspk_webgl1_dynamic_store_mesh(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const TRSPK_BakeTransform* bake)
{
    if( !r || !model || !r->cache || pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return;
    TRSPK_DynamicMesh mesh;
    if( !trspk_dynamic_mesh_build(model, TRSPK_VERTEX_FORMAT_WEBGL1, bake, &mesh) )
        return;

    trspk_webgl1_dynamic_free_pose_slot(r, model_id, pose_index);

    TRSPK_DynamicSlotmap16* map = trspk_webgl1_slotmap_for_usage(r, usage);
    TRSPK_DynamicSlotHandle handle = TRSPK_DYNAMIC_SLOT_INVALID;
    uint8_t chunk = 0u;
    uint32_t vbo_offset = 0u;
    uint32_t ebo_offset = 0u;
    if( !trspk_dynamic_slotmap16_alloc(
            map,
            mesh.vertex_count,
            mesh.index_count,
            &handle,
            &chunk,
            &vbo_offset,
            &ebo_offset) )
    {
        trspk_dynamic_mesh_clear(&mesh);
        return;
    }
    if( !trspk_webgl1_ensure_chunk(r, usage, chunk) )
    {
        trspk_dynamic_slotmap16_free(map, handle);
        trspk_dynamic_mesh_clear(&mesh);
        return;
    }

#ifdef __EMSCRIPTEN__
    GLuint* upload_vbos = trspk_webgl1_vbos_for_usage(r, usage);
    GLuint* upload_ebos = trspk_webgl1_ebos_for_usage(r, usage);
    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_WEBGL1);
    glBindBuffer(GL_ARRAY_BUFFER, upload_vbos[chunk]);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        (GLintptr)((size_t)vbo_offset * stride),
        (GLsizeiptr)mesh.vertex_bytes,
        mesh.vertices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, upload_ebos[chunk]);
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        (GLintptr)((size_t)ebo_offset * sizeof(uint16_t)),
        (GLsizeiptr)mesh.index_bytes,
        mesh.indices);
#endif

    GLuint* vbos = trspk_webgl1_vbos_for_usage(r, usage);
    GLuint* ebos = trspk_webgl1_ebos_for_usage(r, usage);
    TRSPK_ModelPose pose;
    memset(&pose, 0, sizeof(pose));
    pose.vbo = (TRSPK_GPUHandle)vbos[chunk];
    pose.ebo = (TRSPK_GPUHandle)ebos[chunk];
    pose.vbo_offset = vbo_offset;
    pose.ebo_offset = ebo_offset;
    pose.element_count = mesh.index_count;
    pose.batch_id = TRSPK_SCENE_BATCH_NONE;
    pose.chunk_index = chunk;
    pose.usage_class = (uint8_t)usage;
    pose.dynamic = true;
    pose.valid = true;
    trspk_resource_cache_set_model_pose(r->cache, model_id, pose_index, &pose);

    const uint32_t idx = trspk_webgl1_slot_table_index(model_id, pose_index);
    r->dynamic_slot_handles[idx] = handle;
    r->dynamic_slot_usages[idx] = usage;
    trspk_dynamic_mesh_clear(&mesh);
}

void
trspk_webgl1_dynamic_load_model(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake)
{
    trspk_webgl1_dynamic_store_mesh(r, model_id, model, usage_class, 0u, bake);
}

void
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
        return;
    const uint32_t pose_index =
        trspk_resource_cache_allocate_pose_slot(r->cache, model_id, segment, frame_index);
    trspk_webgl1_dynamic_store_mesh(r, model_id, model, usage_class, pose_index, bake);
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
