#include "instance_revconfig_inv_bind.h"

#include "games/runescape.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "ui/ui_behavior.h"
#include "ui/ui_inv_data_service.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <string.h>

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
    assert(game && ctx && ctx->inv_pool);
    for( int i = 0; i < game->inv_data.source_count; i++ )
    {
        assert(game->inv_data.sources[i].used);
        int pool_index =
            uitree_inv_pool_find_by_name(ctx->inv_pool, game->inv_data.sources[i].name);
        if( pool_index >= 0 )
            ui_inv_data_service_seed_from_pool(&game->inv_data, i, ctx->inv_pool, pool_index);
    }
}

void
instance_revconfig_inv_finalize_sidebar(
    enum ToriAuxLibCacheMode cache_mode,
    struct InstanceRevConfigContext* ctx,
    struct RevConfigUIComponentItem const* comp,
    int32_t sidebar_tree_index)
{
    (void)sidebar_tree_index;
    assert(ctx && comp && ctx->tree);
    assert(cache_mode == TORIAUXLIBCACHE_MODE_DAT2);
    assert(strcmp(comp->type, "sidebar") == 0 && comp->componentno >= 0);

    struct GameRunescape* game = ctx->game;
    assert(game);

    if( comp->inv[0] != '\0' )
        (void)instance_revconfig_inv_resolve_source(game, comp->inv);
}

void
instance_revconfig_inv_mark_dirty(
    struct GameRunescape* game,
    int source_id)
{
    assert(game && game->ui_tree && source_id >= 0);
    (void)source_id;
    uitree_mark_all_dirty(game->ui_tree);
}

void
instance_revconfig_inv_setup_after_build(
    struct GameRunescape* game,
    struct InstanceRevConfigContext* ctx)
{
    assert(game && ctx && ctx->tree && ctx->cache_mode == TORIAUXLIBCACHE_MODE_DAT2);

    ui_inv_data_service_init(&game->inv_data);

    for( int i = 0; i < ctx->component_count; i++ )
    {
        struct RevConfigUIComponentItem const* comp = &ctx->components[i];
        if( comp->inv[0] == '\0' )
            continue;
        (void)instance_revconfig_inv_resolve_source(game, comp->inv);
    }

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

    instance_revconfig_inv_seed_sources_from_pool(game, ctx);

    uitree_layout_resolve(ctx->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    GameRunescape_RunOnLoadHooks(game);

    for( int src = 0; src < game->inv_data.source_count; src++ )
    {
        if( !game->inv_data.sources[src].used )
            continue;
        GameRunescape_DispatchInvTransmit(game, game->inv_data.sources[src].container_id);
    }

    uitree_mark_all_dirty(ctx->tree);
}
