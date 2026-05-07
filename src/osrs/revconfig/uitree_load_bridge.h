#ifndef UITREE_LOAD_BRIDGE_H
#define UITREE_LOAD_BRIDGE_H

/*
 * Narrow API from uitree_load.c for uitree_loader.c so the loader does not include
 * uitree_load_private.h (or uitree_load.h).
 */

#include "osrs/revconfig/uitree_ini_load_state.h"
#include "osrs/revconfig/uitree_loader.h"

#include <stdint.h>

struct BuildCacheDat;
struct DashMap;
struct GGame;
struct UITree;
struct UIScene;
struct UIInventoryPool;

uint32_t
uitree_load_parse_item_kind(const char* str);

void
uitree_load_bind_item_name(struct CurrentLoad* load, const char* value);

void
uitree_load_finalize_uiscene_ids(struct GGame* game, struct UIScene* ui_scene);

/** Returns 0 on success, -1 when an asset is needed (fills *out_req). */
int
uitree_load_commit_revconfig_item(
    struct CurrentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req);

#endif /* UITREE_LOAD_BRIDGE_H */
