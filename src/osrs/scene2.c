#include "scene2.h"

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

    return scene2;
}

void
scene2_free(struct Scene2* scene2)
{
    // Free the elements
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

    return element_id;
}

void
scene2_element_release(
    struct Scene2* scene2,
    int element_id)
{
    struct Scene2Element* element = scene2_element_at(scene2, element_id);
    assert(element->active && "Element must be active");
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
}

struct Scene2Element*
scene2_element_at(
    struct Scene2* scene2,
    int element_id)
{
    assert(element_id >= 0 && element_id < scene2->elements_count && "Element id out of bounds");
    return &scene2->elements[element_id];
}