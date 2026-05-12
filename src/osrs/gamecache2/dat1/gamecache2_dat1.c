#include "gamecache2_dat1.h"

#include "../gamecache2_internal.h"
#include "graphics/dash.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables_dat/configs_dat.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct ModelEntry
{
    int id;
    struct CacheModel* model;
};

struct FlotypeEntry
{
    int id;
    struct CacheConfigOverlay* flotype;
};

struct MapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapTerrain* map_terrain;
};

struct MapSceneryEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapLocs* locs;
};

struct GameCache2Dat1
{
    struct FileListDat* cfg_config_jagfile;
    struct FileListDat* cfg_versionlist_jagfile;
    struct FileListDat* cfg_media_jagfile;

    struct DashMap* map_terrains_hmap;
    struct DashMap* map_scenery_hmap;
    struct DashMap* flotype_hmap;
    struct DashMap* models_hmap;
};

struct GameCache2Dat1*
GameCache2Dat1_New()
{
    struct GameCache2Dat1* dat1 = malloc(sizeof(struct GameCache2Dat1));
    if( !dat1 )
        return NULL;
    return dat1;
}

void
GameCache2Dat1_Free(struct GameCache2Dat1* dat1)
{
    if( !dat1 )
        return;
    free(dat1);
}

void
GameCache2Dat1_WorldBuild_BeginFetch(
    struct GameCache2* gc2,
    struct GameCache2IORequestBuffer* request_buffer)
{
    assert(gc2 && request_buffer && "Invalid arguments");
    struct GameCache2Dat1* dat1 = gc2->u.dat1;

    if( !dat1->cfg_versionlist_jagfile )
        GameCache2IORequestBuffer_PushRequest(
            request_buffer, CACHE_DAT_CONFIGS, CONFIG_DAT_VERSION_LIST, 0);
    if( !dat1->cfg_config_jagfile )
        GameCache2IORequestBuffer_PushRequest(
            request_buffer, CACHE_DAT_CONFIGS, CONFIG_DAT_CONFIGS, 0);
}

void
GameCache2Dat1_WorldBuild_BeginLoad(
    struct GameCache2* gc2,
    struct GameCache2Archive* archives)
{
    assert(gc2 && archives && "Invalid arguments");
    struct GameCache2Dat1* dat1 = gc2->u.dat1;

    struct GameCache2Archive* archive = NULL;
    for( archive = archives; archive; archive = archive->next )
    {
        if( archive->table_id != CACHE_DAT_CONFIGS )
            assert(false && "Unexpected table id");
        switch( archive->archive_id )
        {
        case CONFIG_DAT_CONFIGS:
        {
            assert(dat1->cfg_config_jagfile == NULL && "Config file already set");
            struct FileListDat* filelist =
                filelist_dat_new_from_decode(archive->data, archive->data_size);
            dat1->cfg_config_jagfile = filelist;
        }
        break;
        case CONFIG_DAT_VERSION_LIST:
        {
            assert(dat1->cfg_versionlist_jagfile == NULL && "Version list file already set");
            struct FileListDat* filelist =
                filelist_dat_new_from_decode(archive->data, archive->data_size);
            dat1->cfg_versionlist_jagfile = filelist;
        }
        break;
        default:
            assert(false && "Unexpected archive id");
            break;
        }
    }
}

void
GameCache2Dat1_WorldBuild_End(struct GameCache2* gc2)
{
    // No - Op
}