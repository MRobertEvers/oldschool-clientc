#include "osrs/revconfig/game_uitree_load_exec.h"

#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"

static int
resolve_sprite_element(
    struct GGame* game,
    const char* sprite_name)
{
    if( !sprite_name || sprite_name[0] == '\0' )
        return -1;
    if( game->gamecache )
    {
        int e = gamecache_get_component_sprite_element_id(game->gamecache, sprite_name);
        if( e >= 0 )
            return e;
    }
    if( game->buildcachedat )
        return buildcachedat_get_component_sprite_element_id(game->buildcachedat, sprite_name);
    return -1;
}

enum GameUitreeLoadExecResult
game_uitree_load_exec_sprite(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req )
        return GAME_UITREE_EXEC_ERROR;

    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;

    if( req->u.sprite.name[0] == '\0' )
        return GAME_UITREE_EXEC_ERROR;
    if( !game->buildcachedat )
        return GAME_UITREE_EXEC_ERROR;
    if( !game->buildcachedat->cfg_media_jagfile )
        return GAME_UITREE_EXEC_NEED_2D_MEDIA;

    buildcachedat_loader_load_component_sprite_lazy(
        game->buildcachedat, game->ui_scene, game, req->u.sprite.name);

    int e = resolve_sprite_element(game, req->u.sprite.name);
    req->game_binding.uiscene_element_id = e;
    return e >= 0 ? GAME_UITREE_EXEC_OK : GAME_UITREE_EXEC_ERROR;
}

enum GameUitreeLoadExecResult
game_uitree_load_exec_interface(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req || !game->gamecache )
        return GAME_UITREE_EXEC_ERROR;

    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;

    for( int i = 0; i < req->u.interface_file.count; i++ )
    {
        int cid = req->u.interface_file.component_ids[i];
        if( !gamecache_get_component(game->gamecache, cid) )
            return GAME_UITREE_EXEC_NEED_INTERFACES;
    }
    return GAME_UITREE_EXEC_OK;
}

enum GameUitreeLoadExecResult
game_uitree_load_exec_model(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req || !game->gamecache )
        return GAME_UITREE_EXEC_ERROR;

    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;

    if( gamecache_get_model(game->gamecache, req->u.model.model_id) )
        return GAME_UITREE_EXEC_OK;
    return GAME_UITREE_EXEC_NEED_MODEL;
}

enum GameUitreeLoadExecResult
game_uitree_load_exec_font(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    if( !game || !req )
        return GAME_UITREE_EXEC_ERROR;

    (void)req;
    req->game_binding.uiscene_element_id = -1;
    req->game_binding.inv_pool_index = -1;
    return GAME_UITREE_EXEC_NEED_FONT;
}
