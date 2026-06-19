#include "dat1_buildcache.h"

#include "osrs/rscache/shared/rscache_shared_rs_buffer.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_locs.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/rscache_dat2a_maps.h"
#include "osrs/rscache/dat1a/rscache_dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/rscache_dat1a_config_component.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct MapEntry_CacheModel
{
    int id;
    struct RSCacheDat2A_Model* model;
};

struct MapEntry_Terrain
{
    int id;
    struct RSCacheDat2A_MapTerrain* terrain;
};

struct MapEntry_Scenery
{
    int id;
    struct RSCacheDat2A_MapLocs* locs;
};

struct MapEntry_Sequence
{
    int id;
    struct RSCacheDat1A_ConfigSequence* sequence;
};

struct MapEntry_Flotype
{
    int id;
    struct RSCacheDat2A_ConfigOverlay* flotype;
};

struct MapEntry_ConfigLoc
{
    int id;
    struct RSCacheDat2A_ConfigLocation* config_loc;
};

struct MapEntry_AnimBaseFrames
{
    int id;
    struct RSCacheDat1A_AnimBaseFrames* animbaseframes;
};

static void
dat1_buildcache_interfaces_free(struct RSCacheDat1A_ConfigComponentList* interfaces)
{
    if( !interfaces )
        return;

    if( interfaces->components )
    {
        for( int i = 0; i < interfaces->components_count; i++ )
        {
            if( interfaces->components[i] )
                RSCacheDat1A_ConfigComponentFree(interfaces->components[i]);
        }
        free(interfaces->components);
    }
    free(interfaces);
}

static struct ToriDraw_Map*
dat1_buildcache_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = ToriDraw_MapBufferSizeFor(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return ToriDraw_MapNew(&config, 0);
}

static void
dat1_buildcache_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    free(ToriDraw_MapBufferPtr(map));
    ToriDraw_MapFree(map);
}

struct Dat1BuildCache*
dat1_buildcache_new(void)
{
    struct Dat1BuildCache* dat1_buildcache = malloc(sizeof(struct Dat1BuildCache));
    memset(dat1_buildcache, 0, sizeof(struct Dat1BuildCache));

    dat1_buildcache->models_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_CacheModel), 1024);
    dat1_buildcache->map_terrain_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_Terrain), 256);
    dat1_buildcache->map_scenery_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_Scenery), 256);
    dat1_buildcache->sequences_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_Sequence), 1024);
    dat1_buildcache->flotype_hmap = dat1_buildcache_map_new(sizeof(struct MapEntry_Flotype), 256);
    dat1_buildcache->config_loc_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_ConfigLoc), 1024);
    dat1_buildcache->animbaseframes_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_AnimBaseFrames), 512);

    return dat1_buildcache;
}

void
dat1_buildRSCacheDat2Disk_Free(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    if( dat1_buildcache->fromconfigtable_config_jagfile )
        RSCacheShared_FileListDatFree(dat1_buildcache->fromconfigtable_config_jagfile);

    if( dat1_buildcache->versionlist_jagfile )
        RSCacheShared_FileListDatFree(dat1_buildcache->versionlist_jagfile);

    if( dat1_buildcache->media_2d_graphics_jagfile )
        RSCacheShared_FileListDatFree(dat1_buildcache->media_2d_graphics_jagfile);

    if( dat1_buildcache->interfaces )
        dat1_buildcache_interfaces_free(dat1_buildcache->interfaces);

    if( dat1_buildcache->models_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->models_hmap);
        struct MapEntry_CacheModel* entry = NULL;
        while( (entry = (struct MapEntry_CacheModel*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->model )
                RSCacheDat2A_ModelFree(entry->model);
        }
        ToriDraw_MapIterFree(iter);
        dat1_buildcache_map_free(dat1_buildcache->models_hmap);
    }

    if( dat1_buildcache->map_terrain_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->map_terrain_hmap);
        struct MapEntry_Terrain* entry = NULL;
        while( (entry = (struct MapEntry_Terrain*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->terrain )
                RSCacheDat2A_MapTerrainFree(entry->terrain);
        }
        ToriDraw_MapIterFree(iter);
        dat1_buildcache_map_free(dat1_buildcache->map_terrain_hmap);
    }

    if( dat1_buildcache->map_scenery_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->map_scenery_hmap);
        struct MapEntry_Scenery* entry = NULL;
        while( (entry = (struct MapEntry_Scenery*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->locs )
                RSCacheDat2A_MapLocsFree(entry->locs);
        }
        ToriDraw_MapIterFree(iter);
        dat1_buildcache_map_free(dat1_buildcache->map_scenery_hmap);
    }

    if( dat1_buildcache->sequences_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->sequences_hmap);
        struct MapEntry_Sequence* entry = NULL;
        while( (entry = (struct MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->sequence )
                RSCacheDat1A_ConfigSequenceFree(entry->sequence);
        }
        ToriDraw_MapIterFree(iter);
        dat1_buildcache_map_free(dat1_buildcache->sequences_hmap);
    }

    if( dat1_buildcache->flotype_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->flotype_hmap);
        struct MapEntry_Flotype* entry = NULL;
        while( (entry = (struct MapEntry_Flotype*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->flotype )
                config_floortype_overlay_free(entry->flotype);
        }
        ToriDraw_MapIterFree(iter);
        dat1_buildcache_map_free(dat1_buildcache->flotype_hmap);
    }

    if( dat1_buildcache->config_loc_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->config_loc_hmap);
        struct MapEntry_ConfigLoc* entry = NULL;
        while( (entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->config_loc )
                config_locs_free(entry->config_loc);
        }
        ToriDraw_MapIterFree(iter);
        dat1_buildcache_map_free(dat1_buildcache->config_loc_hmap);
    }

    if( dat1_buildcache->animbaseframes_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->animbaseframes_hmap);
        struct MapEntry_AnimBaseFrames* entry = NULL;
        while( (entry = (struct MapEntry_AnimBaseFrames*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->animbaseframes )
                RSCacheDat1A_AnimBaseFramesFree(entry->animbaseframes);
        }
        ToriDraw_MapIterFree(iter);
        dat1_buildcache_map_free(dat1_buildcache->animbaseframes_hmap);
    }

    dat1_buildcache_texture_clear(dat1_buildcache);

    free(dat1_buildcache);
}

void
dat1_buildcache_set_fromconfigtable_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheShared_FileListDat* fromconfigtable_config_jagfile)
{
    if( dat1_buildcache->fromconfigtable_config_jagfile )
        RSCacheShared_FileListDatFree(dat1_buildcache->fromconfigtable_config_jagfile);

    dat1_buildcache->fromconfigtable_config_jagfile = fromconfigtable_config_jagfile;
}

void
dat1_buildcache_clear_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;
    if( dat1_buildcache->fromconfigtable_config_jagfile )
        RSCacheShared_FileListDatFree(dat1_buildcache->fromconfigtable_config_jagfile);
    dat1_buildcache->fromconfigtable_config_jagfile = NULL;
}

void
dat1_buildcache_set_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheShared_FileListDat* versionlist_jagfile)
{
    if( dat1_buildcache->versionlist_jagfile )
        RSCacheShared_FileListDatFree(dat1_buildcache->versionlist_jagfile);

    dat1_buildcache->versionlist_jagfile = versionlist_jagfile;
}

void
dat1_buildcache_clear_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;
    if( dat1_buildcache->versionlist_jagfile )
        RSCacheShared_FileListDatFree(dat1_buildcache->versionlist_jagfile);
    dat1_buildcache->versionlist_jagfile = NULL;
}

void
dat1_buildcache_set_media_2d_graphics_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheShared_FileListDat* media_2d_graphics_jagfile)
{
    if( !dat1_buildcache )
        return;

    if( dat1_buildcache->media_2d_graphics_jagfile )
        RSCacheShared_FileListDatFree(dat1_buildcache->media_2d_graphics_jagfile);

    dat1_buildcache->media_2d_graphics_jagfile = media_2d_graphics_jagfile;
}

struct RSCacheShared_FileListDat*
dat1_buildcache_get_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return NULL;
    return dat1_buildcache->media_2d_graphics_jagfile;
}

void
dat1_buildcache_set_interfaces(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheDat1A_ConfigComponentList* interfaces)
{
    if( !dat1_buildcache )
        return;

    if( dat1_buildcache->interfaces )
        dat1_buildcache_interfaces_free(dat1_buildcache->interfaces);

    dat1_buildcache->interfaces = interfaces;
}

struct RSCacheDat1A_ConfigComponentList*
dat1_buildcache_get_interfaces(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return NULL;
    return dat1_buildcache->interfaces;
}

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct RSCacheDat2A_Model* model)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)ToriDraw_MapSearch(
        dat1_buildcache->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = model;
}

struct RSCacheDat2A_Model*
dat1_buildcache_model_get(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)ToriDraw_MapSearch(
        dat1_buildcache->models_hmap, &model_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

void
dat1_buildcache_model_remove(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    if( !dat1_buildcache )
        return;

    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)ToriDraw_MapSearch(
        dat1_buildcache->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry || !entry->model )
        return;

    RSCacheDat2A_ModelFree(entry->model);
}

void
dat1_buildcache_map_terrain_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct RSCacheDat2A_MapTerrain* terrain)
{
    struct MapEntry_Terrain* entry = (struct MapEntry_Terrain*)ToriDraw_MapSearch(
        dat1_buildcache->map_terrain_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = map_id;
    entry->terrain = terrain;
}

struct RSCacheDat2A_MapTerrain*
dat1_buildcache_map_terrain_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    struct MapEntry_Terrain* entry = (struct MapEntry_Terrain*)ToriDraw_MapSearch(
        dat1_buildcache->map_terrain_hmap, &map_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
dat1_buildcache_map_terrain_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    return dat1_buildcache_map_terrain_get(dat1_buildcache, map_id) != NULL;
}

void
dat1_buildcache_map_scenery_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct RSCacheDat2A_MapLocs* locs)
{
    struct MapEntry_Scenery* entry = (struct MapEntry_Scenery*)ToriDraw_MapSearch(
        dat1_buildcache->map_scenery_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = map_id;
    entry->locs = locs;
}

struct RSCacheDat2A_MapLocs*
dat1_buildcache_map_scenery_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    struct MapEntry_Scenery* entry = (struct MapEntry_Scenery*)ToriDraw_MapSearch(
        dat1_buildcache->map_scenery_hmap, &map_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
dat1_buildcache_map_scenery_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    return dat1_buildcache_map_scenery_get(dat1_buildcache, map_id) != NULL;
}

void
dat1_buildcache_texture_set(
    struct Dat1BuildCache* dat1_buildcache,
    int index,
    struct ToriDraw_Texture* texture)
{
    if( !dat1_buildcache || index < 0 || index >= DAT1_TEXTURE_COUNT )
        return;

    if( dat1_buildcache->textures[index] )
    {
        ToriDraw_TextureFree(dat1_buildcache->textures[index]);
        dat1_buildcache->textures[index] = NULL;
    }

    dat1_buildcache->textures[index] = texture;
    if( index >= dat1_buildcache->texture_count )
        dat1_buildcache->texture_count = index + 1;
}

struct ToriDraw_Texture*
dat1_buildcache_texture_get(
    struct Dat1BuildCache* dat1_buildcache,
    int index)
{
    if( !dat1_buildcache || index < 0 || index >= DAT1_TEXTURE_COUNT )
        return NULL;
    return dat1_buildcache->textures[index];
}

void
dat1_buildcache_texture_clear(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
    {
        if( dat1_buildcache->textures[i] )
            ToriDraw_TextureFree(dat1_buildcache->textures[i]);
        dat1_buildcache->textures[i] = NULL;
    }
    dat1_buildcache->texture_count = 0;
}

void
dat1_buildcache_sequences_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    struct RSCacheShared_FileListDat* config_jagfile = dat1_buildcache->fromconfigtable_config_jagfile;
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = RSCacheShared_FileListDatFindFileByName(config_jagfile, "seq.dat");
    assert(data_file_idx != -1 && "Data file must be found");

    struct RSCacheShared_RSBuffer buffer = {
        .data = (uint8_t*)config_jagfile->files[data_file_idx],
        .size = (uint32_t)config_jagfile->file_sizes[data_file_idx],
        .position = 0,
    };

    int count = g2(&buffer);
    for( int i = 0; i < count; i++ )
    {
        struct RSCacheDat1A_ConfigSequence* sequence = malloc(sizeof(struct RSCacheDat1A_ConfigSequence));
        memset(sequence, 0, sizeof(struct RSCacheDat1A_ConfigSequence));

        buffer.position += (uint32_t)RSCacheDat1A_ConfigSequenceDecodeInplace(
            sequence,
            (char*)buffer.data + buffer.position,
            (int)buffer.size - (int)buffer.position);

        struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)ToriDraw_MapSearch(
            dat1_buildcache->sequences_hmap, &i, TORIDRAW_MAP_INSERT);
        if( !entry )
        {
            RSCacheDat1A_ConfigSequenceFree(sequence);
            continue;
        }

        if( entry->sequence )
            RSCacheDat1A_ConfigSequenceFree(entry->sequence);

        entry->id = i;
        entry->sequence = sequence;
    }
}

void
dat1_buildcache_floortypes_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    struct RSCacheShared_FileListDat* config_jagfile = dat1_buildcache->fromconfigtable_config_jagfile;
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = RSCacheShared_FileListDatFindFileByName(config_jagfile, "flo.dat");
    assert(data_file_idx != -1 && "Data file must be found");

    struct RSCacheShared_RSBuffer buffer = {
        .data = (uint8_t*)config_jagfile->files[data_file_idx],
        .size = (uint32_t)config_jagfile->file_sizes[data_file_idx],
        .position = 0,
    };

    int count = g2(&buffer);
    for( int i = 0; i < count; i++ )
    {
        struct RSCacheDat2A_ConfigOverlay* flotype = malloc(sizeof(struct RSCacheDat2A_ConfigOverlay));
        memset(flotype, 0, sizeof(struct RSCacheDat2A_ConfigOverlay));
        flotype->_id = i;

        buffer.position += (uint32_t)config_floortype_overlay_decode_inplace(
            flotype, (char*)buffer.data + buffer.position, (int)buffer.size - (int)buffer.position);

        struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)ToriDraw_MapSearch(
            dat1_buildcache->flotype_hmap, &i, TORIDRAW_MAP_INSERT);
        if( !entry )
        {
            config_floortype_overlay_free(flotype);
            continue;
        }

        if( entry->flotype )
            config_floortype_overlay_free(entry->flotype);

        entry->id = i;
        entry->flotype = flotype;
    }
}

struct RSCacheDat2A_ConfigLocation*
dat1_buildcache_config_loc_get(
    struct Dat1BuildCache* dat1_buildcache,
    int loc_id)
{
    struct MapEntry_ConfigLoc* entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapSearch(
        dat1_buildcache->config_loc_hmap, &loc_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->config_loc;
}

void
dat1_buildcache_init_scenery_configs_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    struct RSCacheShared_FileListDat* config_jagfile = dat1_buildcache->fromconfigtable_config_jagfile;
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = RSCacheShared_FileListDatFindFileByName(config_jagfile, "loc.dat");
    int index_file_idx = RSCacheShared_FileListDatFindFileByName(config_jagfile, "loc.idx");

    assert(data_file_idx != -1 && "Data file must be found");
    assert(index_file_idx != -1 && "Index file must be found");

    struct RSCacheShared_FileListDatIndexed* filelist_indexed = RSCacheShared_FileListDatIndexedNewFromDecode(
        config_jagfile->files[index_file_idx],
        config_jagfile->file_sizes[index_file_idx],
        config_jagfile->files[data_file_idx],
        config_jagfile->file_sizes[data_file_idx]);

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->map_scenery_hmap);
    struct MapEntry_Scenery* scenery_entry = NULL;
    while( (scenery_entry = (struct MapEntry_Scenery*)ToriDraw_MapIterNext(iter)) )
    {
        struct RSCacheDat2A_MapLocs* locs = scenery_entry->locs;
        if( !locs )
            continue;

        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct RSCacheDat2A_MapLoc* loc = &locs->locs[i];
            assert(loc->loc_id != -1 && "Loc id must be valid");
            assert(loc->loc_id < filelist_indexed->offset_count && "Loc id must be within range");

            if( dat1_buildcache_config_loc_get(dat1_buildcache, loc->loc_id) )
                continue;

            struct RSCacheDat2A_ConfigLocation* config_loc = malloc(sizeof(struct RSCacheDat2A_ConfigLocation));
            memset(config_loc, 0, sizeof(struct RSCacheDat2A_ConfigLocation));

            int offset = filelist_indexed->offsets[loc->loc_id];

            config_locs_decode_inplace(
                config_loc,
                filelist_indexed->data + offset,
                filelist_indexed->data_size - offset,
                CONFIG_LOC_DECODE_DAT);

            config_loc->_id = loc->loc_id;

            struct MapEntry_ConfigLoc* entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapSearch(
                dat1_buildcache->config_loc_hmap, &loc->loc_id, TORIDRAW_MAP_INSERT);
            if( !entry )
            {
                config_locs_free(config_loc);
                continue;
            }

            entry->id = loc->loc_id;
            entry->config_loc = config_loc;
        }
    }
    ToriDraw_MapIterFree(iter);
    RSCacheShared_FileListDatIndexedFree(filelist_indexed);
}

void
dat1_buildcache_animbaseframes_add(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id,
    struct RSCacheDat1A_AnimBaseFrames* animbaseframes)
{
    struct MapEntry_AnimBaseFrames* entry = (struct MapEntry_AnimBaseFrames*)ToriDraw_MapSearch(
        dat1_buildcache->animbaseframes_hmap, &animbaseframes_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->animbaseframes )
        RSCacheDat1A_AnimBaseFramesFree(entry->animbaseframes);

    entry->id = animbaseframes_id;
    entry->animbaseframes = animbaseframes;
}

static void
dat1_buildcache_map_reset(
    struct ToriDraw_Map** map_out,
    int entry_size,
    int capacity)
{
    if( !map_out || !*map_out )
        return;

    dat1_buildcache_map_free(*map_out);
    *map_out = dat1_buildcache_map_new(entry_size, capacity);
}

void
dat1_buildcache_sequences_reset(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    dat1_buildcache_map_reset(
        &dat1_buildcache->sequences_hmap, sizeof(struct MapEntry_Sequence), 1024);
}

void
dat1_buildcache_floortypes_reset(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    dat1_buildcache_map_reset(
        &dat1_buildcache->flotype_hmap, sizeof(struct MapEntry_Flotype), 256);
}

void
dat1_buildcache_scenery_configs_reset(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    dat1_buildcache_map_reset(
        &dat1_buildcache->config_loc_hmap, sizeof(struct MapEntry_ConfigLoc), 1024);
}

void
dat1_buildcache_animbaseframes_reset(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    dat1_buildcache_map_reset(
        &dat1_buildcache->animbaseframes_hmap, sizeof(struct MapEntry_AnimBaseFrames), 512);
}

void
dat1_buildcache_sequences_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache || !dat1_buildcache->sequences_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->sequences_hmap);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sequence )
            RSCacheDat1A_ConfigSequenceFree(entry->sequence);
    }
    ToriDraw_MapIterFree(iter);
    dat1_buildcache_sequences_reset(dat1_buildcache);
}

void
dat1_buildcache_floortypes_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache || !dat1_buildcache->flotype_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->flotype_hmap);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->flotype )
            config_floortype_overlay_free(entry->flotype);
    }
    ToriDraw_MapIterFree(iter);
    dat1_buildcache_floortypes_reset(dat1_buildcache);
}

void
dat1_buildcache_scenery_configs_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache || !dat1_buildcache->config_loc_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->config_loc_hmap);
    struct MapEntry_ConfigLoc* entry = NULL;
    while( (entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->config_loc )
            config_locs_free(entry->config_loc);
    }
    ToriDraw_MapIterFree(iter);
    dat1_buildcache_scenery_configs_reset(dat1_buildcache);
}

void
dat1_buildcache_animbaseframes_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache || !dat1_buildcache->animbaseframes_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->animbaseframes_hmap);
    struct MapEntry_AnimBaseFrames* entry = NULL;
    while( (entry = (struct MapEntry_AnimBaseFrames*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->animbaseframes )
            RSCacheDat1A_AnimBaseFramesFree(entry->animbaseframes);
    }
    ToriDraw_MapIterFree(iter);
    dat1_buildcache_animbaseframes_reset(dat1_buildcache);
}

struct RSCacheDat1A_AnimBaseFrames*
dat1_buildcache_animbaseframes_get(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id)
{
    struct MapEntry_AnimBaseFrames* entry = (struct MapEntry_AnimBaseFrames*)ToriDraw_MapSearch(
        dat1_buildcache->animbaseframes_hmap, &animbaseframes_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->animbaseframes;
}

bool
dat1_buildcache_animbaseframes_has(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id)
{
    return dat1_buildcache_animbaseframes_get(dat1_buildcache, animbaseframes_id) != NULL;
}

int
dat1_buildcache_get_animbaseframes_count_from_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache)
{
    (void)dat1_buildcache;
    return 264;
}

static int
dat1_int_cmp(const void* a, const void* b)
{
    return *(const int*)a - *(const int*)b;
}

static int
dat1_unique_sorted_int(
    int* arr,
    int count)
{
    if( count <= 0 )
        return 0;

    int write = 1;
    for( int i = 1; i < count; i++ )
    {
        if( arr[i] != arr[write - 1] )
            arr[write++] = arr[i];
    }
    return write;
}

int
dat1_buildcache_get_scenery_model_ids(
    struct Dat1BuildCache* dat1_buildcache,
    int loc_id,
    int** model_ids_out)
{
    struct RSCacheDat2A_ConfigLocation* config_loc = dat1_buildcache_config_loc_get(dat1_buildcache, loc_id);
    if( !config_loc || !config_loc->models )
    {
        *model_ids_out = NULL;
        return 0;
    }

    int capacity = 16;
    int count = 0;
    int* model_ids = malloc((size_t)capacity * sizeof(int));
    if( !model_ids )
    {
        *model_ids_out = NULL;
        return 0;
    }

    int** model_id_sets = config_loc->models;
    int* lengths = config_loc->lengths;
    int* shapes = config_loc->shapes;
    int shapes_and_model_count = config_loc->shapes_and_model_count;

    if( !shapes )
    {
        int inner = lengths[0];
        for( int i = 0; i < inner; i++ )
        {
            int model_id = model_id_sets[0][i];
            if( !model_id )
                continue;
            if( count >= capacity )
            {
                capacity *= 2;
                int* grow = realloc(model_ids, (size_t)capacity * sizeof(int));
                if( !grow )
                    goto fail;
                model_ids = grow;
            }
            model_ids[count++] = model_id;
        }
    }
    else
    {
        for( int i = 0; i < shapes_and_model_count; i++ )
        {
            int inner = lengths[i];
            for( int j = 0; j < inner; j++ )
            {
                int model_id = model_id_sets[i][j];
                if( !model_id )
                    continue;
                if( count >= capacity )
                {
                    capacity *= 2;
                    int* grow = realloc(model_ids, (size_t)capacity * sizeof(int));
                    if( !grow )
                        goto fail;
                    model_ids = grow;
                }
                model_ids[count++] = model_id;
            }
        }
    }

    *model_ids_out = model_ids;
    return count;

fail:
    free(model_ids);
    *model_ids_out = NULL;
    return 0;
}

int
dat1_buildcache_get_all_unique_scenery_model_ids(
    struct Dat1BuildCache* dat1_buildcache,
    int** model_ids_out)
{
    *model_ids_out = NULL;
    if( !dat1_buildcache )
        return 0;

    int loc_capacity = 512;
    int loc_count = 0;
    int* loc_arr = malloc((size_t)loc_capacity * sizeof(int));
    if( !loc_arr )
        return 0;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->map_scenery_hmap);
    struct MapEntry_Scenery* scenery_entry = NULL;
    while( (scenery_entry = (struct MapEntry_Scenery*)ToriDraw_MapIterNext(iter)) )
    {
        struct RSCacheDat2A_MapLocs* locs = scenery_entry->locs;
        if( !locs )
            continue;

        for( int i = 0; i < locs->locs_count; i++ )
        {
            int loc_id = locs->locs[i].loc_id;
            if( loc_count >= loc_capacity )
            {
                loc_capacity *= 2;
                int* grow = realloc(loc_arr, (size_t)loc_capacity * sizeof(int));
                if( !grow )
                    goto loc_fail;
                loc_arr = grow;
            }
            loc_arr[loc_count++] = loc_id;
        }
    }
    ToriDraw_MapIterFree(iter);

    if( loc_count == 0 )
    {
        free(loc_arr);
        return 0;
    }

    qsort(loc_arr, (size_t)loc_count, sizeof(int), dat1_int_cmp);
    int nunique_loc = dat1_unique_sorted_int(loc_arr, loc_count);

    int model_capacity = 1024;
    int model_count = 0;
    int* model_arr = malloc((size_t)model_capacity * sizeof(int));
    if( !model_arr )
        goto loc_fail;

    for( int li = 0; li < nunique_loc; li++ )
    {
        int* mids = NULL;
        int nm = dat1_buildcache_get_scenery_model_ids(dat1_buildcache, loc_arr[li], &mids);
        for( int mi = 0; mi < nm; mi++ )
        {
            if( model_count >= model_capacity )
            {
                model_capacity *= 2;
                int* grow = realloc(model_arr, (size_t)model_capacity * sizeof(int));
                if( !grow )
                {
                    free(mids);
                    goto model_fail;
                }
                model_arr = grow;
            }
            model_arr[model_count++] = mids[mi];
        }
        free(mids);
    }
    free(loc_arr);

    if( model_count == 0 )
    {
        free(model_arr);
        return 0;
    }

    qsort(model_arr, (size_t)model_count, sizeof(int), dat1_int_cmp);
    int nunique_m = dat1_unique_sorted_int(model_arr, model_count);
    if( nunique_m < model_count )
    {
        int* shrink = realloc(model_arr, (size_t)nunique_m * sizeof(int));
        if( shrink )
            model_arr = shrink;
    }

    *model_ids_out = model_arr;
    return nunique_m;

loc_fail:
    ToriDraw_MapIterFree(iter);
    free(loc_arr);
    return 0;

model_fail:
    free(loc_arr);
    free(model_arr);
    return 0;
}

void
dat1_buildcache_foreach_sequence(
    struct Dat1BuildCache* dat1_buildcache,
    Dat1BuildCacheSequenceCallback callback,
    void* user_data)
{
    if( !dat1_buildcache || !callback || !dat1_buildcache->sequences_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->sequences_hmap);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sequence )
            callback(entry->id, entry->sequence, user_data);
    }
    ToriDraw_MapIterFree(iter);
}

void
dat1_buildcache_foreach_flotype(
    struct Dat1BuildCache* dat1_buildcache,
    Dat1BuildCacheFlotypeCallback callback,
    void* user_data)
{
    if( !dat1_buildcache || !callback || !dat1_buildcache->flotype_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->flotype_hmap);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->flotype )
            callback(entry->id, entry->flotype, user_data);
    }
    ToriDraw_MapIterFree(iter);
}

void
dat1_buildcache_foreach_config_loc(
    struct Dat1BuildCache* dat1_buildcache,
    Dat1BuildCacheLocationCallback callback,
    void* user_data)
{
    if( !dat1_buildcache || !callback || !dat1_buildcache->config_loc_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat1_buildcache->config_loc_hmap);
    struct MapEntry_ConfigLoc* entry = NULL;
    while( (entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->config_loc )
            callback(entry->id, entry->config_loc, user_data);
    }
    ToriDraw_MapIterFree(iter);
}
