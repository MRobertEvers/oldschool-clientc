#include "libtorirs_scriptapi.h"

#include "../core/tasks/core_task.h"
#include "../ioqueue/libtorirs_ioqueue.h"
#include "../libtorirs_internal.h"
#include "buildcache/dat1_buildcache.h"
#include "gamecache/gamecache.h"
#include "gamecache/gamecache_flotype.h"
#include "gamecache/gamecache_scenery_config.h"
#include "gamecache/gamecache_sequence.h"
#include "gamecache/toridraw_cachemodel.h"
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
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Dat1MapEntry_Sequence
{
    int id;
    struct CacheDatSequence* sequence;
};

struct Dat1MapEntry_Flotype
{
    int id;
    struct CacheConfigOverlay* flotype;
};

struct Dat1MapEntry_ConfigLoc
{
    int id;
    struct CacheConfigLocation* config_loc;
};

struct Dat1MapEntry_AnimBaseFrames
{
    int id;
    struct CacheDatAnimBaseFrames* animbaseframes;
};

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

    dat1_buildcache_set_fromconfigtable_config_jagfile(instance->dat1_buildcache, filelist_dat);

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
    if( !instance->dat1_buildcache )
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
        struct ToriDraw_Texture* toridraw_texture = texture_new_toridraw_from_texture_sprite(
            cache_texture, animation_direction, animation_speed, false, false);
        cache_dat_texture_free(cache_texture);
        if( !toridraw_texture )
        {
            printf("texture_new_toridraw_from_texture_sprite failed for texture %d\n", i);
            assert(false);
            continue;
        }

        dat1_buildcache_texture_set(instance->dat1_buildcache, i, toridraw_texture);
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

    dat1_buildcache_model_add(instance->dat1_buildcache, model_id, model);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel\n");
    if( !instance )
        return;

    struct CacheModel* model = dat1_buildcache_model_get(instance->dat1_buildcache, model_id);
    if( !model )
        return;

    struct CacheModel* copy = model_new_copy(model);
    if( !copy )
        return;

    struct ToriDraw_Model* td = toridraw_model_new_from_cache_model(copy);
    model_free(copy);
    if( !td )
        return;

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td,
    };
    gamecache_model_add(instance->gamecache, model_id, hnd);
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
    if( !instance )
        return false;

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

    dat1_buildcache_model_add(instance->dat1_buildcache, model_id, model);
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

    dat1_buildcache_map_terrain_add(instance->dat1_buildcache, map_id, terrain);
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

    dat1_buildcache_map_scenery_add(instance->dat1_buildcache, map_id, locs);
    cache_dat_archive_free(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Game_ModelViewer_Init(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Game_ModelViewer_Init\n");
    if( !instance )
        return;

    instance->model_viewer = game_modelviewer_new(instance->script_queue);
    if( !instance->model_viewer )
        return;

    instance->model_viewer->gamecache = instance->gamecache;

    instance->model_viewer_handle.kind = GAME_HANDLE_KIND_MODEL_VIEWER;
    instance->model_viewer_handle.u.model_viewer = instance->model_viewer;
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

    struct CoreTask* task = core_task_new_dat1_model_load(*game, model_id);
    if( !task )
        return;

    game_modelviewer_task_add(game->u.model_viewer, task);
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

    if( instance->model_viewer )
        all_done = game_modelviewer_run_tasks(instance->model_viewer, &ctx) && all_done;

    return all_done;
}

void
LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    printf("LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel %d\n", model_id);
    assert(instance && "Invalid instance");

    struct ToriDraw_ModelHandle hnd = gamecache_model_get(instance->gamecache, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
    {
        printf("Invalid model handle\n");
        return;
    }

    toridraw_light_model_default(hnd, 0, 0);

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

    dat1_buildcache_model_remove(instance->dat1_buildcache, model_id);
}

void
LibToriRS_ScriptAPI_Dat1_TexturesCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_TexturesCleanup\n");
    if( !instance || !instance->dat1_buildcache )
        return;

    dat1_buildcache_texture_clear(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitTextures(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitTextures\n");
    if( !instance )
        return;
    if( !instance->dat1_buildcache )
        return;
    if( !instance->model_viewer || !instance->model_viewer->context )
        return;

    struct ToriDraw_Context* context = instance->model_viewer->context;
    struct Dat1BuildCache* buildcache = instance->dat1_buildcache;

    for( int i = 0; i < buildcache->texture_count; i++ )
    {
        struct ToriDraw_Texture* texture = buildcache->textures[i];
        if( !texture )
            continue;

        buildcache->textures[i] = NULL;
        toridraw_context_set_texture(context, i, texture);
    }

    buildcache->texture_count = 0;
}

void
LibToriRS_ScriptAPI_GameCache_ModelsClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_ModelsClearAll\n");
    if( !instance || !instance->gamecache )
        return;

    gamecache_models_clear_all(instance->gamecache);
}

void
LibToriRS_ScriptAPI_Dat1_VersionListFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_VersionListFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_versionlist_fetch(&request);

    LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

bool
LibToriRS_ScriptAPI_Dat1_VersionListLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_VersionListLoad\n");
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
    if( item.u.cache.archive_id != CONFIG_DAT_VERSION_LIST )
        return false;
    if( item.u.cache.flags != 0 )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    struct FileListDat* filelist_dat = filelist_dat_new_from_cache_dat_archive(archive);
    if( !filelist_dat )
        return false;

    dat1_buildcache_set_versionlist_jagfile(instance->dat1_buildcache, filelist_dat);

    cache_dat_archive_free(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Dat1_AnimationsFetch(
    struct LibToriRS_Instance* instance,
    int archive_id,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_AnimationsFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_animations_fetch(archive_id, &request);

    LibToriRS_IOQueuePushCache(io_queue, request.table_id, request.archive_id, request.flags);
}

bool
LibToriRS_ScriptAPI_Dat1_AnimationsLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_AnimationsLoad\n");
    if( !instance )
        return false;

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.kind != TORIRSIO_KIND_CACHE )
        return false;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return false;
    if( item.u.cache.table_id != CACHE_DAT_ANIMATIONS )
        return false;
    if( item.u.cache.flags != 0 )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    int animbaseframes_id = item.u.cache.archive_id;

    struct CacheDatAnimBaseFrames* animbaseframes =
        cache_dat_animbaseframes_new_decode(archive->data, archive->data_size);
    if( !animbaseframes )
    {
        cache_dat_archive_free(archive);
        return false;
    }

    dat1_buildcache_animbaseframes_add(
        instance->dat1_buildcache, animbaseframes_id, animbaseframes);

    cache_dat_archive_free(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Dat1_SequencesInitFromConfigJagfile(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SequencesInitFromConfigJagfile\n");
    if( !instance || !instance->dat1_buildcache )
        return;

    dat1_buildcache_sequences_init_from_config_jagfile(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile\n");
    if( !instance || !instance->dat1_buildcache )
        return;

    dat1_buildcache_floortypes_init_from_config_jagfile(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile\n");
    if( !instance || !instance->dat1_buildcache )
        return;

    dat1_buildcache_init_scenery_configs_from_config_jagfile(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitSequences(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitSequences\n");
    if( !instance || !instance->dat1_buildcache || !instance->gamecache )
        return;

    struct Dat1BuildCache* buildcache = instance->dat1_buildcache;
    struct GameCache* gamecache = instance->gamecache;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(buildcache->sequences_hmap);
    struct Dat1MapEntry_Sequence* entry = NULL;

    while( (entry = (struct Dat1MapEntry_Sequence*)toridraw_map_iter_next(iter)) )
    {
        if( !entry->sequence )
            continue;

        struct GameCache_Sequence* seq =
            gamecache_sequence_new_from_cache_dat_sequence(entry->sequence);
        if( !seq )
            continue;

        entry->sequence = NULL;
        gamecache_sequence_add(gamecache, entry->id, seq);
    }
    toridraw_map_iter_free(iter);

    dat1_buildcache_sequences_reset(buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitFloortypes(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitFloortypes\n");
    if( !instance || !instance->dat1_buildcache || !instance->gamecache )
        return;

    struct Dat1BuildCache* buildcache = instance->dat1_buildcache;
    struct GameCache* gamecache = instance->gamecache;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(buildcache->flotype_hmap);
    struct Dat1MapEntry_Flotype* entry = NULL;

    while( (entry = (struct Dat1MapEntry_Flotype*)toridraw_map_iter_next(iter)) )
    {
        if( !entry->flotype )
            continue;

        struct GameCache_Flotype* flotype =
            gamecache_flotype_new_from_cache_config_overlay(entry->flotype);
        if( !flotype )
            continue;

        entry->flotype = NULL;
        gamecache_flotype_add(gamecache, entry->id, flotype);
    }
    toridraw_map_iter_free(iter);

    dat1_buildcache_floortypes_reset(buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitSceneryConfigs(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitSceneryConfigs\n");
    if( !instance || !instance->dat1_buildcache || !instance->gamecache )
        return;

    struct Dat1BuildCache* buildcache = instance->dat1_buildcache;
    struct GameCache* gamecache = instance->gamecache;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(buildcache->config_loc_hmap);
    struct Dat1MapEntry_ConfigLoc* entry = NULL;

    while( (entry = (struct Dat1MapEntry_ConfigLoc*)toridraw_map_iter_next(iter)) )
    {
        if( !entry->config_loc )
            continue;

        struct GameCache_SceneryConfig* config =
            gamecache_scenery_config_new_from_cache_config_location(entry->config_loc);
        if( !config )
            continue;

        entry->config_loc = NULL;
        gamecache_scenery_config_add(gamecache, entry->id, config);
    }
    toridraw_map_iter_free(iter);

    dat1_buildcache_scenery_configs_reset(buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitAnimations(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitAnimations\n");
    if( !instance || !instance->dat1_buildcache || !instance->gamecache )
        return;

    struct Dat1BuildCache* buildcache = instance->dat1_buildcache;
    struct GameCache* gamecache = instance->gamecache;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(buildcache->animbaseframes_hmap);
    struct Dat1MapEntry_AnimBaseFrames* entry = NULL;

    while( (entry = (struct Dat1MapEntry_AnimBaseFrames*)toridraw_map_iter_next(iter)) )
    {
        if( !entry->animbaseframes )
            continue;

        struct ToriDraw_Animation* animation =
            toridraw_animation_new_from_cache_dat_animbaseframes(entry->animbaseframes);
        if( !animation )
            continue;

        entry->animbaseframes = NULL;
        gamecache_animation_add(gamecache, entry->id, animation);
    }
    toridraw_map_iter_free(iter);

    dat1_buildcache_animbaseframes_reset(buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SequencesCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SequencesCleanup\n");
    if( !instance || !instance->dat1_buildcache )
        return;

    dat1_buildcache_sequences_cleanup(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_FloortypesCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_FloortypesCleanup\n");
    if( !instance || !instance->dat1_buildcache )
        return;

    dat1_buildcache_floortypes_cleanup(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_SceneryConfigsCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SceneryConfigsCleanup\n");
    if( !instance || !instance->dat1_buildcache )
        return;

    dat1_buildcache_scenery_configs_cleanup(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_Dat1_AnimationsCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_AnimationsCleanup\n");
    if( !instance || !instance->dat1_buildcache )
        return;

    dat1_buildcache_animbaseframes_cleanup(instance->dat1_buildcache);
}

void
LibToriRS_ScriptAPI_GameCache_SequencesClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_SequencesClearAll\n");
    if( !instance || !instance->gamecache )
        return;

    gamecache_sequences_clear_all(instance->gamecache);
}

void
LibToriRS_ScriptAPI_GameCache_FloortypesClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_FloortypesClearAll\n");
    if( !instance || !instance->gamecache )
        return;

    gamecache_floortypes_clear_all(instance->gamecache);
}

void
LibToriRS_ScriptAPI_GameCache_SceneryConfigsClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_SceneryConfigsClearAll\n");
    if( !instance || !instance->gamecache )
        return;

    gamecache_scenery_configs_clear_all(instance->gamecache);
}

void
LibToriRS_ScriptAPI_GameCache_AnimationsClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_AnimationsClearAll\n");
    if( !instance || !instance->gamecache )
        return;

    gamecache_animations_clear_all(instance->gamecache);
}
