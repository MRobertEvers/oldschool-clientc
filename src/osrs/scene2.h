#ifndef SCENE2__H
#define SCENE2__H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

struct Scene2Element;
struct Scene2ElementFast;
struct Scene2ElementFull;

/** Max merged world-rebuild GPU batches (ids 0 .. SCENE2_MAX_GPU_BATCHES-1). */
#define SCENE2_MAX_GPU_BATCHES 10

/** Sentinel: no completed rebuild batch to clear on next begin (not a valid batch slot). */
#define SCENE2_GPU_BATCH_SLOT_INVALID 0xFFFFFFFFu

/** What kind of scene element this is for GPU/usage classification (matches `ToriRS_UsageHint`
 * order: SCENERY=0 .. PROJECTILE=3). */
enum Scene2ElementCategory
{
    SCENE2_ELEMENT_SCENERY = 0,
    SCENE2_ELEMENT_NPC = 1,
    SCENE2_ELEMENT_PLAYER = 2,
    SCENE2_ELEMENT_PROJECTILE = 3,
};

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
    SCENE2_EVENT_MODEL_LOADED = 3,
    SCENE2_EVENT_TEXTURE_LOADED = 4,
    SCENE2_EVENT_VERTEX_ARRAY_ADDED = 5,
    SCENE2_EVENT_VERTEX_ARRAY_REMOVED = 6,
    SCENE2_EVENT_FACE_ARRAY_ADDED = 7,
    SCENE2_EVENT_FACE_ARRAY_REMOVED = 8,
    SCENE2_EVENT_MODEL_UNLOADED = 9,
    SCENE2_EVENT_BATCH_BEGIN = 10,
    SCENE2_EVENT_BATCH_END = 11,
    SCENE2_EVENT_BATCH_CLEAR = 12,
    /** Pre-baked animated pose ready for upload: (model_gpu_id, anim_id, animation_index,
     * frame_index). */
    SCENE2_EVENT_ANIMATION_LOADED = 13,
    /** Begin batching world texture uploads under `batch_id`. */
    SCENE2_EVENT_TEXTURE_BATCH_BEGIN = 14,
    /** End world texture batch. */
    SCENE2_EVENT_TEXTURE_BATCH_END = 15,
};

/** Payload is in `u` according to `type` (element 1–2, model 3+9, texture 4, vertex_array 5–6,
 * face_array 7–8, batch 10–12). */
struct Scene2Event
{
    enum Scene2EventType type;
    /** True for MODEL_LOADED / VERTEX_ARRAY_ADDED / FACE_ARRAY_ADDED emitted while batch is active.
     */
    bool batched;
    union
    {
        struct
        {
            int element_id;
            int parent_entity_id;
            /** `Scene2ElementCategory` at event time. */
            uint8_t element_category;
        } element;
        struct
        {
            int element_id;
            int parent_entity_id;
            int model_id;
            /** `Scene2ElementCategory` at event time. */
            uint8_t element_category;
            /** Valid until scene2_flush_deferred_array_frees (deferred dashmodel_free). */
            struct DashModel* model;
            /** World placement from `scene2_element_dash_position` at load time (r2pi2048 yaw). */
            int32_t world_x;
            int32_t world_y;
            int32_t world_z;
            int32_t world_yaw_r2pi2048;
        } model;
        struct
        {
            int texture_id;
        } texture;
        struct
        {
            int array_id;
            struct DashVertexArray* array;
        } vertex_array;
        struct
        {
            int array_id;
            struct DashFaceArray* array;
        } face_array;
        struct
        {
            uint32_t batch_id;
        } batch;
        /** ANIMATION_LOADED: pose for (model_gpu_id, anim_id, animation_index, frame_index). */
        struct
        {
            int model_gpu_id;
            int anim_id;
            /** 0 = primary animation track, 1 = secondary. */
            int animation_index;
            int frame_index;
            /** `Scene2ElementCategory` for the owning element when known. */
            uint8_t element_category;
            struct DashModel* model;
            struct DashFrame* frame;
            struct DashFramemap* framemap;
        } animation;
    } u;
};

/** Deferred `SCENE2_EVENT_MODEL_LOADED` while `scene2->batch_active`: world_xyz is read at
 * `scene2_batch_end` after callers set `dash_position`. */
struct Scene2BatchedModelLoad
{
    int element_id;
    int parent_entity_id;
    int model_gpu_id;
    uint8_t element_category;
    struct DashModel* model;
};

enum Scene2BatchDeferKind
{
    SCENE2_BATCH_DEFER_MODEL = 0,
    SCENE2_BATCH_DEFER_ANIM = 1,
};

/** Deferred `SCENE2_EVENT_ANIMATION_LOADED` while batch_active (interleaved with model ops). */
struct Scene2BatchedAnimationLoad
{
    int model_gpu_id;
    int anim_id;
    int animation_index;
    int frame_index;
    uint8_t element_category;
    struct DashModel* model;
    struct DashFrame* frame;
    struct DashFramemap* framemap;
};

struct Scene2BatchDeferredOp
{
    enum Scene2BatchDeferKind kind;
    union
    {
        struct Scene2BatchedModelLoad model;
        struct Scene2BatchedAnimationLoad anim;
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

    int next_vertex_array_gpu_id;
    int next_face_array_gpu_id;
    int next_model_gpu_id;

    /** Models removed via set_dash_model/release; freed after MODEL_UNLOADED events are drained. */
    struct DashModel** models_deferred_free;
    int models_deferred_free_count;
    int models_deferred_free_capacity;

    /** World rebuild GPU batch: when true, static load events are tagged `batched`. */
    bool batch_active;
    uint32_t batch_current_id;
    /** Which slots in `gpu_batch_slot_live` are in use (batch ids 0 .. SCENE2_MAX_GPU_BATCHES-1).
     */
    bool gpu_batch_slot_live[SCENE2_MAX_GPU_BATCHES];

    /** Ordered deferred MODEL_LOADED + ANIMATION_LOADED while batch_active; flushed at batch_end.
     */
    struct Scene2BatchDeferredOp* batch_deferred_ops;
    int batch_deferred_ops_count;
    int batch_deferred_ops_capacity;

    /** World texture batch: wraps all TEXTURE_LOADED events during initial cache load. */
    bool texture_batch_active;
    uint32_t texture_batch_current_id;
};

struct Scene2*
scene2_new(
    int fast_count,
    int full_count);

void
scene2_free(struct Scene2* scene2);

/** Total element slots (fast_count + full_count). */
int
scene2_elements_total(const struct Scene2* scene2);

int
scene2_element_acquire_fast(
    struct Scene2* scene2,
    int parent_entity_id,
    enum Scene2ElementCategory category);

int
scene2_element_acquire_full(
    struct Scene2* scene2,
    int parent_entity_id,
    enum Scene2ElementCategory category);

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

/**
 * Push one keyframe. If `scene2` is set and the element has a model with a valid
 * `dash_model_gpu_id`, queues SCENE2_EVENT_ANIMATION_LOADED (primary track,
 * `animation_index` 0) for the GPU to upload that frame.
 * `frame_index` is the index of this keyframe within the sequence.
 */
void
scene2_element_push_animation_frame(
    struct Scene2* scene2,
    struct Scene2Element* element,
    int anim_id,
    int frame_index,
    struct DashFrame* dash_frame,
    int length);

/** Like scene2_element_push_animation_frame, secondary track (animation_index 1). */
void
scene2_element_push_secondary_animation_frame(
    struct Scene2* scene2,
    struct Scene2Element* element,
    int anim_id,
    int frame_index,
    struct DashFrame* dash_frame,
    int length);

void
scene2_element_set_framemap(
    struct Scene2Element* element,
    struct DashFramemap* dash_framemap);

/**
 * Enqueue a SCENE2_EVENT_ANIMATION_LOADED event for a pre-baked animated pose.
 * `animation_index`: 0 primary, 1 secondary (GPU cache2 slot).
 * `frame` and `framemap` pointers are stored in the event payload (not copied).
 */
void
scene2_element_queue_animation_load(
    struct Scene2* scene2,
    int model_gpu_id,
    int anim_id,
    int animation_index,
    int frame_index,
    enum Scene2ElementCategory element_category,
    struct DashModel* model,
    struct DashFrame* frame,
    struct DashFramemap* framemap);

/** Valid ids are 0 .. scene2_elements_total(scene2)-1. Returns NULL if scene2 is NULL or id is out
 * of range. */
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

/** Category assigned at acquire (default SCENERY for pool slots before first acquire). */
enum Scene2ElementCategory
scene2_element_category(const struct Scene2Element* element);

struct DashModel*
scene2_element_dash_model(struct Scene2Element* element);

const struct DashModel*
scene2_element_dash_model_const(const struct Scene2Element* element);

/** Id assigned when a model is set on this element; 0 if none. Used with render MODEL_LOAD/UNLOAD.
 */
int
scene2_element_dash_model_gpu_id(const struct Scene2Element* element);

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
scene2_element_set_active_anim_id(
    struct Scene2Element* element,
    uint16_t id);

void
scene2_element_set_active_frame(
    struct Scene2Element* element,
    uint8_t frame);

uint8_t
scene2_element_active_animation_index(const struct Scene2Element* element);

void
scene2_element_set_active_animation_index(
    struct Scene2Element* element,
    uint8_t index);

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

/** Reset batch slot bookkeeping (call at world rebuild start before `scene2_batch_begin`). */
void
scene2_gpu_batches_reset(struct Scene2* scene2);

/** Begin a rebuild batch; returns a slot id in `0 .. SCENE2_MAX_GPU_BATCHES-1`. Asserts no nested
 * batch and that a free slot exists. */
uint32_t
scene2_batch_begin(struct Scene2* scene2);

/** End the active batch; emits BATCH_END with current batch id. */
void
scene2_batch_end(struct Scene2* scene2);

/** Tell renderers to unload merged GPU data for a prior batch id. */
void
scene2_batch_clear(
    struct Scene2* scene2,
    uint32_t batch_id);

/** Begin a world-texture array batch (wraps texture loads 0-49). */
void
scene2_texture_batch_begin(
    struct Scene2* scene2,
    uint32_t batch_id);

/** End the active texture batch; emits TEXTURE_BATCH_END. */
void
scene2_texture_batch_end(struct Scene2* scene2);

/** Frees vertex/face arrays and DashModels queued for deferred free; call after consumers finish
 * popping scene2 events. */
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
