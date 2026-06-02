#ifndef UI_SCENE_H
#define UI_SCENE_H

#include "toridraw/toridraw_sprite.h"
#include "ui_scene_events.h"

#include <stdbool.h>
#include <stdint.h>

struct UISceneElement
{
    int id;
    bool active;
    struct UISceneElement* next;
    struct UISceneElement* prev;
    char name[64];
    struct ToriDraw_Sprite** toridraw_sprites;
    int toridraw_sprites_count;
    bool toridraw_sprites_borrowed;
};

struct UIScene
{
    struct UISceneElement* elements;
    int elements_count;
    struct UISceneElement* active_list;
    struct UISceneElement* free_list;
    int active_len;
    int free_len;
    struct UISceneEventQueue events;
};

struct UIScene*
ui_scene_new(int capacity);

void
ui_scene_free(struct UIScene* scene);

int
ui_scene_element_acquire(struct UIScene* scene);

int
ui_scene_element_acquire_with_sprites(
    struct UIScene* scene,
    struct ToriDraw_Sprite** sprites,
    int sprites_count,
    bool borrowed,
    const char* name);

struct UISceneElement*
ui_scene_element_at(
    struct UIScene* scene,
    int element_id);

struct UISceneEventQueue*
ui_scene_get_event_queue(struct UIScene* scene);

#endif
