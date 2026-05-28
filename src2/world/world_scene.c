#include "world_scene.h"

#include "toridraw/toridraw_vec.h"
#include "world_scene_events.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_SCENE_MAX_ELEMENTS 65536

// clang-format off
#define WORLD_SCENE_BATCH_ELEMENT_HANDLE_INVALID(HANDLE, SCENE) \
    do { \
        (HANDLE).scene = (SCENE); \
        (HANDLE).batch_id = WORLD_SCENE_INVALID_BATCH_ID; \
        (HANDLE).id = WORLD_SCENE_INVALID_ELEMENT_ID; \
    } while(0)
// clang-format on

struct WorldScene
{
    struct WorldScene_EventQueue* event_queue;
    struct ToriDraw_Vec* elements;

    bool slots[WORLD_SCENE_MAX_ELEMENTS];
    int slot_count;

    int free_list[WORLD_SCENE_MAX_ELEMENTS];
    int free_count;

    bool batch_building;
    int current_batch_id;
    int current_batch_element_count;
    int next_batch_id;
};

static void
world_scene_emit(
    struct WorldScene* scene,
    enum WorldScene_EventKind kind,
    int batch_id,
    int element_id,
    const struct ToriDraw_ModelHandle* model,
    struct ToriDraw_Animation* animation)
{
    struct WorldScene_Event event;

    if( !scene || !scene->event_queue )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.batch_id = batch_id;
    event.element_id = element_id;
    if( model )
        event.model = *model;
    event.animation = animation;

    worldscene_eventqueue_push(scene->event_queue, &event);
}

static bool
world_scene_element_valid(
    const struct WorldScene* scene,
    int element_id)
{
    if( !scene )
        return false;
    if( element_id < 0 || element_id >= WORLD_SCENE_MAX_ELEMENTS )
        return false;
    if( !scene->slots[element_id] )
        return false;
    return true;
}

static struct WorldSceneElement*
world_scene_element_ptr(
    struct WorldScene* scene,
    int element_id)
{
    void* slot;

    if( !world_scene_element_valid(scene, element_id) )
        return NULL;
    if( (size_t)element_id >= toridraw_vec_size(scene->elements) )
        return NULL;

    slot = toridraw_vec_get(scene->elements, (size_t)element_id);
    return slot ? (struct WorldSceneElement*)slot : NULL;
}

static bool
world_scene_prepare_element_slot(
    struct WorldScene* scene,
    int element_id)
{
    struct WorldSceneElement* element;

    if( (size_t)element_id + 1 > toridraw_vec_size(scene->elements) )
    {
        if( toridraw_vec_resize(scene->elements, (size_t)element_id + 1) != TORIDRAW_VEC_OK )
            return false;
    }

    element = world_scene_element_ptr(scene, element_id);
    if( !element )
        return false;

    memset(element, 0, sizeof(*element));
    return true;
}

static int
world_scene_allocate_element_id(struct WorldScene* scene)
{
    int id;

    if( !scene )
        return -1;

    if( scene->free_count > 0 )
    {
        id = scene->free_list[--scene->free_count];
    }
    else if( scene->slot_count < WORLD_SCENE_MAX_ELEMENTS )
    {
        id = scene->slot_count++;
    }
    else
    {
        return -1;
    }

    scene->slots[id] = true;

    if( !world_scene_prepare_element_slot(scene, id) )
    {
        scene->slots[id] = false;
        scene->free_list[scene->free_count++] = id;
        return -1;
    }

    return id;
}

static void
world_scene_free_element_id(
    struct WorldScene* scene,
    int element_id)
{
    struct WorldSceneElement* element;

    if( !scene )
        return;
    if( element_id < 0 || element_id >= WORLD_SCENE_MAX_ELEMENTS )
        return;
    if( !scene->slots[element_id] )
        return;

    element = world_scene_element_ptr(scene, element_id);
    if( element )
        memset(element, 0, sizeof(*element));

    scene->slots[element_id] = false;
    scene->free_list[scene->free_count++] = element_id;
}

struct WorldScene*
world_scene_new(void)
{
    struct WorldScene* scene = calloc(1, sizeof(struct WorldScene));
    if( !scene )
        return NULL;

    scene->event_queue = worldscene_eventqueue_new();
    if( !scene->event_queue )
    {
        free(scene);
        return NULL;
    }

    scene->elements = toridraw_vec_new(sizeof(struct WorldSceneElement), 0);
    if( !scene->elements )
    {
        worldscene_eventqueue_free(scene->event_queue);
        free(scene);
        return NULL;
    }

    return scene;
}

void
world_scene_free(struct WorldScene* scene)
{
    if( !scene )
        return;

    if( scene->elements )
        toridraw_vec_free(scene->elements);
    if( scene->event_queue )
        worldscene_eventqueue_free(scene->event_queue);
    free(scene);
}

void
world_scene_batch_begin(struct WorldScene* scene)
{
    assert(scene);
    assert(!scene->batch_building);

    scene->batch_building = true;
    scene->current_batch_id = scene->next_batch_id++;
    scene->current_batch_element_count = 0;

    world_scene_emit(scene, WSE_BATCH_BEGIN, scene->current_batch_id, 0, NULL, NULL);
}

struct WorldSceneBatchElementHandle
world_scene_batch_add_element(struct WorldScene* scene)
{
    struct WorldSceneBatchElementHandle invalid;
    struct WorldSceneBatchElementHandle handle;
    int element_id;

    WORLD_SCENE_BATCH_ELEMENT_HANDLE_INVALID(invalid, scene);

    assert(scene);
    assert(scene->batch_building);

    element_id = world_scene_allocate_element_id(scene);
    if( element_id < 0 )
        return invalid;

    scene->current_batch_element_count++;

    world_scene_emit(scene, WSE_BATCH_MODEL_ADD, scene->current_batch_id, element_id, NULL, NULL);

    handle.scene = scene;
    handle.batch_id = scene->current_batch_id;
    handle.id = element_id;
    return handle;
}

void
world_scene_batch_end(struct WorldScene* scene)
{
    assert(scene);
    assert(scene->batch_building);

    world_scene_emit(scene, WSE_BATCH_END, scene->current_batch_id, 0, NULL, NULL);

    scene->batch_building = false;
    scene->current_batch_element_count = 0;
}

void
world_scene_batch_clear(
    struct WorldScene* scene,
    int batch_id)
{
    assert(scene);

    world_scene_emit(scene, WSE_BATCH_CLEAR, batch_id, 0, NULL, NULL);
}

int
world_scene_add_element(struct WorldScene* scene)
{
    int element_id;

    assert(scene);

    element_id = world_scene_allocate_element_id(scene);
    if( element_id < 0 )
        return -1;

    return element_id;
}

int
world_scene_remove_element(
    struct WorldScene* scene,
    int element_id)
{
    struct WorldSceneElement* element;

    assert(scene);

    if( !world_scene_element_valid(scene, element_id) )
        return -1;

    element = world_scene_element_ptr(scene, element_id);
    world_scene_emit(
        scene,
        WSE_MODEL_UNLOAD,
        0,
        element_id,
        element && element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
        NULL);

    world_scene_free_element_id(scene, element_id);
    return 0;
}

void
world_scene_clear(struct WorldScene* scene)
{
    assert(scene);

    memset(scene->slots, 0, sizeof(scene->slots));
    scene->slot_count = 0;
    scene->free_count = 0;
    scene->batch_building = false;
    scene->current_batch_id = 0;
    scene->current_batch_element_count = 0;
    scene->next_batch_id = 0;

    toridraw_vec_clear(scene->elements);

    world_scene_emit(scene, WSE_SCENE_RESET, 0, 0, NULL, NULL);
}

struct WorldSceneElement*
world_scene_element_get(
    struct WorldScene* scene,
    int element_id)
{
    return world_scene_element_ptr(scene, element_id);
}

struct WorldSceneElement*
world_scene_element_get_handle(struct WorldSceneElementHandle handle)
{
    return world_scene_element_ptr(handle.scene, handle.id);
}

void
world_scene_element_set_model(
    struct WorldScene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model)
{
    struct WorldSceneElement* element;

    assert(scene);
    assert(model.kind == TORIDRAWMK_MODEL);
    assert(world_scene_element_valid(scene, element_id));

    element = world_scene_element_ptr(scene, element_id);
    assert(element);

    element->model = model;
    world_scene_emit(scene, WSE_MODEL_LOAD, 0, element_id, &element->model, NULL);
}

void
world_scene_element_set_animation(
    struct WorldScene* scene,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary)
{
    struct WorldSceneElement* element;
    struct ToriDraw_Animation** slot;

    assert(scene);
    assert(world_scene_element_valid(scene, element_id));

    element = world_scene_element_ptr(scene, element_id);
    assert(element);

    slot = primary ? &element->animation : &element->secondary_animation;

    if( animation )
    {
        *slot = animation;
        world_scene_emit(scene, WSE_ANIM_LOAD, 0, element_id, &element->model, animation);
    }
    else
    {
        *slot = NULL;
        world_scene_emit(scene, WSE_ANIM_UNLOAD, 0, element_id, &element->model, NULL);
    }
}

struct WorldScene_EventQueue*
world_scene_get_event_queue(struct WorldScene* scene)
{
    assert(scene);
    return scene->event_queue;
}