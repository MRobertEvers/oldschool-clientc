#include "toridraw_gccontext.h"

#include "toridraw.h"
#include "toridraw_animation.h"
#include "toridraw_map.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_vec.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#define TDGC_BATCH_ELEMENT_HANDLE_INVALID(HANDLE, CTX) \
    do { \
        (HANDLE).context = (CTX); \
        (HANDLE).batch_id = TDGC_INVALID_BATCH_ID; \
        (HANDLE).id = TDGC_INVALID_ELEMENT_ID; \
    } while(0)
// clang-format on

struct MapEntry_ToriModel
{
    int id;
    struct ToriDraw_ModelHandle model;
};

struct MapEntry_Animation
{
    int id;
    struct ToriDraw_Animation* animation;
};

struct ToriDraw_GCPendingPose
{
    struct ToriDraw_Model* model;
};

static struct ToriDraw_Map*
tdgc_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = toridraw_map_buffer_size_for(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return toridraw_map_new(&config, 0);
}

static void
tdgc_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    free(toridraw_map_buffer_ptr(map));
    toridraw_map_free(map);
}

static void
tdgc_map_reset(
    struct ToriDraw_Map** map_out,
    int entry_size,
    int capacity)
{
    if( !map_out || !*map_out )
        return;

    tdgc_map_free(*map_out);
    *map_out = tdgc_map_new(entry_size, capacity);
}

static bool
tdgc_element_valid(
    const struct ToriDraw_Context* gc,
    int element_id);

static struct ToriDraw_GCElement*
tdgc_element_ptr(
    struct ToriDraw_Context* gc,
    int element_id);

static void
tdgc_emit(
    struct ToriDraw_Context* gc,
    enum ToriDraw_GCEventKind kind,
    int batch_id,
    int element_id,
    int pose_id,
    int texture_id,
    const struct ToriDraw_ModelHandle* model,
    struct ToriDraw_Animation* animation,
    struct ToriDraw_Texture* texture)
{
    struct ToriDraw_GCEvent event;

    if( !gc )
        return;
    if( gc->event_queue.count >= TDGC_EVENT_QUEUE_MAX_SIZE )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.batch_id = batch_id;
    event.element_id = element_id;
    event.pose_id = pose_id;
    event.texture_id = texture_id;
    if( model )
        event.model = *model;
    event.animation = animation;
    event.texture = texture;
    if( tdgc_element_valid(gc, element_id) )
    {
        struct ToriDraw_GCElement* element = tdgc_element_ptr(gc, element_id);
        if( element )
            event.world_position = element->world_position;
    }

    gc->event_queue.events[gc->event_queue.count++] = event;
}

static void
tdgc_retain_pending_pose(
    struct ToriDraw_Context* gc,
    struct ToriDraw_Model* model)
{
    if( !gc || !model )
        return;

    if( gc->pending_pose_count >= gc->pending_pose_cap )
    {
        int new_cap = gc->pending_pose_cap ? gc->pending_pose_cap * 2 : 64;
        struct ToriDraw_GCPendingPose* grown =
            realloc(gc->pending_poses, (size_t)new_cap * sizeof(struct ToriDraw_GCPendingPose));
        if( !grown )
            return;
        gc->pending_poses = grown;
        gc->pending_pose_cap = new_cap;
    }

    gc->pending_poses[gc->pending_pose_count++].model = model;
}

static bool
tdgc_element_valid(
    const struct ToriDraw_Context* gc,
    int element_id)
{
    if( !gc )
        return false;
    if( element_id < 0 || element_id >= TDGC_MAX_ELEMENTS )
        return false;
    if( !gc->slots[element_id] )
        return false;
    return true;
}

static struct ToriDraw_GCElement*
tdgc_element_ptr(
    struct ToriDraw_Context* gc,
    int element_id)
{
    void* slot;

    if( !tdgc_element_valid(gc, element_id) )
        return NULL;
    if( (size_t)element_id >= toridraw_vec_size(gc->elements) )
        return NULL;

    slot = toridraw_vec_get(gc->elements, (size_t)element_id);
    return slot ? (struct ToriDraw_GCElement*)slot : NULL;
}

static bool
tdgc_prepare_element_slot(
    struct ToriDraw_Context* gc,
    int element_id)
{
    struct ToriDraw_GCElement* element;

    if( (size_t)element_id + 1 > toridraw_vec_size(gc->elements) )
    {
        if( toridraw_vec_resize(gc->elements, (size_t)element_id + 1) != TORIDRAW_VEC_OK )
            return false;
    }

    element = tdgc_element_ptr(gc, element_id);
    if( !element )
        return false;

    memset(element, 0, sizeof(*element));
    element->anim_seq_id = -1;
    return true;
}

static int
tdgc_allocate_element_id(struct ToriDraw_Context* gc)
{
    int id;

    if( !gc )
        return -1;

    if( gc->free_count > 0 )
        id = gc->free_list[--gc->free_count];
    else if( gc->slot_count < TDGC_MAX_ELEMENTS )
        id = gc->slot_count++;
    else
        return -1;

    gc->slots[id] = true;

    if( !tdgc_prepare_element_slot(gc, id) )
    {
        gc->slots[id] = false;
        gc->free_list[gc->free_count++] = id;
        return -1;
    }

    return id;
}

static void
tdgc_dispose_element_model(struct ToriDraw_GCElement* element)
{
    if( !element )
        return;
    if( element->model.kind == TORIDRAWMK_MODEL && element->model.u.model.model )
        toridraw_model_free(element->model.u.model.model);
    element->model.kind = TORIDRAWMK_NONE;
    element->model.u.model.model = NULL;
}

static void
tdgc_free_element_id(
    struct ToriDraw_Context* gc,
    int element_id)
{
    struct ToriDraw_GCElement* element;

    if( element_id < 0 || element_id >= TDGC_MAX_ELEMENTS )
        return;
    if( !gc->slots[element_id] )
        return;

    element = tdgc_element_ptr(gc, element_id);
    if( element )
    {
        if( element->anim_seq_id != -1 )
        {
            tdgc_emit(
                gc,
                TDGC_ANIM_UNLOAD,
                0,
                element_id,
                0,
                0,
                element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
                NULL,
                NULL);
        }
        tdgc_dispose_element_model(element);
        memset(element, 0, sizeof(*element));
        element->anim_seq_id = -1;
    }

    gc->slots[element_id] = false;
    gc->free_list[gc->free_count++] = element_id;
}

static void
tdgc_free_models_map(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_ToriModel* entry = NULL;
    while( (entry = (struct MapEntry_ToriModel*)toridraw_map_iter_next(iter)) )
    {
        if( entry->model.kind == TORIDRAWMK_MODEL )
            toridraw_model_free(entry->model.u.model.model);
    }
    toridraw_map_iter_free(iter);
}

static void
tdgc_free_animations_map(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_Animation* entry = NULL;
    while( (entry = (struct MapEntry_Animation*)toridraw_map_iter_next(iter)) )
    {
        if( entry->animation )
            toridraw_animation_free(entry->animation);
    }
    toridraw_map_iter_free(iter);
}

static void
tdgc_free_textures(struct ToriDraw_TextureMap* map)
{
    if( !map )
        return;

    for( int i = 0; i < 256; i++ )
    {
        if( map->textures[i] )
            toridraw_texture_free(map->textures[i]);
        map->textures[i] = NULL;
    }
    map->count = 0;
}

struct ToriDraw_Context*
toridraw_context_new(void)
{
    struct ToriDraw_Context* context = calloc(1, sizeof(struct ToriDraw_Context));
    if( !context )
        return NULL;

    context->models_hmap = tdgc_map_new(sizeof(struct MapEntry_ToriModel), 1024);
    context->animation_hmap = tdgc_map_new(sizeof(struct MapEntry_Animation), 512);
    context->elements = toridraw_vec_new(sizeof(struct ToriDraw_GCElement), 0);

    if( !context->models_hmap || !context->animation_hmap || !context->elements )
    {
        toridraw_context_free(context);
        return NULL;
    }

    return context;
}

void
toridraw_context_free(struct ToriDraw_Context* context)
{
    if( !context )
        return;

    for( int i = 0; i < context->slot_count; i++ )
    {
        if( !context->slots[i] )
            continue;
        struct ToriDraw_GCElement* element = tdgc_element_ptr(context, i);
        if( element )
            tdgc_dispose_element_model(element);
    }

    for( int i = 0; i < context->pending_pose_count; i++ )
    {
        if( context->pending_poses[i].model )
            toridraw_model_free(context->pending_poses[i].model);
    }
    free(context->pending_poses);

    if( context->elements )
        toridraw_vec_free(context->elements);

    tdgc_free_models_map(context->models_hmap);
    tdgc_map_free(context->models_hmap);

    tdgc_free_animations_map(context->animation_hmap);
    tdgc_map_free(context->animation_hmap);

    tdgc_free_textures(&context->texture_map);

    free(context);
}

void
toridraw_gc_model_add(
    struct ToriDraw_Context* gc,
    int model_id,
    struct ToriDraw_ModelHandle model)
{
    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)toridraw_map_search(
        gc->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = model;
}

struct ToriDraw_ModelHandle
toridraw_gc_model_get(
    struct ToriDraw_Context* gc,
    int model_id)
{
    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)toridraw_map_search(
        gc->models_hmap, &model_id, TORIDRAW_MAP_FIND);

    struct ToriDraw_ModelHandle model = { .kind = TORIDRAWMK_NONE };
    if( !entry )
        return model;

    return entry->model;
}

bool
toridraw_gc_model_has(
    struct ToriDraw_Context* gc,
    int model_id)
{
    return toridraw_gc_model_get(gc, model_id).kind == TORIDRAWMK_MODEL;
}

struct ToriDraw_ModelHandle
toridraw_gc_model_remove(
    struct ToriDraw_Context* gc,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    if( !gc )
        return none;

    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)toridraw_map_search(
        gc->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry )
        return none;

    return entry->model;
}

void
toridraw_gc_models_clear_all(struct ToriDraw_Context* gc)
{
    if( !gc || !gc->models_hmap )
        return;

    tdgc_free_models_map(gc->models_hmap);
    tdgc_map_reset(&gc->models_hmap, sizeof(struct MapEntry_ToriModel), 1024);
}

void
toridraw_gc_animation_add(
    struct ToriDraw_Context* gc,
    int anim_id,
    struct ToriDraw_Animation* animation)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)toridraw_map_search(
        gc->animation_hmap, &anim_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->animation )
        toridraw_animation_free(entry->animation);

    entry->id = anim_id;
    entry->animation = animation;
}

struct ToriDraw_Animation*
toridraw_gc_animation_get(
    struct ToriDraw_Context* gc,
    int anim_id)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)toridraw_map_search(
        gc->animation_hmap, &anim_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->animation;
}

bool
toridraw_gc_animation_has(
    struct ToriDraw_Context* gc,
    int anim_id)
{
    return toridraw_gc_animation_get(gc, anim_id) != NULL;
}

void
toridraw_gc_set_texture(
    struct ToriDraw_Context* gc,
    int id,
    struct ToriDraw_Texture* texture)
{
    if( !gc || id < 0 || id >= 256 )
        return;

    struct ToriDraw_TextureMap* map = &gc->texture_map;
    struct ToriDraw_Texture* const old = map->textures[id];

    if( old == texture )
        return;

    if( old )
    {
        tdgc_emit(gc, TDGC_TEX_UNLOAD, 0, 0, 0, id, NULL, NULL, NULL);
        toridraw_texture_free(old);
        map->textures[id] = NULL;
    }

    if( texture )
    {
        map->textures[id] = texture;
        tdgc_emit(gc, TDGC_TEX_LOAD, 0, 0, 0, id, NULL, NULL, texture);
        if( id >= map->count )
            map->count = id + 1;
    }
    else if( id == map->count - 1 )
    {
        while( map->count > 0 && !map->textures[map->count - 1] )
            map->count--;
    }
}

void
toridraw_gc_clear_scene(struct ToriDraw_Context* gc)
{
    assert(gc);

    for( int i = 0; i < gc->slot_count; i++ )
    {
        if( !gc->slots[i] )
            continue;

        struct ToriDraw_GCElement* element = tdgc_element_ptr(gc, i);
        if( !element )
            continue;

        if( element->anim_seq_id != -1 )
        {
            tdgc_emit(
                gc,
                TDGC_ANIM_UNLOAD,
                0,
                i,
                0,
                0,
                element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
                NULL,
                NULL);
        }
        tdgc_emit(
            gc,
            TDGC_MODEL_UNLOAD,
            0,
            i,
            0,
            0,
            element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
            NULL,
            NULL);
        tdgc_dispose_element_model(element);
    }

    memset(gc->slots, 0, sizeof(gc->slots));
    gc->slot_count = 0;
    gc->free_count = 0;
    gc->batch_building = false;
    gc->current_batch_id = 0;
    gc->current_batch_element_count = 0;
    gc->next_batch_id = 0;

    toridraw_vec_clear(gc->elements);

    tdgc_emit(gc, TDGC_SCENE_RESET, 0, 0, 0, 0, NULL, NULL, NULL);
}

int
toridraw_gc_element_add(struct ToriDraw_Context* gc)
{
    assert(gc);
    return tdgc_allocate_element_id(gc);
}

int
toridraw_gc_element_remove(
    struct ToriDraw_Context* gc,
    int element_id)
{
    struct ToriDraw_GCElement* element;

    assert(gc);

    if( !tdgc_element_valid(gc, element_id) )
        return -1;

    element = tdgc_element_ptr(gc, element_id);
    tdgc_emit(
        gc,
        TDGC_MODEL_UNLOAD,
        0,
        element_id,
        0,
        0,
        element && element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
        NULL,
        NULL);

    tdgc_free_element_id(gc, element_id);
    return 0;
}

struct ToriDraw_GCElement*
toridraw_gc_element_get(
    struct ToriDraw_Context* gc,
    int element_id)
{
    return tdgc_element_ptr(gc, element_id);
}

bool
toridraw_gc_element_is_live(
    struct ToriDraw_Context* gc,
    int element_id)
{
    return tdgc_element_valid(gc, element_id);
}

int
toridraw_gc_element_slot_count(struct ToriDraw_Context* gc)
{
    assert(gc);
    return gc->slot_count;
}

static void
tdgc_element_assign_model(
    struct ToriDraw_Context* gc,
    int element_id,
    struct ToriDraw_ModelHandle model)
{
    struct ToriDraw_GCElement* element;

    assert(gc);
    assert(model.kind == TORIDRAWMK_MODEL);
    assert(tdgc_element_valid(gc, element_id));

    element = tdgc_element_ptr(gc, element_id);
    assert(element);

    tdgc_dispose_element_model(element);
    element->model = model;

    if( gc->batch_building )
        element->pending_batch_add = true;
    else
        tdgc_emit(gc, TDGC_MODEL_LOAD, 0, element_id, 0, 0, &element->model, NULL, NULL);
}

void
toridraw_gc_element_set_model(
    struct ToriDraw_Context* gc,
    int element_id,
    struct ToriDraw_ModelHandle model)
{
    tdgc_element_assign_model(gc, element_id, model);
}

void
toridraw_gc_element_set_animation(
    struct ToriDraw_Context* gc,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary)
{
    struct ToriDraw_GCElement* element;
    struct ToriDraw_Animation** slot;

    assert(gc);
    assert(tdgc_element_valid(gc, element_id));

    element = tdgc_element_ptr(gc, element_id);
    assert(element);

    slot = primary ? &element->animation : &element->secondary_animation;

    if( animation )
    {
        *slot = animation;
        tdgc_emit(gc, TDGC_ANIM_LOAD, 0, element_id, 0, 0, &element->model, animation, NULL);
    }
    else
    {
        *slot = NULL;
        if( primary )
            element->anim_seq_id = -1;
        tdgc_emit(gc, TDGC_ANIM_UNLOAD, 0, element_id, 0, 0, &element->model, NULL, NULL);
    }
}

void
toridraw_gc_element_set_animation_seq(
    struct ToriDraw_Context* gc,
    int element_id,
    int seq_id)
{
    struct ToriDraw_GCElement* element;

    assert(gc);
    assert(tdgc_element_valid(gc, element_id));

    element = tdgc_element_ptr(gc, element_id);
    assert(element);

    element->anim_seq_id = seq_id;
    element->anim_frame = 0;
    element->anim_cycle = 0;
    element->animation = NULL;

    if( element->model.kind == TORIDRAWMK_MODEL )
        toridraw_model_capture_original_vertices(element->model.u.model.model);
}

void
toridraw_gc_element_set_position(
    struct ToriDraw_Context* gc,
    int element_id,
    int x,
    int y,
    int z,
    int yaw)
{
    struct ToriDraw_GCElement* element;

    assert(gc);
    assert(tdgc_element_valid(gc, element_id));

    element = tdgc_element_ptr(gc, element_id);
    assert(element);

    element->world_position.x = x;
    element->world_position.y = y;
    element->world_position.z = z;
    element->world_position.yaw = toridraw_normalize_angle(yaw);
    element->world_position.pitch = 0;
    element->world_position.roll = 0;

    if( gc->batch_building && element->pending_batch_add )
    {
        tdgc_emit(
            gc,
            TDGC_BATCH_MODEL_ADD,
            gc->current_batch_id,
            element_id,
            0,
            0,
            &element->model,
            NULL,
            NULL);
        element->pending_batch_add = false;
    }
}

void
toridraw_gc_batch_begin(struct ToriDraw_Context* gc)
{
    assert(gc);
    assert(!gc->batch_building);

    gc->batch_building = true;
    gc->current_batch_id = gc->next_batch_id++;
    gc->current_batch_element_count = 0;

    tdgc_emit(gc, TDGC_BATCH_BEGIN, gc->current_batch_id, 0, 0, 0, NULL, NULL, NULL);
}

struct ToriDraw_GCBatchElementHandle
toridraw_gc_batch_add_element(struct ToriDraw_Context* gc)
{
    struct ToriDraw_GCBatchElementHandle invalid;
    struct ToriDraw_GCBatchElementHandle handle;
    int element_id;

    TDGC_BATCH_ELEMENT_HANDLE_INVALID(invalid, gc);

    assert(gc);
    assert(gc->batch_building);

    element_id = tdgc_allocate_element_id(gc);
    if( element_id < 0 )
        return invalid;

    gc->current_batch_element_count++;

    tdgc_emit(gc, TDGC_BATCH_MODEL_ADD, gc->current_batch_id, element_id, 0, 0, NULL, NULL, NULL);

    handle.context = gc;
    handle.batch_id = gc->current_batch_id;
    handle.id = element_id;
    return handle;
}

void
toridraw_gc_batch_end(struct ToriDraw_Context* gc)
{
    assert(gc);
    assert(gc->batch_building);

    for( int i = 0; i < gc->slot_count; i++ )
    {
        struct ToriDraw_GCElement* element;

        if( !gc->slots[i] )
            continue;
        element = tdgc_element_ptr(gc, i);
        if( !element || !element->pending_batch_add )
            continue;
        tdgc_emit(
            gc, TDGC_BATCH_MODEL_ADD, gc->current_batch_id, i, 0, 0, &element->model, NULL, NULL);
        element->pending_batch_add = false;
    }

    tdgc_emit(gc, TDGC_BATCH_END, gc->current_batch_id, 0, 0, 0, NULL, NULL, NULL);

    gc->batch_building = false;
    gc->current_batch_element_count = 0;
}

void
toridraw_gc_batch_clear(
    struct ToriDraw_Context* gc,
    int batch_id)
{
    assert(gc);
    tdgc_emit(gc, TDGC_BATCH_CLEAR, batch_id, 0, 0, 0, NULL, NULL, NULL);
}

void
toridraw_gc_batch_element_add_pose(
    struct ToriDraw_Context* gc,
    int element_id,
    int pose_id,
    struct ToriDraw_ModelHandle baked)
{
    assert(gc);
    assert(gc->batch_building);
    assert(baked.kind == TORIDRAWMK_MODEL);
    assert(tdgc_element_valid(gc, element_id));

    tdgc_emit(
        gc, TDGC_BATCH_ANIM_ADD, gc->current_batch_id, element_id, pose_id, 0, &baked, NULL, NULL);

    if( baked.u.model.model )
        tdgc_retain_pending_pose(gc, baked.u.model.model);
}

struct ToriDraw_GCEventQueue*
toridraw_gc_events(struct ToriDraw_Context* gc)
{
    assert(gc);
    return &gc->event_queue;
}

void
toridraw_gc_frame_end(struct ToriDraw_Context* gc)
{
    for( int i = 0; i < gc->pending_pose_count; i++ )
    {
        if( gc->pending_poses[i].model )
            toridraw_model_free(gc->pending_poses[i].model);
    }
    gc->pending_pose_count = 0;
    gc->event_queue.count = 0;
}
