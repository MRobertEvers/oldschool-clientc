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
};

struct UISceneEvent
{
    enum UISceneEventType type;
    int element_id;
    int parent_entity_id;
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

bool
uiscene_eventbuffer_is_empty(struct UIScene* uiscene);

bool
uiscene_eventbuffer_pop(
    struct UIScene* uiscene,
    struct UISceneEvent* out_event);

void
uiscene_eventbuffer_clear(struct UIScene* uiscene);

#endif
