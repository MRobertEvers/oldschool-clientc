#ifndef UISCENE_H
#define UISCENE_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

enum UISceneEventType
{
    UISCENE_EVENT_NONE = 0,
    UISCENE_EVENT_ELEMENT_ACQUIRED = 1,
    UISCENE_EVENT_ELEMENT_RELEASED = 2,
    UISCENE_EVENT_FONT_ADDED = 3,
};

struct UISceneEvent
{
    enum UISceneEventType type;
    int element_id;
    int parent_entity_id;
    int font_id;
};

struct UISceneElement
{
    int id;
    bool active;
    int parent_entity_id;
    struct UISceneElement* next;
    struct UISceneElement* prev;

    char name[64];
    struct DashSprite** dash_sprites;
    int dash_sprites_count;
    /** If true, dash_sprites point at BuildCacheDat (or elsewhere); do not dashsprite_free them. */
    bool dash_sprites_borrowed;
};

#define UISCENE_FONT_MAX 16
#define UISCENE_FONT_NAME_MAX 64

struct UISceneFontSlot
{
    char name[UISCENE_FONT_NAME_MAX];
    struct DashPixFont* font;
};

struct UIScene
{
    // Intrusive list. Elements point to their next element.
    struct UISceneElement* elements;
    int elements_count;

    struct UISceneElement* active_list;
    int active_len;
    struct UISceneElement* free_list;
    int free_len;

    struct UISceneEvent* eventbuffer;
    int eventbuffer_capacity;
    int eventbuffer_head;
    int eventbuffer_tail;
    int eventbuffer_count;

    struct UISceneFontSlot fonts[UISCENE_FONT_MAX];
    int font_count;
};

struct UIScene*
uiscene_new(int size);

void
uiscene_free(struct UIScene* uiscene);

int
uiscene_element_acquire(
    struct UIScene* uiscene,
    int parent_entity_id);

void
uiscene_element_release(
    struct UIScene* uiscene,
    int element_id);

struct UISceneElement*
uiscene_element_at(
    struct UIScene* uiscene,
    int element_id);

/** First active element whose `name` matches; sprite at atlas_index or NULL. */
struct DashSprite*
uiscene_sprite_by_name(
    struct UIScene* uiscene,
    const char* name,
    int atlas_index);

bool
uiscene_eventbuffer_is_empty(struct UIScene* uiscene);

bool
uiscene_eventbuffer_pop(
    struct UIScene* uiscene,
    struct UISceneEvent* out_event);

void
uiscene_eventbuffer_clear(struct UIScene* uiscene);

/** Register a font under `name`. Returns dense id in [0, font_count), or -1 on error.
 * Duplicate name updates the pointer and returns the existing id (no second FONT_ADDED event). */
int
uiscene_font_add(
    struct UIScene* uiscene,
    const char* name,
    struct DashPixFont* font);

struct DashPixFont*
uiscene_font_get(struct UIScene* uiscene, int font_id);

/** Returns font id or -1 if not found. */
int
uiscene_font_find_id(struct UIScene* uiscene, const char* name);

#endif
