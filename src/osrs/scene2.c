#include "scene2.h"

#include "osrs/game_entity.h"
#include "osrs/scene2_element_internal.h"
#include "osrs/texture.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE2_EVENTBUFFER_DEFAULT_CAPACITY 4096

/* prev/next live at the same offsets on Scene2ElementFast and Scene2ElementFull; use the
 * correct concrete type so we do not violate strict aliasing when reading/writing links. */
static struct Scene2Element*
scene2_el_next(struct Scene2Element* e)
{
    if( scene2__flags_raw(e) & SCENE2_FLAG_FAST )
        return ((struct Scene2ElementFast*)e)->next;
    return ((struct Scene2ElementFull*)e)->next;
}

static struct Scene2Element*
scene2_el_prev(struct Scene2Element* e)
{
    if( scene2__flags_raw(e) & SCENE2_FLAG_FAST )
        return ((struct Scene2ElementFast*)e)->prev;
    return ((struct Scene2ElementFull*)e)->prev;
}

static void
scene2_el_set_next(
    struct Scene2Element* e,
    struct Scene2Element* n)
{
    if( scene2__flags_raw(e) & SCENE2_FLAG_FAST )
        ((struct Scene2ElementFast*)e)->next = n;
    else
        ((struct Scene2ElementFull*)e)->next = n;
}

static void
scene2_el_set_prev(
    struct Scene2Element* e,
    struct Scene2Element* p)
{
    if( scene2__flags_raw(e) & SCENE2_FLAG_FAST )
        ((struct Scene2ElementFast*)e)->prev = p;
    else
        ((struct Scene2ElementFull*)e)->prev = p;
}

static uint32_t
scene2_pack_parent(int parent_entity_id)
{
    if( parent_entity_id < 0 )
        return SCENE2_PARENT_NONE;
    uint32_t uid = (uint32_t)parent_entity_id;
    enum EntityKind k = entity_kind_from_uid(uid);
    int eid = entity_id_from_uid(uid);
    assert(eid >= 0 && (unsigned)eid < (1u << 20));
    return ((uint32_t)k << 20) | ((uint32_t)eid & 0xfffffu);
}

static int
scene2_unpack_parent(uint32_t flags_entity)
{
    uint32_t p = flags_entity & SCENE2_PARENT_MASK;
    if( p == SCENE2_PARENT_NONE )
        return -1;
    enum EntityKind k = (enum EntityKind)((p >> 20) & 0xfu);
    int eid = (int)(p & 0xfffffu);
    return (int)entity_unified_id(k, eid);
}

int
scene2_element_id(
    struct Scene2* scene2,
    const struct Scene2Element* el)
{
    assert(scene2 && el);
    if( scene2__flags_raw(el) & SCENE2_FLAG_FAST )
    {
        ptrdiff_t d = (const struct Scene2ElementFast*)el - scene2->fast_pool;
        assert(d >= 0 && d < scene2->fast_count && "Scene2Element pointer not from fast pool");
        return (int)d;
    }
    ptrdiff_t d2 = (const struct Scene2ElementFull*)el - scene2->full_pool;
    assert(d2 >= 0 && d2 < scene2->full_count && "Scene2Element pointer not from full pool");
    return scene2->fast_count + (int)d2;
}

static int
scene2_element_id_from_ptr(
    struct Scene2* scene2,
    struct Scene2Element* el)
{
    return scene2_element_id(scene2, el);
}

static void
scene2_eventbuffer_push(
    struct Scene2* scene2,
    struct Scene2Event event)
{
    if( !scene2 || !scene2->eventbuffer || scene2->eventbuffer_capacity <= 0 )
        return;

    if( scene2->eventbuffer_count == scene2->eventbuffer_capacity )
    {
        int old_cap = scene2->eventbuffer_capacity;
        int new_cap = old_cap > 0 ? old_cap * 2 : 16;
        struct Scene2Event* nb = calloc((size_t)new_cap, sizeof(struct Scene2Event));
        if( !nb )
            return;
        for( int i = 0; i < scene2->eventbuffer_count; i++ )
        {
            nb[i] = scene2->eventbuffer[(scene2->eventbuffer_head + i) % old_cap];
        }
        free(scene2->eventbuffer);
        scene2->eventbuffer = nb;
        scene2->eventbuffer_capacity = new_cap;
        scene2->eventbuffer_head = 0;
        scene2->eventbuffer_tail = scene2->eventbuffer_count;
    }

    scene2->eventbuffer[scene2->eventbuffer_tail] = event;
    scene2->eventbuffer_tail = (scene2->eventbuffer_tail + 1) % scene2->eventbuffer_capacity;
    scene2->eventbuffer_count++;
}

int
scene2_elements_total(const struct Scene2* scene2)
{
    if( !scene2 )
        return 0;
    return scene2->fast_count + scene2->full_count;
}

struct Scene2*
scene2_new(
    int fast_count,
    int full_count)
{
    struct Scene2* scene2 = malloc(sizeof(struct Scene2));
    memset(scene2, 0, sizeof(struct Scene2));

    scene2->fast_count = fast_count;
    scene2->full_count = full_count;

    if( fast_count > 0 )
    {
        scene2->fast_pool = (struct Scene2ElementFast*)malloc(
            (size_t)fast_count * sizeof(struct Scene2ElementFast));
        memset(scene2->fast_pool, 0, (size_t)fast_count * sizeof(struct Scene2ElementFast));
        for( int i = 0; i < fast_count; i++ )
        {
            struct Scene2ElementFast* f = &scene2->fast_pool[i];
            f->flags_entity = SCENE2_FLAG_VALID | SCENE2_FLAG_FAST | SCENE2_PARENT_NONE;
            if( i > 0 )
                f->prev = (struct Scene2Element*)&scene2->fast_pool[i - 1];
            if( i < fast_count - 1 )
                f->next = (struct Scene2Element*)&scene2->fast_pool[i + 1];
        }
        scene2->fast_free_list = (struct Scene2Element*)&scene2->fast_pool[0];
    }

    if( full_count > 0 )
    {
        scene2->full_pool = (struct Scene2ElementFull*)malloc(
            (size_t)full_count * sizeof(struct Scene2ElementFull));
        memset(scene2->full_pool, 0, (size_t)full_count * sizeof(struct Scene2ElementFull));
        for( int i = 0; i < full_count; i++ )
        {
            struct Scene2ElementFull* u = &scene2->full_pool[i];
            u->flags_entity = SCENE2_FLAG_VALID | SCENE2_PARENT_NONE;
            if( i > 0 )
                u->prev = (struct Scene2Element*)&scene2->full_pool[i - 1];
            if( i < full_count - 1 )
                u->next = (struct Scene2Element*)&scene2->full_pool[i + 1];
        }
        scene2->full_free_list = (struct Scene2Element*)&scene2->full_pool[0];
    }

    scene2->active_list = NULL;
    scene2->active_len = 0;
    scene2->fast_free_len = fast_count;
    scene2->full_free_len = full_count;

    scene2->eventbuffer_capacity = SCENE2_EVENTBUFFER_DEFAULT_CAPACITY;
    scene2->eventbuffer = calloc((size_t)scene2->eventbuffer_capacity, sizeof(struct Scene2Event));
    scene2->eventbuffer_head = 0;
    scene2->eventbuffer_tail = 0;
    scene2->eventbuffer_count = 0;

    scene2->next_vertex_array_gpu_id = 0;
    scene2->next_face_array_gpu_id = 0;
    scene2->next_model_gpu_id = 0;

    scene2->batch_active = false;
    scene2->batch_current_id = 0;

    return scene2;
}

void
scene2_free(struct Scene2* scene2)
{
    if( !scene2 )
        return;

    for( int i = 0; i < scene2->fast_count; i++ )
    {
        struct Scene2Element* e = (struct Scene2Element*)&scene2->fast_pool[i];
        if( scene2_element_is_active(e) )
            scene2_element_release(scene2, i);
    }
    for( int j = 0; j < scene2->full_count; j++ )
    {
        int id = scene2->fast_count + j;
        struct Scene2Element* e = (struct Scene2Element*)&scene2->full_pool[j];
        if( scene2_element_is_active(e) )
            scene2_element_release(scene2, id);
    }

    free(scene2->fast_pool);
    free(scene2->full_pool);
    for( int i = 0; i < scene2->textures_count; i++ )
        texture_free(scene2->textures[i].texture);
    free(scene2->textures);

    scene2_flush_deferred_array_frees(scene2);

    for( int i = 0; i < scene2->vertex_arrays_count; i++ )
        dashvertexarray_free(scene2->vertex_arrays[i]);
    free(scene2->vertex_arrays);

    for( int i = 0; i < scene2->face_arrays_count; i++ )
        dashfacearray_free(scene2->face_arrays[i]);
    free(scene2->face_arrays);

    free(scene2->vertex_arrays_deferred_free);
    free(scene2->face_arrays_deferred_free);
    free(scene2->models_deferred_free);
    free(scene2->eventbuffer);
    free(scene2);
}

static void
scene2_splice_out_active(
    struct Scene2* scene2,
    struct Scene2Element* element)
{
    struct Scene2Element* nx = scene2_el_next(element);
    struct Scene2Element* pr = scene2_el_prev(element);
    if( nx != NULL )
        scene2_el_set_prev(nx, pr);
    if( pr != NULL )
        scene2_el_set_next(pr, nx);
    else
        scene2->active_list = nx;
}

static void
scene2_push_active(
    struct Scene2* scene2,
    struct Scene2Element* element)
{
    scene2_el_set_next(element, scene2->active_list);
    scene2_el_set_prev(element, NULL);
    if( scene2->active_list != NULL )
        scene2_el_set_prev(scene2->active_list, element);
    scene2->active_list = element;
    scene2->active_len++;
}

int
scene2_element_acquire_fast(
    struct Scene2* scene2,
    int parent_entity_id)
{
    if( !scene2 || scene2->fast_free_list == NULL )
    {
        fprintf(
            stderr,
            "scene2_element_acquire_fast: no free fast elements (scene2=%p fast_free_len=%d)\n",
            (void*)scene2,
            scene2 ? scene2->fast_free_len : -1);
        abort();
    }

    struct Scene2Element* element = scene2->fast_free_list;
    scene2->fast_free_list = scene2_el_next(element);
    if( scene2->fast_free_list != NULL )
        scene2_el_set_prev(scene2->fast_free_list, NULL);

    struct Scene2ElementFast* f = scene2__as_fast(element);
    f->flags_entity = SCENE2_FLAG_VALID | SCENE2_FLAG_FAST | SCENE2_FLAG_ACTIVE |
                      scene2_pack_parent(parent_entity_id);

    scene2_push_active(scene2, element);

    scene2->fast_free_len--;
    int element_id = scene2_element_id_from_ptr(scene2, element);
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_ELEMENT_ACQUIRED,
            .batched = false,
            .u.element.element_id = element_id,
            .u.element.parent_entity_id = parent_entity_id,
        });

    return element_id;
}

int
scene2_element_acquire_full(
    struct Scene2* scene2,
    int parent_entity_id)
{
    if( !scene2 || scene2->full_free_list == NULL )
    {
        fprintf(
            stderr,
            "scene2_element_acquire_full: no free full elements (scene2=%p full_free_len=%d)\n",
            (void*)scene2,
            scene2 ? scene2->full_free_len : -1);
        abort();
    }

    struct Scene2Element* element = scene2->full_free_list;
    scene2->full_free_list = scene2_el_next(element);
    if( scene2->full_free_list != NULL )
        scene2_el_set_prev(scene2->full_free_list, NULL);

    struct Scene2ElementFull* u = scene2__as_full(element);
    u->flags_entity = SCENE2_FLAG_VALID | SCENE2_FLAG_ACTIVE | scene2_pack_parent(parent_entity_id);

    scene2_push_active(scene2, element);

    scene2->full_free_len--;
    int element_id = scene2_element_id_from_ptr(scene2, element);
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_ELEMENT_ACQUIRED,
            .batched = false,
            .u.element.element_id = element_id,
            .u.element.parent_entity_id = parent_entity_id,
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
    if( scene2__is_fast(element) )
        return;
    scene2_element_clear_frames(&scene2__as_full(element)->primary_frames);
}

void
scene2_element_clear_secondary_animation(struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return;
    scene2_element_clear_frames(&scene2__as_full(element)->secondary_frames);
}

void
scene2_element_clear_framemap(struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return;
    struct Scene2ElementFull* u = scene2__as_full(element);
    if( u->dash_framemap )
        dashframemap_free(u->dash_framemap);
    u->dash_framemap = NULL;
}

static void
scene2__defer_model_free(
    struct Scene2* scene2,
    struct DashModel* model)
{
    if( !scene2 || !model )
        return;
    if( scene2->models_deferred_free_count >= scene2->models_deferred_free_capacity )
    {
        int ncap = scene2->models_deferred_free_capacity == 0 ? 8
                                                           : scene2->models_deferred_free_capacity * 2;
        scene2->models_deferred_free = realloc(
            scene2->models_deferred_free,
            (size_t)ncap * sizeof(scene2->models_deferred_free[0]));
        scene2->models_deferred_free_capacity = ncap;
    }
    scene2->models_deferred_free[scene2->models_deferred_free_count++] = model;
}

void
scene2_element_set_dash_model(
    struct Scene2* scene2,
    struct Scene2Element* element,
    struct DashModel* dash_model)
{
    if( !element )
    {
        fprintf(stderr, "scene2_element_set_dash_model: NULL element\n");
        abort();
    }
    struct DashModel* old = NULL;
    if( scene2__is_fast(element) )
        old = scene2__as_fast(element)->dash_model;
    else
        old = scene2__as_full(element)->dash_model;
    if( old == dash_model )
        return;

    int element_id = scene2_element_id_from_ptr(scene2, element);
    int parent_entity_id = scene2_element_parent_entity_id(element);

    if( old && scene2 )
    {
        int old_model_id = 0;
        if( scene2__is_fast(element) )
            old_model_id = scene2__as_fast(element)->dash_model_gpu_id;
        else
            old_model_id = scene2__as_full(element)->dash_model_gpu_id;

        scene2_eventbuffer_push(
            scene2,
            (struct Scene2Event){
                .type = SCENE2_EVENT_MODEL_UNLOADED,
                .batched = false,
                .u.model = {
                    .element_id = element_id,
                    .parent_entity_id = parent_entity_id,
                    .model_id = old_model_id,
                    .model = old,
                },
            });
        scene2__defer_model_free(scene2, old);
    }

    if( scene2__is_fast(element) )
    {
        struct Scene2ElementFast* f = scene2__as_fast(element);
        f->dash_model = dash_model;
        f->dash_model_gpu_id = 0;
    }
    else
    {
        struct Scene2ElementFull* u = scene2__as_full(element);
        u->dash_model = dash_model;
        u->dash_model_gpu_id = 0;
    }

    if( dash_model && scene2 )
    {
        scene2->next_model_gpu_id++;
        int new_id = scene2->next_model_gpu_id;
        if( scene2__is_fast(element) )
            scene2__as_fast(element)->dash_model_gpu_id = new_id;
        else
            scene2__as_full(element)->dash_model_gpu_id = new_id;

        scene2_eventbuffer_push(
            scene2,
            (struct Scene2Event){
                .type = SCENE2_EVENT_MODEL_LOADED,
                .batched = scene2->batch_active,
                .u.model = {
                    .element_id = element_id,
                    .parent_entity_id = parent_entity_id,
                    .model_id = new_id,
                    .model = dash_model,
                },
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
    struct Scene2ElementFull* u = scene2__as_full(element);
    scene2_element_push_frame(&u->primary_frames, dash_frame, length);
}

void
scene2_element_push_secondary_animation_frame(
    struct Scene2Element* element,
    struct DashFrame* dash_frame,
    int length)
{
    struct Scene2ElementFull* u = scene2__as_full(element);
    scene2_element_push_frame(&u->secondary_frames, dash_frame, length);
}

void
scene2_element_set_framemap(
    struct Scene2Element* element,
    struct DashFramemap* dash_framemap)
{
    struct Scene2ElementFull* u = scene2__as_full(element);
    if( u->dash_framemap )
        dashframemap_free(u->dash_framemap);
    u->dash_framemap = dash_framemap;
}

void
scene2_element_release(
    struct Scene2* scene2,
    int element_id)
{
    if( element_id == -1 )
        return;
    if( !scene2 )
        return;

    struct Scene2Element* element = scene2_element_at(scene2, element_id);
    if( !element )
    {
        fprintf(
            stderr,
            "scene2_element_release: invalid element_id=%d (total=%d)\n",
            element_id,
            scene2_elements_total(scene2));
        abort();
    }
    assert(scene2_element_is_active(element) && "Element must be active");
    int parent_entity_id = scene2_element_parent_entity_id(element);

    if( scene2__is_fast(element) )
    {
        struct Scene2ElementFast* f = scene2__as_fast(element);
        if( f->dash_model )
        {
            scene2_eventbuffer_push(
                scene2,
                (struct Scene2Event){
                    .type = SCENE2_EVENT_MODEL_UNLOADED,
                    .batched = false,
                    .u.model = {
                        .element_id = element_id,
                        .parent_entity_id = parent_entity_id,
                        .model_id = f->dash_model_gpu_id,
                        .model = f->dash_model,
                    },
                });
            scene2__defer_model_free(scene2, f->dash_model);
        }
        f->dash_model = NULL;
        f->dash_model_gpu_id = 0;
        dashposition_free(f->dash_position);
        f->dash_position = NULL;
        f->flags_entity = SCENE2_FLAG_VALID | SCENE2_FLAG_FAST | SCENE2_PARENT_NONE;
    }
    else
    {
        struct Scene2ElementFull* u = scene2__as_full(element);
        if( u->dash_model )
        {
            scene2_eventbuffer_push(
                scene2,
                (struct Scene2Event){
                    .type = SCENE2_EVENT_MODEL_UNLOADED,
                    .batched = false,
                    .u.model = {
                        .element_id = element_id,
                        .parent_entity_id = parent_entity_id,
                        .model_id = u->dash_model_gpu_id,
                        .model = u->dash_model,
                    },
                });
            scene2__defer_model_free(scene2, u->dash_model);
        }
        u->dash_model = NULL;
        u->dash_model_gpu_id = 0;
        dashposition_free(u->dash_position);
        u->dash_position = NULL;
        dashframemap_free(u->dash_framemap);
        u->dash_framemap = NULL;
        scene2_element_clear_frames(&u->primary_frames);
        scene2_element_clear_frames(&u->secondary_frames);
        u->active_anim_id = 0;
        u->active_frame = 0;
        u->flags_entity = SCENE2_FLAG_VALID | SCENE2_PARENT_NONE;
    }

    scene2_splice_out_active(scene2, element);

    if( scene2__is_fast(element) )
    {
        scene2_el_set_next(element, scene2->fast_free_list);
        scene2_el_set_prev(element, NULL);
        if( scene2->fast_free_list != NULL )
            scene2_el_set_prev(scene2->fast_free_list, element);
        scene2->fast_free_list = element;
        scene2->fast_free_len++;
    }
    else
    {
        scene2_el_set_next(element, scene2->full_free_list);
        scene2_el_set_prev(element, NULL);
        if( scene2->full_free_list != NULL )
            scene2_el_set_prev(scene2->full_free_list, element);
        scene2->full_free_list = element;
        scene2->full_free_len++;
    }

    scene2->active_len--;
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_ELEMENT_RELEASED,
            .batched = false,
            .u.element.element_id = element_id,
            .u.element.parent_entity_id = parent_entity_id,
        });
}

void
scene2_element_expect(
    struct Scene2Element* el,
    const char* context)
{
    if( el )
        return;
    fprintf(
        stderr, "scene2_element_expect: NULL element (%s)\n", context ? context : "(no context)");
    abort();
}

struct Scene2Element*
scene2_element_at(
    struct Scene2* scene2,
    int element_id)
{
    if( !scene2 )
        return NULL;
    int total = scene2_elements_total(scene2);
    assert(element_id >= 0 && element_id < total && "Element id out of bounds");
    if( element_id < 0 || element_id >= total )
        return NULL;
    if( element_id < scene2->fast_count )
        return (struct Scene2Element*)&scene2->fast_pool[element_id];
    return (struct Scene2Element*)&scene2->full_pool[element_id - scene2->fast_count];
}

bool
scene2_element_is_active(const struct Scene2Element* element)
{
    return (scene2__flags_raw(element) & SCENE2_FLAG_ACTIVE) != 0;
}

int
scene2_element_parent_entity_id(const struct Scene2Element* element)
{
    return scene2_unpack_parent(scene2__flags_raw(element));
}

struct DashModel*
scene2_element_dash_model(struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return scene2__as_fast(element)->dash_model;
    return scene2__as_full(element)->dash_model;
}

const struct DashModel*
scene2_element_dash_model_const(const struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return scene2__as_fast_const(element)->dash_model;
    return scene2__as_full_const(element)->dash_model;
}

int
scene2_element_dash_model_gpu_id(const struct Scene2Element* element)
{
    if( !element )
        return 0;
    if( scene2__is_fast(element) )
        return scene2__as_fast_const(element)->dash_model_gpu_id;
    return scene2__as_full_const(element)->dash_model_gpu_id;
}

struct DashPosition*
scene2_element_dash_position(struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return scene2__as_fast(element)->dash_position;
    return scene2__as_full(element)->dash_position;
}

void
scene2_element_set_dash_position_ptr(
    struct Scene2Element* element,
    struct DashPosition* dash_position)
{
    if( scene2__is_fast(element) )
        scene2__as_fast(element)->dash_position = dash_position;
    else
        scene2__as_full(element)->dash_position = dash_position;
}

struct Scene2Element*
scene2_element_next(struct Scene2Element* element)
{
    return scene2_el_next(element);
}

struct DashFramemap*
scene2_element_dash_framemap(struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return NULL;
    return scene2__as_full(element)->dash_framemap;
}

uint16_t
scene2_element_active_anim_id(const struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return 0;
    return scene2__as_full_const(element)->active_anim_id;
}

uint8_t
scene2_element_active_frame(const struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return 0;
    return scene2__as_full_const(element)->active_frame;
}

void
scene2_element_set_active_anim_id(
    struct Scene2Element* element,
    uint16_t id)
{
    if( scene2__is_fast(element) )
        return;
    scene2__as_full(element)->active_anim_id = id;
}

void
scene2_element_set_active_frame(
    struct Scene2Element* element,
    uint8_t frame)
{
    if( scene2__is_fast(element) )
        return;
    scene2__as_full(element)->active_frame = frame;
}

struct Scene2Frames*
scene2_element_primary_frames(struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return NULL;
    return &scene2__as_full(element)->primary_frames;
}

struct Scene2Frames*
scene2_element_secondary_frames(struct Scene2Element* element)
{
    if( scene2__is_fast(element) )
        return NULL;
    return &scene2__as_full(element)->secondary_frames;
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
            .batched = false,
            .u.texture.texture_id = texture_id,
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

static bool
scene2__ptr_in_vertex_arrays(
    const struct Scene2* scene2,
    const struct DashVertexArray* va)
{
    for( int i = 0; i < scene2->vertex_arrays_count; i++ )
    {
        if( scene2->vertex_arrays[i] == va )
            return true;
    }
    return false;
}

static bool
scene2__ptr_in_face_arrays(
    const struct Scene2* scene2,
    const struct DashFaceArray* fa)
{
    for( int i = 0; i < scene2->face_arrays_count; i++ )
    {
        if( scene2->face_arrays[i] == fa )
            return true;
    }
    return false;
}

static void
scene2__defer_vertex_array_free(
    struct Scene2* scene2,
    struct DashVertexArray* va)
{
    if( !scene2 || !va )
        return;
    if( scene2->vertex_arrays_deferred_free_count >= scene2->vertex_arrays_deferred_free_capacity )
    {
        int ncap = scene2->vertex_arrays_deferred_free_capacity == 0
                       ? 8
                       : scene2->vertex_arrays_deferred_free_capacity * 2;
        scene2->vertex_arrays_deferred_free = realloc(
            scene2->vertex_arrays_deferred_free,
            (size_t)ncap * sizeof(scene2->vertex_arrays_deferred_free[0]));
        scene2->vertex_arrays_deferred_free_capacity = ncap;
    }
    scene2->vertex_arrays_deferred_free[scene2->vertex_arrays_deferred_free_count++] = va;
}

static void
scene2__defer_face_array_free(
    struct Scene2* scene2,
    struct DashFaceArray* fa)
{
    if( !scene2 || !fa )
        return;
    if( scene2->face_arrays_deferred_free_count >= scene2->face_arrays_deferred_free_capacity )
    {
        int ncap = scene2->face_arrays_deferred_free_capacity == 0
                       ? 8
                       : scene2->face_arrays_deferred_free_capacity * 2;
        scene2->face_arrays_deferred_free = realloc(
            scene2->face_arrays_deferred_free,
            (size_t)ncap * sizeof(scene2->face_arrays_deferred_free[0]));
        scene2->face_arrays_deferred_free_capacity = ncap;
    }
    scene2->face_arrays_deferred_free[scene2->face_arrays_deferred_free_count++] = fa;
}

void
scene2_flush_deferred_array_frees(struct Scene2* scene2)
{
    if( !scene2 )
        return;
    for( int i = 0; i < scene2->vertex_arrays_deferred_free_count; i++ )
        dashvertexarray_free(scene2->vertex_arrays_deferred_free[i]);
    scene2->vertex_arrays_deferred_free_count = 0;

    for( int j = 0; j < scene2->face_arrays_deferred_free_count; j++ )
        dashfacearray_free(scene2->face_arrays_deferred_free[j]);
    scene2->face_arrays_deferred_free_count = 0;

    for( int k = 0; k < scene2->models_deferred_free_count; k++ )
        dashmodel_free(scene2->models_deferred_free[k]);
    scene2->models_deferred_free_count = 0;
}

void
scene2_vertex_array_register(
    struct Scene2* scene2,
    struct DashVertexArray* vertex_array)
{
    if( !scene2 || !vertex_array )
        return;
    if( scene2__ptr_in_vertex_arrays(scene2, vertex_array) )
        return;

    if( scene2->vertex_arrays_count >= scene2->vertex_arrays_capacity )
    {
        int ncap = scene2->vertex_arrays_capacity == 0 ? 8 : scene2->vertex_arrays_capacity * 2;
        scene2->vertex_arrays =
            realloc(scene2->vertex_arrays, (size_t)ncap * sizeof(scene2->vertex_arrays[0]));
        scene2->vertex_arrays_capacity = ncap;
    }

    scene2->next_vertex_array_gpu_id++;
    vertex_array->scene2_gpu_id = scene2->next_vertex_array_gpu_id;

    scene2->vertex_arrays[scene2->vertex_arrays_count++] = vertex_array;
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_VERTEX_ARRAY_ADDED,
            .batched = scene2->batch_active,
            .u.vertex_array = {
                .array_id = vertex_array->scene2_gpu_id,
                .array = vertex_array,
            },
        });
}

void
scene2_vertex_array_unregister(
    struct Scene2* scene2,
    struct DashVertexArray* vertex_array)
{
    if( !scene2 || !vertex_array )
        return;

    for( int i = 0; i < scene2->vertex_arrays_count; i++ )
    {
        if( scene2->vertex_arrays[i] != vertex_array )
            continue;
        scene2_eventbuffer_push(
            scene2,
            (struct Scene2Event){
                .type = SCENE2_EVENT_VERTEX_ARRAY_REMOVED,
                .batched = false,
                .u.vertex_array = {
                    .array_id = vertex_array->scene2_gpu_id,
                    .array = vertex_array,
                },
            });
        scene2->vertex_arrays[i] = scene2->vertex_arrays[scene2->vertex_arrays_count - 1];
        scene2->vertex_arrays_count--;
        scene2__defer_vertex_array_free(scene2, vertex_array);
        return;
    }
}

void
scene2_face_array_register(
    struct Scene2* scene2,
    struct DashFaceArray* face_array)
{
    if( !scene2 || !face_array )
        return;
    if( scene2__ptr_in_face_arrays(scene2, face_array) )
        return;

    if( scene2->face_arrays_count >= scene2->face_arrays_capacity )
    {
        int ncap = scene2->face_arrays_capacity == 0 ? 8 : scene2->face_arrays_capacity * 2;
        scene2->face_arrays =
            realloc(scene2->face_arrays, (size_t)ncap * sizeof(scene2->face_arrays[0]));
        scene2->face_arrays_capacity = ncap;
    }

    scene2->next_face_array_gpu_id++;
    face_array->scene2_gpu_id = scene2->next_face_array_gpu_id;

    scene2->face_arrays[scene2->face_arrays_count++] = face_array;
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_FACE_ARRAY_ADDED,
            .batched = scene2->batch_active,
            .u.face_array = {
                .array_id = face_array->scene2_gpu_id,
                .array = face_array,
            },
        });
}

void
scene2_face_array_unregister(
    struct Scene2* scene2,
    struct DashFaceArray* face_array)
{
    if( !scene2 || !face_array )
        return;

    for( int i = 0; i < scene2->face_arrays_count; i++ )
    {
        if( scene2->face_arrays[i] != face_array )
            continue;
        scene2_eventbuffer_push(
            scene2,
            (struct Scene2Event){
                .type = SCENE2_EVENT_FACE_ARRAY_REMOVED,
                .batched = false,
                .u.face_array = {
                    .array_id = face_array->scene2_gpu_id,
                    .array = face_array,
                },
            });
        scene2->face_arrays[i] = scene2->face_arrays[scene2->face_arrays_count - 1];
        scene2->face_arrays_count--;
        scene2__defer_face_array_free(scene2, face_array);
        return;
    }
}

int
scene2_vertex_arrays_count(const struct Scene2* scene2)
{
    return scene2 ? scene2->vertex_arrays_count : 0;
}

struct DashVertexArray*
scene2_vertex_array_at(
    struct Scene2* scene2,
    int index)
{
    if( !scene2 || index < 0 || index >= scene2->vertex_arrays_count )
        return NULL;
    return scene2->vertex_arrays[index];
}

int
scene2_face_arrays_count(const struct Scene2* scene2)
{
    return scene2 ? scene2->face_arrays_count : 0;
}

struct DashFaceArray*
scene2_face_array_at(
    struct Scene2* scene2,
    int index)
{
    if( !scene2 || index < 0 || index >= scene2->face_arrays_count )
        return NULL;
    return scene2->face_arrays[index];
}

void
scene2_batch_begin(
    struct Scene2* scene2,
    uint32_t batch_id)
{
    if( !scene2 )
        return;
    assert(!scene2->batch_active && "scene2_batch_begin: nested batch");
    scene2->batch_current_id = batch_id;
    scene2->batch_active = true;
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_BATCH_BEGIN,
            .batched = false,
            .u.batch.batch_id = batch_id,
        });
}

void
scene2_batch_end(struct Scene2* scene2)
{
    if( !scene2 )
        return;
    assert(scene2->batch_active && "scene2_batch_end: no active batch");
    uint32_t id = scene2->batch_current_id;
    scene2->batch_active = false;
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_BATCH_END,
            .batched = false,
            .u.batch.batch_id = id,
        });
}

void
scene2_batch_clear(
    struct Scene2* scene2,
    uint32_t batch_id)
{
    if( !scene2 )
        return;
    scene2_eventbuffer_push(
        scene2,
        (struct Scene2Event){
            .type = SCENE2_EVENT_BATCH_CLEAR,
            .batched = false,
            .u.batch.batch_id = batch_id,
        });
}
