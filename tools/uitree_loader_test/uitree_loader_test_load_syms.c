/*
 * Standalone uitree_loader_test links gamenet + Lua UI without uitree_load.c.
 * Provide the small uitree_load.h surface those TUs reference so the binary links.
 * Full game builds link uitree_load.c instead.
 */

#include "osrs/revconfig/uitree_load.h"

#include "osrs/revconfig/revconfig.h"

#include <stdlib.h>
#include <string.h>

enum uitree_loader_test_load_kind
{
    UITREE_LT_LK_NONE,
    UITREE_LT_LK_INV
};

static enum uitree_loader_test_load_kind
uitree_loader_test_load_kind(const char* str)
{
    if( !str )
        return UITREE_LT_LK_NONE;
    if( strcmp(str, "inv") == 0 )
        return UITREE_LT_LK_INV;
    return UITREE_LT_LK_NONE;
}

void
uitree_translate_agnostic_fields(
    struct UITree* tree,
    int32_t node_idx,
    struct GameCacheComponent* cc)
{
    (void)tree;
    (void)node_idx;
    (void)cc;
}

void
uitree_from_revconfig_buildcachedat(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer)
{
    (void)ui;
    (void)ui_scene;
    (void)inv_pool;
    (void)game;
    (void)revconfig_buffer;
}

int
uitree_revconfig_collect_inv_obj_ids(
    struct RevConfigBuffer* revconfig_buffer,
    int* out_ids,
    int max_ids)
{
    if( !revconfig_buffer || !out_ids || max_ids <= 0 )
        return 0;

    int n = 0;
    enum uitree_loader_test_load_kind current_kind = UITREE_LT_LK_NONE;

    for( uint32_t i = 0; i < revconfig_buffer->field_count; i++ )
    {
        struct RevConfigField* field = &revconfig_buffer->fields[i];
        if( field->kind == RCFIELD_ITEMTYPE )
        {
            current_kind = uitree_loader_test_load_kind(field->value);
        }
        else if( field->kind == RCFIELD_INV_ITEM && current_kind == UITREE_LT_LK_INV )
        {
            int id = atoi(field->value);
            int dup = 0;
            for( int j = 0; j < n; j++ )
            {
                if( out_ids[j] == id )
                {
                    dup = 1;
                    break;
                }
            }
            if( !dup && n < max_ids )
                out_ids[n++] = id;
        }
        else if( field->kind == RCFIELD_ITEMDONE )
        {
            current_kind = UITREE_LT_LK_NONE;
        }
    }
    return n;
}

void
uitree_load_inventories_from_revconfig(
    struct UIScene* ui_scene,
    struct GGame* game,
    struct UIInventoryPool* inv_pool,
    struct RevConfigBuffer* revconfig_buffer)
{
    (void)ui_scene;
    (void)game;
    (void)inv_pool;
    (void)revconfig_buffer;
}

void
uitree_load_ui_from_revconfig(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer)
{
    (void)ui;
    (void)ui_scene;
    (void)inv_pool;
    (void)game;
    (void)revconfig_buffer;
}

int
uitree_load_single_component_tree_from_gamecache(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache,
    int component_root_id)
{
    (void)game;
    (void)ui;
    (void)ui_scene;
    (void)gamecache;
    (void)component_root_id;
    return -1;
}

void
uitree_expand_chat_dialog_for_interface(struct GGame* game, int component_id)
{
    (void)game;
    (void)component_id;
}

void
uitree_expand_sidebar_overlay_for_interface(struct GGame* game, int component_id)
{
    (void)game;
    (void)component_id;
}

void
uitree_expand_viewport_overlay_for_interface(struct GGame* game, int component_id)
{
    (void)game;
    (void)component_id;
}

void
uitree_expand_sidebar_for_tab(struct GGame* game, int tabno, int component_id)
{
    (void)game;
    (void)tabno;
    (void)component_id;
}

void
uitree_rs_model_refresh_from_gamecache(struct GGame* game, int component_id)
{
    (void)game;
    (void)component_id;
}

void
uitree_rs_model_refresh_subtree_for_gamecache_root(struct GGame* game, int root_component_id)
{
    (void)game;
    (void)root_component_id;
}

void
uitree_debug_log_sidebar_state(
    struct GGame* game,
    char const* where,
    int tab_id,
    int component_id)
{
    (void)game;
    (void)where;
    (void)tab_id;
    (void)component_id;
}
