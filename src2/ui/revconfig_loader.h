#ifndef REVCONFIG_LOADER_H
#define REVCONFIG_LOADER_H

#include "osrs/revconfig/revconfig.h"

#include <stdbool.h>

struct Dat1BuildCache;
struct GameCache;
struct LibToriRS_IOQueue;
struct RevConfigLoaderState;
struct UIScene;
struct UITree;

/* ------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------ */

struct RevConfigLoaderState*
revconfig_loader_state_new(void);

void
revconfig_loader_state_free(struct RevConfigLoaderState* state);

void
revconfig_loader_reset(struct RevConfigLoaderState* state);

/* ------------------------------------------------------------------
 * Target store setters (call before process)
 * ------------------------------------------------------------------ */

void
revconfig_loader_set_buildcache(
    struct RevConfigLoaderState* state,
    struct Dat1BuildCache* buildcache);

void
revconfig_loader_set_gamecache(
    struct RevConfigLoaderState* state,
    struct GameCache* gamecache);

void
revconfig_loader_set_ui_scene(
    struct RevConfigLoaderState* state,
    struct UIScene* scene);

/* ------------------------------------------------------------------
 * RevConfig buffer (populated by the ScriptAPI config-file load step)
 * ------------------------------------------------------------------ */

struct RevConfigBuffer*
revconfig_loader_revconfig(struct RevConfigLoaderState* state);

void
revconfig_loader_revconfig_reset(struct RevConfigLoaderState* state);

/* ------------------------------------------------------------------
 * Queue an RS component for loading + baking (called from ScriptAPI)
 * ------------------------------------------------------------------ */

void
revconfig_loader_queue_rs_component(
    struct RevConfigLoaderState* state,
    int component_id);

/* ------------------------------------------------------------------
 * Per-tick driver
 * ------------------------------------------------------------------ */

/** Returns true once all queued elements are DONE/FAILED. */
bool
revconfig_loader_ready(struct RevConfigLoaderState* state);

/**
 * Drive the load loop one tick:
 *   1. Route any resolved IO from io_queue into dat1_buildcache.
 *   2. Step every queued element's state machine.
 *   3. Collect outstanding archive requests and push unique ones to io_queue.
 */
void
revconfig_loader_process(
    struct RevConfigLoaderState* state,
    struct LibToriRS_IOQueue* io_queue);

/* ------------------------------------------------------------------
 * Bake phase (call once revconfig_loader_ready returns true)
 * ------------------------------------------------------------------ */

/**
 * Re-iterate the RevConfigBuffer and build the UITree in a single
 * forward pass, assuming gamecache/UIScene already hold every resource.
 */
bool
revconfig_loader_submit(
    struct RevConfigLoaderState* state,
    struct UITree* tree);

#endif /* REVCONFIG_LOADER_H */
