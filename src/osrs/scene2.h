#ifndef SCENE2__H
#define SCENE2__H

#include "graphics/dash.h"

struct Scene2Frames
{
    struct DashFrame** frames;
    int* lengths;
    int count;
    int capacity;
};

struct Scene2Element
{
    int id;
    bool active;
    int parent_entity_id;
    struct Scene2Element* next;
    struct Scene2Element* prev;

    struct DashModel* dash_model;
    struct DashPosition* dash_position;

    struct DashFramemap* dash_framemap;

    struct Scene2Frames primary_frames;
    struct Scene2Frames secondary_frames;
};

struct Scene2
{
    // Intrusive list. Elements point to their next element.
    struct Scene2Element* elements;
    int elements_count;

    struct Scene2Element* active_list;
    int active_len;
    struct Scene2Element* free_list;
    int free_len;
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

void
scene2_element_clear_animation(struct Scene2Element* element);

void
scene2_element_clear_secondary_animation(struct Scene2Element* element);

void
scene2_element_clear_framemap(struct Scene2Element* element);

void
scene2_element_set_dash_model(
    struct Scene2Element* element,
    struct DashModel* dash_model);
void
scene2_element_push_animation_frame(
    struct Scene2Element* element,
    struct DashFrame* dash_frame,
    int length);

void
scene2_element_push_secondary_animation_frame(
    struct Scene2Element* element,
    struct DashFrame* dash_frame,
    int length);

void
scene2_element_set_framemap(
    struct Scene2Element* element,
    struct DashFramemap* dash_framemap);

struct Scene2Element*
scene2_element_at(
    struct Scene2* scene2,
    int element_id);

#endif