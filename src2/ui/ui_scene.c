#include "ui_scene.h"

#include <stdlib.h>
#include <string.h>

static void
ui_scene_push_event(
    struct UIScene* scene,
    enum UISceneEventKind kind,
    int element_id)
{
    uiscene_eventqueue_push(
        &scene->events,
        &(struct UISceneEvent){
            .kind = kind,
            .element_id = element_id,
        });
}

struct UIScene*
ui_scene_new(int capacity)
{
    if( capacity <= 0 )
        capacity = 256;

    struct UIScene* scene = calloc(1, sizeof(struct UIScene));
    if( !scene )
        return NULL;

    scene->elements = calloc((size_t)capacity, sizeof(struct UISceneElement));
    if( !scene->elements )
    {
        free(scene);
        return NULL;
    }
    scene->elements_count = capacity;

    for( int i = 0; i < capacity; i++ )
    {
        scene->elements[i].id = i;
        scene->elements[i].active = false;
        if( i < capacity - 1 )
            scene->elements[i].next = &scene->elements[i + 1];
        if( i > 0 )
            scene->elements[i].prev = &scene->elements[i - 1];
    }
    scene->free_list = &scene->elements[0];
    scene->free_len = capacity;

    return scene;
}

void
ui_scene_free(struct UIScene* scene)
{
    if( !scene )
        return;

    for( int i = 0; i < scene->elements_count; i++ )
    {
        struct UISceneElement* el = &scene->elements[i];
        if( !el->toridraw_sprites || el->toridraw_sprites_borrowed )
            continue;
        for( int j = 0; j < el->toridraw_sprites_count; j++ )
        {
            if( el->toridraw_sprites[j] )
                toridraw_sprite_free(el->toridraw_sprites[j]);
        }
        free(el->toridraw_sprites);
        el->toridraw_sprites = NULL;
        el->toridraw_sprites_count = 0;
    }

    free(scene->elements);
    free(scene);
}

static struct UISceneElement*
ui_scene_take_free(struct UIScene* scene)
{
    if( !scene || !scene->free_list )
        return NULL;

    struct UISceneElement* el = scene->free_list;
    scene->free_list = el->next;
    scene->free_len--;
    el->next = scene->active_list;
    el->prev = NULL;
    if( scene->active_list )
        scene->active_list->prev = el;
    scene->active_list = el;
    scene->active_len++;
    el->active = true;
    return el;
}

int
ui_scene_element_acquire(struct UIScene* scene)
{
    return ui_scene_element_acquire_with_sprites(scene, NULL, 0, false, NULL);
}

int
ui_scene_element_acquire_with_sprites(
    struct UIScene* scene,
    struct ToriDraw_Sprite** sprites,
    int sprites_count,
    bool borrowed,
    const char* name)
{
    struct UISceneElement* el = ui_scene_take_free(scene);
    if( !el )
        return -1;

    el->toridraw_sprites = sprites;
    el->toridraw_sprites_count = sprites_count;
    el->toridraw_sprites_borrowed = borrowed;
    if( name )
    {
        strncpy(el->name, name, sizeof(el->name) - 1);
        el->name[sizeof(el->name) - 1] = '\0';
    }
    else
        el->name[0] = '\0';

    ui_scene_push_event(scene, UISCENE_EVENT_ELEMENT_ACQUIRED, el->id);
    return el->id;
}

struct UISceneElement*
ui_scene_element_at(
    struct UIScene* scene,
    int element_id)
{
    if( !scene || element_id < 0 || element_id >= scene->elements_count )
        return NULL;
    return &scene->elements[element_id];
}

struct UISceneEventQueue*
ui_scene_get_event_queue(struct UIScene* scene)
{
    if( !scene )
        return NULL;
    return &scene->events;
}
