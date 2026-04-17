#include "uiscene.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define UISCENE_EVENTBUFFER_DEFAULT_CAPACITY 4096

static void
uiscene_event_discard_payload(struct UISceneEvent* ev)
{
    if( !ev || ev->type != UISCENE_EVENT_ELEMENT_RELEASED )
        return;
    if( !ev->released_sprites || ev->released_sprites_count <= 0 )
    {
        free(ev->released_sprites);
        ev->released_sprites = NULL;
        ev->released_sprites_count = 0;
        return;
    }
    if( !ev->released_sprites_borrowed )
    {
        for( int i = 0; i < ev->released_sprites_count; i++ )
        {
            if( ev->released_sprites[i] )
                dashsprite_free(ev->released_sprites[i]);
        }
    }
    free(ev->released_sprites);
    ev->released_sprites = NULL;
    ev->released_sprites_count = 0;
}

static void
uiscene_eventbuffer_push(
    struct UIScene* uiscene,
    struct UISceneEvent event)
{
    if( !uiscene || !uiscene->eventbuffer || uiscene->eventbuffer_capacity <= 0 )
        return;

    if( uiscene->eventbuffer_count == uiscene->eventbuffer_capacity )
    {
        int old_cap = uiscene->eventbuffer_capacity;
        int new_cap = old_cap > 0 ? old_cap * 2 : 16;
        struct UISceneEvent* nb = calloc((size_t)new_cap, sizeof(struct UISceneEvent));
        if( !nb )
            return;
        for( int i = 0; i < uiscene->eventbuffer_count; i++ )
        {
            nb[i] = uiscene->eventbuffer[(uiscene->eventbuffer_head + i) % old_cap];
        }
        free(uiscene->eventbuffer);
        uiscene->eventbuffer = nb;
        uiscene->eventbuffer_capacity = new_cap;
        uiscene->eventbuffer_head = 0;
        uiscene->eventbuffer_tail = uiscene->eventbuffer_count;
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
    memset(uiscene->elements, 0, (size_t)size * sizeof(struct UISceneElement));

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
    uiscene->eventbuffer =
        calloc((size_t)uiscene->eventbuffer_capacity, sizeof(struct UISceneEvent));
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
    for( int i = 0; i < uiscene->eventbuffer_count; i++ )
    {
        struct UISceneEvent* ev =
            &uiscene->eventbuffer[(uiscene->eventbuffer_head + i) % uiscene->eventbuffer_capacity];
        uiscene_event_discard_payload(ev);
    }
    for( int i = 0; i < uiscene->elements_count; i++ )
    {
        struct UISceneElement* elem = &uiscene->elements[i];
        if( elem->dash_sprites )
        {
            if( !elem->dash_sprites_borrowed )
            {
                for( int j = 0; j < elem->dash_sprites_count; j++ )
                    dashsprite_free(elem->dash_sprites[j]);
            }
            free(elem->dash_sprites);
        }
    }
    for( int i = 0; i < uiscene->font_count; i++ )
    {
        if( uiscene->fonts[i].font )
            dashpixfont_free(uiscene->fonts[i].font);
    }
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

    bool borrowed = element->dash_sprites_borrowed;
    int n = element->dash_sprites_count;
    struct DashSprite** snap = NULL;
    if( n > 0 && element->dash_sprites )
    {
        snap = malloc((size_t)n * sizeof(struct DashSprite*));
        if( snap )
            memcpy(snap, element->dash_sprites, (size_t)n * sizeof(struct DashSprite*));
        else if( !borrowed )
        {
            for( int i = 0; i < n; i++ )
            {
                if( element->dash_sprites[i] )
                    dashsprite_free(element->dash_sprites[i]);
            }
        }
        free(element->dash_sprites);
    }
    else
        free(element->dash_sprites);

    element->dash_sprites = NULL;
    element->dash_sprites_count = 0;
    element->dash_sprites_borrowed = false;

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
            .released_sprites = snap,
            .released_sprites_count = snap ? n : 0,
            .released_sprites_borrowed = borrowed,
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

struct DashSprite*
uiscene_sprite_by_name(
    struct UIScene* uiscene,
    const char* name,
    int atlas_index)
{
    if( !uiscene || !name )
        return NULL;
    for( int i = 0; i < uiscene->elements_count; i++ )
    {
        struct UISceneElement* e = &uiscene->elements[i];
        if( !e->active )
            continue;
        if( strcmp(e->name, name) != 0 )
            continue;
        if( !e->dash_sprites || atlas_index < 0 || atlas_index >= e->dash_sprites_count )
            return NULL;
        return e->dash_sprites[atlas_index];
    }
    return NULL;
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
    for( int i = 0; i < uiscene->eventbuffer_count; i++ )
    {
        struct UISceneEvent* ev =
            &uiscene->eventbuffer[(uiscene->eventbuffer_head + i) % uiscene->eventbuffer_capacity];
        uiscene_event_discard_payload(ev);
    }
    uiscene->eventbuffer_head = 0;
    uiscene->eventbuffer_tail = 0;
    uiscene->eventbuffer_count = 0;
}

int
uiscene_font_add(
    struct UIScene* uiscene,
    const char* name,
    struct DashPixFont* font)
{
    if( !uiscene || !name || !name[0] || !font )
        return -1;

    for( int i = 0; i < uiscene->font_count; i++ )
    {
        if( strcmp(uiscene->fonts[i].name, name) == 0 )
        {
            if( uiscene->fonts[i].font && uiscene->fonts[i].font != font )
                dashpixfont_free(uiscene->fonts[i].font);
            uiscene->fonts[i].font = font;
            return i;
        }
    }

    if( uiscene->font_count >= UISCENE_FONT_MAX )
        return -1;

    int id = uiscene->font_count++;
    strncpy(uiscene->fonts[id].name, name, UISCENE_FONT_NAME_MAX - 1);
    uiscene->fonts[id].name[UISCENE_FONT_NAME_MAX - 1] = '\0';
    uiscene->fonts[id].font = font;

    uiscene_eventbuffer_push(
        uiscene,
        (struct UISceneEvent){
            .type = UISCENE_EVENT_FONT_ADDED,
            .font_id = id,
        });
    return id;
}

struct DashPixFont*
uiscene_font_get(
    struct UIScene* uiscene,
    int font_id)
{
    if( !uiscene || font_id < 0 || font_id >= uiscene->font_count )
        return NULL;
    return uiscene->fonts[font_id].font;
}

int
uiscene_font_find_id(
    struct UIScene* uiscene,
    const char* name)
{
    if( !uiscene || !name || !name[0] )
        return -1;
    for( int i = 0; i < uiscene->font_count; i++ )
    {
        if( strcmp(uiscene->fonts[i].name, name) == 0 )
            return i;
    }
    return -1;
}