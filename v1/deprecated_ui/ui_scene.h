#ifndef UI_SCENE_H
#define UI_SCENE_H

#include "toridraw/toridraw_sprite.h"
#include "ui_scene_events.h"

#include <stdbool.h>
#include <stdint.h>

#define UI_SCENE_MAX_FONTS 8

struct UISceneFontEntry
{
    char name[16];
};

struct UISceneElement
{
    int id;
    bool active;
    struct UISceneElement* next;
    struct UISceneElement* prev;
    char name[64];
    struct ToriDraw_Sprite** ToriDraw_Sprites;
    int ToriDraw_SpritesCount;
    bool ToriDraw_SpritesBorrowed;
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
    struct UISceneFontEntry fonts[UI_SCENE_MAX_FONTS];
    int font_count;
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

int
ui_scene_font_find_id(
    struct UIScene* scene,
    const char* name);

struct RSCacheDat1A_PixFont*
ui_scene_font_get(
    struct UIScene* scene,
    int font_id);

#endif
