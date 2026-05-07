#ifndef UITREE_LOAD_PRIVATE_H
#define UITREE_LOAD_PRIVATE_H

/*
 * Internal declarations for uitree_load.c only (incremental loader uses
 * uitree_load_bridge.h instead).
 */

#include "osrs/revconfig/uitree_ini_load_state.h"
#include "osrs/revconfig/uitree_loader.h"

#include <stdint.h>

struct BuildCacheDat;
struct GameCache;
struct UIScene;
struct UIInventoryPool;
struct Scene2;
struct GGame;

uint32_t
uitree_impl_load_kind(const char* str);

void
uitree_impl_on_itemname(
    struct CurrentLoad* load,
    const char* value);

int
uitree_impl_load_sprite(
    struct SpriteLoad* load,
    struct DashMap* sprite_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat,
    struct UITreeLoaderAssetRequest* out_req);

int
uitree_impl_load_component(
    struct ComponentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache,
    struct UITreeLoaderAssetRequest* out_req);

int
uitree_impl_load_layout(
    struct LayoutLoad* load,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req);

int
uitree_impl_load_inv(
    struct InvLoad* il,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UIScene* ui_scene,
    struct UITreeLoaderAssetRequest* out_req);

int
uitree_impl_load_item(
    struct CurrentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req);

void
uitree_impl_resolve_game_uiscene_sprite_ids(
    struct GGame* game,
    struct UIScene* ui_scene);

#endif /* UITREE_LOAD_PRIVATE_H */
