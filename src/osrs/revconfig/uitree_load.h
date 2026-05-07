#ifndef UITREE_LOAD_H
#define UITREE_LOAD_H

#include "osrs/buildcachedat.h"
#include "osrs/gamecache/gamecache.h"
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
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer);

/** Rebuild IF_OPENCHAT interface under UIELEM_BUILTIN_CHAT_DIALOG. component_id < 0 clears only. */
void
uitree_expand_chat_dialog_for_interface(struct GGame* game, int component_id);

/** Rebuild IF_OPENSIDE interface under UIELEM_BUILTIN_SIDEBAR_OVERLAY. component_id < 0 clears only. */
void
uitree_expand_sidebar_overlay_for_interface(struct GGame* game, int component_id);

/** Clear sidebar RS subtree and rebuild from component_id (buildcachedat). No-op if no sidebar
 * node for tabno. component_id < 0 clears only. */
void
uitree_expand_sidebar_for_tab(
    struct GGame* game,
    int tabno,
    int component_id);

/** Rebuild Scene2 model for a UIELEM_RS_MODEL node after IF_SETOBJECT / IF_SETMODEL / head packets. */
void
uitree_rs_model_refresh_from_gamecache(struct GGame* game, int component_id);

/** When TORI_SIDEBAR_DEBUG is set in the environment, log iface + sidebar UITree slot for tab_id.
 * component_id is the wire/root id for SETTAB, or -1 for SETTAB_ACTIVE-only logs. */
void
uitree_debug_log_sidebar_state(
    struct GGame* game,
    char const* where,
    int tab_id,
    int component_id);

// /** Push RS `StaticUIComponent` rows and register sprites/models in ui_scene / scene2. */
// void
// static_ui_rs_from_buildcachedat(p
//     struct UITree* ui,
//     struct UIScene* ui_scene,
//     struct Scene2* scene2,
//     struct BuildCacheDat* buildcachedat);

#endif