#include "libtorirs_scriptapi.h"

#include "../core/tasks/core_task.h"
#include "../ioqueue/libtorirs_ioqueue.h"
#include "../libtorirs_internal.h"
#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/toriauxlib.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"
#include "toriauxlib/c/revconfig_ui_load.h"
#include "games/runescape.h"
#include "platforms/platform_x/cachelib_client.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/dat2a/dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/dat1a_config_textures.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat1a/dat1a_pix32.h"
#include "osrs/rscache/dat1a/dat1a_pix8.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2a/dat2a_textures.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "src/osrs/texture.h"
#include "toridraw/toridraw_animation.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_types.h"
#include "toriauxlib/toriauxlib.h"
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

    struct RSCacheDat2DiskLib_IORequest request;
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
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Configs )
        return false;
    if( item.u.cache.archive_id != RSCacheDat1A_ConfigKind_Configs )
        return false;
    if( item.u.cache.flags != 0 )
        return false;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return false;

    printf("CacheDatArchive: %p\n", archive);

    struct RSCacheShared_FileListDat* filelist_dat = RSCacheShared_FileListDatNewFromCacheDatArchive(archive);
    if( !filelist_dat )
        return false;

    dat1_buildcache_set_fromconfigtable_config_jagfile(dat1(ToriAuxLib_C(instance->toriauxlib)), filelist_dat);

    RSCacheDat1Disk_ArchiveFree(archive);
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

    struct RSCacheDat2DiskLib_IORequest request;
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
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Configs )
        return false;
    if( item.u.cache.archive_id != RSCacheDat1A_ConfigKind_Textures )
        return false;
    if( item.u.cache.flags != 0 )
        return false;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return false;

    struct RSCacheShared_FileListDat* filelist = RSCacheShared_FileListDatNewFromCacheDatArchive(archive);
    RSCacheDat1Disk_ArchiveFree(archive);
    if( !filelist )
        return false;

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
    {
        struct RSCacheDat1A_ConfigTexture* cache_texture =
            RSCacheDat1A_ConfigTextureNewFromFilelistDat(filelist, i, 0);
        if( !cache_texture )
        {
            printf("RSCacheDat1A_ConfigTextureNewFromFilelistDat failed for texture %d\n", i);
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
        struct ToriAuxLibCore_Texture* gc_texture = ToriAuxLibC_TextureNewFromCacheDatTexture(
            cache_texture, animation_direction, animation_speed);
        RSCacheDat1A_ConfigTextureFree(cache_texture);
        if( !gc_texture )
        {
            printf("ToriAuxLibC_TextureNewFromCacheDatTexture failed for texture %d\n", i);
            assert(false);
            continue;
        }

        ToriAuxLibC_SubmitTexture(ToriAuxLib_C(instance->toriauxlib), i, gc_texture);
    }

    RSCacheShared_FileListDatFree(filelist);
    return true;
}

bool
LibToriRS_ScriptAPI_Dat2_TexturesLoad(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat2_TexturesLoad\n");
    if( !instance )
        return false;

    struct ToriAuxLibC* c = ToriAuxLib_C(instance->toriauxlib);
    if( !c || ToriAuxLibC_Mode(c) != TORIAUXLIBC_MODE_DAT2 )
        return false;

    struct RSCacheDat2Disk* cache = ToriAuxLibC_Dat2Disk(c);
    if( !cache )
        return false;

    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Textures, 0);
    if( !archive )
        return false;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, archive);

    struct RSCacheShared_FileList* filelist =
        RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return false;
    }

    struct RSCacheDat2Disk_ReferenceTable* textures_table =
        cache->tables[RSCacheDat2Disk_Table_Textures];
    if( !textures_table )
    {
        RSCacheShared_FileListFree(filelist);
        RSCacheDat2Disk_ArchiveFree(archive);
        return false;
    }

    struct RSCacheDat2Disk_ArchiveReference* reference = &textures_table->archives[0];
    int count = reference->children.count;
    if( filelist->file_count < count )
        count = filelist->file_count;

    printf("Dat2 textures: file_count=%d, children.count=%d, effective count=%d\n",
           filelist->file_count, reference->children.count, count);

    int loaded_count = 0;
    for( int i = 0; i < count; i++ )
    {
        int texture_id = reference->children.files[i].id;
        struct RSCacheDat2A_Texture* def = RSCacheDat2A_TextureDefinitionNewDecode(
            (const unsigned char*)filelist->files[i], filelist->file_sizes[i]);
        if( !def )
        {
            printf("Dat2 textures: failed to decode texture id %d at index %d\n", texture_id, i);
            continue;
        }

        struct RSCacheDat2A_SpritePack** packs = NULL;
        if( def->sprite_ids_count > 0 )
        {
            packs = calloc((size_t)def->sprite_ids_count, sizeof(struct RSCacheDat2A_SpritePack*));
            if( !packs )
            {
                RSCacheDat2A_TextureDefinitionFree(def);
                continue;
            }

            for( int k = 0; k < def->sprite_ids_count; k++ )
            {
                packs[k] = RSCacheDat2A_SpritePackNewFromCache(cache, def->sprite_ids[k]);
                if( !packs[k] )
                {
                    printf(
                        "Dat2 textures: failed to load sprite %d for texture %d\n",
                        def->sprite_ids[k],
                        texture_id);
                }
            }
        }

        struct ToriAuxLibCore_Texture* gc_texture = ToriAuxLibC_TextureNewFromDat2Definition(
            def,
            packs,
            def->animation_direction,
            def->animation_speed);

        if( packs )
        {
            for( int k = 0; k < def->sprite_ids_count; k++ )
            {
                if( packs[k] )
                    RSCacheDat2A_SpritePackFree(packs[k]);
            }
            free(packs);
        }

        RSCacheDat2A_TextureDefinitionFree(def);

        if( !gc_texture )
        {
            printf("Dat2 textures: failed to build engine texture %d\n", texture_id);
            continue;
        }

        ToriAuxLibC_SubmitTexture(c, texture_id, gc_texture);
        loaded_count++;
    }

    printf("Dat2 textures: loaded %d textures into core\n", loaded_count);

    RSCacheShared_FileListFree(filelist);
    RSCacheDat2Disk_ArchiveFree(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Dat2_SubmitTextures(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat2_SubmitTextures\n");
    if( !instance || !instance->toriauxlib )
        return;

    struct ToriAuxLibTD* td = ToriAuxLib_TD(instance->toriauxlib);
    struct ToriAuxLibCore* core = ToriAuxLib_Core(instance->toriauxlib);
    if( !td || !core )
        return;

    int submitted_count = 0;
    for( int i = 0; i < 256; i++ )
    {
        if( ToriAuxLibCore_TextureHas(core, i) )
        {
            if( ToriAuxLibTD_Texture(td, i) )
                submitted_count++;
        }
    }
    printf("Dat2 textures: submitted %d textures to scene\n", submitted_count);
}

const char*
LibToriRS_ScriptAPI_GetCacheMode(struct LibToriRS_Instance* instance)
{
    if( !instance || !instance->toriauxlib )
        return "dat1";

    if( ToriAuxLibC_Mode(ToriAuxLib_C(instance->toriauxlib)) == TORIAUXLIBC_MODE_DAT2 )
        return "dat2";

    return "dat1";
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

    struct RSCacheDat2A_Model* model = RSCacheDat2A_ModelNewDecode(data, data_size);
    if( !model )
        return;

    dat1_buildcache_model_add(dat1(ToriAuxLib_C(instance->toriauxlib)), model_id, model);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel\n");
    if( !instance || !ToriAuxLib_TD(instance->toriauxlib) )
        return;

    ToriAuxLibTD_SubmitModelFromDat1(ToriAuxLib_TD(instance->toriauxlib), model_id);
    ToriAuxLibTD_Model(ToriAuxLib_TD(instance->toriauxlib), model_id);
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

    struct RSCacheDat2DiskLib_IORequest request;
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
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Models )
        return false;
    if( item.u.cache.flags != 0 )
        return false;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return false;

    int model_id = item.u.cache.archive_id;

    struct RSCacheDat2A_Model* model = RSCacheDat2A_ModelNewFromDatArchive(archive, model_id);
    if( !model )
        return false;

    dat1_buildcache_model_add(dat1(ToriAuxLib_C(instance->toriauxlib)), model_id, model);
    RSCacheDat1Disk_ArchiveFree(archive);
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

    struct RSCacheDat2DiskLib_IORequest request;
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

    struct RSCacheDat2DiskLib_IORequest request;
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
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Maps )
        return false;
    if( item.u.cache.flags != CACHELIB_MAPCHUNK_TERRAIN )
        return false;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return false;

    int map_id = item.u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;

    struct RSCacheDat2A_MapTerrain* terrain = RSCacheDat2A_MapTerrainNewFromDecodeFlags(
        archive->data, archive->data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);
    if( !terrain )
        return false;

    dat1_buildcache_map_terrain_add(dat1(ToriAuxLib_C(instance->toriauxlib)), map_id, terrain);
    RSCacheDat1Disk_ArchiveFree(archive);
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
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Maps )
        return false;
    if( item.u.cache.flags != CACHELIB_MAPCHUNK_SCENERY )
        return false;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return false;

    int map_id = item.u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;

    struct RSCacheDat2A_MapLocs* locs = map_locs_new_from_decode(archive->data, archive->data_size);
    locs->_chunk_mapx = map_x;
    locs->_chunk_mapz = map_z;
    if( !locs )
        return false;

    dat1_buildcache_map_scenery_add(dat1(ToriAuxLib_C(instance->toriauxlib)), map_id, locs);
    RSCacheDat1Disk_ArchiveFree(archive);
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

    instance->model_viewer->core = ToriAuxLib_Core(instance->toriauxlib);

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

    game_runescape_set_core(instance->runescape, ToriAuxLib_Core(instance->toriauxlib));
    game_runescape_set_td(instance->runescape, ToriAuxLib_TD(instance->toriauxlib));
    game_runescape_set_vm(instance->runescape, ToriAuxLib_VM(instance->toriauxlib));

    instance->runescape_handle.kind = GAME_HANDLE_KIND_RUNESCAPE;
    instance->runescape_handle.u.runescape = instance->runescape;
    instance->active_game_kind = GAME_HANDLE_KIND_RUNESCAPE;

    struct Task_ToriAuxLibC_WorldRebuildNormal* task = Task_ToriAuxLibC_WorldRebuildNormal_New(
        ToriAuxLib_C(instance->toriauxlib), RUNESCAPE_ZONE_CENTER_X, RUNESCAPE_ZONE_CENTER_Z);
    LibToriRS_TasksAdd(instance, task, Task_ToriAuxLibC_WorldRebuildNormal_Run);

    if( ToriAuxLibC_Mode(ToriAuxLib_C(instance->toriauxlib)) == TORIAUXLIBC_MODE_DAT1 )
    {
        struct Task_RevConfigUILoad* ui_task = Task_RevConfigUILoad_New(
            ToriAuxLib_C(instance->toriauxlib), instance->scene, instance->runescape->ui_tree);
        LibToriRS_TasksAdd(instance, ui_task, Task_RevConfigUILoad_Run);
    }
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

    struct ToriDraw_ModelHandle hnd = ToriAuxLibTD_Model(ToriAuxLib_TD(instance->toriauxlib), model_id);
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

    dat1_buildcache_model_remove(dat1(ToriAuxLib_C(instance->toriauxlib)), model_id);
}

void
LibToriRS_ScriptAPI_Dat1_TexturesCleanup(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_TexturesCleanup\n");
    if( !instance )
        return;

    ToriAuxLibCore_TexturesClearAll(ToriAuxLib_Core(instance->toriauxlib));
}

void
LibToriRS_ScriptAPI_Dat1_SubmitTextures(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitTextures\n");
    if( !instance || !ToriAuxLib_TD(instance->toriauxlib) )
        return;

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
        ToriAuxLibTD_Texture(ToriAuxLib_TD(instance->toriauxlib), i);
}

void
LibToriRS_ScriptAPI_GameCache_ModelsClearAll(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_GameCache_ModelsClearAll\n");
    if( !instance )
        return;

    if( instance->scene )
        ToriDraw_SceneModelsClearAll(instance->scene);
    ToriAuxLibCore_ModelsClearAll(ToriAuxLib_Core(instance->toriauxlib));
}

void
LibToriRS_ScriptAPI_Dat1_VersionListFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_VersionListFetch\n");

    // struct RSCacheDat2DiskLib_IORequest request;
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
    // if( item.u.cache.table_id != RSCacheDat1Disk_Table_Configs )
    //     return false;
    // if( item.u.cache.archive_id != RSCacheDat1A_ConfigKind_VersionList )
    //     return false;
    // if( item.u.cache.flags != 0 )
    //     return false;

    // struct RSCacheDat1Disk_Archive* archive = item.data;
    // if( !archive )
    //     return false;

    // struct RSCacheShared_FileListDat* filelist_dat = RSCacheShared_FileListDatNewFromCacheDatArchive(archive);
    // if( !filelist_dat )
    //     return false;

    // dat1_buildcache_set_versionlist_jagfile(instance->dat1_buildcache, filelist_dat);

    // RSCacheDat1Disk_ArchiveFree(archive);
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

    // struct RSCacheDat2DiskLib_IORequest request;
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
    // if( item.u.cache.table_id != RSCacheDat1Disk_Table_Animations )
    //     return false;
    // if( item.u.cache.flags != 0 )
    //     return false;

    // struct RSCacheDat1Disk_Archive* archive = item.data;
    // if( !archive )
    //     return false;

    // int animbaseframes_id = item.u.cache.archive_id;

    // struct RSCacheDat1A_AnimBaseFrames* animbaseframes =
    //     RSCacheDat1A_AnimBaseFramesNewDecode(archive->data, archive->data_size);
    // if( !animbaseframes )
    // {
    //     RSCacheDat1Disk_ArchiveFree(archive);
    //     return false;
    // }

    // dat1_buildcache_animbaseframes_add(
    //     instance->dat1_buildcache, animbaseframes_id, animbaseframes);

    // RSCacheDat1Disk_ArchiveFree(archive);
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
    // // struct ToriAuxLibCore* gamecache = instance->core;

    // struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(buildcache->sequences_hmap);
    // struct Dat1MapEntry_Sequence* entry = NULL;

    // // while( (entry = (struct Dat1MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
    // // {
    // //     if( !entry->sequence )
    // //         continue;

    // //     struct ToriAuxLibCore_Sequence* seq =
    // //         gamecache_sequence_new_from_cache_dat_sequence(entry->sequence);
    // //     if( !seq )
    // //         continue;

    // //     entry->sequence = NULL;
    // //     gamecache_sequence_add(core, entry->id, seq);
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
