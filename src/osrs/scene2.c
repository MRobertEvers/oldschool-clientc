#include "scene2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Scene2*
scene2_new(int size)
{
    struct Scene2* scene2 = malloc(sizeof(struct Scene2));
    memset(scene2, 0, sizeof(struct Scene2));

    scene2->elements = (struct Scene2Element*)malloc(size * sizeof(struct Scene2Element));
    memset(scene2->elements, 0, size * sizeof(struct Scene2Element));
    scene2->elements_count = size;

    for( int i = 0; i < size; i++ )
    {
        if( i > 0 )
            scene2->elements[i].prev = &scene2->elements[i - 1];
        if( i < size - 1 )
            scene2->elements[i].next = &scene2->elements[i + 1];

        scene2->elements[i].id = i;
        scene2->elements[i].parent_entity_id = -1;
        scene2->elements[i].active = false;
    }

    scene2->active_list = NULL;
    scene2->free_list = &scene2->elements[0];

    scene2->active_len = 0;
    scene2->free_len = size;

    return scene2;
}

void
scene2_free(struct Scene2* scene2)
{
    // Free the elements
    for( int i = 0; i < scene2->elements_count; i++ )
    {
        scene2_element_release(scene2, i);
    }
    free(scene2->elements);
    free(scene2);
}

int
scene2_element_acquire(
    struct Scene2* scene2,
    int parent_entity_id)
{
    if( scene2->free_list == NULL )
    {
        assert(false && "No free elements");
    }

    int element_id = scene2->free_list->id;
    struct Scene2Element* element = scene2->free_list;
    element->active = true;
    element->parent_entity_id = parent_entity_id;
    scene2->free_list = element->next;

    element->next = scene2->active_list;
    element->prev = NULL;
    if( scene2->active_list != NULL )
        scene2->active_list->prev = element;

    scene2->active_list = element;

    scene2->active_len++;
    scene2->free_len--;

    return element_id;
}

void
scene2_element_clear_animation(struct Scene2Element* element)
{
    if( !element->dash_frames )
        return;

    for( int i = 0; i < element->dash_frame_count; i++ )
    {
        dashframe_free(element->dash_frames[i]);
    }
    free(element->dash_frames);
    free(element->dash_frame_lengths);

    element->dash_frame_count = 0;
    element->dash_frame_capacity = 0;
    element->dash_frames = NULL;
    element->dash_frame_lengths = NULL;
}

void
scene2_element_clear_secondary_animation(struct Scene2Element* element)
{
    if( !element->dash_frames_secondary )
        return;

    for( int i = 0; i < element->dash_frame_count_secondary; i++ )
    {
        dashframe_free(element->dash_frames_secondary[i]);
    }
    free(element->dash_frames_secondary);
    free(element->dash_frame_lengths_secondary);

    element->dash_frame_count_secondary = 0;
    element->dash_frame_capacity_secondary = 0;
    element->dash_frames_secondary = NULL;
    element->dash_frame_lengths_secondary = NULL;
}

void
scene2_element_clear_framemap(struct Scene2Element* element)
{
    if( element->dash_framemap )
        dashframemap_free(element->dash_framemap);
    element->dash_framemap = NULL;
}

void
scene2_element_set_dash_model(
    struct Scene2Element* element,
    struct DashModel* dash_model)
{
    if( element->dash_model )
        dashmodel_free(element->dash_model);
    element->dash_model = dash_model;
}

void
scene2_element_push_animation_frame(
    struct Scene2Element* element,
    struct DashFrame* dash_frame,
    int length)
{
    if( element->dash_frame_count >= element->dash_frame_capacity )
    {
        if( element->dash_frame_capacity == 0 )
            element->dash_frame_capacity = 8;
        element->dash_frame_capacity *= 2;
        element->dash_frames =
            realloc(element->dash_frames, element->dash_frame_capacity * sizeof(struct DashFrame*));
        element->dash_frame_lengths =
            realloc(element->dash_frame_lengths, element->dash_frame_capacity * sizeof(int));
    }
    element->dash_frames[element->dash_frame_count] = dash_frame;
    element->dash_frame_lengths[element->dash_frame_count] = length;
    element->dash_frame_count++;
}

void
scene2_element_push_secondary_animation_frame(
    struct Scene2Element* element,
    struct DashFrame* dash_frame,
    int length)
{
    if( element->dash_frame_count_secondary >= element->dash_frame_capacity_secondary )
    {
        if( element->dash_frame_capacity_secondary == 0 )
            element->dash_frame_capacity_secondary = 8;
        element->dash_frame_capacity_secondary *= 2;
        element->dash_frames_secondary = realloc(
            element->dash_frames_secondary,
            element->dash_frame_capacity_secondary * sizeof(struct DashFrame*));
        element->dash_frame_lengths_secondary = realloc(
            element->dash_frame_lengths_secondary,
            element->dash_frame_capacity_secondary * sizeof(int));
    }

    element->dash_frames_secondary[element->dash_frame_count_secondary] = dash_frame;
    element->dash_frame_lengths_secondary[element->dash_frame_count_secondary] = length;
    element->dash_frame_count_secondary++;
}

void
scene2_element_set_framemap(
    struct Scene2Element* element,
    struct DashFramemap* dash_framemap)
{
    if( element->dash_framemap )
        dashframemap_free(element->dash_framemap);
    element->dash_framemap = dash_framemap;
}

void
scene2_element_release(
    struct Scene2* scene2,
    int element_id)
{
    if( element_id == -1 )
        return;

    struct Scene2Element* element = scene2_element_at(scene2, element_id);
    assert(element->active && "Element must be active");

    dashmodel_free(element->dash_model);
    element->dash_model = NULL;
    dashposition_free(element->dash_position);
    element->dash_position = NULL;
    dashframemap_free(element->dash_framemap);
    element->dash_framemap = NULL;
    scene2_element_clear_animation(element);
    scene2_element_clear_secondary_animation(element);

    element->active = false;
    element->parent_entity_id = -1;

    if( element->next != NULL )
        element->next->prev = element->prev;
    if( element->prev != NULL )
        element->prev->next = element->next;

    element->next = scene2->free_list;
    element->prev = NULL;
    if( scene2->free_list != NULL )
        scene2->free_list->prev = element;

    scene2->free_list = element;

    scene2->active_len--;
    scene2->free_len++;
}

struct Scene2Element*
scene2_element_at(
    struct Scene2* scene2,
    int element_id)
{
    assert(element_id >= 0 && element_id < scene2->elements_count && "Element id out of bounds");
    return &scene2->elements[element_id];
}