#ifndef SCENE2__H
#define SCENE2__H

#include "graphics/dash.h"

struct Scene2Element
{
    int id;
    struct Scene2Element* next;

    struct DashModel* dash_model;
    struct DashPosition* dash_position;
    struct DashAnimation* dash_animation;
};

struct Scene2
{
    // Intrusive list. Elements point to their next element.
    struct Scene2Element* element_list;
    int element_count;
};

struct Scene2*
scene2_new(int size);

void
scene2_free(struct Scene2* scene2);

#endif