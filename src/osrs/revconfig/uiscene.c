#include "uiscene.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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

    return uiscene;
}

void
uiscene_free(struct UIScene* uiscene)
{
    if( !uiscene )
        return;
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

    return element->id;
}

void
uiscene_element_release(
    struct UIScene* uiscene,
    int element_id)
{
    struct UISceneElement* element = &uiscene->elements[element_id];
    assert(element->active && "Element must be active");
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
}

struct UISceneElement*
uiscene_element_at(
    struct UIScene* uiscene,
    int element_id)
{
    assert(element_id >= 0 && element_id < uiscene->elements_count && "Element id out of bounds");
    return &uiscene->elements[element_id];
}