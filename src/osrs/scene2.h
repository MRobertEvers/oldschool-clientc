#ifndef SCENE2__H
#define SCENE2__H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

struct Scene2Element;
struct Scene2ElementFast;
struct Scene2ElementFull;

struct Scene2TextureEntry
{
    int id;
    struct DashTexture* texture;
};

struct Scene2Frames
{
    struct DashFrame** frames;
    int* lengths;
    int count;
    int capacity;
};

enum Scene2EventType
{
    SCENE2_EVENT_NONE = 0,
    SCENE2_EVENT_ELEMENT_ACQUIRED = 1,
    SCENE2_EVENT_ELEMENT_RELEASED = 2,
    SCENE2_EVENT_MODEL_CHANGED = 3,
    SCENE2_EVENT_TEXTURE_LOADED = 4,
    SCENE2_EVENT_VERTEX_ARRAY_ADDED = 5,
    SCENE2_EVENT_VERTEX_ARRAY_REMOVED = 6,
    SCENE2_EVENT_FACE_ARRAY_ADDED = 7,
    SCENE2_EVENT_FACE_ARRAY_REMOVED = 8,
};

/** Payload is in `u` according to `type`: element (1–3), texture (4), vertex_array (5–6), face_array (7–8). */
struct Scene2Event
{
    enum Scene2EventType type;
    union {
        struct
        {
            int element_id;
            int parent_entity_id;
        } element;
        struct
        {
            int texture_id;
        } texture;
        struct
        {
            struct DashVertexArray* array;
        } vertex_array;
        struct
        {
            struct DashFaceArray* array;
        } face_array;
    } u;
};

struct Scene2
{
    struct Scene2ElementFast* fast_pool;
    struct Scene2ElementFull* full_pool;
    int fast_count;
    int full_count;

    struct Scene2Element* active_list;
    int active_len;
    struct Scene2Element* fast_free_list;
    struct Scene2Element* full_free_list;
    int fast_free_len;
    int full_free_len;

    struct Scene2TextureEntry* textures;
    int textures_count;
    int textures_capacity;

    /** Scene2 owns these; register transfers ownership. Small growable lists. */
    struct DashVertexArray** vertex_arrays;
    int vertex_arrays_count;
    int vertex_arrays_capacity;

    struct DashFaceArray** face_arrays;
    int face_arrays_count;
    int face_arrays_capacity;

    /** Unregister queues frees here until after REMOVED events are drained (see flush below). */
    struct DashVertexArray** vertex_arrays_deferred_free;
    int vertex_arrays_deferred_free_count;
    int vertex_arrays_deferred_free_capacity;

    struct DashFaceArray** face_arrays_deferred_free;
    int face_arrays_deferred_free_count;
    int face_arrays_deferred_free_capacity;

    struct Scene2Event* eventbuffer;
    int eventbuffer_capacity;
    int eventbuffer_head;
    int eventbuffer_tail;
    int eventbuffer_count;
};

struct Scene2*
scene2_new(int fast_count, int full_count);

void
scene2_free(struct Scene2* scene2);

/** Total element slots (fast_count + full_count). */
int
scene2_elements_total(const struct Scene2* scene2);

int
scene2_element_acquire_fast(
    struct Scene2* scene2,
    int parent_entity_id);

int
scene2_element_acquire_full(
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
    struct Scene2* scene2,
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

/** Valid ids are 0 .. scene2_elements_total(scene2)-1. Returns NULL if scene2 is NULL or id is out of range. */
struct Scene2Element*
scene2_element_at(
    struct Scene2* scene2,
    int element_id);

/** Abort if el is NULL (e.g. invalid scene2_element_at). context is logged for diagnostics. */
void
scene2_element_expect(
    struct Scene2Element* el,
    const char* context);

/** Index of this element in the scene2 pools (0 .. fast_count+full_count-1). */
int
scene2_element_id(
    struct Scene2* scene2,
    const struct Scene2Element* element);

bool
scene2_element_is_active(const struct Scene2Element* element);

int
scene2_element_parent_entity_id(const struct Scene2Element* element);

struct DashModel*
scene2_element_dash_model(struct Scene2Element* element);

const struct DashModel*
scene2_element_dash_model_const(const struct Scene2Element* element);

struct DashPosition*
scene2_element_dash_position(struct Scene2Element* element);

void
scene2_element_set_dash_position_ptr(
    struct Scene2Element* element,
    struct DashPosition* dash_position);

struct Scene2Element*
scene2_element_next(struct Scene2Element* element);

struct DashFramemap*
scene2_element_dash_framemap(struct Scene2Element* element);

uint16_t
scene2_element_active_anim_id(const struct Scene2Element* element);

uint8_t
scene2_element_active_frame(const struct Scene2Element* element);

void
scene2_element_set_active_anim_id(struct Scene2Element* element, uint16_t id);

void
scene2_element_set_active_frame(struct Scene2Element* element, uint8_t frame);

struct Scene2Frames*
scene2_element_primary_frames(struct Scene2Element* element);

struct Scene2Frames*
scene2_element_secondary_frames(struct Scene2Element* element);

bool
scene2_eventbuffer_is_empty(struct Scene2* scene2);

bool
scene2_eventbuffer_pop(
    struct Scene2* scene2,
    struct Scene2Event* out_event);

void
scene2_eventbuffer_clear(struct Scene2* scene2);

void
scene2_texture_add(
    struct Scene2* scene2,
    int texture_id,
    struct DashTexture* texture);

struct DashTexture*
scene2_texture_get(
    struct Scene2* scene2,
    int texture_id);

/** Takes ownership of vertex_array; caller must not free it. */
void
scene2_vertex_array_register(
    struct Scene2* scene2,
    struct DashVertexArray* vertex_array);

/** Drops ownership; queues free after scene2_flush_deferred_array_frees (after REMOVED events). */
void
scene2_vertex_array_unregister(
    struct Scene2* scene2,
    struct DashVertexArray* vertex_array);

/** Takes ownership of face_array; caller must not free it. */
void
scene2_face_array_register(
    struct Scene2* scene2,
    struct DashFaceArray* face_array);

void
scene2_face_array_unregister(
    struct Scene2* scene2,
    struct DashFaceArray* face_array);

/** Frees arrays removed via unregister; call after consumers finish popping scene2 events. */
void
scene2_flush_deferred_array_frees(struct Scene2* scene2);

int
scene2_vertex_arrays_count(const struct Scene2* scene2);

struct DashVertexArray*
scene2_vertex_array_at(
    struct Scene2* scene2,
    int index);

int
scene2_face_arrays_count(const struct Scene2* scene2);

struct DashFaceArray*
scene2_face_array_at(
    struct Scene2* scene2,
    int index);

#endif
