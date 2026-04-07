#ifndef UITREE_LOAD_H
#define UITREE_LOAD_H

#include "osrs/buildcachedat.h"
#include "revconfig.h"
#include "uiscene.h"
#include "uitree.h"

struct Scene2;
struct GGame;

void
uitree_from_revconfig_buildcachedat(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct Scene2* scene2,
    struct BuildCacheDat* buildcachedat,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer);

int
uitree_revconfig_collect_inv_obj_ids(
    struct RevConfigBuffer* revconfig_buffer,
    int* out_ids,
    int max_ids);

void
uitree_load_inventories_from_revconfig(
    struct UIScene* ui_scene,
    struct GGame* game,
    struct UIInventoryPool* inv_pool,
    struct RevConfigBuffer* revconfig_buffer);

void
uitree_load_ui_from_revconfig(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct Scene2* scene2,
    struct BuildCacheDat* buildcachedat,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer);

// /** Push RS `StaticUIComponent` rows and register sprites/models in ui_scene / scene2. */
// void
// static_ui_rs_from_buildcachedat(p
//     struct UITree* ui,
//     struct UIScene* ui_scene,
//     struct Scene2* scene2,
//     struct BuildCacheDat* buildcachedat);

#endif