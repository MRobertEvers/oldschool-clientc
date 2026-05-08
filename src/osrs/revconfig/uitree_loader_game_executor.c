#include "osrs/revconfig/uitree_loader_game_executor.h"

#include "osrs/buildcachedat_loader.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/gamecache/gamecache_from_buildcachedat.h"
#include "osrs/revconfig/uiscene.h"

static void
uitree_loader_game_executor_clear_binding(struct UITreeLoaderAssetRequest* req)
{
    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;
    req->binding_ready = 0;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_sprite(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    uitree_loader_game_executor_clear_binding(req);

    if( req->u.sprite.name[0] == '\0' )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;
    if( !game->gamecache )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    int e = gamecache_get_component_sprite_element_id(game->gamecache, req->u.sprite.name);
    if( e >= 0 )
    {
        req->game_binding.uiscene_element_id = e;
        req->binding_ready = 1;
        return UITREE_LOADER_GAME_EXECUTOR_OK;
    }

    if( game->buildcachedat && game->ui_scene )
    {
        buildcachedat_loader_load_component_sprite_lazy(
            game->buildcachedat, game->ui_scene, game, req->u.sprite.name);
        gamecache_convert_reftables_from_buildcachedat(game->gamecache, game->buildcachedat);
        e = gamecache_get_component_sprite_element_id(game->gamecache, req->u.sprite.name);
        if( e >= 0 )
        {
            req->game_binding.uiscene_element_id = e;
            req->binding_ready = 1;
            return UITREE_LOADER_GAME_EXECUTOR_OK;
        }
    }

    return UITREE_LOADER_GAME_EXECUTOR_NEED_2D_MEDIA;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_interface(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req || !game->gamecache )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    uitree_loader_game_executor_clear_binding(req);

    int n = req->u.interface_file.count;
    if( n < 0 || n > UITREE_MAX_INTERFACE_REQUESTS )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;
    if( n == 0 )
    {
        req->binding_ready = 1;
        return UITREE_LOADER_GAME_EXECUTOR_OK;
    }

    struct GameCache* gc = game->gamecache;
    for( int i = 0; i < n; i++ )
    {
        int cid = req->u.interface_file.component_ids[i];
        if( cid < 0 )
            return UITREE_LOADER_GAME_EXECUTOR_ERROR;
        if( !gamecache_get_component(gc, cid) )
            return UITREE_LOADER_GAME_EXECUTOR_NEED_INTERFACES;
    }

    if( game->buildcachedat && game->ui_scene )
    {
        for( int i = 0; i < n; i++ )
        {
            buildcachedat_loader_ensure_component_graphics_for_gamecache_interface(
                game->buildcachedat,
                game->ui_scene,
                game,
                gc,
                req->u.interface_file.component_ids[i]);
        }
        gamecache_convert_reftables_from_buildcachedat(gc, game->buildcachedat);
    }

    req->binding_ready = 1;
    return UITREE_LOADER_GAME_EXECUTOR_OK;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_model(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req || !game->gamecache )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    uitree_loader_game_executor_clear_binding(req);

    int mid = req->u.model.model_id;
    if( mid < 0 )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;
    if( gamecache_get_model(game->gamecache, mid) )
    {
        req->binding_ready = 1;
        return UITREE_LOADER_GAME_EXECUTOR_OK;
    }
    return UITREE_LOADER_GAME_EXECUTOR_NEED_MODEL;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_font(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req || !game->gamecache || !game->ui_scene )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    uitree_loader_game_executor_clear_binding(req);

    if( req->u.font.name[0] == '\0' )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    struct UIScene* scene = game->ui_scene;
    int fid = uiscene_font_find_id(scene, req->u.font.name);
    if( fid < 0 )
        fid = gamecache_get_font_ref_id(game->gamecache, req->u.font.name);

    if( fid >= 0 && uiscene_font_get(scene, fid) )
    {
        req->game_binding.uiscene_element_id = fid;
        req->binding_ready = 1;
        return UITREE_LOADER_GAME_EXECUTOR_OK;
    }

    return UITREE_LOADER_GAME_EXECUTOR_NEED_FONT;
}
