#ifndef REVCONFIG_UI_BUILD_H
#define REVCONFIG_UI_BUILD_H

#include "revconfig/revconfig.h"
#include "ui/uitree.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <stdbool.h>
#include <stdint.h>

#define REVCONFIG_UI_MAX_SPRITE_REFS 256
#define REVCONFIG_UI_MAX_WORK_ITEMS 512
#define REVCONFIG_UI_DYNAMIC_SPRITE_MAX 512
#define REVCONFIG_UI_MAX_INV_ENTRIES 16

struct RevConfigUIDynamicSprite
{
    char name[128];
    int element_id;
};

struct RevConfigUISpriteRef
{
    char name[64];
    int element_id;
    struct RevConfigCacheItem cache_item;
};

struct RevConfigUIBuildState
{
    struct RevConfigUISpriteRef sprite_refs[REVCONFIG_UI_MAX_SPRITE_REFS];
    int sprite_ref_count;
    int next_element_id;

    struct RevConfigUIComponentItem components[128];
    int component_count;

    struct RevConfigUILayoutItem layout_entries[192];
    int layout_entry_count;
    int32_t layout_node_index[192];

    struct ToriAuxLibCore* core;
    struct ToriAuxLibCache* cache;
    struct Dat1BuildCache* bc;
    struct ToriDraw_Scene* scene;
    struct UIInventoryPool* inv_pool;

    struct RevConfigInvItem inv_entries[REVCONFIG_UI_MAX_INV_ENTRIES];
    int inv_entry_count;

    struct RevConfigUIDynamicSprite dynamic_sprites[REVCONFIG_UI_DYNAMIC_SPRITE_MAX];
    int dynamic_sprite_count;

    /** Maps Kronos panel iface id -> root LAYER component id in merged interfaces decode. */
    int panel_root_id[1024];
};

struct Dat1BuildCache;
struct ToriAuxLibCache;
struct ToriDraw_Scene;

void
revconfig_ui_build_set_expand_context(
    struct RevConfigUIBuildState* state,
    struct ToriAuxLibCache* cache,
    struct Dat1BuildCache* bc,
    struct ToriDraw_Scene* scene,
    struct UIInventoryPool* inv_pool);

void
revconfig_ui_build_populate_inv_pool(
    struct RevConfigUIBuildState* state,
    struct RevConfigItemBuffer const* items);

void
revconfig_ui_build_set_core(
    struct RevConfigUIBuildState* state,
    struct ToriAuxLibCore* core);

void
revconfig_ui_build_init(struct RevConfigUIBuildState* state);

void
revconfig_ui_build_collect_items(
    struct RevConfigUIBuildState* state,
    struct RevConfigItemBuffer const* items);

void
revconfig_ui_build_mark_needed_components(
    struct RevConfigUIBuildState const* state,
    bool* needed,
    int needed_count);

struct RevConfigUISpriteRef const*
revconfig_ui_build_find_ref(
    struct RevConfigUIBuildState const* state,
    char const* sprite_name);

int
revconfig_ui_build_lookup_sprite_id(
    struct RevConfigUIBuildState const* state,
    char const* sprite_name,
    int* out_atlas_index);

int32_t
revconfig_ui_build_node(
    struct RevConfigUIBuildState* state,
    struct UITree* tree,
    struct RevConfigUILayoutItem const* le,
    int32_t parent_index);

bool
revconfig_ui_build_tree(
    struct RevConfigUIBuildState* state,
    struct UITree* tree,
    char const* active_layout_group);

#endif
