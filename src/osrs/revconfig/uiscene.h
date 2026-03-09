#ifndef UISCENE_H
#define UISCENE_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

struct UISceneElement
{
    int id;
    bool active;
    int parent_entity_id;
    struct UISceneElement* next;
    struct UISceneElement* prev;

    char name[64];
    struct DashSprite* dash_sprite;
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

#endif
