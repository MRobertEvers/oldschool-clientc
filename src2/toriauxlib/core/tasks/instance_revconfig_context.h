#ifndef INSTANCE_REVCONFIG_CONTEXT_H
#define INSTANCE_REVCONFIG_CONTEXT_H

#include "3rd/minipt.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "ui/ui_sprite_lookup.h"
#include "ui/ui_font_lookup.h"
#include "ui/uitree.h"

#include <stdbool.h>
#include <stdint.h>

#define INSTANCE_RC_MAX_COMPONENTS 128
#define INSTANCE_RC_MAX_LAYOUTS 192
#define INSTANCE_RC_MAX_WORK_ITEMS 512
#define INSTANCE_RC_COMPONENT_STACK_MAX RS_COMPONENT_STACK_MAX
#define INSTANCE_RC_MAX_RS_SUBTREES 32
#define INSTANCE_RC_MAX_RS_SUBTREE_ITEMS 1024
/** Max per-interface file index when remapping dat2 packed component ids during bake. */
#define INSTANCE_RC_RS_FILE_INDEX_MAX 1024

struct InstanceRevConfigRSSubtree
{
    char owner_component[64];
    int component_ids[INSTANCE_RC_MAX_RS_SUBTREE_ITEMS];
    int item_count;
};

struct Dat1BuildCache;
struct Dat2BuildCache;
struct ToriDraw_Scene;
struct ToriAuxLibCore;
struct ToriAuxLibTD;
struct GameRunescape;

struct InstanceRevConfigContext
{
    enum ToriAuxLibCacheMode cache_mode;
    struct ToriAuxLibCache* cache;
    struct ToriDraw_Scene* scene;
    struct ToriAuxLibCore* core;
    struct ToriAuxLibTD* td;
    struct UITree* tree;
    struct GameRunescape* game;

    struct UISpriteLookup sprite_lookup;
    struct UIFontLookup font_lookup;
    int next_element_id;

    struct RevConfigUIComponentItem components[INSTANCE_RC_MAX_COMPONENTS];
    int component_count;

    struct RevConfigUILayoutItem layouts[INSTANCE_RC_MAX_LAYOUTS];
    int layout_count;
    int32_t layout_node_index[INSTANCE_RC_MAX_LAYOUTS];

    struct UIInventoryPool* inv_pool;

    struct InstanceRevConfigRSSubtree rs_subtrees[INSTANCE_RC_MAX_RS_SUBTREES];
    int rs_subtree_count;

    struct Dat1BuildCache* dat1_bc;
    struct Dat2BuildCache* dat2_bc;

    int panel_root_id[1024];
    bool jagfiles_ready;
    bool title_fonts_ready;
    bool rs_capture_enabled;
    char const* layout_group;
};

void
instance_revconfig_context_init(struct InstanceRevConfigContext* ctx);

struct RevConfigUIComponentItem const*
instance_revconfig_find_component(
    struct InstanceRevConfigContext const* ctx,
    char const* name);

int32_t
instance_revconfig_layout_parent_index(
    struct InstanceRevConfigContext const* ctx,
    char const* parent_name);

bool
instance_revconfig_build_tree(struct InstanceRevConfigContext* ctx);

void
instance_revconfig_resolve_panel_roots(struct InstanceRevConfigContext* ctx);

struct InstanceRevConfigRSSubtree*
instance_revconfig_rs_subtree_find(
    struct InstanceRevConfigContext* ctx,
    char const* owner_component);

struct InstanceRevConfigRSSubtree*
instance_revconfig_rs_subtree_get_or_create(
    struct InstanceRevConfigContext* ctx,
    char const* owner_component);

bool
instance_revconfig_rs_subtree_append(
    struct InstanceRevConfigRSSubtree* subtree,
    int component_id);

void
instance_revconfig_bake_rs_subtree(
    struct InstanceRevConfigContext* ctx,
    struct RevConfigUIComponentItem const* comp,
    int32_t owner_uitree_index);

void
instance_revconfig_context_release_build_state(struct InstanceRevConfigContext* ctx);

void
instance_revconfig_assert_fonts_in_scene(struct InstanceRevConfigContext* ctx);

#endif
