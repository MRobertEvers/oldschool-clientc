#include "libtorirs_scriptapi.h"

#include "../core/tasks/core_task.h"
#include "../ioqueue/libtorirs_ioqueue.h"
#include "../libtorirs_internal.h"
#include "buildcache/dat1_buildcache.h"
#include "gamecache/gamecache.h"
#include "gamecache/gamecache_l.h"
#include "gamecache/gamecache_submit.h"
#include "games/runescape.h"
#include "platforms/platform_x/cachelib_client.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/filelist.h"
#include "src/osrs/rscache/tables/config_floortype.h"
#include "src/osrs/rscache/tables/config_locs.h"
#include "src/osrs/rscache/tables/config_sequence.h"
#include "src/osrs/rscache/tables/maps.h"
#include "src/osrs/rscache/tables/model.h"
#include "src/osrs/rscache/tables_dat/animframe.h"
#include "src/osrs/rscache/tables_dat/config_textures.h"
#include "src/osrs/rscache/tables_dat/configs_dat.h"
#include "src/osrs/rscache/tables_dat/pix32.h"
#include "src/osrs/rscache/tables_dat/pix8.h"
#include "src/osrs/texture.h"
#include "toridraw/toridraw_animation.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_types.h"
#include "toridrawx/toridrawx.h"
#include "world/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ConfigFileFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_config_file_fetch(&request);

    LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

bool
LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ConfigFileLoad\n");
    if( !instance )
        return false;

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.kind != TORIRSIO_KIND_CACHE )
        return false;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return false;
    if( item.u.cache.table_id != CACHE_DAT_CONFIGS )
        return false;
    if( item.u.cache.archive_id != CONFIG_DAT_CONFIGS )
        return false;
    if( item.u.cache.flags != 0 )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    printf("CacheDatArchive: %p\n", archive);

    struct FileListDat* filelist_dat = filelist_dat_new_from_cache_dat_archive(archive);
    if( !filelist_dat )
        return false;

    dat1_buildcache_set_fromconfigtable_config_jagfile(dat1(instance->gamecache_l), filelist_dat);

    cache_dat_archive_free(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Dat1_TexturesFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_TexturesFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_textures_archive_fetch(&request);

    LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

bool
LibToriRS_ScriptAPI_Dat1_TexturesLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_TexturesLoad\n");
    if( !instance )
        return false;

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.kind != TORIRSIO_KIND_CACHE )
        return false;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return false;
    if( item.u.cache.table_id != CACHE_DAT_CONFIGS )
        return false;
    if( item.u.cache.archive_id != CONFIG_DAT_TEXTURES )
        return false;
    if( item.u.cache.flags != 0 )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);
    cache_dat_archive_free(archive);
    if( !filelist )
        return false;

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
    {
        struct CacheDatTexture* cache_texture =
            cache_dat_texture_new_from_filelist_dat(filelist, i, 0);
        if( !cache_texture )
        {
            printf("cache_dat_texture_new_from_filelist_dat failed for texture %d\n", i);
            assert(false);
            continue;
        }

        int animation_direction = TORIDRAW_TEXANIM_DIRECTION_NONE;
        int animation_speed = 0;
        if( i == 17 || i == 24 )
        {
            animation_direction = TORIDRAW_TEXANIM_DIRECTION_V_DOWN;
            animation_speed = 2;
        }

        if( i == 8 )
        {
            printf("cache_texture: %p\n", cache_texture);
        }
        struct GameCache_Texture* gc_texture = gamecache_texture_new_from_cache_dat_texture(
            cache_texture, animation_direction, animation_speed);
        cache_dat_texture_free(cache_texture);
        if( !gc_texture )
        {
            printf("gamecache_texture_new_from_cache_dat_texture failed for texture %d\n", i);
            assert(false);
            continue;
        }

        gamecache_submit_texture(gamecache(instance->gamecache_l), i, gc_texture);
    }

    filelist_dat_free(filelist);
    return true;
}

void
LibToriRS_ScriptAPI_Dat1_ModelCacheAdd(
    struct LibToriRS_Instance* instance,
    int model_id,
    int data_size,
    void* data)
{
    printf("LibToriRS_ScriptAPI_Dat1_ModelCacheAdd\n");
    if( !instance )
        return;
    if( !data )
        return;

    struct CacheModel* model = model_new_decode(data, data_size);
    if( !model )
        return;

    dat1_buildcache_model_add(dat1(instance->gamecache_l), model_id, model);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel\n");
    if( !instance || !instance->toridrawx )
        return;

    ToriDrawX_SubmitModelFromDat1(instance->toridrawx, model_id);
    ToriDrawX_Model(instance->toridrawx, model_id);
}

void
LibToriRS_ScriptAPI_Dat1_ModelFetch(
    struct LibToriRS_Instance* instance,
    int model_id,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ModelFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_model_fetch(model_id, &request);

    LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

bool
LibToriRS_ScriptAPI_Dat1_ModelLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ModelLoad\n");

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.kind != TORIRSIO_KIND_CACHE )
        return false;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return false;
    if( item.u.cache.table_id != CACHE_DAT_MODELS )
        return false;
    if( item.u.cache.flags != 0 )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    int model_id = item.u.cache.archive_id;

    struct CacheModel* model = model_new_from_dat_archive(archive, model_id);
    if( !model )
        return false;

    dat1_buildcache_model_add(dat1(instance->gamecache_l), model_id, model);
    cache_dat_archive_free(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Dat1_MapChunkTerrainFetch(
    struct LibToriRS_Instance* instance,
    int mapx,
    int mapz,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_MapChunkTerrainFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_map_chunk_terrain_fetch(mapx, mapz, &request);

    LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

void
LibToriRS_ScriptAPI_Dat1_MapChunkSceneryFetch(
    struct LibToriRS_Instance* instance,
    int mapx,
    int mapz,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_MapChunkSceneryFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_map_chunk_scenery_fetch(mapx, mapz, &request);

    LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

bool
LibToriRS_ScriptAPI_Dat1_MapChunkTerrainLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_MapChunkTerrainLoad\n");

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.kind != TORIRSIO_KIND_CACHE )
        return false;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return false;
    if( item.u.cache.table_id != CACHE_DAT_MAPS )
        return false;
    if( item.u.cache.flags != CACHELIB_MAPCHUNK_TERRAIN )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    int map_id = item.u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;

    struct CacheMapTerrain* terrain = map_terrain_new_from_decode_flags(
        archive->data, archive->data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);
    if( !terrain )
        return false;

    dat1_buildcache_map_terrain_add(dat1(instance->gamecache_l), map_id, terrain);
    cache_dat_archive_free(archive);
    return true;
}

bool
LibToriRS_ScriptAPI_Dat1_MapChunkSceneryLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_MapChunkSceneryLoad\n");
    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.kind != TORIRSIO_KIND_CACHE )
        return false;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return false;
    if( item.u.cache.table_id != CACHE_DAT_MAPS )
        return false;
    if( item.u.cache.flags != CACHELIB_MAPCHUNK_SCENERY )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    int map_id = item.u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;

    struct CacheMapLocs* locs = map_locs_new_from_decode(archive->data, archive->data_size);
    locs->_chunk_mapx = map_x;
    locs->_chunk_mapz = map_z;
    if( !locs )
        return false;

    dat1_buildcache_map_scenery_add(dat1(instance->gamecache_l), map_id, locs);
    cache_dat_archive_free(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Game_ModelViewer_Init(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Game_ModelViewer_Init\n");
    if( !instance )
        return;

    instance->model_viewer = game_modelviewer_new(instance->script_queue, instance->scene);
    if( !instance->model_viewer )
        return;

    instance->model_viewer->gamecache = gamecache(instance->gamecache_l);

    instance->model_viewer_handle.kind = GAME_HANDLE_KIND_MODEL_VIEWER;
    instance->model_viewer_handle.u.model_viewer = instance->model_viewer;
    instance->active_game_kind = GAME_HANDLE_KIND_MODEL_VIEWER;
}

void
LibToriRS_ScriptAPI_Game_Runescape_Init(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Game_Runescape_Init\n");
    if( !instance )
        return;

    instance->runescape = game_runescape_new(instance->script_queue, instance->scene);
    if( !instance->runescape )
        return;

    game_runescape_set_gamecache(instance->runescape, gamecache(instance->gamecache_l));
    game_runescape_set_toridrawx(instance->runescape, instance->toridrawx);

    instance->runescape_handle.kind = GAME_HANDLE_KIND_RUNESCAPE;
    instance->runescape_handle.u.runescape = instance->runescape;
    instance->active_game_kind = GAME_HANDLE_KIND_RUNESCAPE;

    struct Task_GameCacheL_WorldRebuildNormal* task = Task_GameCacheL_WorldRebuildNormal_New(
        instance->gamecache_l, RUNESCAPE_ZONE_CENTER_X, RUNESCAPE_ZONE_CENTER_Z);
    LibToriRS_TasksAdd(instance, task, Task_GameCacheL_WorldRebuildNormal_Run);
}

void
LibToriRS_ScriptAPI_Game_Runescape_BuildWorld(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Game_Runescape_BuildWorld\n");

    game_runescape_build_world(instance->runescape);
}

struct GameHandle*
LibToriRS_ScriptAPI_Game_ModelViewer_GetGameHandle(struct LibToriRS_Instance* instance)
{
    if( !instance || !instance->model_viewer )
        return NULL;

    return &instance->model_viewer_handle;
}

void
LibToriRS_ScriptAPI_CoreTask_Dat1LoadModel(
    struct LibToriRS_Instance* instance,
    struct GameHandle* game,
    int model_id)
{
    if( !instance || !game )
        return;
    if( game->kind != GAME_HANDLE_KIND_MODEL_VIEWER )
        return;
    if( !game->u.model_viewer )
        return;

    // struct CoreTask* task = core_task_new_dat1_model_load(*game, model_id);
    // if( !task )
    //     return;

    // game_modelviewer_task_add(game->u.model_viewer, task);
}

void
LibToriRS_ScriptAPI_CoreTask_RevConfigQueue(
    struct LibToriRS_Instance* instance,
    struct GameHandle* game,
    const char* filename)
{
    (void)instance;

    switch( game->kind )
    {
    case GAME_HANDLE_KIND_MODEL_VIEWER:
        if( !game->u.model_viewer )
            return;
        game_modelviewer_revconfig_queue(game->u.model_viewer, filename);
        break;
    default:
        break;
    }
}

void
LibToriRS_ScriptAPI_CoreTask_RevConfigLoad(
    struct LibToriRS_Instance* instance,
    struct GameHandle* game)
{
    switch( game->kind )
    {
    case GAME_HANDLE_KIND_MODEL_VIEWER:
        if( !game->u.model_viewer )
            return;

        // struct CoreTask* task = NULL;
        // task = core_task_new_revconfig_load(
        //     *game,
        //     game->u.model_viewer->revconfig_filenames[0],
        //     game->u.model_viewer->revconfig_filenames[1],
        //     game->u.model_viewer->revconfig_filenames[2],
        //     game->u.model_viewer->revconfig_filenames[3]);
        // if( !task )
        //     return;

        // game_modelviewer_task_add(game->u.model_viewer, task);
        break;
    default:
        break;
    }
}

bool
LibToriRS_ScriptAPI_RunTasks(struct LibToriRS_Instance* instance)
{
    if( !instance || !instance->io_queue )
        return true;

    struct LibToriRS_IOContext ctx = {
        .io = instance->io_queue,
    };

    bool all_done = true;

    all_done = !LibToriRS_TasksRun(instance);

    // if( instance->model_viewer )
    //     all_done = game_modelviewer_run_tasks(instance->model_viewer, &ctx) && all_done;

    return all_done;
}

void
LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    printf("LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel %d\n", model_id);
    assert(instance && "Invalid instance");

    struct ToriDraw_ModelHandle hnd = ToriDrawX_Model(instance->toridrawx, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
    {
        printf("Invalid model handle\n");
        return;
    }

    ToriDraw_LightModelDefault(hnd, 0, 0);

    game_modelviewer_set_model(instance->model_viewer, model_id, hnd);
}

void
LibToriRS_ScriptAPI_Dat1_ModelCleanup(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    printf("LibToriRS_ScriptAPI_Dat1_ModelCleanup\n");
    if( !instance )
        return;

    dat1_buildcache_model_remove(dat1(instance->gamecache_l), model_id);
}

void
LibToriRS_ScriptAPI_Dat1_TexturesCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_TexturesCleanup\n");
    if( !instance )
        return;

    gamecache_textures_clear_all(gamecache(instance->gamecache_l));
}

void
LibToriRS_ScriptAPI_Dat1_SubmitTextures(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitTextures\n");
    if( !instance || !instance->toridrawx )
        return;

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
        ToriDrawX_Texture(instance->toridrawx, i);
}

void
LibToriRS_ScriptAPI_GameCache_ModelsClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_ModelsClearAll\n");
    if( !instance )
        return;

    if( instance->scene )
        ToriDraw_SceneModelsClearAll(instance->scene);
    gamecache_models_clear_all(gamecache(instance->gamecache_l));
}

void
LibToriRS_ScriptAPI_Dat1_VersionListFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_VersionListFetch\n");

    // struct CacheLib_IORequest request;
    // cachelib_dat1_versionlist_fetch(&request);

    // LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

bool
LibToriRS_ScriptAPI_Dat1_VersionListLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_VersionListLoad\n");
    // if( !instance )
    //     return false;

    // struct LibToriRS_IOQueueItem item = { 0 };
    // if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
    //     return false;
    // if( item.kind != TORIRSIO_KIND_CACHE )
    //     return false;
    // if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
    //     return false;
    // if( item.u.cache.table_id != CACHE_DAT_CONFIGS )
    //     return false;
    // if( item.u.cache.archive_id != CONFIG_DAT_VERSION_LIST )
    //     return false;
    // if( item.u.cache.flags != 0 )
    //     return false;

    // struct CacheDatArchive* archive = item.data;
    // if( !archive )
    //     return false;

    // struct FileListDat* filelist_dat = filelist_dat_new_from_cache_dat_archive(archive);
    // if( !filelist_dat )
    //     return false;

    // dat1_buildcache_set_versionlist_jagfile(instance->dat1_buildcache, filelist_dat);

    // cache_dat_archive_free(archive);
    // return true;
    return false;
}

void
LibToriRS_ScriptAPI_Dat1_AnimationsFetch(
    struct LibToriRS_Instance* instance,
    int archive_id,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_AnimationsFetch\n");

    // struct CacheLib_IORequest request;
    // cachelib_dat1_animations_fetch(archive_id, &request);

    // LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

bool
LibToriRS_ScriptAPI_Dat1_AnimationsLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_AnimationsLoad\n");
    // if( !instance )
    //     return false;

    // struct LibToriRS_IOQueueItem item = { 0 };
    // if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
    //     return false;
    // if( item.kind != TORIRSIO_KIND_CACHE )
    //     return false;
    // if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
    //     return false;
    // if( item.u.cache.table_id != CACHE_DAT_ANIMATIONS )
    //     return false;
    // if( item.u.cache.flags != 0 )
    //     return false;

    // struct CacheDatArchive* archive = item.data;
    // if( !archive )
    //     return false;

    // int animbaseframes_id = item.u.cache.archive_id;

    // struct CacheDatAnimBaseFrames* animbaseframes =
    //     cache_dat_animbaseframes_new_decode(archive->data, archive->data_size);
    // if( !animbaseframes )
    // {
    //     cache_dat_archive_free(archive);
    //     return false;
    // }

    // dat1_buildcache_animbaseframes_add(
    //     instance->dat1_buildcache, animbaseframes_id, animbaseframes);

    // cache_dat_archive_free(archive);
    // return true;
    return false;
}

void
LibToriRS_ScriptAPI_Dat1_SequencesInitFromConfigJagfile(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SequencesInitFromConfigJagfile\n");

    // dat1_buildcache_sequences_init_from_config_jagfile(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile(struct LibToriRS_Instance* instance)
{
    // printf("LibToriRS_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile\n");

    // dat1_buildcache_floortypes_init_from_config_jagfile(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile\n");

    // dat1_buildcache_init_scenery_configs_from_config_jagfile(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitSequences(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitSequences\n");

    // struct Dat1BuildCache* buildcache = instance->dat1_buildcache;
    // // struct GameCache* gamecache = instance->gamecache;

    // struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(buildcache->sequences_hmap);
    // struct Dat1MapEntry_Sequence* entry = NULL;

    // // while( (entry = (struct Dat1MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
    // // {
    // //     if( !entry->sequence )
    // //         continue;

    // //     struct GameCache_Sequence* seq =
    // //         gamecache_sequence_new_from_cache_dat_sequence(entry->sequence);
    // //     if( !seq )
    // //         continue;

    // //     entry->sequence = NULL;
    // //     gamecache_sequence_add(gamecache, entry->id, seq);
    // // }
    // ToriDraw_MapIterFree(iter);

    // dat1_buildcache_sequences_reset(buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitFloortypes(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitFloortypes\n");
}

void
LibToriRS_ScriptAPI_Dat1_SubmitSceneryConfigs(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitSceneryConfigs\n");
}

void
LibToriRS_ScriptAPI_Dat1_SubmitAnimations(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitAnimations\n");
}

void
LibToriRS_ScriptAPI_Dat1_SequencesCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SequencesCleanup\n");
}

void
LibToriRS_ScriptAPI_Dat1_FloortypesCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_FloortypesCleanup\n");
}

void
LibToriRS_ScriptAPI_Dat1_SceneryConfigsCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SceneryConfigsCleanup\n");
}

void
LibToriRS_ScriptAPI_Dat1_AnimationsCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_AnimationsCleanup\n");
}

void
LibToriRS_ScriptAPI_GameCache_SequencesClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_SequencesClearAll\n");
}

void
LibToriRS_ScriptAPI_GameCache_FloortypesClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_FloortypesClearAll\n");
}

void
LibToriRS_ScriptAPI_GameCache_SceneryConfigsClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_SceneryConfigsClearAll\n");
}

void
LibToriRS_ScriptAPI_GameCache_AnimationsClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_AnimationsClearAll\n");
}
