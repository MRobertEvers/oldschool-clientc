#include "lua_buildcachedat.h"
#include "lua_gametypes.h"

#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/game.h"

#include <stdlib.h>
#include <string.h>

/* Helper: get int at args[i]. args must be VarTypeArray. */
static int
arg_int(struct LuaGameType* args, int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetInt(elem);
}

/* Helper: get userdata at args[i]. */
static void*
arg_userdata(struct LuaGameType* args, int i)
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

void
LuaBuildCacheDat_cache_map_scenery(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int param_a = arg_int(args, 0);
    int param_b = arg_int(args, 1);
    int data_size = arg_int(args, 2);
    void* data = arg_userdata(args, 3);

    buildcachedat_loader_cache_map_scenery(buildcachedat, param_a, param_b, data_size, data);
}

void
LuaBuildCacheDat_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int data_size = arg_int(args, 0);
    void* data = arg_userdata(args, 1);

    buildcachedat_loader_set_config_jagfile(buildcachedat, data_size, data);
}

void
LuaBuildCacheDat_init_varp_varbit_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct GGame* game = arg_game(args);
    buildcachedat_loader_init_varp_varbit(buildcachedat, game);
}

void
LuaBuildCacheDat_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int data_size = arg_int(args, 0);
    void* data = arg_userdata(args, 1);

    buildcachedat_loader_set_versionlist_jagfile(buildcachedat, data_size, data);
}

void
LuaBuildCacheDat_cache_map_terrain(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int param_a = arg_int(args, 0);
    int param_b = arg_int(args, 1);
    int data_size = arg_int(args, 2);
    void* data = arg_userdata(args, 3);

    buildcachedat_loader_cache_map_terrain(buildcachedat, param_a, param_b, data_size, data);
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

void
LuaBuildCacheDat_init_floortypes_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_floortypes_from_config_jagfile(buildcachedat);
}

void
LuaBuildCacheDat_init_scenery_configs_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_scenery_configs_from_config_jagfile(buildcachedat);
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
        struct LuaGameType* elem = LuaGameType_NewIntArray(row, 3);
        if( elem )
            LuaGameType_VarTypeArrayPush(result, elem);
        else
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

    struct LuaGameType* result = LuaGameType_NewIntArray(model_ids, count);
    if( !result && model_ids )
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
        return LuaGameType_NewIntArray(NULL, 0);

    int* model_ids = malloc((size_t)npc->models_count * sizeof(int));
    if( !model_ids )
        return NULL;
    memcpy(model_ids, npc->models, (size_t)npc->models_count * sizeof(int));
    return LuaGameType_NewIntArray(model_ids, npc->models_count);
}

struct LuaGameType*
LuaBuildCacheDat_get_npc_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int npc_id = arg_int(args, 0);

    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(buildcachedat, npc_id);
    if( !npc || !npc->heads )
        return LuaGameType_NewIntArray(NULL, 0);

    int idx = 0;
    int* head_ids = malloc((size_t)npc->heads_count * sizeof(int));
    if( !head_ids )
        return NULL;
    for( int i = 0; i < npc->heads_count; i++ )
    {
        if( npc->heads[i] != -1 )
            head_ids[idx++] = npc->heads[i];
    }
    struct LuaGameType* result = LuaGameType_NewIntArray(head_ids, idx);
    if( !result )
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
        return LuaGameType_NewIntArray(NULL, 0);

    int* model_ids = malloc((size_t)idk->models_count * sizeof(int));
    if( !model_ids )
        return NULL;
    memcpy(model_ids, idk->models, (size_t)idk->models_count * sizeof(int));
    return LuaGameType_NewIntArray(model_ids, idk->models_count);
}

struct LuaGameType*
LuaBuildCacheDat_get_idk_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int idk_id = arg_int(args, 0);

    struct CacheDatConfigIdk* idk = buildcachedat_get_idk(buildcachedat, idk_id);
    if( !idk )
        return LuaGameType_NewIntArray(NULL, 0);

    int* head_ids = malloc(10 * sizeof(int));
    if( !head_ids )
        return NULL;
    int idx = 0;
    for( int i = 0; i < 10; i++ )
    {
        if( idk->heads[i] != -1 && idk->heads[i] != 0 )
            head_ids[idx++] = idk->heads[i];
    }
    struct LuaGameType* result = LuaGameType_NewIntArray(head_ids, idx);
    if( !result )
        free(head_ids);
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
        return LuaGameType_NewIntArray(NULL, 0);

    int* model_ids = malloc(4 * sizeof(int));
    if( !model_ids )
        return NULL;
    int idx = 0;
    if( obj->model != -1 )
        model_ids[idx++] = obj->model;
    if( obj->manwear != -1 )
        model_ids[idx++] = obj->manwear;
    if( obj->manwear2 != -1 )
        model_ids[idx++] = obj->manwear2;
    if( obj->manwear3 != -1 )
        model_ids[idx++] = obj->manwear3;
    struct LuaGameType* result = LuaGameType_NewIntArray(model_ids, idx);
    if( !result )
        free(model_ids);
    return result;
}

struct LuaGameType*
LuaBuildCacheDat_get_obj_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int obj_id = arg_int(args, 0);
    int gender = LuaGameType_GetVarTypeArrayCount(args) > 1 ? arg_int(args, 1) : 0;

    struct CacheDatConfigObj* obj = buildcachedat_get_obj(buildcachedat, obj_id);
    if( !obj )
        return LuaGameType_NewIntArray(NULL, 0);

    int head1 = (gender == 1) ? obj->womanhead : obj->manhead;
    int head2 = (gender == 1) ? obj->womanhead2 : obj->manhead2;

    int* head_ids = malloc(2 * sizeof(int));
    if( !head_ids )
        return NULL;
    int idx = 0;
    if( head1 != -1 )
        head_ids[idx++] = head1;
    if( head2 != -1 )
        head_ids[idx++] = head2;
    struct LuaGameType* result = LuaGameType_NewIntArray(head_ids, idx);
    if( !result )
        free(head_ids);
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

void
LuaBuildCacheDat_cache_model(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int model_id = arg_int(args, 0);
    int data_size = arg_int(args, 1);
    void* data = arg_userdata(args, 2);

    buildcachedat_loader_cache_model(buildcachedat, model_id, data_size, data);
}

void
LuaBuildCacheDat_load_interfaces(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int data_size = arg_int(args, 0);
    void* data = arg_userdata(args, 1);

    buildcachedat_loader_load_interfaces(buildcachedat, data, data_size);
}

void
LuaBuildCacheDat_load_component_sprites_from_media(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct GGame* game = arg_game(args);
    buildcachedat_loader_load_component_sprites_from_media(buildcachedat, game);
}

void
LuaBuildCacheDat_cache_textures(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct GGame* game = arg_game(args);
    int data_size = arg_int(args, 1);
    void* data = arg_userdata(args, 2);

    buildcachedat_loader_cache_textures(buildcachedat, game, data_size, data);
}

void
LuaBuildCacheDat_init_sequences_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_sequences_from_config_jagfile(buildcachedat);
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

void
LuaBuildCacheDat_cache_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    int animbaseframes_id = arg_int(args, 0);
    int data_size = arg_int(args, 1);
    void* data = arg_userdata(args, 2);

    buildcachedat_loader_cache_animbaseframes(buildcachedat, animbaseframes_id, data_size, data);
}

void
LuaBuildCacheDat_cache_media(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct GGame* game = arg_game(args);
    int data_size = arg_int(args, 1);
    void* data = arg_userdata(args, 2);

    buildcachedat_loader_cache_media(buildcachedat, game, data_size, data);
}

void
LuaBuildCacheDat_cache_title(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct GGame* game = arg_game(args);
    int data_size = arg_int(args, 1);
    void* data = arg_userdata(args, 2);

    buildcachedat_loader_cache_title(buildcachedat, game, data_size, data);
}

void
LuaBuildCacheDat_init_idkits_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_idkits_from_config_jagfile(buildcachedat);
}

void
LuaBuildCacheDat_init_objects_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_init_objects_from_config_jagfile(buildcachedat);
}

void
LuaBuildCacheDat_finalize_scene(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    struct GGame* game = arg_game(args);
    int map_sw_x = arg_int(args, 1);
    int map_sw_z = arg_int(args, 2);
    int map_ne_x = arg_int(args, 3);
    int map_ne_z = arg_int(args, 4);

    buildcachedat_loader_finalize_scene(
        buildcachedat, game, map_sw_x, map_sw_z, map_ne_x, map_ne_z);
}
