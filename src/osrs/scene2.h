#ifndef SCENE2__H
#define SCENE2__H

#include "graphics/dash.h"

struct Scene2Element
{
    int id;
    bool active;
    int parent_entity_id;
    struct Scene2Element* next;
    struct Scene2Element* prev;

    struct DashModel* dash_model;
    struct DashPosition* dash_position;
    struct DashAnimation* dash_animation;
};

struct Scene2
{
    // Intrusive list. Elements point to their next element.
    struct Scene2Element* elements;
    int elements_count;

    struct Scene2Element* active_list;
    struct Scene2Element* free_list;
};

struct Scene2*
scene2_new(int size);

void
scene2_free(struct Scene2* scene2);

int
scene2_element_acquire(
    struct Scene2* scene2,
    int parent_entity_id);

void
scene2_element_release(
    struct Scene2* scene2,
    int element_id);

struct Scene2Element*
scene2_element_at(
    struct Scene2* scene2,
    int element_id);

#endif