#ifndef SRC_ENGINE_UITREE_ANIM_H
#define SRC_ENGINE_UITREE_ANIM_H

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "toridraw_scene.h"
#include "ui/uitree.h"

/*
 * Model-widget animation driver (TS WidgetManager.tickModelAnimations).
 * Split into a request phase (enqueue sequence-load tasks, never pump) and a
 * pure advance phase (apply loaded frames), so the WASM shell can return to
 * the browser loop between the two. While a sequence load is in flight the
 * model renders at its rest pose — the natural loading placeholder.
 */

#define UITREE_ANIM_SEQ_TRACK_MAX 64

struct SeqLoadTracker
{
    int seq_ids[UITREE_ANIM_SEQ_TRACK_MAX];
    int count;
};

/** Enqueue loads for sequences referenced by RS_MODEL nodes that are not yet
 * in the scene and not already requested. Returns the number enqueued. */
int
UITreeAnim_RequestMissing(
    struct UITree* tree,
    struct ToriDraw_Scene* scene,
    struct CacheProvider* provider,
    struct ToriRS_TaskQueue* queue,
    struct SeqLoadTracker* tracker);

/**
 * Advance every animated RS_MODEL by `cycles` 50hz client cycles (0 =
 * (re)apply current frame). queue_idle tells the driver the task queue has
 * drained: a requested sequence still absent then is unavailable in this
 * cache and is disabled so it is not re-requested every tick.
 * Returns non-zero if any animation was applied (caller should redraw).
 */
int
UITreeAnim_Advance(
    struct UITree* tree,
    struct ToriDraw_Scene* scene,
    struct SeqLoadTracker* tracker,
    int cycles,
    int queue_idle);

#endif
