#include "osrs/revconfig/uitree_loader_game_executor.h"

#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_sprite(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;

    if( req->u.sprite.name[0] == '\0' )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;
    if( !game->gamecache )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    int e = gamecache_get_component_sprite_element_id(game->gamecache, req->u.sprite.name);
    req->game_binding.uiscene_element_id = e;
    if( e >= 0 )
        return UITREE_LOADER_GAME_EXECUTOR_OK;
    return UITREE_LOADER_GAME_EXECUTOR_NEED_2D_MEDIA;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_interface(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req || !game->gamecache )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;

    int n = req->u.interface_file.count;
    if( n < 0 || n > UITREE_MAX_INTERFACE_REQUESTS )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;
    if( n == 0 )
        return UITREE_LOADER_GAME_EXECUTOR_OK;

    struct GameCache* gc = game->gamecache;
    for( int i = 0; i < n; i++ )
    {
        int cid = req->u.interface_file.component_ids[i];
        if( cid < 0 )
            return UITREE_LOADER_GAME_EXECUTOR_ERROR;
        if( !gamecache_get_component(gc, cid) )
            return UITREE_LOADER_GAME_EXECUTOR_NEED_INTERFACES;
    }
    return UITREE_LOADER_GAME_EXECUTOR_OK;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_model(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req || !game->gamecache )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;

    int mid = req->u.model.model_id;
    if( mid < 0 )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;
    if( gamecache_get_model(game->gamecache, mid) )
        return UITREE_LOADER_GAME_EXECUTOR_OK;
    return UITREE_LOADER_GAME_EXECUTOR_NEED_MODEL;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_font(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;

    if( req->u.font.name[0] == '\0' )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;
    if( !game->gamecache )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    int fid = gamecache_get_font_ref_id(game->gamecache, req->u.font.name);
    if( fid >= 0 )
    {
        req->game_binding.uiscene_element_id = fid;
        return UITREE_LOADER_GAME_EXECUTOR_OK;
    }
    return UITREE_LOADER_GAME_EXECUTOR_NEED_FONT;
}
