#include "scene2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE2_EVENTBUFFER_DEFAULT_CAPACITY 512

static void
scene2_eventbuffer_push(
    struct Scene2* scene2,
    struct Scene2Event event)
{
    if( !scene2 || !scene2->eventbuffer || scene2->eventbuffer_capacity <= 0 )
        return;

    if( scene2->eventbuffer_count == scene2->eventbuffer_capacity )
    {
        scene2->eventbuffer_head = (scene2->eventbuffer_head + 1) % scene2->eventbuffer_capacity;
        scene2->eventbuffer_count--;
    }

    scene2->eventbuffer[scene2->eventbuffer_tail] = event;
    scene2->eventbuffer_tail = (scene2->eventbuffer_tail + 1) % scene2->eventbuffer_capacity;
    scene2->eventbuffer_count++;
}

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

        scene2->elements[i].parent_entity_id = -1;
        scene2->elements[i].active = false;
    }

    scene2->active_list = NULL;
    scene2->free_list = &scene2->elements[0];

    scene2->active_len = 0;
    scene2->free_len = size;
    scene2->eventbuffer_capacity = SCENE2_EVENTBUFFER_DEFAULT_CAPACITY;
    scene2->eventbuffer = calloc((size_t)scene2->eventbuffer_capacity, sizeof(struct Scene2Event));
    scene2->eventbuffer_head = 0;
    scene2->eventbuffer_tail = 0;
    scene2->eventbuffer_count = 0;

    return scene2;
}

void
scene2_free(struct Scene2* scene2)
{
    // Free the elements
    for( int i = 0; i < scene2->elements_count; i++ )
    {
        if( scene2->elements[i].active )
            scene2_element_release(scene2, i);
    }
    free(scene2->elements);
    free(scene2->textures);
    free(scene2->eventbuffer);
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

    int element_id = (int)(scene2->free_list - scene2->elements);
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
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_ELEMENT_ACQUIRED,
            .element_id = element_id,
            .parent_entity_id = parent_entity_id,
        });

    return element_id;
}

static void
scene2_element_clear_frames(struct Scene2Frames* frames)
{
    if( !frames )
        return;

    for( int i = 0; i < frames->count; i++ )
    {
        dashframe_free(frames->frames[i]);
    }
    free(frames->frames);
    free(frames->lengths);
    frames->count = 0;
    frames->capacity = 0;
    frames->frames = NULL;
    frames->lengths = NULL;
}

void
scene2_element_clear_animation(struct Scene2Element* element)
{
    scene2_element_clear_frames(&element->primary_frames);
}

void
scene2_element_clear_secondary_animation(struct Scene2Element* element)
{
    scene2_element_clear_frames(&element->secondary_frames);
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
    struct Scene2* scene2,
    struct Scene2Element* element,
    struct DashModel* dash_model)
{
    assert(element && "scene2_element_set_dash_model element is NULL");
    bool model_changed = element->dash_model != dash_model;
    if( element->dash_model )
        dashmodel_free(element->dash_model);
    element->dash_model = dash_model;
    if( model_changed )
    {
        scene2_eventbuffer_push(
            scene2,
            (struct Scene2Event){
                .type = SCENE2_EVENT_MODEL_CHANGED,
                .element_id = (int)(element - scene2->elements),
                .parent_entity_id = element->parent_entity_id,
            });
    }
}

static void
scene2_element_push_frame(
    struct Scene2Frames* frames,
    struct DashFrame* dash_frame,
    int length)
{
    if( frames->count >= frames->capacity )
    {
        if( frames->capacity == 0 )
            frames->capacity = 8;
        frames->capacity *= 2;
        frames->frames = realloc(frames->frames, frames->capacity * sizeof(struct DashFrame*));
        frames->lengths = realloc(frames->lengths, frames->capacity * sizeof(int));
    }

    frames->frames[frames->count] = dash_frame;
    frames->lengths[frames->count] = length;
    frames->count++;
}

void
scene2_element_push_animation_frame(
    struct Scene2Element* element,
    struct DashFrame* dash_frame,
    int length)
{
    scene2_element_push_frame(&element->primary_frames, dash_frame, length);
}

void
scene2_element_push_secondary_animation_frame(
    struct Scene2Element* element,
    struct DashFrame* dash_frame,
    int length)
{
    scene2_element_push_frame(&element->secondary_frames, dash_frame, length);
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
    int parent_entity_id = element->parent_entity_id;

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
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_ELEMENT_RELEASED,
            .element_id = element_id,
            .parent_entity_id = parent_entity_id,
        });
}

struct Scene2Element*
scene2_element_at(
    struct Scene2* scene2,
    int element_id)
{
    assert(element_id >= 0 && element_id < scene2->elements_count && "Element id out of bounds");
    return &scene2->elements[element_id];
}

bool
scene2_eventbuffer_is_empty(struct Scene2* scene2)
{
    if( !scene2 )
        return true;
    return scene2->eventbuffer_count <= 0;
}

bool
scene2_eventbuffer_pop(
    struct Scene2* scene2,
    struct Scene2Event* out_event)
{
    if( !scene2 || !out_event || scene2->eventbuffer_count <= 0 )
        return false;
    *out_event = scene2->eventbuffer[scene2->eventbuffer_head];
    scene2->eventbuffer_head = (scene2->eventbuffer_head + 1) % scene2->eventbuffer_capacity;
    scene2->eventbuffer_count--;
    return true;
}

void
scene2_eventbuffer_clear(struct Scene2* scene2)
{
    if( !scene2 )
        return;
    scene2->eventbuffer_head = 0;
    scene2->eventbuffer_tail = 0;
    scene2->eventbuffer_count = 0;
}

void
scene2_texture_add(
    struct Scene2* scene2,
    int texture_id,
    struct DashTexture* texture)
{
    if( !scene2 )
        return;

    if( scene2->textures_count >= scene2->textures_capacity )
    {
        int new_capacity = scene2->textures_capacity == 0 ? 8 : scene2->textures_capacity * 2;
        scene2->textures =
            realloc(scene2->textures, (size_t)new_capacity * sizeof(struct Scene2TextureEntry));
        scene2->textures_capacity = new_capacity;
    }

    scene2->textures[scene2->textures_count].id = texture_id;
    scene2->textures[scene2->textures_count].texture = texture;
    scene2->textures_count++;

    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_TEXTURE_LOADED,
            .texture_id = texture_id,
        });
}

struct DashTexture*
scene2_texture_get(
    struct Scene2* scene2,
    int texture_id)
{
    if( !scene2 )
        return NULL;
    for( int i = 0; i < scene2->textures_count; i++ )
    {
        if( scene2->textures[i].id == texture_id )
            return scene2->textures[i].texture;
    }
    return NULL;
}
