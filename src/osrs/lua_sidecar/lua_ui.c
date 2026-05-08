#include "lua_ui.h"

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/lua_sidecar/lua_configfile.h"
#include "osrs/obj_icon.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/revconfig_load.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/revconfig/uitree_load.h"
#include "osrs/revconfig/uitree_loader.h"
#include "osrs/rs_component_state.h"
#include "osrs/world.h"
#include "tori_rs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: get userdata at args[i]. */
static void*
arg_userdata(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetUserData(elem);
}

/** Replace UIScene; salvage fonts, reset BuildCacheDat reftables tied to element/font ids, reload
 * component sprites. Returns -1 if uiscene_new fails. */
static int
lua_ui_reset_uiscene_and_refs(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat)
{
    if( !game->ui_scene )
        return 0;

    int cap = game->ui_scene->elements_count;
    /* RS revconfig + interface sprites (tabs, inv, etc.) need far more than 512 slots once
     * BuildCacheDat keeps decoded interfaces and uitree expands sidebar content. */
    int new_cap = cap * 2;
    if( new_cap < 4096 )
        new_cap = 4096;

    struct UIScene* old = game->ui_scene;
    struct DashPixFont* saved_fonts[UISCENE_FONT_MAX];
    char saved_names[UISCENE_FONT_MAX][UISCENE_FONT_NAME_MAX];
    int saved_count = old->font_count;
    if( saved_count > UISCENE_FONT_MAX )
        saved_count = UISCENE_FONT_MAX;
    for( int i = 0; i < saved_count; i++ )
    {
        saved_fonts[i] = old->fonts[i].font;
        memcpy(saved_names[i], old->fonts[i].name, UISCENE_FONT_NAME_MAX);
        /* UIScene owns fonts; uiscene_free always frees non-NULL slots — transfer out first. */
        old->fonts[i].font = NULL;
    }

    uiscene_free(old);
    game->ui_scene = uiscene_new(new_cap);
    if( !game->ui_scene )
        return -1;

    buildcachedat_reset_uiscene_linked_reftables(buildcachedat);

    for( int i = 0; i < saved_count; i++ )
    {
        if( !saved_fonts[i] )
            continue;
        int fid = uiscene_font_add(game->ui_scene, saved_names[i], saved_fonts[i]);
        if( fid >= 0 )
            buildcachedat_add_font_ref(buildcachedat, saved_names[i], fid);
    }

    buildcachedat_loader_load_component_sprites_from_media(buildcachedat, game);
    return 0;
}

struct LuaGameType*
LuaUI_load_revconfig(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    assert(game && buildcachedat);
    assert(args);
    struct LuaConfigFile* ui_config = (struct LuaConfigFile*)arg_userdata(args, 0);
    struct LuaConfigFile* cache_config = (struct LuaConfigFile*)arg_userdata(args, 1);

    // Reset UI state (best-effort) before reloading.
    if( game->ui_root_buffer )
    {
        game->ui_root_buffer->component_count = 0;
        game->ui_root_buffer->root_index = -1;
    }
    if( game->inv_pool )
        uitree_inv_pool_free(game->inv_pool);
    game->inv_pool = uitree_inv_pool_new(32);
    if( !game->inv_pool )
        return LuaGameType_NewVoid();
    if( lua_ui_reset_uiscene_and_refs(game, buildcachedat) != 0 )
        return LuaGameType_NewVoid();

    struct RevConfigBuffer* buf = revconfig_buffer_new(64);
    if( !buf )
        return LuaGameType_NewVoid();

    assert(cache_config);
    revconfig_load_fields_from_ini_bytes(cache_config->data, (uint32_t)cache_config->size, buf);

    assert(ui_config);
    revconfig_load_fields_from_ini_bytes(ui_config->data, (uint32_t)ui_config->size, buf);

    if( game->ui_root_buffer && game->ui_scene )
        uitree_from_revconfig_buildcachedat(
            game->ui_root_buffer, game->ui_scene, game->inv_pool, game, buf);

    revconfig_buffer_free(buf);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_load_fonts(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->ui_scene || !buildcachedat || !game->gamecache )
        return LuaGameType_NewVoid();

    struct DashMapIter* iter = gamecache_iter_new_fonts_reftable(game->gamecache);
    char name_buf[BUILDCACHEDAT_FONT_NAME_MAX + 1];
    int ref_slot = 0;
    while( gamecache_iter_next_font_ref(iter, name_buf, (int)sizeof(name_buf), &ref_slot) )
    {
        struct DashPixFont* font = uiscene_font_get(game->ui_scene, ref_slot);
        if( font )
            uiscene_font_add(game->ui_scene, name_buf, font);
    }
    dashmap_iter_free(iter);

    /* Ensure default interface fonts exist in UIScene for RS text (p11/p12/b12/q8). */
    static char const* const required_fonts[] = { "p11", "p12", "b12", "q8" };
    for( int i = 0; i < 4; i++ )
    {
        char const* nm = required_fonts[i];
        if( uiscene_font_find_id(game->ui_scene, nm) >= 0 )
            continue;
        int sid = gamecache_get_font_ref_id(game->gamecache, nm);
        if( sid < 0 )
            continue;
        struct DashPixFont* f = uiscene_font_get(game->ui_scene, sid);
        if( f )
            uiscene_font_add(game->ui_scene, nm, f);
    }

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_load_rs_components(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->ui_scene || !game->ui_root_buffer || !buildcachedat )
        return LuaGameType_NewVoid();

    if( game->rs_component_state )
    {
        rs_component_state_pool_free(game->rs_component_state);
        game->rs_component_state = NULL;
    }

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_resolve_inv_sprites(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)buildcachedat;
    (void)args;
    if( !game || !game->inv_pool || !game->ui_scene )
        return LuaGameType_NewVoid();

    struct UIInventoryPool* pool = game->inv_pool;
    struct UIScene* ui_scene = game->ui_scene;

    for( int inv_i = 0; inv_i < pool->count; inv_i++ )
    {
        struct UIInventory* inv = &pool->inventories[inv_i];
        for( int it = 0; it < inv->item_count; it++ )
        {
            struct UIInventoryItem* item = &inv->items[it];
            if( item->scene_id >= 0 || item->obj_id <= 0 )
                continue;

            int obj_lookup = item->obj_id - 1;
            struct DashSprite* cached = obj_icon_get(game, obj_lookup, 1);
            if( !cached )
                continue;
            struct DashSprite* sp = dashsprite_clone(cached);
            if( !sp )
                continue;

            struct DashSprite** arr = malloc(sizeof(struct DashSprite*));
            if( !arr )
            {
                dashsprite_free(sp);
                continue;
            }
            arr[0] = sp;

            int eid = uiscene_element_acquire(ui_scene, -1);
            if( eid < 0 )
            {
                free(arr);
                dashsprite_free(sp);
                continue;
            }

            struct UISceneElement* el = uiscene_element_at(ui_scene, eid);
            if( !el )
            {
                free(arr);
                dashsprite_free(sp);
                continue;
            }

            el->dash_sprites = arr;
            el->dash_sprites_count = 1;
            item->scene_id = eid;
            item->atlas_index = 0;
        }
    }

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_parse_revconfig(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    assert(game && buildcachedat);
    assert(args);
    struct LuaConfigFile* ui_config = (struct LuaConfigFile*)arg_userdata(args, 0);
    struct LuaConfigFile* cache_config = (struct LuaConfigFile*)arg_userdata(args, 1);

    if( game->ui_root_buffer )
    {
        game->ui_root_buffer->component_count = 0;
        game->ui_root_buffer->root_index = -1;
    }
    if( game->inv_pool )
        uitree_inv_pool_free(game->inv_pool);
    game->inv_pool = uitree_inv_pool_new(32);
    if( !game->inv_pool )
        return LuaGameType_NewVoid();
    if( lua_ui_reset_uiscene_and_refs(game, buildcachedat) != 0 )
        return LuaGameType_NewVoid();

    if( game->pending_revconfig )
        revconfig_buffer_free(game->pending_revconfig);

    struct RevConfigBuffer* buf = revconfig_buffer_new(64);
    if( !buf )
        return LuaGameType_NewVoid();

    assert(cache_config);
    revconfig_load_fields_from_ini_bytes(cache_config->data, (uint32_t)cache_config->size, buf);
    assert(ui_config);
    revconfig_load_fields_from_ini_bytes(ui_config->data, (uint32_t)ui_config->size, buf);

    game->pending_revconfig = buf;
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_get_revconfig_inv_obj_ids(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)buildcachedat;
    (void)args;
    if( !game || !game->pending_revconfig )
        return LuaGameType_NewIntArray(0);

    enum
    {
        kMaxIds = 4096
    };
    int tmp[kMaxIds];
    int n = uitree_revconfig_collect_inv_obj_ids(game->pending_revconfig, tmp, kMaxIds);

    struct LuaGameType* arr = LuaGameType_NewIntArray(n);
    for( int i = 0; i < n; i++ )
        LuaGameType_IntArrayPush(arr, tmp[i]);
    return arr;
}

struct LuaGameType*
LuaUI_load_revconfig_inventories(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)buildcachedat;
    (void)args;
    if( !game || !game->pending_revconfig || !game->inv_pool || !game->ui_scene )
        return LuaGameType_NewVoid();

    uitree_load_inventories_from_revconfig(
        game->ui_scene, game, game->inv_pool, game->pending_revconfig);

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_load_revconfig_ui(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)buildcachedat;
    (void)args;
    if( !game || !game->pending_revconfig || !game->ui_root_buffer || !game->ui_scene )
    {
        if( game && game->pending_revconfig )
        {
            revconfig_buffer_free(game->pending_revconfig);
            game->pending_revconfig = NULL;
        }
        return LuaGameType_NewVoid();
    }

    /* Free any stale loader from a previous (possibly interrupted) load. */
    if( game->pending_uitree_loader )
    {
        uitree_loader_free(game->pending_uitree_loader);
        game->pending_uitree_loader = NULL;
    }

    /* Create the incremental loader.  The revconfig buffer is consumed here;
     * step_loader() drives it to completion. */
    // game->pending_uitree_loader = uitree_loader_new(game->ui_root_buffer,
    // game->pending_revconfig);

    return LuaGameType_NewVoid();
}

/** VarTypeArray { kind_string, payload } for one UITreeLoaderAssetRequest. */
static struct LuaGameType*
LuaUI_asset_descriptor_from_request(const struct UITreeLoaderAssetRequest* req)
{
    struct LuaGameType* arr = LuaGameType_NewVarTypeArray(2);

    static const char* kind_names[] = {
        [UITREE_ASSET_NONE] = "none",
        [UITREE_ASSET_SPRITE] = "sprite",
        [UITREE_ASSET_RS_COMPONENT] = "rs_component",
        [UITREE_ASSET_MODEL] = "model",
        [UITREE_ASSET_FONT] = "font",
    };
    int kind_idx = (int)req->kind;
    const char* kind_name =
        (kind_idx >= 0 && kind_idx < (int)(sizeof(kind_names) / sizeof(kind_names[0])))
            ? kind_names[kind_idx]
            : "unknown";

    LuaGameType_VarTypeArrayPush(
        arr, LuaGameType_NewString((char*)kind_name, (int)strlen(kind_name)));

    switch( req->kind )
    {
    case UITREE_ASSET_SPRITE:
        LuaGameType_VarTypeArrayPush(
            arr, LuaGameType_NewString(req->u.sprite.name, (int)strlen(req->u.sprite.name)));
        break;
    case UITREE_ASSET_RS_COMPONENT:
    {
        int n = req->u.rs_component.resolve.children_count;
        if( n <= 0 )
        {
            LuaGameType_VarTypeArrayPush(arr, LuaGameType_NewInt(0));
            break;
        }
        struct LuaGameType* id_arr = LuaGameType_NewVarTypeArray(n);
        for( int i = 0; i < n; i++ )
            LuaGameType_VarTypeArrayPush(
                id_arr, LuaGameType_NewInt(req->u.rs_component.resolve.children[i]));
        LuaGameType_VarTypeArrayPush(arr, id_arr);
        break;
    }
    case UITREE_ASSET_MODEL:
        LuaGameType_VarTypeArrayPush(arr, LuaGameType_NewInt(req->u.rs_model.model_id));
        break;
    case UITREE_ASSET_FONT:
        LuaGameType_VarTypeArrayPush(
            arr, LuaGameType_NewString(req->u.font.name, (int)strlen(req->u.font.name)));
        break;
    default:
        LuaGameType_VarTypeArrayPush(arr, LuaGameType_NewInt(0));
        break;
    }

    return arr;
}

struct LuaGameType*
LuaUI_step_loader(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)buildcachedat;
    (void)args;

    if( !game || !game->pending_uitree_loader )
        return LuaGameType_NewVoid(); /* nil — loader not active */

    /* Drain the loader until it needs an asset or finishes. */
    enum UITreeLoaderStatus status;
    // do
    // {
    //     status = uitree_loader_step(game->pending_uitree_loader, game->pending_revconfig);
    // } while( status == UITREE_LOADER_RUNNING );

    if( status == UITREE_LOADER_DONE )
    {
        uitree_loader_free(game->pending_uitree_loader);
        game->pending_uitree_loader = NULL;

        /* Free the revconfig buffer now that loading is complete. */
        if( game->pending_revconfig )
        {
            revconfig_buffer_free(game->pending_revconfig);
            game->pending_revconfig = NULL;
        }

        if( game->world && game->world->minimap )
            LibToriRS_WorldMinimapStaticRebuild(game);

        return LuaGameType_NewVoid(); /* nil — done */
    }

    if( status == UITREE_LOADER_ERROR )
    {
        uitree_loader_free(game->pending_uitree_loader);
        game->pending_uitree_loader = NULL;
        if( game->pending_revconfig )
        {
            revconfig_buffer_free(game->pending_revconfig);
            game->pending_revconfig = NULL;
        }
        static char err_str[] = "error";
        return LuaGameType_NewString(err_str, (int)(sizeof(err_str) - 1));
    }

    /* UITREE_LOADER_NEEDS_ASSET — return descriptor(s) to Lua.
     * One request: VarTypeArray { kind_string, payload } (unchanged shape for Lua).
     * Several: outer VarTypeArray of those pairs. */
    int n = uitree_loader_pending_asset_count(game->pending_uitree_loader);
    const struct UITreeLoaderAssetRequest* reqs =
        uitree_loader_pending_assets(game->pending_uitree_loader);
    if( !reqs || n <= 0 )
        return LuaGameType_NewVarTypeArray(0);

    if( n == 1 )
        return LuaUI_asset_descriptor_from_request(&reqs[0]);

    struct LuaGameType* outer = LuaGameType_NewVarTypeArray(n);
    for( int i = 0; i < n; i++ )
        LuaGameType_VarTypeArrayPush(outer, LuaUI_asset_descriptor_from_request(&reqs[i]));
    return outer;
}
