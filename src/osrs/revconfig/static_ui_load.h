#ifndef STATIC_UI_LOAD_H
#define STATIC_UI_LOAD_H

#include "osrs/buildcachedat.h"
#include "revconfig.h"
#include "static_ui.h"
#include "uiscene.h"

struct Scene2;

/** Largest `CacheDatConfigComponent.id` in the component map, or -1 if empty. */
int
buildcachedat_max_component_id(struct BuildCacheDat* buildcachedat);

void
static_ui_from_revconfig_buildcachedat(
    struct StaticUIBuffer* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat,
    struct RevConfigBuffer* revconfig_buffer);

// /** Push RS `StaticUIComponent` rows and register sprites/models in ui_scene / scene2. */
// void
// static_ui_rs_from_buildcachedat(
//     struct StaticUIBuffer* ui,
//     struct UIScene* ui_scene,
//     struct Scene2* scene2,
//     struct BuildCacheDat* buildcachedat);

#endif