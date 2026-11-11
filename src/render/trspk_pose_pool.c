#include "render/trspk_pose_pool.h"

#include "mem/torirs_mem.h"
#include "toridraw.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <string.h>

bool
trspk_pose_key_for_command(
    struct ToriDraw_Scene* scene,
    const struct ToriRS_RenderCommand_Model* command,
    struct TRSPK_PoseKey* out)
{
    const struct ToriDraw_SceneElement* element;
    const struct ToriDraw_Model* model;
    assert(scene);
    assert(command);
    assert(out);
    if( command->element_id < 0 || command->model.kind != TORIDRAWMK_MODEL ||
        !ToriDraw_SceneElementIsLive(scene, command->element_id) )
        return false;
    element = ToriDraw_SceneElementGet(scene, command->element_id);
    model = command->model.u.model.model;
    if( !element || element->model.kind != TORIDRAWMK_MODEL ||
        element->model.u.model.model != model || !model || element->posed_primary < 0 )
        return false;
    memset(out, 0, sizeof(*out));
    out->element_id = command->element_id;
    out->model = model;
    out->vertices_x = model->vertices_x;
    out->face_colors_c = model->face_colors_c;
    out->vertex_count = model->vertex_count;
    out->face_count = model->face_count;
    out->model_revision = element->model_revision;
    out->posed_primary = element->posed_primary;
    out->posed_frame = element->posed_frame;
    out->posed_frame2 = element->posed_frame2;
    out->posed_track = element->posed_track;
    out->posed_track2 = element->posed_track2;
    out->position = command->world_position;
    return true;
}

static bool
trspk_pose_key_equal(
    const struct TRSPK_PoseKey* a,
    const struct TRSPK_PoseKey* b)
{
    return a->element_id == b->element_id && a->model == b->model &&
        a->vertices_x == b->vertices_x && a->face_colors_c == b->face_colors_c &&
        a->vertex_count == b->vertex_count && a->face_count == b->face_count &&
        a->model_revision == b->model_revision && a->posed_primary == b->posed_primary &&
        a->posed_frame == b->posed_frame && a->posed_frame2 == b->posed_frame2 &&
        a->posed_track == b->posed_track && a->posed_track2 == b->posed_track2 &&
        a->position.x == b->position.x && a->position.y == b->position.y &&
        a->position.z == b->position.z && a->position.pitch == b->position.pitch &&
        a->position.yaw == b->position.yaw && a->position.roll == b->position.roll;
}

static uint64_t
trspk_pose_entry_bytes(
    uint32_t vertex_count,
    uint32_t vertex_size)
{
    return (uint64_t)vertex_count * vertex_size + (uint64_t)(vertex_count / 3u) * sizeof(int);
}

static void
trspk_pose_pool_drop(
    struct TRSPK_PosePool* pool,
    struct TRSPK_PosePoolEntry* entry)
{
    assert(entry->vertices);
    pool->bytes -= trspk_pose_entry_bytes(entry->vertex_count, entry->vertex_size);
    ToriMem_Free(ToriMem_Renderer(), entry->vertices);
    ToriMem_Free(ToriMem_Renderer(), entry->configs);
    entry->vertices = NULL;
    entry->configs = NULL;
}

void
trspk_pose_pool_tick(struct TRSPK_PosePool* pool)
{
    assert(pool);
    pool->frame++;
}

static struct TRSPK_PosePoolEntry*
trspk_pose_pool_find(
    struct TRSPK_PosePool* pool,
    const struct TRSPK_PoseKey* key)
{
    uint32_t i;
    for( i = 0u; i < TRSPK_POSE_POOL_ENTRIES; i++ )
    {
        struct TRSPK_PosePoolEntry* entry = &pool->entries[i];
        if( !entry->vertices )
            continue;
        if( pool->frame - entry->last_used > TRSPK_POSE_POOL_MAX_AGE )
        {
            trspk_pose_pool_drop(pool, entry);
            continue;
        }
        if( trspk_pose_key_equal(&entry->key, key) )
            return entry;
    }
    return NULL;
}

uint32_t
trspk_pose_pool_place(
    struct TRSPK_PosePool* pool,
    const struct TRSPK_PoseKey* key,
    struct TRSPK_ModelArena* arena,
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    int arena_element_id,
    int arena_pose_id)
{
    struct TRSPK_PosePoolEntry* entry;
    const struct TRSPK_ModelSlot* slot;
    uint32_t slot_index;
    assert(pool);
    assert(key);
    assert(arena);
    assert(vbo);
    assert(triangles);
    entry = trspk_pose_pool_find(pool, key);
    if( !entry )
        return UINT32_MAX;
    assert(entry->vertex_size == trspk_vbo_vertex_size(vbo->format));
    slot_index = trspk_modelarena_load(arena, arena_element_id, arena_pose_id, entry->vertex_count);
    slot = trspk_modelarena_get(arena, slot_index);
    if( !slot )
        return UINT32_MAX;
    assert(slot->vertex_base % 3u == 0u);
    assert((slot->vertex_base + entry->vertex_count) / 3u <= triangles->cap);
    memcpy((uint8_t*)vbo->vertices.as_webgl1 + (size_t)slot->vertex_base * entry->vertex_size,
        entry->vertices,
        (size_t)entry->vertex_count * entry->vertex_size);
    memcpy(&triangles->config[slot->vertex_base / 3u], entry->configs,
        (size_t)(entry->vertex_count / 3u) * sizeof(int));
    trspk_vbo_mark_dirty_range(vbo, slot->vertex_base, entry->vertex_count);
    entry->last_used = pool->frame;
    return slot->vertex_base;
}

void
trspk_pose_pool_store(
    struct TRSPK_PosePool* pool,
    const struct TRSPK_PoseKey* key,
    const struct TRSPK_VBO* vbo,
    const struct TRSPK_Triangles* triangles,
    uint32_t vertex_base,
    uint32_t vertex_count)
{
    uint32_t const vertex_size = (uint32_t)trspk_vbo_vertex_size(vbo->format);
    uint64_t const bytes = trspk_pose_entry_bytes(vertex_count, vertex_size);
    struct TRSPK_PosePoolEntry* entry = NULL;
    uint32_t i;
    assert(pool);
    assert(key);
    assert(triangles);
    assert(vertex_base % 3u == 0u);
    /* A model too big to be worth a quarter of the pool is not kept. */
    if( vertex_count == 0u || bytes > TRSPK_POSE_POOL_BYTES / 4u )
        return;
    for( ;; )
    {
        struct TRSPK_PosePoolEntry* oldest = NULL;
        entry = NULL;
        for( i = 0u; i < TRSPK_POSE_POOL_ENTRIES; i++ )
        {
            struct TRSPK_PosePoolEntry* candidate = &pool->entries[i];
            if( !candidate->vertices )
            {
                if( !entry )
                    entry = candidate;
                continue;
            }
            if( !oldest || candidate->last_used < oldest->last_used )
                oldest = candidate;
        }
        if( entry && pool->bytes + bytes <= TRSPK_POSE_POOL_BYTES )
            break;
        assert(oldest);
        trspk_pose_pool_drop(pool, oldest);
    }
    entry->key = *key;
    entry->vertex_count = vertex_count;
    entry->vertex_size = vertex_size;
    entry->vertices =
        (uint8_t*)ToriMem_Alloc(ToriMem_Renderer(), (size_t)vertex_count * vertex_size);
    assert(entry->vertices);
    entry->configs =
        (int*)ToriMem_Alloc(ToriMem_Renderer(), (size_t)(vertex_count / 3u) * sizeof(int));
    assert(entry->configs);
    memcpy(entry->vertices,
        (const uint8_t*)vbo->vertices.as_webgl1 + (size_t)vertex_base * vertex_size,
        (size_t)vertex_count * vertex_size);
    memcpy(entry->configs, &triangles->config[vertex_base / 3u],
        (size_t)(vertex_count / 3u) * sizeof(int));
    entry->last_used = pool->frame;
    pool->bytes += bytes;
}

void
trspk_pose_pool_free(struct TRSPK_PosePool* pool)
{
    uint32_t i;
    if( !pool )
        return;
    for( i = 0u; i < TRSPK_POSE_POOL_ENTRIES; i++ )
        if( pool->entries[i].vertices )
            trspk_pose_pool_drop(pool, &pool->entries[i]);
}
