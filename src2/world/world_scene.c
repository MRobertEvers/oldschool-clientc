#include "world_scene.h"

#include "world_scene_events.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_SCENE_MAX_ELEMENTS 65536
#define WORLD_SCENE_INVALID_BATCH_ID (-1)
#define WORLD_SCENE_INVALID_ELEMENT_ID (-1)

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
    struct WorldScene_EventQueue* event_queue_ref;

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
    int asset_id)
{
    struct WorldScene_Event event;

    if( !scene || !scene->event_queue_ref )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.batch_id = batch_id;
    event.asset_id = asset_id;

    worldscene_eventqueue_push(scene->event_queue_ref, &event);
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
    return id;
}

static void
world_scene_free_element_id(
    struct WorldScene* scene,
    int element_id)
{
    if( !scene )
        return;
    if( element_id < 0 || element_id >= WORLD_SCENE_MAX_ELEMENTS )
        return;
    if( !scene->slots[element_id] )
        return;

    scene->slots[element_id] = false;
    scene->free_list[scene->free_count++] = element_id;
}

struct WorldScene*
world_scene_new(struct WorldScene_EventQueue* event_queue_ref)
{
    struct WorldScene* scene = calloc(1, sizeof(struct WorldScene));
    if( !scene )
        return NULL;

    scene->event_queue_ref = event_queue_ref;
    return scene;
}

void
world_scene_free(struct WorldScene* scene)
{
    if( !scene )
        return;
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

    world_scene_emit(scene, WSE_BATCH_BEGIN, scene->current_batch_id, 0);
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

    world_scene_emit(scene, WSE_BATCH_MODEL_ADD, scene->current_batch_id, element_id);

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

    world_scene_emit(scene, WSE_BATCH_END, scene->current_batch_id, 0);

    scene->batch_building = false;
    scene->current_batch_element_count = 0;
}

void
world_scene_batch_clear(
    struct WorldScene* scene,
    int batch_id)
{
    assert(scene);

    world_scene_emit(scene, WSE_BATCH_CLEAR, batch_id, 0);
}

int
world_scene_add_element(struct WorldScene* scene)
{
    int element_id;

    assert(scene);

    element_id = world_scene_allocate_element_id(scene);
    if( element_id < 0 )
        return -1;

    world_scene_emit(scene, WSE_MODEL_LOAD, 0, element_id);
    return element_id;
}

int
world_scene_remove_element(
    struct WorldScene* scene,
    int element_id)
{
    assert(scene);

    if( element_id < 0 || element_id >= WORLD_SCENE_MAX_ELEMENTS )
        return -1;
    if( !scene->slots[element_id] )
        return -1;

    world_scene_free_element_id(scene, element_id);
    world_scene_emit(scene, WSE_MODEL_UNLOAD, 0, element_id);
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

    world_scene_emit(scene, WSE_SCENE_RESET, 0, 0);
}
