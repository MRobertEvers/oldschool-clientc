#include "uiscene.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define UISCENE_EVENTBUFFER_DEFAULT_CAPACITY 4096

static void
uiscene_eventbuffer_push(
    struct UIScene* uiscene,
    struct UISceneEvent event)
{
    if( !uiscene || !uiscene->eventbuffer || uiscene->eventbuffer_capacity <= 0 )
        return;

    if( uiscene->eventbuffer_count == uiscene->eventbuffer_capacity )
    {
        uiscene->eventbuffer_head = (uiscene->eventbuffer_head + 1) % uiscene->eventbuffer_capacity;
        uiscene->eventbuffer_count--;
    }

    uiscene->eventbuffer[uiscene->eventbuffer_tail] = event;
    uiscene->eventbuffer_tail = (uiscene->eventbuffer_tail + 1) % uiscene->eventbuffer_capacity;
    uiscene->eventbuffer_count++;
}

struct UIScene*
uiscene_new(int size)
{
    struct UIScene* uiscene = malloc(sizeof(struct UIScene));
    if( !uiscene )
        return NULL;
    memset(uiscene, 0, sizeof(struct UIScene));
    uiscene->elements = malloc(size * sizeof(struct UISceneElement));
    if( !uiscene->elements )
    {
        free(uiscene);
        return NULL;
    }

    for( int i = 0; i < size; i++ )
    {
        uiscene->elements[i].id = i;
        uiscene->elements[i].active = false;
        uiscene->elements[i].parent_entity_id = -1;

        if( i < size - 1 )
            uiscene->elements[i].next = &uiscene->elements[i + 1];

        if( i > 0 )
            uiscene->elements[i].prev = &uiscene->elements[i - 1];
    }
    uiscene->elements_count = size;

    uiscene->active_list = NULL;
    uiscene->free_list = &uiscene->elements[0];

    uiscene->active_len = 0;
    uiscene->free_len = size;

    uiscene->eventbuffer_capacity = UISCENE_EVENTBUFFER_DEFAULT_CAPACITY;
    uiscene->eventbuffer = calloc((size_t)uiscene->eventbuffer_capacity, sizeof(struct UISceneEvent));
    if( !uiscene->eventbuffer )
    {
        free(uiscene->elements);
        free(uiscene);
        return NULL;
    }
    uiscene->eventbuffer_head = 0;
    uiscene->eventbuffer_tail = 0;
    uiscene->eventbuffer_count = 0;

    return uiscene;
}

void
uiscene_free(struct UIScene* uiscene)
{
    if( !uiscene )
        return;
    free(uiscene->eventbuffer);
    free(uiscene->elements);
    free(uiscene);
}

int
uiscene_element_acquire(
    struct UIScene* uiscene,
    int parent_entity_id)
{
    if( uiscene->free_len == 0 )
        return -1;

    struct UISceneElement* element = uiscene->free_list;
    element->active = true;
    element->parent_entity_id = parent_entity_id;

    uiscene->free_list = element->next;

    element->next = uiscene->active_list;
    element->prev = NULL;
    if( uiscene->active_list != NULL )
        uiscene->active_list->prev = element;

    uiscene->active_list = element;

    uiscene->active_len++;
    uiscene->free_len--;

    uiscene_eventbuffer_push(
        uiscene,
        (struct UISceneEvent){
            .type = UISCENE_EVENT_ELEMENT_ACQUIRED,
            .element_id = element->id,
            .parent_entity_id = parent_entity_id,
        });

    return element->id;
}

void
uiscene_element_release(
    struct UIScene* uiscene,
    int element_id)
{
    if( element_id == -1 )
        return;

    struct UISceneElement* element = uiscene_element_at(uiscene, element_id);
    assert(element->active && "Element must be active");
    int parent_entity_id = element->parent_entity_id;

    element->active = false;
    element->parent_entity_id = -1;

    if( element->next != NULL )
        element->next->prev = element->prev;
    if( element->prev != NULL )
        element->prev->next = element->next;

    element->next = uiscene->free_list;
    element->prev = NULL;
    if( uiscene->free_list != NULL )
        uiscene->free_list->prev = element;

    uiscene->free_list = element;

    uiscene->active_len--;
    uiscene->free_len++;

    uiscene_eventbuffer_push(
        uiscene,
        (struct UISceneEvent){
            .type = UISCENE_EVENT_ELEMENT_RELEASED,
            .element_id = element_id,
            .parent_entity_id = parent_entity_id,
        });
}

struct UISceneElement*
uiscene_element_at(
    struct UIScene* uiscene,
    int element_id)
{
    assert(element_id >= 0 && element_id < uiscene->elements_count && "Element id out of bounds");
    return &uiscene->elements[element_id];
}

bool
uiscene_eventbuffer_is_empty(struct UIScene* uiscene)
{
    if( !uiscene )
        return true;
    return uiscene->eventbuffer_count <= 0;
}

bool
uiscene_eventbuffer_pop(
    struct UIScene* uiscene,
    struct UISceneEvent* out_event)
{
    if( !uiscene || !out_event || uiscene->eventbuffer_count <= 0 )
        return false;
    *out_event = uiscene->eventbuffer[uiscene->eventbuffer_head];
    uiscene->eventbuffer_head = (uiscene->eventbuffer_head + 1) % uiscene->eventbuffer_capacity;
    uiscene->eventbuffer_count--;
    return true;
}

void
uiscene_eventbuffer_clear(struct UIScene* uiscene)
{
    if( !uiscene )
        return;
    uiscene->eventbuffer_head = 0;
    uiscene->eventbuffer_tail = 0;
    uiscene->eventbuffer_count = 0;
}