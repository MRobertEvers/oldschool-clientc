#ifndef SRC_ENGINE_UITREE_ANIM_H
#define SRC_ENGINE_UITREE_ANIM_H

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/asset_requests.h"
#include "toridraw_scene.h"
#include "ui/uitree.h"

/*
 * Model-widget animation driver (TS WidgetManager.tickModelAnimations).
 * Split into a request phase (enqueue sequence-load tasks, never pump) and a
 * native-clock advance phase (rendering resolves private widget poses), so the WASM shell can return to
 * the browser loop between the two. While a sequence load is in flight the
 * model renders at its rest pose — the natural loading placeholder.
 */

/** Resolve a widget's private rendered pose without mutating the registered asset. */
struct ToriDraw_ModelHandle UITreeAnim_ModelForDraw(struct ToriDraw_Scene* scene,
    struct UITreeModelRenderCache* cache, int model_id, int sequence, int frame);

/** Want (ASSET_KIND_SEQUENCE) every sequence an RS_MODEL node plays that is
 * not yet in the scene; the assets table holds one load per sequence however
 * often it is asked. Returns how many were wanted. */
int
UITreeAnim_RequestMissing(
    struct UITree* tree,
    struct ToriDraw_Scene* scene,
    struct Assets* assets);

/**
 * Advance every animated RS_MODEL by `cycles` 50hz client cycles (0 =
 * (re)apply current frame). A sequence not yet registered in the scene is
 * still loading (rest pose); the load task registers an empty sentinel for
 * sequences unavailable in this cache, which advance skips permanently.
 * Returns non-zero when an animated model needs rendering. Posing occurs in
 * UITreeAnim_ModelForDraw without changing the registered asset.
 */
int
UITreeAnim_Advance(
    struct UITree* tree,
    struct ToriDraw_Scene* scene,
    int cycles);

#endif
