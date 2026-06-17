#ifndef TORIDRAW_GCCONTEXT_H
#define TORIDRAW_GCCONTEXT_H

#include "toridraw_types.h"

#include <stdbool.h>

/* GC context lifecycle (called by toridraw.c context_new/free) */

bool
toridraw_gc_init(struct ToriDraw_Context* context);

void
toridraw_gc_shutdown(struct ToriDraw_Context* context);

/* Asset registry: models */

void
toridraw_gc_model_add(
    struct ToriDraw_Context* context,
    int model_id,
    struct ToriDraw_ModelHandle model);

struct ToriDraw_ModelHandle
toridraw_gc_model_get(
    struct ToriDraw_Context* context,
    int model_id);

bool
toridraw_gc_model_has(
    struct ToriDraw_Context* context,
    int model_id);

struct ToriDraw_ModelHandle
toridraw_gc_model_remove(
    struct ToriDraw_Context* context,
    int model_id);

void
toridraw_gc_models_clear_all(struct ToriDraw_Context* context);

/* Asset registry: animations */

void
toridraw_gc_animation_add(
    struct ToriDraw_Context* context,
    int anim_id,
    struct ToriDraw_Animation* animation);

struct ToriDraw_Animation*
toridraw_gc_animation_get(
    struct ToriDraw_Context* context,
    int anim_id);

bool
toridraw_gc_animation_has(
    struct ToriDraw_Context* context,
    int anim_id);

/* Asset registry: textures */

void
toridraw_gc_set_texture(
    struct ToriDraw_Context* context,
    int id,
    struct ToriDraw_Texture* texture);

/* Scene elements */

void
toridraw_gc_clear_scene(struct ToriDraw_Context* context);

int
toridraw_gc_element_add(struct ToriDraw_Context* context);

int
toridraw_gc_element_remove(
    struct ToriDraw_Context* context,
    int element_id);

struct ToriDraw_GCElement*
toridraw_gc_element_get(
    struct ToriDraw_Context* context,
    int element_id);

bool
toridraw_gc_element_is_live(
    struct ToriDraw_Context* context,
    int element_id);

int
toridraw_gc_element_slot_count(struct ToriDraw_Context* context);

void
toridraw_gc_element_set_model(
    struct ToriDraw_Context* context,
    int element_id,
    struct ToriDraw_ModelHandle model);

void
toridraw_gc_element_set_animation(
    struct ToriDraw_Context* context,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary);

void
toridraw_gc_element_set_animation_seq(
    struct ToriDraw_Context* context,
    int element_id,
    int seq_id);

void
toridraw_gc_element_set_position(
    struct ToriDraw_Context* context,
    int element_id,
    int x,
    int y,
    int z,
    int yaw);

/* Batch building */

void
toridraw_gc_batch_begin(struct ToriDraw_Context* context);

struct ToriDraw_GCBatchElementHandle
toridraw_gc_batch_add_element(struct ToriDraw_Context* context);

void
toridraw_gc_batch_end(struct ToriDraw_Context* context);

void
toridraw_gc_batch_clear(
    struct ToriDraw_Context* context,
    int batch_id);

void
toridraw_gc_batch_element_add_pose(
    struct ToriDraw_Context* context,
    int element_id,
    int pose_id,
    struct ToriDraw_ModelHandle baked);

/* Events and frame lifecycle */

struct ToriDraw_GCEventQueue*
toridraw_gc_events(struct ToriDraw_Context* context);

void
toridraw_gc_frame_end(struct ToriDraw_Context* context);

#endif
