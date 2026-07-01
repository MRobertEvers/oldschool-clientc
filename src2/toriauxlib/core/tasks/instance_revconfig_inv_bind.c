#include "instance_revconfig_inv_bind.h"

#include "games/runescape.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "ui/ui_inv_data_service.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <string.h>

static int const k_equipment_387_slot_files[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };
static int const k_equipment_387_slot_count =
    (int)(sizeof(k_equipment_387_slot_files) / sizeof(k_equipment_387_slot_files[0]));

int
instance_revconfig_inv_equipment_slot_index(int component_id)
{
    int const iface_id = component_id >> 16;
    int const file = component_id & 0xFFFF;
    if( iface_id != 387 )
        return -1;
    for( int i = 0; i < k_equipment_387_slot_count; i++ )
    {
        if( k_equipment_387_slot_files[i] == file )
            return i;
    }
    return -1;
}

bool
instance_revconfig_inv_is_equipment_slot_layer(int component_id)
{
    return instance_revconfig_inv_equipment_slot_index(component_id) >= 0;
}

int
instance_revconfig_inv_resolve_source(
    struct GameRunescape* game,
    char const* inv_name)
{
    assert(game && inv_name && inv_name[0] != '\0');
    return ui_inv_data_service_resolve_source(&game->inv_data, inv_name);
}

void
instance_revconfig_inv_seed_sources_from_pool(
    struct GameRunescape* game,
    struct InstanceRevConfigContext* ctx)
{
    if( !game || !ctx || !ctx->inv_pool )
        return;

    for( int i = 0; i < game->inv_data.source_count; i++ )
    {
        if( !game->inv_data.sources[i].used )
            continue;
        int pool_index =
            uitree_inv_pool_find_by_name(ctx->inv_pool, game->inv_data.sources[i].name);
        if( pool_index >= 0 )
            ui_inv_data_service_seed_from_pool(&game->inv_data, i, ctx->inv_pool, pool_index);
    }
}

static bool
subtree_has_core_inv(
    struct InstanceRevConfigContext* ctx,
    char const* owner_name)
{
    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_find(ctx, owner_name);
    if( !subtree || !ctx->core )
        return false;

    for( int i = 0; i < subtree->item_count; i++ )
    {
        struct ToriAuxLibCore_Component* info =
            ToriAuxLibCore_ComponentGet(ctx->core, subtree->component_ids[i]);
        if( info && info->type == TORIAUXLIBCORE_COMPONENT_INV )
            return true;
    }
    return false;
}

static int32_t
uitree_push_backpack_grid(
    struct UITree* tree,
    int32_t parent_index,
    int inv_source_id)
{
    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_INV_GRID;
    spec.component_id = -1;
    spec.always_dirty = 1;
    spec.u.inv_grid.inv_source_id = inv_source_id;
    spec.u.inv_grid.cols = 4;
    spec.u.inv_grid.rows = 7;
    spec.u.inv_grid.margin_x = 10;
    spec.u.inv_grid.margin_y = 4;
    spec.x = 0;
    spec.y = 0;
    spec.width = UITREE_SIDEBAR_PANEL_W;
    spec.height = UITREE_SIDEBAR_PANEL_H;
    return uitree_push(tree, parent_index, &spec);
}

void
instance_revconfig_inv_finalize_sidebar(
    enum ToriAuxLibCacheMode cache_mode,
    struct InstanceRevConfigContext* ctx,
    struct RevConfigUIComponentItem const* comp,
    int32_t sidebar_tree_index)
{
    assert(ctx && comp && ctx->tree && sidebar_tree_index >= 0);
    assert(cache_mode == TORIAUXLIBCACHE_MODE_DAT2);
    assert(strcmp(comp->type, "sidebar") == 0 && comp->componentno >= 0);

    struct GameRunescape* game = ctx->game;
    assert(game);

    if( comp->tabno == 3 && comp->componentno == 149 )
    {
        if( subtree_has_core_inv(ctx, comp->name) )
            return;

        int inv_source_id = UI_INV_SOURCE_INVALID;
        if( comp->inv[0] != '\0' )
            inv_source_id = instance_revconfig_inv_resolve_source(game, comp->inv);

        int32_t grid_idx = uitree_push_backpack_grid(ctx->tree, sidebar_tree_index, inv_source_id);
        if( grid_idx < 0 )
        {
            fprintf(
                stderr, "instance_revconfig_inv_finalize_sidebar: failed to push backpack grid\n");
            return;
        }
        fprintf(
            stderr,
            "instance_revconfig_inv_finalize_sidebar: iface 149 IF3 shell -> source %d grid at "
            "tree %d\n",
            inv_source_id,
            grid_idx);
        return;
    }

    if( comp->tabno == 4 && comp->componentno == 387 )
    {
        /* Register "worn" source (container 94); slot layers are baked as UIELEM_INV_SLOT.
         * Item icons are seeded from [inv:worn] via instance_revconfig_inv_seed_sources_from_pool.
         */
        if( comp->inv[0] != '\0' )
            (void)instance_revconfig_inv_resolve_source(game, comp->inv);
        else
            (void)instance_revconfig_inv_resolve_source(game, UI_INV_SOURCE_NAME_WORN);
    }
}

void
instance_revconfig_inv_mark_dirty(
    struct GameRunescape* game,
    int source_id)
{
    if( !game || !game->ui_tree || source_id < 0 )
        return;
    (void)source_id;
    uitree_mark_all_dirty(game->ui_tree);
}

void
instance_revconfig_inv_setup_after_build(
    struct GameRunescape* game,
    struct InstanceRevConfigContext* ctx)
{
    if( !game || !ctx || !ctx->tree || ctx->cache_mode != TORIAUXLIBCACHE_MODE_DAT2 )
        return;

    ui_inv_data_service_init(&game->inv_data);

    for( int i = 0; i < ctx->component_count; i++ )
    {
        struct RevConfigUIComponentItem const* comp = &ctx->components[i];
        if( strcmp(comp->type, "sidebar") != 0 )
            continue;

        int32_t sidebar_idx = -1;
        for( int li = 0; li < ctx->layout_count; li++ )
        {
            if( strcmp(ctx->layouts[li].component, comp->name) != 0 )
                continue;
            sidebar_idx = ctx->layout_node_index[li];
            break;
        }
        if( sidebar_idx < 0 )
            continue;

        instance_revconfig_inv_finalize_sidebar(ctx->cache_mode, ctx, comp, sidebar_idx);
    }

    /* Sources are registered during finalize (inv= resolve); seed after they exist. */
    instance_revconfig_inv_seed_sources_from_pool(game, ctx);

    for( int src = 0; src < game->inv_data.source_count; src++ )
    {
        if( !game->inv_data.sources[src].used )
            continue;
        GameRunescape_DispatchInvTransmit(game, game->inv_data.sources[src].container_id);
    }

    uitree_layout_resolve(ctx->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    uitree_mark_all_dirty(ctx->tree);
}
