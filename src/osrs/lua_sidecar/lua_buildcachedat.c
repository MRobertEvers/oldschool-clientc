#include "lua_buildcachedat.h"

#include "lua_gametypes.h"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/datatypes/appearances.h"
#include "osrs/datatypes/player_appearance.h"
#include "osrs/game.h"
#include "osrs/packets/pkt_npc_info.h"
#include "osrs/packets/pkt_player_info.h"
#include "osrs/rscache/cache_dat.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Helper: get int at args[i]. args must be VarTypeArray. */
static int
arg_int(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetInt(elem);
}

/* Helper: get userdata at args[i]. */
static void*
arg_userdata(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetUserData(elem);
}

/* Helper: get GGame* from args[0]. Used when function needs game. */
static struct GGame*
arg_game(struct LuaGameType* args)
{
    return (struct GGame*)arg_userdata(args, 0);
}

struct LuaGameType*
LuaBuildCacheDat_cache_map_scenery(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);
    int map_id = arg_int(args, 1);

    buildcachedat_loader_cache_map_scenery_mapid(
        buildcachedat, map_id, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_set_2d_media_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);
    buildcachedat_loader_set_2d_media_jagfile(buildcachedat, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);

    buildcachedat_loader_set_config_jagfile(buildcachedat, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_init_varp_varbit_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    // struct GGame* game = arg_game(args);
    // buildcachedat_loader_init_varp_varbit(buildcachedat, game);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);

    buildcachedat_loader_set_versionlist_jagfile(buildcachedat, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_cache_map_terrain(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);
    int map_id = arg_int(args, 1);

    buildcachedat_loader_cache_map_terrain_mapid(
        buildcachedat, map_id, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_has_map_terrain(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int chunk_x = arg_int(args, 0);
    int chunk_z = arg_int(args, 1);
    struct CacheMapTerrain* t = buildcachedat_get_map_terrain(buildcachedat, chunk_x, chunk_z);
    return LuaGameType_NewBool(t != NULL);
}

struct LuaGameType*
LuaBuildCacheDat_has_map_scenery(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int chunk_x = arg_int(args, 0);
    int chunk_z = arg_int(args, 1);
    struct CacheMapLocs* s = buildcachedat_get_scenery(buildcachedat, chunk_x, chunk_z);
    return LuaGameType_NewBool(s != NULL);
}

struct LuaGameType*
LuaBuildCacheDat_has_model(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int model_id = arg_int(args, 0);
    struct CacheModel* m = buildcachedat_get_model(buildcachedat, model_id);
    return LuaGameType_NewBool(m != NULL);
}

struct LuaGameType*
LuaBuildCacheDat_has_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int animbaseframes_id = arg_int(args, 0);
    struct CacheDatAnimBaseFrames* ab =
        buildcachedat_get_animbaseframes(buildcachedat, animbaseframes_id);
    return LuaGameType_NewBool(ab != NULL);
}

struct LuaGameType*
LuaBuildCacheDat_init_floortypes_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_floortypes_from_config_jagfile(buildcachedat);

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_init_scenery_configs_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_scenery_configs_from_config_jagfile(buildcachedat);

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_get_all_scenery_locs(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    int* loc_ids = NULL;
    int* chunk_x = NULL;
    int* chunk_z = NULL;

    int count =
        buildcachedat_loader_get_all_scenery_locs(buildcachedat, &loc_ids, &chunk_x, &chunk_z);

    struct LuaGameType* result = LuaGameType_NewVarTypeArray(count > 0 ? count : 4);
    if( !result )
    {
        free(loc_ids);
        free(chunk_x);
        free(chunk_z);
        return NULL;
    }

    for( int i = 0; i < count; i++ )
    {
        int* row = malloc(3 * sizeof(int));
        if( !row )
        {
            LuaGameType_Free(result);
            free(loc_ids);
            free(chunk_x);
            free(chunk_z);
            return NULL;
        }
        row[0] = loc_ids[i];
        row[1] = chunk_x[i];
        row[2] = chunk_z[i];
        struct LuaGameType* elem = LuaGameType_NewIntArray(3);
        for( int j = 0; j < 3; j++ )
            LuaGameType_IntArrayPush(elem, row[j]);
        LuaGameType_VarTypeArrayPush(result, elem);
        free(row);
    }

    free(loc_ids);
    free(chunk_x);
    free(chunk_z);

    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_scenery_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int loc_id = arg_int(args, 0);

    int* model_ids = NULL;
    int count = buildcachedat_loader_get_scenery_model_ids(buildcachedat, loc_id, &model_ids);

    struct LuaGameType* result = LuaGameType_NewIntArray(count);
    for( int i = 0; i < count; i++ )
        LuaGameType_IntArrayPush(result, model_ids[i]);
    free(model_ids);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_all_unique_scenery_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    int* model_ids = NULL;
    int count = buildcachedat_loader_get_all_unique_scenery_model_ids(buildcachedat, &model_ids);

    struct LuaGameType* result = LuaGameType_NewIntArray(count);
    for( int i = 0; i < count; i++ )
        LuaGameType_IntArrayPush(result, model_ids[i]);
    free(model_ids);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_npc_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int npc_id = arg_int(args, 0);

    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(buildcachedat, npc_id);
    if( !npc || !npc->models )
        return LuaGameType_NewIntArray(0);

    struct LuaGameType* result = LuaGameType_NewIntArray(npc->models_count);
    for( int i = 0; i < npc->models_count; i++ )
        LuaGameType_IntArrayPush(result, npc->models[i]);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_npc_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int npc_id = arg_int(args, 0);

    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(buildcachedat, npc_id);
    if( !npc || !npc->heads )
        return LuaGameType_NewIntArray(0);

    int idx = 0;
    int* head_ids = malloc((size_t)npc->heads_count * sizeof(int));
    if( !head_ids )
        return NULL;
    for( int i = 0; i < npc->heads_count; i++ )
    {
        if( npc->heads[i] != -1 )
            head_ids[idx++] = npc->heads[i];
    }
    struct LuaGameType* result = LuaGameType_NewIntArray(idx);
    for( int i = 0; i < idx; i++ )
        LuaGameType_IntArrayPush(result, head_ids[i]);
    free(head_ids);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_idk_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int idk_id = arg_int(args, 0);

    struct CacheDatConfigIdk* idk = buildcachedat_get_idk(buildcachedat, idk_id);
    if( !idk || !idk->models )
        return LuaGameType_NewIntArray(0);

    struct LuaGameType* result = LuaGameType_NewIntArray(idk->models_count);
    for( int i = 0; i < idk->models_count; i++ )
        LuaGameType_IntArrayPush(result, idk->models[i]);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_idk_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int idk_id = arg_int(args, 0);

    struct CacheDatConfigIdk* idk = buildcachedat_get_idk(buildcachedat, idk_id);
    if( !idk )
        return LuaGameType_NewIntArray(0);

    struct LuaGameType* result = LuaGameType_NewIntArray(10);
    for( int i = 0; i < 10; i++ )
        if( idk->heads[i] != -1 && idk->heads[i] != 0 )
            LuaGameType_IntArrayPush(result, idk->heads[i]);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_obj_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int obj_id = arg_int(args, 0);

    struct CacheDatConfigObj* obj = buildcachedat_get_obj(buildcachedat, obj_id);
    if( !obj )
        return LuaGameType_NewIntArray(0);

    struct LuaGameType* result = LuaGameType_NewIntArray(4);
    if( obj->model != -1 )
        LuaGameType_IntArrayPush(result, obj->model);
    if( obj->manwear != -1 )
        LuaGameType_IntArrayPush(result, obj->manwear);
    if( obj->manwear2 != -1 )
        LuaGameType_IntArrayPush(result, obj->manwear2);
    if( obj->manwear3 != -1 )
        LuaGameType_IntArrayPush(result, obj->manwear3);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_obj_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int obj_id = arg_int(args, 0);

    struct CacheDatConfigObj* obj = buildcachedat_get_obj(buildcachedat, obj_id);
    if( !obj )
        return LuaGameType_NewIntArray(0);

    struct LuaGameType* result = LuaGameType_NewIntArray(4);
    if( obj->manhead != -1 )
        LuaGameType_IntArrayPush(result, obj->manhead);
    if( obj->manhead2 != -1 )
        LuaGameType_IntArrayPush(result, obj->manhead2);
    if( obj->womanhead != -1 )
        LuaGameType_IntArrayPush(result, obj->womanhead);
    if( obj->womanhead2 != -1 )
        LuaGameType_IntArrayPush(result, obj->womanhead2);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_obj(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int obj_id = arg_int(args, 0);

    struct CacheDatConfigObj* obj = buildcachedat_get_obj(buildcachedat, obj_id);
    if( !obj )
        return LuaGameType_NewVoid();

    /* Return VarTypeArray [model, name, manwear, manwear2, manwear3] */
    struct LuaGameType* result = LuaGameType_NewVarTypeArray(5);
    if( !result )
        return NULL;

    char* name_copy = obj->name ? strdup(obj->name) : strdup("");
    if( !name_copy )
    {
        LuaGameType_Free(result);
        return NULL;
    }
    int name_len = (int)strlen(name_copy);

    LuaGameType_VarTypeArrayPush(result, LuaGameType_NewInt(obj->model));
    LuaGameType_VarTypeArrayPush(result, LuaGameType_NewString(name_copy, name_len));
    LuaGameType_VarTypeArrayPush(result, LuaGameType_NewInt(obj->manwear));
    LuaGameType_VarTypeArrayPush(result, LuaGameType_NewInt(obj->manwear2));
    LuaGameType_VarTypeArrayPush(result, LuaGameType_NewInt(obj->manwear3));

    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_player_appearance_ids_from_packet(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    void* data = arg_userdata(args, 0);
    int length = arg_int(args, 1);

    struct LuaGameType* idk_ids = LuaGameType_NewIntArray(10);
    struct LuaGameType* obj_ids = LuaGameType_NewIntArray(10);

    static struct PktPlayerInfoReader reader;
    reader.extended_count = 0;
    reader.current_op = 0;
    reader.max_ops = 2048;
    struct PktPlayerInfoOp ops[2048];

    struct PktPlayerInfo pkt;
    pkt.data = data;
    pkt.length = length;
    int count = pkt_player_info_reader_read(&reader, &pkt, ops, 2048);

    int idk_idx = 0;
    int obj_idx = 0;

    for( int i = 0; i < count; i++ )
    {
        if( ops[i].kind != PKT_PLAYER_INFO_OP_APPEARANCE )
            continue;
        struct PlayerAppearance appearance;
        player_appearance_decode(
            &appearance, ops[i]._appearance.appearance, ops[i]._appearance.len);
        struct AppearanceOp op;
        for( int slot = 0; slot < 12; slot++ )
        {
            appearances_decode(&op, appearance.appearance, slot);
            if( op.kind == APPEARANCE_KIND_IDK )
            {
                LuaGameType_IntArrayPush(idk_ids, op.id);
            }
            else if( op.kind == APPEARANCE_KIND_OBJ )
            {
                LuaGameType_IntArrayPush(obj_ids, op.id);
            }
        }
    }

    struct LuaGameType* result = LuaGameType_NewVarTypeArraySpread(2);
    LuaGameType_VarTypeArrayPush(result, idk_ids);
    LuaGameType_VarTypeArrayPush(result, obj_ids);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_npc_ids_from_packet(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    void* data = arg_userdata(args, 0);
    int length = arg_int(args, 1);

    struct PktNpcInfo pkt;
    pkt.data = data;
    pkt.length = length;

    static struct PktNpcInfoReader reader;
    reader.extended_count = 0;
    reader.current_op = 0;
    reader.max_ops = 2048;
    struct PktNpcInfoOp ops[2048];
    int count = pkt_npc_info_reader_read(&reader, &pkt, ops, 2048);

    struct LuaGameType* result = LuaGameType_NewIntArray(count);
    for( int i = 0; i < count; i++ )
    {
        if( ops[i].kind == PKT_NPC_INFO_OPBITS_NPCTYPE )
        {
            LuaGameType_IntArrayPush(result, ops[i]._bitvalue);
        }
    }
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_cache_model(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);
    int model_id = archive->archive_id;

    buildcachedat_loader_cache_model(buildcachedat, model_id, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_load_interfaces(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int data_size = arg_int(args, 0);
    void* data = arg_userdata(args, 1);

    buildcachedat_loader_load_interfaces(buildcachedat, data, data_size);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_load_component_sprites_from_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( game )
        buildcachedat_loader_load_component_sprites_from_media(buildcachedat, game);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_cache_textures(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);
    if( game && game->scene2 )
        buildcachedat_loader_cache_textures(
            buildcachedat, game->scene2, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_init_sequences_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_sequences_from_config_jagfile(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_get_animbaseframes_count_from_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    int count =
        buildcachedat_loader_get_animbaseframes_count_from_versionlist_jagfile(buildcachedat);
    return LuaGameType_NewInt(count);
}

struct LuaGameType*
LuaBuildCacheDat_cache_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);
    int animbaseframes_id = arg_int(args, 1);
    buildcachedat_loader_cache_animbaseframes(
        buildcachedat, animbaseframes_id, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_cache_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args)
{
    int data_size = arg_int(args, 0);
    void* data = arg_userdata(args, 1);

    buildcachedat_loader_cache_media(buildcachedat, game, data_size, data);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_cache_title(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = arg_userdata(args, 0);

    if( game && game->ui_scene )
        buildcachedat_loader_cache_title(
            buildcachedat, game->ui_scene, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_init_idkits_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_idkits_from_config_jagfile(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_init_objects_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_objects_from_config_jagfile(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_finalize_scene(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args)
{
    int map_sw_x = arg_int(args, 0);
    int map_sw_z = arg_int(args, 1);
    int map_ne_x = arg_int(args, 2);
    int map_ne_z = arg_int(args, 3);

    buildcachedat_loader_finalize_scene(
        buildcachedat, game, map_sw_x, map_sw_z, map_ne_x, map_ne_z);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear_map_chunks(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear_map_chunks(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear_scenery_models(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear_scenery_models(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear_scenery_configs(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear_scenery_configs(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear_component_cache(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear_component_cache(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear_objects(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear_objects(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear_config_jagfile(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear_versionlist_jagfile(buildcachedat);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaBuildCacheDat_clear_media_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_clear_media_jagfile(buildcachedat);
    return LuaGameType_NewVoid();
}

static char const g_prefix[] = "buildcachedat_";

#define DISPATCH_COMMAND(command, func)                                                            \
    if( strncmp(command, #func, sizeof(#func) - 1) == 0 )                                          \
    {                                                                                              \
        return LuaBuildCacheDat_##func(buildcachedat, args);                                       \
    }

bool
LuaBuildCacheDat_CommandHasPrefix(char* command)
{
    return strncmp(command, g_prefix, sizeof(g_prefix) - 1) == 0;
}

struct LuaGameType*
LuaBuildCacheDat_DispatchCommand(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    char* full_command,
    struct LuaGameType* args)
{
    assert(memcmp(full_command, g_prefix, sizeof(g_prefix) - 1) == 0);
    char command[256];
    size_t suffix_len = strlen(full_command) - sizeof(g_prefix) + 1;
    assert(suffix_len < sizeof(command));
    memcpy(command, full_command + sizeof(g_prefix) - 1, suffix_len);
    command[suffix_len] = '\0';

    // clang-format off
    if( strcmp(command, "cache_textures") == 0 )
        return LuaBuildCacheDat_cache_textures(buildcachedat, game, args);
    else if( strcmp(command, "cache_title") == 0 )
        return LuaBuildCacheDat_cache_title(buildcachedat, game, args);
    else if( strcmp(command, "cache_media") == 0 )
        return LuaBuildCacheDat_cache_media(buildcachedat, game, args);
    else if( strcmp(command, "load_component_sprites_from_media") == 0 )
        return LuaBuildCacheDat_load_component_sprites_from_media(buildcachedat, game, args);
    else if( strcmp(command, "finalize_scene") == 0 )
        return LuaBuildCacheDat_finalize_scene(buildcachedat, game, args);
    else DISPATCH_COMMAND(command, cache_map_scenery)
    else DISPATCH_COMMAND(command, set_config_jagfile)
    else DISPATCH_COMMAND(command, init_varp_varbit_from_config_jagfile)
    else DISPATCH_COMMAND(command, set_versionlist_jagfile)
    else DISPATCH_COMMAND(command, cache_map_terrain)
    else DISPATCH_COMMAND(command, has_map_terrain)
    else DISPATCH_COMMAND(command, has_map_scenery)
    else DISPATCH_COMMAND(command, has_model)
    else DISPATCH_COMMAND(command, has_animbaseframes)
    else DISPATCH_COMMAND(command, init_floortypes_from_config_jagfile)
    else DISPATCH_COMMAND(command, init_scenery_configs_from_config_jagfile)
    else DISPATCH_COMMAND(command, get_all_scenery_locs)
    else DISPATCH_COMMAND(command, get_scenery_model_ids)
    else DISPATCH_COMMAND(command, get_all_unique_scenery_model_ids)
    else DISPATCH_COMMAND(command, get_npc_model_ids)
    else DISPATCH_COMMAND(command, get_npc_head_model_ids)
    else DISPATCH_COMMAND(command, get_idk_model_ids)
    else DISPATCH_COMMAND(command, get_idk_head_model_ids)
    else DISPATCH_COMMAND(command, get_obj_model_ids)
    else DISPATCH_COMMAND(command, get_obj_head_model_ids)
    else DISPATCH_COMMAND(command, get_obj)
    else DISPATCH_COMMAND(command, get_player_appearance_ids_from_packet)
    else DISPATCH_COMMAND(command, get_npc_ids_from_packet)
    else DISPATCH_COMMAND(command, cache_model)
    else DISPATCH_COMMAND(command, load_interfaces)
    else DISPATCH_COMMAND(command, init_sequences_from_config_jagfile)
    else DISPATCH_COMMAND(command, get_animbaseframes_count_from_versionlist_jagfile)
    else DISPATCH_COMMAND(command, cache_animbaseframes)
    else DISPATCH_COMMAND(command, init_idkits_from_config_jagfile)
    else DISPATCH_COMMAND(command, init_objects_from_config_jagfile)
    else DISPATCH_COMMAND(command, set_2d_media_jagfile)
    else if( strcmp(command, "clear_component_cache") == 0 )
        return LuaBuildCacheDat_clear_component_cache(buildcachedat, args);
    else if( strcmp(command, "clear_objects") == 0 )
        return LuaBuildCacheDat_clear_objects(buildcachedat, args);
    else if( strcmp(command, "clear_map_chunks") == 0 )
        return LuaBuildCacheDat_clear_map_chunks(buildcachedat, args);
    else if( strcmp(command, "clear_scenery_models") == 0 )
        return LuaBuildCacheDat_clear_scenery_models(buildcachedat, args);
    else if( strcmp(command, "clear_scenery_configs") == 0 )
        return LuaBuildCacheDat_clear_scenery_configs(buildcachedat, args);
    else if( strcmp(command, "clear_config_jagfile") == 0 )
        return LuaBuildCacheDat_clear_config_jagfile(buildcachedat, args);
    else if( strcmp(command, "clear_versionlist_jagfile") == 0 )
        return LuaBuildCacheDat_clear_versionlist_jagfile(buildcachedat, args);
    else if( strcmp(command, "clear_media_jagfile") == 0 )
        return LuaBuildCacheDat_clear_media_jagfile(buildcachedat, args);
    else if( strcmp(command, "clear") == 0 )
        return LuaBuildCacheDat_clear(buildcachedat, args);
    else 
    {
        printf("Unknown command: %s\n", command);
        assert(false);
        return NULL;
    }
    // clang-format on

    return LuaGameType_NewVoid();
}
