#ifndef UI_LOADER_H
#define UI_LOADER_H

#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/uitree.h"
#include "ui_scene.h"

#include <stdbool.h>

struct Dat1BuildCache;
struct ToriAuxLibCore;
struct LibToriRS_IOQueue;
struct UILoaderState;

struct UILoaderState*
ui_loader_state_new(void);

void
ui_loader_state_free(struct UILoaderState* state);

void
ui_loader_reset(struct UILoaderState* state);

struct RevConfigBuffer*
ui_loader_revconfig(struct UILoaderState* state);

void
ui_loader_revconfig_reset(struct UILoaderState* state);

void
ui_loader_set_buildcache(
    struct UILoaderState* state,
    struct Dat1BuildCache* buildcache);

void
ui_loader_set_gamecache(
    struct UILoaderState* state,
    struct ToriAuxLibCore* gamecache);

void
ui_loader_set_ui_scene(
    struct UILoaderState* state,
    struct UIScene* scene);

bool
ui_loader_ready(struct UILoaderState* state);

void
ui_loader_process(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue);

/** Acquire UIScene elements from fulfilled queue items and build UITree from saved layout. */
bool
ui_loader_submit(
    struct UILoaderState* state,
    struct UITree* tree,
    struct UIScene* scene);

void
ui_loader_queue_rs_component(
    struct UILoaderState* state,
    int component_id);

#endif
