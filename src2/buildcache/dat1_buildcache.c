#include "dat1_buildcache.h"

#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct MapEntry_CacheModel
{
    int id;
    struct CacheModel* model;
};

struct MapEntry_Terrain
{
    int id;
    struct CacheMapTerrain* terrain;
};

struct MapEntry_Scenery
{
    int id;
    struct CacheMapLocs* locs;
};

struct MapEntry_Sequence
{
    int id;
    struct CacheDatSequence* sequence;
};

struct MapEntry_Flotype
{
    int id;
    struct CacheConfigOverlay* flotype;
};

struct MapEntry_ConfigLoc
{
    int id;
    struct CacheConfigLocation* config_loc;
};

struct MapEntry_AnimBaseFrames
{
    int id;
    struct CacheDatAnimBaseFrames* animbaseframes;
};

static struct ToriDraw_Map*
dat1_buildcache_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = toridraw_map_buffer_size_for(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return toridraw_map_new(&config, 0);
}

static void
dat1_buildcache_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    free(toridraw_map_buffer_ptr(map));
    toridraw_map_free(map);
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
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    if( dat1_buildcache->fromconfigtable_config_jagfile )
        filelist_dat_free(dat1_buildcache->fromconfigtable_config_jagfile);

    if( dat1_buildcache->versionlist_jagfile )
        filelist_dat_free(dat1_buildcache->versionlist_jagfile);

    if( dat1_buildcache->media_2d_graphics_jagfile )
        filelist_dat_free(dat1_buildcache->media_2d_graphics_jagfile);

    if( dat1_buildcache->models_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->models_hmap);
        struct MapEntry_CacheModel* entry = NULL;
        while( (entry = (struct MapEntry_CacheModel*)toridraw_map_iter_next(iter)) )
        {
            if( entry->model )
                model_free(entry->model);
        }
        toridraw_map_iter_free(iter);
        dat1_buildcache_map_free(dat1_buildcache->models_hmap);
    }

    if( dat1_buildcache->map_terrain_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->map_terrain_hmap);
        struct MapEntry_Terrain* entry = NULL;
        while( (entry = (struct MapEntry_Terrain*)toridraw_map_iter_next(iter)) )
        {
            if( entry->terrain )
                map_terrain_free(entry->terrain);
        }
        toridraw_map_iter_free(iter);
        dat1_buildcache_map_free(dat1_buildcache->map_terrain_hmap);
    }

    if( dat1_buildcache->map_scenery_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->map_scenery_hmap);
        struct MapEntry_Scenery* entry = NULL;
        while( (entry = (struct MapEntry_Scenery*)toridraw_map_iter_next(iter)) )
        {
            if( entry->locs )
                map_locs_free(entry->locs);
        }
        toridraw_map_iter_free(iter);
        dat1_buildcache_map_free(dat1_buildcache->map_scenery_hmap);
    }

    if( dat1_buildcache->sequences_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->sequences_hmap);
        struct MapEntry_Sequence* entry = NULL;
        while( (entry = (struct MapEntry_Sequence*)toridraw_map_iter_next(iter)) )
        {
            if( entry->sequence )
                config_dat_sequence_free(entry->sequence);
        }
        toridraw_map_iter_free(iter);
        dat1_buildcache_map_free(dat1_buildcache->sequences_hmap);
    }

    if( dat1_buildcache->flotype_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->flotype_hmap);
        struct MapEntry_Flotype* entry = NULL;
        while( (entry = (struct MapEntry_Flotype*)toridraw_map_iter_next(iter)) )
        {
            if( entry->flotype )
                config_floortype_overlay_free(entry->flotype);
        }
        toridraw_map_iter_free(iter);
        dat1_buildcache_map_free(dat1_buildcache->flotype_hmap);
    }

    if( dat1_buildcache->config_loc_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->config_loc_hmap);
        struct MapEntry_ConfigLoc* entry = NULL;
        while( (entry = (struct MapEntry_ConfigLoc*)toridraw_map_iter_next(iter)) )
        {
            if( entry->config_loc )
                config_locs_free(entry->config_loc);
        }
        toridraw_map_iter_free(iter);
        dat1_buildcache_map_free(dat1_buildcache->config_loc_hmap);
    }

    if( dat1_buildcache->animbaseframes_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->animbaseframes_hmap);
        struct MapEntry_AnimBaseFrames* entry = NULL;
        while( (entry = (struct MapEntry_AnimBaseFrames*)toridraw_map_iter_next(iter)) )
        {
            if( entry->animbaseframes )
                cache_dat_animbaseframes_free(entry->animbaseframes);
        }
        toridraw_map_iter_free(iter);
        dat1_buildcache_map_free(dat1_buildcache->animbaseframes_hmap);
    }

    dat1_buildcache_texture_clear(dat1_buildcache);

    free(dat1_buildcache);
}

void
dat1_buildcache_set_fromconfigtable_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* fromconfigtable_config_jagfile)
{
    if( dat1_buildcache->fromconfigtable_config_jagfile )
        filelist_dat_free(dat1_buildcache->fromconfigtable_config_jagfile);

    dat1_buildcache->fromconfigtable_config_jagfile = fromconfigtable_config_jagfile;
}

void
dat1_buildcache_set_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* versionlist_jagfile)
{
    if( dat1_buildcache->versionlist_jagfile )
        filelist_dat_free(dat1_buildcache->versionlist_jagfile);

    dat1_buildcache->versionlist_jagfile = versionlist_jagfile;
}

void
dat1_buildcache_set_media_2d_graphics_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* media_2d_graphics_jagfile)
{
    if( !dat1_buildcache )
        return;

    if( dat1_buildcache->media_2d_graphics_jagfile )
        filelist_dat_free(dat1_buildcache->media_2d_graphics_jagfile);

    dat1_buildcache->media_2d_graphics_jagfile = media_2d_graphics_jagfile;
}

struct FileListDat*
dat1_buildcache_get_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return NULL;
    return dat1_buildcache->media_2d_graphics_jagfile;
}

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct CacheModel* model)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)toridraw_map_search(
        dat1_buildcache->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = model;
}

struct CacheModel*
dat1_buildcache_model_get(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)toridraw_map_search(
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

    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)toridraw_map_search(
        dat1_buildcache->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry || !entry->model )
        return;

    model_free(entry->model);
}

void
dat1_buildcache_map_terrain_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct CacheMapTerrain* terrain)
{
    struct MapEntry_Terrain* entry = (struct MapEntry_Terrain*)toridraw_map_search(
        dat1_buildcache->map_terrain_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = map_id;
    entry->terrain = terrain;
}

void
dat1_buildcache_map_scenery_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct CacheMapLocs* locs)
{
    struct MapEntry_Scenery* entry = (struct MapEntry_Scenery*)toridraw_map_search(
        dat1_buildcache->map_scenery_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = map_id;
    entry->locs = locs;
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
        toridraw_texture_free(dat1_buildcache->textures[index]);
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
            toridraw_texture_free(dat1_buildcache->textures[i]);
        dat1_buildcache->textures[i] = NULL;
    }
    dat1_buildcache->texture_count = 0;
}

void
dat1_buildcache_sequences_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    struct FileListDat* config_jagfile = dat1_buildcache->fromconfigtable_config_jagfile;
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "seq.dat");
    assert(data_file_idx != -1 && "Data file must be found");

    struct RSBuffer buffer = {
        .data = (uint8_t*)config_jagfile->files[data_file_idx],
        .size = (uint32_t)config_jagfile->file_sizes[data_file_idx],
        .position = 0,
    };

    int count = g2(&buffer);
    for( int i = 0; i < count; i++ )
    {
        struct CacheDatSequence* sequence = malloc(sizeof(struct CacheDatSequence));
        memset(sequence, 0, sizeof(struct CacheDatSequence));

        buffer.position += (uint32_t)config_dat_sequence_decode_inplace(
            sequence,
            (char*)buffer.data + buffer.position,
            (int)buffer.size - (int)buffer.position);

        struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)toridraw_map_search(
            dat1_buildcache->sequences_hmap, &i, TORIDRAW_MAP_INSERT);
        if( !entry )
        {
            config_dat_sequence_free(sequence);
            continue;
        }

        if( entry->sequence )
            config_dat_sequence_free(entry->sequence);

        entry->id = i;
        entry->sequence = sequence;
    }
}

void
dat1_buildcache_floortypes_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    struct FileListDat* config_jagfile = dat1_buildcache->fromconfigtable_config_jagfile;
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "flo.dat");
    assert(data_file_idx != -1 && "Data file must be found");

    struct RSBuffer buffer = {
        .data = (uint8_t*)config_jagfile->files[data_file_idx],
        .size = (uint32_t)config_jagfile->file_sizes[data_file_idx],
        .position = 0,
    };

    int count = g2(&buffer);
    for( int i = 0; i < count; i++ )
    {
        struct CacheConfigOverlay* flotype = malloc(sizeof(struct CacheConfigOverlay));
        memset(flotype, 0, sizeof(struct CacheConfigOverlay));
        flotype->_id = i;

        buffer.position += (uint32_t)config_floortype_overlay_decode_inplace(
            flotype, (char*)buffer.data + buffer.position, (int)buffer.size - (int)buffer.position);

        struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)toridraw_map_search(
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

static struct CacheConfigLocation*
dat1_buildcache_config_loc_get(
    struct Dat1BuildCache* dat1_buildcache,
    int loc_id)
{
    struct MapEntry_ConfigLoc* entry = (struct MapEntry_ConfigLoc*)toridraw_map_search(
        dat1_buildcache->config_loc_hmap, &loc_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->config_loc;
}

void
dat1_buildcache_init_scenery_configs_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    struct FileListDat* config_jagfile = dat1_buildcache->fromconfigtable_config_jagfile;
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.dat");
    int index_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.idx");

    assert(data_file_idx != -1 && "Data file must be found");
    assert(index_file_idx != -1 && "Index file must be found");

    struct FileListDatIndexed* filelist_indexed = filelist_dat_indexed_new_from_decode(
        config_jagfile->files[index_file_idx],
        config_jagfile->file_sizes[index_file_idx],
        config_jagfile->files[data_file_idx],
        config_jagfile->file_sizes[data_file_idx]);

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->map_scenery_hmap);
    struct MapEntry_Scenery* scenery_entry = NULL;
    while( (scenery_entry = (struct MapEntry_Scenery*)toridraw_map_iter_next(iter)) )
    {
        struct CacheMapLocs* locs = scenery_entry->locs;
        if( !locs )
            continue;

        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct CacheMapLoc* loc = &locs->locs[i];
            assert(loc->loc_id != -1 && "Loc id must be valid");
            assert(loc->loc_id < filelist_indexed->offset_count && "Loc id must be within range");

            if( dat1_buildcache_config_loc_get(dat1_buildcache, loc->loc_id) )
                continue;

            struct CacheConfigLocation* config_loc = malloc(sizeof(struct CacheConfigLocation));
            memset(config_loc, 0, sizeof(struct CacheConfigLocation));

            int offset = filelist_indexed->offsets[loc->loc_id];

            config_locs_decode_inplace(
                config_loc,
                filelist_indexed->data + offset,
                filelist_indexed->data_size - offset,
                CONFIG_LOC_DECODE_DAT);

            config_loc->_id = loc->loc_id;

            struct MapEntry_ConfigLoc* entry = (struct MapEntry_ConfigLoc*)toridraw_map_search(
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
    toridraw_map_iter_free(iter);
    filelist_dat_indexed_free(filelist_indexed);
}

void
dat1_buildcache_animbaseframes_add(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id,
    struct CacheDatAnimBaseFrames* animbaseframes)
{
    struct MapEntry_AnimBaseFrames* entry = (struct MapEntry_AnimBaseFrames*)toridraw_map_search(
        dat1_buildcache->animbaseframes_hmap, &animbaseframes_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->animbaseframes )
        cache_dat_animbaseframes_free(entry->animbaseframes);

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

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->sequences_hmap);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)toridraw_map_iter_next(iter)) )
    {
        if( entry->sequence )
            config_dat_sequence_free(entry->sequence);
    }
    toridraw_map_iter_free(iter);
    dat1_buildcache_sequences_reset(dat1_buildcache);
}

void
dat1_buildcache_floortypes_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache || !dat1_buildcache->flotype_hmap )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->flotype_hmap);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)toridraw_map_iter_next(iter)) )
    {
        if( entry->flotype )
            config_floortype_overlay_free(entry->flotype);
    }
    toridraw_map_iter_free(iter);
    dat1_buildcache_floortypes_reset(dat1_buildcache);
}

void
dat1_buildcache_scenery_configs_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache || !dat1_buildcache->config_loc_hmap )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->config_loc_hmap);
    struct MapEntry_ConfigLoc* entry = NULL;
    while( (entry = (struct MapEntry_ConfigLoc*)toridraw_map_iter_next(iter)) )
    {
        if( entry->config_loc )
            config_locs_free(entry->config_loc);
    }
    toridraw_map_iter_free(iter);
    dat1_buildcache_scenery_configs_reset(dat1_buildcache);
}

void
dat1_buildcache_animbaseframes_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache || !dat1_buildcache->animbaseframes_hmap )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->animbaseframes_hmap);
    struct MapEntry_AnimBaseFrames* entry = NULL;
    while( (entry = (struct MapEntry_AnimBaseFrames*)toridraw_map_iter_next(iter)) )
    {
        if( entry->animbaseframes )
            cache_dat_animbaseframes_free(entry->animbaseframes);
    }
    toridraw_map_iter_free(iter);
    dat1_buildcache_animbaseframes_reset(dat1_buildcache);
}
