#include "dat2_buildcache.h"

#include "osrs/rscache/dat2a/dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/varp_varbit_manager.h"
#include "toridraw/toridraw_map.h"

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
    struct RSCacheDat2A_ConfigSequence* sequence;
};

struct MapEntry_Flotype
{
    int id;
    struct RSCacheDat2A_ConfigOverlay* flotype;
};

struct MapEntry_Underlay
{
    int id;
    struct RSCacheDat2A_ConfigUnderlay* underlay;
};

struct MapEntry_ConfigLoc
{
    int id;
    struct RSCacheDat2A_ConfigLocation* config_loc;
};

static struct ToriDraw_Map*
dat2_buildcache_map_new(
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
dat2_buildcache_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    free(ToriDraw_MapBufferPtr(map));
    ToriDraw_MapFree(map);
}

static struct RSCacheDat2Disk_ArchiveReference*
dat2_buildcache_archive_reference(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive)
{
    struct RSCacheDat2Disk_ReferenceTable* table;

    if( !cache || !archive )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, archive);
    table = cache->tables[archive->table_id];
    if( !table || archive->archive_id < 0 || archive->archive_id >= table->archive_count )
        return NULL;

    return &table->archives[archive->archive_id];
}

struct Dat2BuildCache*
dat2_buildcache_new(void)
{
    struct Dat2BuildCache* dat2_buildcache = calloc(1, sizeof(struct Dat2BuildCache));
    if( !dat2_buildcache )
        return NULL;

    dat2_buildcache->models_hmap = dat2_buildcache_map_new(sizeof(struct MapEntry_CacheModel), 1024);
    dat2_buildcache->map_terrain_hmap = dat2_buildcache_map_new(sizeof(struct MapEntry_Terrain), 256);
    dat2_buildcache->map_scenery_hmap = dat2_buildcache_map_new(sizeof(struct MapEntry_Scenery), 256);
    dat2_buildcache->sequences_hmap = dat2_buildcache_map_new(sizeof(struct MapEntry_Sequence), 1024);
    dat2_buildcache->flotype_hmap = dat2_buildcache_map_new(sizeof(struct MapEntry_Flotype), 256);
    dat2_buildcache->underlay_hmap = dat2_buildcache_map_new(sizeof(struct MapEntry_Underlay), 256);
    dat2_buildcache->config_loc_hmap = dat2_buildcache_map_new(sizeof(struct MapEntry_ConfigLoc), 1024);

    return dat2_buildcache;
}

void
dat2_buildcache_free(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache )
        return;

    if( dat2_buildcache->models_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->models_hmap);
        struct MapEntry_CacheModel* entry = NULL;
        while( (entry = (struct MapEntry_CacheModel*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->model )
                RSCacheDat2A_ModelFree(entry->model);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache->models_hmap);
    }

    if( dat2_buildcache->map_terrain_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->map_terrain_hmap);
        struct MapEntry_Terrain* entry = NULL;
        while( (entry = (struct MapEntry_Terrain*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->terrain )
                RSCacheDat2A_MapTerrainFree(entry->terrain);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache->map_terrain_hmap);
    }

    if( dat2_buildcache->map_scenery_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->map_scenery_hmap);
        struct MapEntry_Scenery* entry = NULL;
        while( (entry = (struct MapEntry_Scenery*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->locs )
                RSCacheDat2A_MapLocsFree(entry->locs);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache->map_scenery_hmap);
    }

    if( dat2_buildcache->sequences_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->sequences_hmap);
        struct MapEntry_Sequence* entry = NULL;
        while( (entry = (struct MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->sequence )
                RSCacheDat2A_ConfigSequenceFree(entry->sequence);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache->sequences_hmap);
    }

    if( dat2_buildcache->flotype_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->flotype_hmap);
        struct MapEntry_Flotype* entry = NULL;
        while( (entry = (struct MapEntry_Flotype*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->flotype )
                config_floortype_overlay_free(entry->flotype);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache->flotype_hmap);
    }

    if( dat2_buildcache->underlay_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->underlay_hmap);
        struct MapEntry_Underlay* entry = NULL;
        while( (entry = (struct MapEntry_Underlay*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->underlay )
                config_floortype_underlay_free(entry->underlay);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache->underlay_hmap);
    }

    if( dat2_buildcache->config_loc_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->config_loc_hmap);
        struct MapEntry_ConfigLoc* entry = NULL;
        while( (entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->config_loc )
                config_locs_free(entry->config_loc);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache->config_loc_hmap);
    }

    free(dat2_buildcache);
}

void
dat2_buildcache_model_add(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id,
    struct RSCacheDat2A_Model* model)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)ToriDraw_MapSearch(
        dat2_buildcache->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = model;
}

struct RSCacheDat2A_Model*
dat2_buildcache_model_get(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)ToriDraw_MapSearch(
        dat2_buildcache->models_hmap, &model_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

void
dat2_buildcache_map_terrain_add(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id,
    struct RSCacheDat2A_MapTerrain* terrain)
{
    struct MapEntry_Terrain* entry = (struct MapEntry_Terrain*)ToriDraw_MapSearch(
        dat2_buildcache->map_terrain_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = map_id;
    entry->terrain = terrain;
}

struct RSCacheDat2A_MapTerrain*
dat2_buildcache_map_terrain_get(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id)
{
    struct MapEntry_Terrain* entry = (struct MapEntry_Terrain*)ToriDraw_MapSearch(
        dat2_buildcache->map_terrain_hmap, &map_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
dat2_buildcache_map_terrain_has(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id)
{
    return dat2_buildcache_map_terrain_get(dat2_buildcache, map_id) != NULL;
}

void
dat2_buildcache_map_scenery_add(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id,
    struct RSCacheDat2A_MapLocs* locs)
{
    struct MapEntry_Scenery* entry = (struct MapEntry_Scenery*)ToriDraw_MapSearch(
        dat2_buildcache->map_scenery_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = map_id;
    entry->locs = locs;
}

struct RSCacheDat2A_MapLocs*
dat2_buildcache_map_scenery_get(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id)
{
    struct MapEntry_Scenery* entry = (struct MapEntry_Scenery*)ToriDraw_MapSearch(
        dat2_buildcache->map_scenery_hmap, &map_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
dat2_buildcache_map_scenery_has(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id)
{
    return dat2_buildcache_map_scenery_get(dat2_buildcache, map_id) != NULL;
}

static bool
dat2_loc_id_wanted(
    int loc_id,
    const int* wanted_ids,
    int wanted_count)
{
    if( !wanted_ids )
        return true;

    for( int i = 0; i < wanted_count; i++ )
    {
        if( wanted_ids[i] == loc_id )
            return true;
    }
    return false;
}

static void
dat2_buildcache_underlays_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheShared_FileList* filelist,
    struct RSCacheDat2Disk_ArchiveReference* archive_ref)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        struct RSCacheDat2A_ConfigUnderlay* underlay = malloc(sizeof(struct RSCacheDat2A_ConfigUnderlay));
        if( !underlay )
            continue;

        memset(underlay, 0, sizeof(struct RSCacheDat2A_ConfigUnderlay));
        config_floortype_underlay_decode_inplace(
            underlay, filelist->files[i], filelist->file_sizes[i]);

        struct MapEntry_Underlay* entry = (struct MapEntry_Underlay*)ToriDraw_MapSearch(
            dat2_buildcache->underlay_hmap, &id, TORIDRAW_MAP_INSERT);
        if( !entry )
        {
            config_floortype_underlay_free(underlay);
            continue;
        }

        if( entry->underlay )
            config_floortype_underlay_free(entry->underlay);

        entry->id = id;
        entry->underlay = underlay;
    }
}

static void
dat2_buildcache_overlays_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheShared_FileList* filelist,
    struct RSCacheDat2Disk_ArchiveReference* archive_ref)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        struct RSCacheDat2A_ConfigOverlay* flotype = malloc(sizeof(struct RSCacheDat2A_ConfigOverlay));
        if( !flotype )
            continue;

        memset(flotype, 0, sizeof(struct RSCacheDat2A_ConfigOverlay));
        flotype->_id = id;
        config_floortype_overlay_decode_inplace(flotype, filelist->files[i], filelist->file_sizes[i]);

        struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)ToriDraw_MapSearch(
            dat2_buildcache->flotype_hmap, &id, TORIDRAW_MAP_INSERT);
        if( !entry )
        {
            config_floortype_overlay_free(flotype);
            continue;
        }

        if( entry->flotype )
            config_floortype_overlay_free(entry->flotype);

        entry->id = id;
        entry->flotype = flotype;
    }
}

static void
dat2_buildcache_sequences_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheShared_FileList* filelist,
    struct RSCacheDat2Disk_ArchiveReference* archive_ref,
    int revision)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        struct RSCacheDat2A_ConfigSequence* sequence = calloc(1, sizeof(struct RSCacheDat2A_ConfigSequence));
        if( !sequence )
            continue;

        RSCacheDat2A_ConfigSequenceDecodeInplace(
            sequence, revision, filelist->files[i], filelist->file_sizes[i]);
        sequence->id = id;

        struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)ToriDraw_MapSearch(
            dat2_buildcache->sequences_hmap, &id, TORIDRAW_MAP_INSERT);
        if( !entry )
        {
            RSCacheDat2A_ConfigSequenceFree(sequence);
            continue;
        }

        if( entry->sequence )
            RSCacheDat2A_ConfigSequenceFree(entry->sequence);

        entry->id = id;
        entry->sequence = sequence;
    }
}

static void
dat2_buildcache_locs_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheShared_FileList* filelist,
    struct RSCacheDat2Disk_ArchiveReference* archive_ref,
    const int* wanted_loc_ids,
    int wanted_loc_count)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        if( !dat2_loc_id_wanted(id, wanted_loc_ids, wanted_loc_count) )
            continue;
        if( dat2_buildcache_config_loc_get(dat2_buildcache, id) )
            continue;

        struct RSCacheDat2A_ConfigLocation* config_loc = calloc(1, sizeof(struct RSCacheDat2A_ConfigLocation));
        if( !config_loc )
            continue;

        config_locs_decode_inplace(
            config_loc, filelist->files[i], filelist->file_sizes[i], CONFIG_LOC_DECODE_DAT2);
        config_loc->_id = id;

        struct MapEntry_ConfigLoc* entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapSearch(
            dat2_buildcache->config_loc_hmap, &id, TORIDRAW_MAP_INSERT);
        if( !entry )
        {
            config_locs_free(config_loc);
            continue;
        }

        entry->id = id;
        entry->config_loc = config_loc;
    }
}

void
dat2_buildcache_underlays_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;

    archive_ref = dat2_buildcache_archive_reference(cache, archive);
    if( !archive_ref )
        return;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return;

    dat2_buildcache_underlays_init_from_filelist(dat2_buildcache, filelist, archive_ref);
    RSCacheShared_FileListFree(filelist);
}

void
dat2_buildcache_overlays_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;

    archive_ref = dat2_buildcache_archive_reference(cache, archive);
    if( !archive_ref )
        return;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return;

    dat2_buildcache_overlays_init_from_filelist(dat2_buildcache, filelist, archive_ref);
    RSCacheShared_FileListFree(filelist);
}

void
dat2_buildcache_sequences_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;

    archive_ref = dat2_buildcache_archive_reference(cache, archive);
    if( !archive_ref )
        return;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return;

    dat2_buildcache_sequences_init_from_filelist(
        dat2_buildcache, filelist, archive_ref, archive->revision);
    RSCacheShared_FileListFree(filelist);
}

void
dat2_buildcache_scenery_configs_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    struct VarPVarBitManager* varp_mgr)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;
    int loc_capacity = 512;
    int loc_count = 0;
    int* loc_arr = malloc((size_t)loc_capacity * sizeof(int));
    struct ToriDraw_MapIter* iter;
    struct MapEntry_Scenery* scenery_entry;

    if( !loc_arr )
        return;

    iter = ToriDraw_MapIterNew(dat2_buildcache->map_scenery_hmap);
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
                    goto cleanup;
                loc_arr = grow;
            }
            loc_arr[loc_count++] = loc_id;
        }
    }
    ToriDraw_MapIterFree(iter);

    archive_ref = dat2_buildcache_archive_reference(cache, archive);
    if( !archive_ref )
        goto cleanup;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        goto cleanup;

    dat2_buildcache_locs_init_from_filelist(
        dat2_buildcache, filelist, archive_ref, loc_arr, loc_count);

    for( ;; )
    {
        int target_capacity = 64;
        int target_count = 0;
        int* target_arr = malloc((size_t)target_capacity * sizeof(int));
        struct ToriDraw_MapIter* loc_iter;
        struct MapEntry_ConfigLoc* loc_entry;

        if( !target_arr )
            break;

        loc_iter = ToriDraw_MapIterNew(dat2_buildcache->config_loc_hmap);
        while( (loc_entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapIterNext(loc_iter)) )
        {
            struct RSCacheDat2A_ConfigLocation* config_loc = loc_entry->config_loc;
            int transform_id;

            if( !config_loc || config_loc->transform_count <= 0 || !config_loc->transforms )
                continue;

            transform_id = varp_varbit_resolve_transform(
                varp_mgr,
                config_loc->transforms,
                config_loc->transform_count,
                config_loc->transform_varbit,
                config_loc->transform_varp);
            if( transform_id < 0 )
                continue;
            if( dat2_buildcache_config_loc_get(dat2_buildcache, transform_id) )
                continue;

            if( target_count >= target_capacity )
            {
                target_capacity *= 2;
                int* grow = realloc(target_arr, (size_t)target_capacity * sizeof(int));
                if( !grow )
                {
                    free(target_arr);
                    ToriDraw_MapIterFree(loc_iter);
                    goto after_transforms;
                }
                target_arr = grow;
            }
            target_arr[target_count++] = transform_id;
        }
        ToriDraw_MapIterFree(loc_iter);

        if( target_count == 0 )
        {
            free(target_arr);
            break;
        }

        dat2_buildcache_locs_init_from_filelist(
            dat2_buildcache, filelist, archive_ref, target_arr, target_count);
        free(target_arr);
    }

after_transforms:
    RSCacheShared_FileListFree(filelist);

cleanup:
    free(loc_arr);
}

struct RSCacheDat2A_ConfigLocation*
dat2_buildcache_config_loc_get(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id)
{
    struct MapEntry_ConfigLoc* entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapSearch(
        dat2_buildcache->config_loc_hmap, &loc_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->config_loc;
}

static int
dat2_int_cmp(const void* a, const void* b)
{
    return *(const int*)a - *(const int*)b;
}

static int
dat2_unique_sorted_int(
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
dat2_buildcache_get_scenery_model_ids(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id,
    int** model_ids_out)
{
    struct RSCacheDat2A_ConfigLocation* config_loc = dat2_buildcache_config_loc_get(dat2_buildcache, loc_id);
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
dat2_buildcache_get_all_unique_scenery_model_ids(
    struct Dat2BuildCache* dat2_buildcache,
    int** model_ids_out)
{
    *model_ids_out = NULL;
    if( !dat2_buildcache )
        return 0;

    int model_capacity = 1024;
    int model_count = 0;
    int* model_arr = malloc((size_t)model_capacity * sizeof(int));
    if( !model_arr )
        return 0;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->config_loc_hmap);
    struct MapEntry_ConfigLoc* loc_entry = NULL;
    while( (loc_entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapIterNext(iter)) )
    {
        int* mids = NULL;
        int nm = dat2_buildcache_get_scenery_model_ids(dat2_buildcache, loc_entry->id, &mids);
        for( int mi = 0; mi < nm; mi++ )
        {
            if( model_count >= model_capacity )
            {
                model_capacity *= 2;
                int* grow = realloc(model_arr, (size_t)model_capacity * sizeof(int));
                if( !grow )
                {
                    free(mids);
                    ToriDraw_MapIterFree(iter);
                    free(model_arr);
                    return 0;
                }
                model_arr = grow;
            }
            model_arr[model_count++] = mids[mi];
        }
        free(mids);
    }
    ToriDraw_MapIterFree(iter);

    if( model_count == 0 )
    {
        free(model_arr);
        return 0;
    }

    qsort(model_arr, (size_t)model_count, sizeof(int), dat2_int_cmp);
    int nunique_m = dat2_unique_sorted_int(model_arr, model_count);
    if( nunique_m < model_count )
    {
        int* shrink = realloc(model_arr, (size_t)nunique_m * sizeof(int));
        if( shrink )
            model_arr = shrink;
    }

    *model_ids_out = model_arr;
    return nunique_m;
}

void
dat2_buildcache_foreach_sequence(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheSequenceCallback callback,
    void* user_data)
{
    if( !dat2_buildcache || !callback || !dat2_buildcache->sequences_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->sequences_hmap);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sequence )
            callback(entry->id, entry->sequence, user_data);
    }
    ToriDraw_MapIterFree(iter);
}

void
dat2_buildcache_foreach_flotype(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheFlotypeCallback callback,
    void* user_data)
{
    if( !dat2_buildcache || !callback || !dat2_buildcache->flotype_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->flotype_hmap);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->flotype )
            callback(entry->id, entry->flotype, user_data);
    }
    ToriDraw_MapIterFree(iter);
}

void
dat2_buildcache_foreach_underlay(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheUnderlayCallback callback,
    void* user_data)
{
    if( !dat2_buildcache || !callback || !dat2_buildcache->underlay_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->underlay_hmap);
    struct MapEntry_Underlay* entry = NULL;
    while( (entry = (struct MapEntry_Underlay*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->underlay )
            callback(entry->id, entry->underlay, user_data);
    }
    ToriDraw_MapIterFree(iter);
}

void
dat2_buildcache_foreach_config_loc(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheLocationCallback callback,
    void* user_data)
{
    if( !dat2_buildcache || !callback || !dat2_buildcache->config_loc_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->config_loc_hmap);
    struct MapEntry_ConfigLoc* entry = NULL;
    while( (entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->config_loc )
            callback(entry->id, entry->config_loc, user_data);
    }
    ToriDraw_MapIterFree(iter);
}
