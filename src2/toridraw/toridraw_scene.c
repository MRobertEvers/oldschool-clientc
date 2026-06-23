#include "toridraw_scene.h"

#include "toridraw_animation.h"
#include "toridraw_map.h"
#include "toridraw_math.h"
#include "toridraw_model.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#define TD_SCENE_BATCH_ELEMENT_HANDLE_INVALID(HANDLE, SCENE) \
    do { \
        (HANDLE).scene = (SCENE); \
        (HANDLE).batch_id = TORIDRAW_SCENE_INVALID_BATCH_ID; \
        (HANDLE).id = TORIDRAW_SCENE_INVALID_ELEMENT_ID; \
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

struct ToriDraw_ScenePendingPose
{
    struct ToriDraw_Model* model;
};

static struct ToriDraw_Map*
td_scene_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = ToriDraw_MapBufferSizeFor(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return ToriDraw_MapNew(&config, 0);
}

static void
td_scene_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    free(ToriDraw_MapBufferPtr(map));
    ToriDraw_MapFree(map);
}

static void
td_scene_map_reset(
    struct ToriDraw_Map** map_out,
    int entry_size,
    int capacity)
{
    if( !map_out || !*map_out )
        return;

    td_scene_map_free(*map_out);
    *map_out = td_scene_map_new(entry_size, capacity);
}

static bool
td_scene_element_valid(
    const struct ToriDraw_Scene* scene,
    int element_id);

static struct ToriDraw_SceneElement*
td_scene_element_ptr(
    struct ToriDraw_Scene* scene,
    int element_id);

static void
td_scene_emit(
    struct ToriDraw_Scene* scene,
    enum ToriDraw_EventKind kind,
    int batch_id,
    int element_id,
    int pose_id,
    int texture_id,
    const struct ToriDraw_ModelHandle* model,
    struct ToriDraw_Animation* animation,
    struct ToriDraw_Texture* texture)
{
    struct ToriDraw_Event event;

    if( !scene )
        return;
    if( scene->event_queue.count >= TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE )
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
    if( td_scene_element_valid(scene, element_id) )
    {
        struct ToriDraw_SceneElement* element = td_scene_element_ptr(scene, element_id);
        if( element )
            event.world_position = element->world_position;
    }

    scene->event_queue.events[scene->event_queue.count++] = event;
}

static void
td_scene_retain_pending_pose(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Model* model)
{
    if( !scene || !model )
        return;

    if( scene->pending_pose_count >= scene->pending_pose_cap )
    {
        int new_cap = scene->pending_pose_cap ? scene->pending_pose_cap * 2 : 64;
        struct ToriDraw_ScenePendingPose* grown =
            realloc(scene->pending_poses, (size_t)new_cap * sizeof(struct ToriDraw_ScenePendingPose));
        if( !grown )
            return;
        scene->pending_poses = grown;
        scene->pending_pose_cap = new_cap;
    }

    scene->pending_poses[scene->pending_pose_count++].model = model;
}

static bool
td_scene_element_valid(
    const struct ToriDraw_Scene* scene,
    int element_id)
{
    if( !scene )
        return false;
    if( element_id < 0 || element_id >= TORIDRAW_SCENE_MAX_ELEMENTS )
        return false;
    return ToriDraw_IntrusiveListIsLive(&scene->elements, element_id);
}

static struct ToriDraw_SceneElement*
td_scene_element_ptr(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    if( !td_scene_element_valid(scene, element_id) )
        return NULL;
    return (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListGet(&scene->elements, element_id);
}

static void
td_scene_reset_element(struct ToriDraw_SceneElement* element)
{
    if( !element )
        return;
    int scene_id = element->scene_id;
    memset(element, 0, sizeof(*element));
    element->scene_id = scene_id;
    element->anim_seq_id = -1;
}

static int
td_scene_allocate_element_id(struct ToriDraw_Scene* scene)
{
    struct ToriDraw_SceneElement* element;
    int id;

    if( !scene )
        return -1;

    if( scene->elements.free_head != TORIDRAW_INTRUSIVE_NIL )
        element = (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListGet(
            &scene->elements, scene->elements.free_head);
    else
    {
        if( scene->elements.count >= TORIDRAW_SCENE_MAX_ELEMENTS )
            return -1;
        element = calloc(1, sizeof(struct ToriDraw_SceneElement));
        if( !element )
            return -1;
    }

    td_scene_reset_element(element);

    id = ToriDraw_IntrusiveListAlloc(&scene->elements, element);
    if( id < 0 )
    {
        if( scene->elements.free_head == TORIDRAW_INTRUSIVE_NIL )
            free(element);
        return -1;
    }

    element->scene_id = id;
    return id;
}

static void
td_scene_dispose_element_model(struct ToriDraw_SceneElement* element)
{
    if( !element )
        return;
    if( element->model.kind == TORIDRAWMK_MODEL && element->model.u.model.model )
        ToriDraw_ModelFree(element->model.u.model.model);
    element->model.kind = TORIDRAWMK_NONE;
    element->model.u.model.model = NULL;
}

static void
td_scene_free_element_id(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* element;

    if( !td_scene_element_valid(scene, element_id) )
        return;

    element = td_scene_element_ptr(scene, element_id);
    if( element )
    {
        if( element->anim_seq_id != -1 )
        {
            td_scene_emit(
                scene,
                TORIDRAW_EVENT_ANIM_UNLOAD,
                0,
                element_id,
                0,
                0,
                element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
                NULL,
                NULL);
        }
        td_scene_dispose_element_model(element);
        td_scene_reset_element(element);
    }

    ToriDraw_IntrusiveListRelease(&scene->elements, element_id);
}

static void
td_scene_free_models_map(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_ToriModel* entry = NULL;
    while( (entry = (struct MapEntry_ToriModel*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->model.kind == TORIDRAWMK_MODEL )
            ToriDraw_ModelFree(entry->model.u.model.model);
    }
    ToriDraw_MapIterFree(iter);
}

static void
td_scene_free_animations_map(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Animation* entry = NULL;
    while( (entry = (struct MapEntry_Animation*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->animation )
            ToriDraw_AnimationFree(entry->animation);
    }
    ToriDraw_MapIterFree(iter);
}

static void
td_scene_free_textures(struct ToriDraw_TextureMap* map)
{
    if( !map )
        return;

    for( int i = 0; i < 256; i++ )
    {
        if( map->textures[i] )
            ToriDraw_TextureFree(map->textures[i]);
        map->textures[i] = NULL;
    }
    map->count = 0;
}

static void
td_scene_free_all_elements(struct ToriDraw_Scene* scene)
{
    int i;

    if( !scene )
        return;

    for( i = 0; i < scene->elements.count; i++ )
    {
        struct ToriDraw_SceneElement* element =
            (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListGet(&scene->elements, i);
        if( !element )
            continue;
        if( ToriDraw_IntrusiveListIsLive(&scene->elements, i) )
            td_scene_dispose_element_model(element);
        free(element);
    }
    ToriDraw_IntrusiveListFree(&scene->elements);
}

bool
ToriDraw_SceneGraphInit(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return false;

    scene->models_hmap = td_scene_map_new(sizeof(struct MapEntry_ToriModel), 1024);
    scene->animation_hmap = td_scene_map_new(sizeof(struct MapEntry_Animation), 512);
    ToriDraw_IntrusiveListInit(&scene->elements);

    if( !scene->models_hmap || !scene->animation_hmap )
    {
        ToriDraw_SceneGraphShutdown(scene);
        return false;
    }

    return true;
}

void
ToriDraw_SceneGraphShutdown(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return;

    td_scene_free_all_elements(scene);

    for( int i = 0; i < scene->pending_pose_count; i++ )
    {
        if( scene->pending_poses[i].model )
            ToriDraw_ModelFree(scene->pending_poses[i].model);
    }
    free(scene->pending_poses);

    td_scene_free_models_map(scene->models_hmap);
    td_scene_map_free(scene->models_hmap);

    td_scene_free_animations_map(scene->animation_hmap);
    td_scene_map_free(scene->animation_hmap);

    if( scene->tex_state )
        td_scene_free_textures(&scene->tex_state->texture_map);

    scene->models_hmap = NULL;
    scene->animation_hmap = NULL;
    scene->pending_poses = NULL;
}

void
ToriDraw_SceneModelAdd(
    struct ToriDraw_Scene* scene,
    int model_id,
    struct ToriDraw_ModelHandle model)
{
    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)ToriDraw_MapSearch(
        scene->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = model;
}

struct ToriDraw_ModelHandle
ToriDraw_SceneModelGet(
    struct ToriDraw_Scene* scene,
    int model_id)
{
    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)ToriDraw_MapSearch(
        scene->models_hmap, &model_id, TORIDRAW_MAP_FIND);

    struct ToriDraw_ModelHandle model = { .kind = TORIDRAWMK_NONE };
    if( !entry )
        return model;

    return entry->model;
}

bool
ToriDraw_SceneModelHas(
    struct ToriDraw_Scene* scene,
    int model_id)
{
    return ToriDraw_SceneModelGet(scene, model_id).kind == TORIDRAWMK_MODEL;
}

struct ToriDraw_ModelHandle
ToriDraw_SceneModelRemove(
    struct ToriDraw_Scene* scene,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    if( !scene )
        return none;

    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)ToriDraw_MapSearch(
        scene->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry )
        return none;

    return entry->model;
}

void
ToriDraw_SceneModelsClearAll(struct ToriDraw_Scene* scene)
{
    if( !scene || !scene->models_hmap )
        return;

    td_scene_free_models_map(scene->models_hmap);
    td_scene_map_reset(&scene->models_hmap, sizeof(struct MapEntry_ToriModel), 1024);
}

void
ToriDraw_SceneAnimationAdd(
    struct ToriDraw_Scene* scene,
    int anim_id,
    struct ToriDraw_Animation* animation)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)ToriDraw_MapSearch(
        scene->animation_hmap, &anim_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->animation )
        ToriDraw_AnimationFree(entry->animation);

    entry->id = anim_id;
    entry->animation = animation;
}

struct ToriDraw_Animation*
ToriDraw_SceneAnimationGet(
    struct ToriDraw_Scene* scene,
    int anim_id)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)ToriDraw_MapSearch(
        scene->animation_hmap, &anim_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->animation;
}

bool
ToriDraw_SceneAnimationHas(
    struct ToriDraw_Scene* scene,
    int anim_id)
{
    return ToriDraw_SceneAnimationGet(scene, anim_id) != NULL;
}

void
ToriDraw_SceneSetTexture(
    struct ToriDraw_Scene* scene,
    int id,
    struct ToriDraw_Texture* texture)
{
    if( !scene || id < 0 || id >= 256 )
        return;

    struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(scene);
    if( !tex_state )
        return;

    struct ToriDraw_TextureMap* map = &tex_state->texture_map;
    struct ToriDraw_Texture* const old = map->textures[id];

    if( old == texture )
        return;

    if( old )
    {
        td_scene_emit(scene, TORIDRAW_EVENT_TEX_UNLOAD, 0, 0, 0, id, NULL, NULL, NULL);
        ToriDraw_TextureFree(old);
        map->textures[id] = NULL;
    }

    if( texture )
    {
        map->textures[id] = texture;
        td_scene_emit(scene, TORIDRAW_EVENT_TEX_LOAD, 0, 0, 0, id, NULL, NULL, texture);
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
ToriDraw_SceneClear(struct ToriDraw_Scene* scene)
{
    int i;
    int next;

    assert(scene);

    for( i = scene->elements.head; i != TORIDRAW_INTRUSIVE_NIL; i = next )
    {
        struct ToriDraw_SceneElement* element;

        next = scene->elements.nodes[i].next;
        element = td_scene_element_ptr(scene, i);
        if( !element )
            continue;

        if( element->anim_seq_id != -1 )
        {
            td_scene_emit(
                scene,
                TORIDRAW_EVENT_ANIM_UNLOAD,
                0,
                i,
                0,
                0,
                element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
                NULL,
                NULL);
        }
        td_scene_emit(
            scene,
            TORIDRAW_EVENT_MODEL_UNLOAD,
            0,
            i,
            0,
            0,
            element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
            NULL,
            NULL);
        td_scene_dispose_element_model(element);
        td_scene_reset_element(element);
        ToriDraw_IntrusiveListRelease(&scene->elements, i);
    }

    scene->batch_building = false;
    scene->current_batch_id = 0;
    scene->current_batch_element_count = 0;
    scene->next_batch_id = 0;

    td_scene_emit(scene, TORIDRAW_EVENT_SCENE_RESET, 0, 0, 0, 0, NULL, NULL, NULL);
}

int
ToriDraw_SceneElementAdd(struct ToriDraw_Scene* scene)
{
    assert(scene);
    return td_scene_allocate_element_id(scene);
}

int
ToriDraw_SceneElementRemove(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);

    if( !td_scene_element_valid(scene, element_id) )
        return -1;

    element = td_scene_element_ptr(scene, element_id);
    td_scene_emit(
        scene,
        TORIDRAW_EVENT_MODEL_UNLOAD,
        0,
        element_id,
        0,
        0,
        element && element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
        NULL,
        NULL);

    td_scene_free_element_id(scene, element_id);
    return 0;
}

struct ToriDraw_SceneElement*
ToriDraw_SceneElementGet(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    return td_scene_element_ptr(scene, element_id);
}

bool
ToriDraw_SceneElementIsLive(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    return td_scene_element_valid(scene, element_id);
}

int
ToriDraw_SceneElementSlotCount(struct ToriDraw_Scene* scene)
{
    assert(scene);
    return scene->elements.count;
}

static void
td_scene_element_assign_model(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(model.kind == TORIDRAWMK_MODEL);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    td_scene_dispose_element_model(element);
    element->model = model;

    if( scene->batch_building )
        element->pending_batch_add = true;
    else
        td_scene_emit(scene, TORIDRAW_EVENT_MODEL_LOAD, 0, element_id, 0, 0, &element->model, NULL, NULL);
}

void
ToriDraw_SceneElementSetModel(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model)
{
    td_scene_element_assign_model(scene, element_id, model);
}

void
ToriDraw_SceneElementSetAnimation(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary)
{
    struct ToriDraw_SceneElement* element;
    struct ToriDraw_Animation** slot;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    slot = primary ? &element->animation : &element->secondary_animation;

    if( animation )
    {
        *slot = animation;
        td_scene_emit(
            scene, TORIDRAW_EVENT_ANIM_LOAD, 0, element_id, 0, 0, &element->model, animation, NULL);
    }
    else
    {
        *slot = NULL;
        if( primary )
            element->anim_seq_id = -1;
        td_scene_emit(
            scene, TORIDRAW_EVENT_ANIM_UNLOAD, 0, element_id, 0, 0, &element->model, NULL, NULL);
    }
}

void
ToriDraw_SceneElementSetAnimationSeq(
    struct ToriDraw_Scene* scene,
    int element_id,
    int seq_id)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    element->anim_seq_id = seq_id;
    element->anim_frame = 0;
    element->anim_cycle = 0;
    element->animation = NULL;

    if( element->model.kind == TORIDRAWMK_MODEL )
        ToriDraw_ModelCaptureOriginalVertices(element->model.u.model.model);
}

void
ToriDraw_SceneElementApplyAnimation(
    struct ToriDraw_Scene* scene,
    int element_id,
    bool primary,
    int frame)
{
    struct ToriDraw_SceneElement* element;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Model* model;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    animation = primary ? element->animation : element->secondary_animation;
    if( !animation || !animation->base || !animation->frames || animation->frame_count <= 0 )
        return;
    if( frame < 0 || frame >= animation->frame_count )
        return;
    if( element->model.kind != TORIDRAWMK_MODEL )
        return;

    model = element->model.u.model.model;
    if( !model )
        return;

    ToriDraw_ModelAnimateReset(model);
    ToriDraw_ModelAnimateFrame(model, animation->base, &animation->frames[frame]);
}

void
ToriDraw_SceneElementSetPosition(
    struct ToriDraw_Scene* scene,
    int element_id,
    int x,
    int y,
    int z,
    int yaw)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    element->world_position.x = x;
    element->world_position.y = y;
    element->world_position.z = z;
    element->world_position.yaw = ToriDraw_NormalizeAngle(yaw);
    element->world_position.pitch = 0;
    element->world_position.roll = 0;

    if( scene->batch_building && element->pending_batch_add )
    {
        td_scene_emit(
            scene,
            TORIDRAW_EVENT_BATCH_MODEL_ADD,
            scene->current_batch_id,
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
ToriDraw_SceneBatchBegin(struct ToriDraw_Scene* scene)
{
    assert(scene);
    assert(!scene->batch_building);

    scene->batch_building = true;
    scene->current_batch_id = scene->next_batch_id++;
    scene->current_batch_element_count = 0;

    td_scene_emit(scene, TORIDRAW_EVENT_BATCH_BEGIN, scene->current_batch_id, 0, 0, 0, NULL, NULL, NULL);
}

struct ToriDraw_SceneBatchElementHandle
ToriDraw_SceneBatchAddElement(struct ToriDraw_Scene* scene)
{
    struct ToriDraw_SceneBatchElementHandle invalid;
    struct ToriDraw_SceneBatchElementHandle handle;
    int element_id;

    TD_SCENE_BATCH_ELEMENT_HANDLE_INVALID(invalid, scene);

    assert(scene);
    assert(scene->batch_building);

    element_id = td_scene_allocate_element_id(scene);
    if( element_id < 0 )
        return invalid;

    scene->current_batch_element_count++;

    td_scene_emit(
        scene, TORIDRAW_EVENT_BATCH_MODEL_ADD, scene->current_batch_id, element_id, 0, 0, NULL, NULL, NULL);

    handle.scene = scene;
    handle.batch_id = scene->current_batch_id;
    handle.id = element_id;
    return handle;
}

void
ToriDraw_SceneBatchEnd(struct ToriDraw_Scene* scene)
{
    int i;

    assert(scene);
    assert(scene->batch_building);

    for( i = scene->elements.head; i != TORIDRAW_INTRUSIVE_NIL; i = scene->elements.nodes[i].next )
    {
        struct ToriDraw_SceneElement* element = td_scene_element_ptr(scene, i);
        if( !element || !element->pending_batch_add )
            continue;
        td_scene_emit(
            scene,
            TORIDRAW_EVENT_BATCH_MODEL_ADD,
            scene->current_batch_id,
            i,
            0,
            0,
            &element->model,
            NULL,
            NULL);
        element->pending_batch_add = false;
    }

    td_scene_emit(scene, TORIDRAW_EVENT_BATCH_END, scene->current_batch_id, 0, 0, 0, NULL, NULL, NULL);

    scene->batch_building = false;
    scene->current_batch_element_count = 0;
}

void
ToriDraw_SceneBatchClear(
    struct ToriDraw_Scene* scene,
    int batch_id)
{
    assert(scene);
    td_scene_emit(scene, TORIDRAW_EVENT_BATCH_CLEAR, batch_id, 0, 0, 0, NULL, NULL, NULL);
}

void
ToriDraw_SceneBatchElementAddPose(
    struct ToriDraw_Scene* scene,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle baked)
{
    struct ToriDraw_Event* event;

    assert(scene);
    assert(scene->batch_building);
    assert(baked.kind == TORIDRAWMK_MODEL);
    assert(td_scene_element_valid(scene, element_id));

    td_scene_emit(
        scene,
        TORIDRAW_EVENT_BATCH_ANIM_ADD,
        scene->current_batch_id,
        element_id,
        pose_id,
        0,
        &baked,
        NULL,
        NULL);

    /* Back-patch anim_index onto the event we just pushed. */
    if( scene->event_queue.count > 0 )
    {
        event = &scene->event_queue.events[scene->event_queue.count - 1];
        event->anim_index = anim_index;
    }

    if( baked.u.model.model )
        td_scene_retain_pending_pose(scene, baked.u.model.model);
}

struct ToriDraw_EventQueue*
ToriDraw_SceneEvents(struct ToriDraw_Scene* scene)
{
    assert(scene);
    return &scene->event_queue;
}

void
ToriDraw_SceneFrameEnd(struct ToriDraw_Scene* scene)
{
    for( int i = 0; i < scene->pending_pose_count; i++ )
    {
        if( scene->pending_poses[i].model )
            ToriDraw_ModelFree(scene->pending_poses[i].model);
    }
    scene->pending_pose_count = 0;
    scene->event_queue.count = 0;
}
