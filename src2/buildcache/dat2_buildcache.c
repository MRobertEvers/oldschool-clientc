#include "dat2_buildcache.h"

#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/dat2a_config_idk.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2a/dat2a_framemap.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_mem.h"
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

struct MapEntry_FramesArchive
{
    int id;
    struct Dat2BuildCache_FramesArchive* archive;
};

struct MapEntry_Skeletal
{
    int id;
    struct RSCacheDat2A_AnimMaya* maya;
};

struct MapEntry_ConfigIdentkit
{
    int id;
    struct RSCacheDat2A_ConfigIdk* idk;
};

struct MapEntry_ConfigObject
{
    int id;
    struct RSCacheDat2A_ConfigObject* object;
};

struct MapEntry_ConfigNpctype
{
    int id;
    struct RSCacheDat2A_ConfigNpctype* npc;
};

#define DAT2_BUILDCACHE_PRUNE_CAPACITY 64

static void
dat2_buildcache_mem_track_add(
    struct Dat2BuildCache* cache,
    enum Dat2BuildCache_Kind kind,
    size_t bytes)
{
    if( !cache || kind >= DAT2_BUILDCACHE_KIND_COUNT )
        return;
    cache->asset_bytes[kind] += bytes;
}

static void
dat2_buildcache_mem_track_sub(
    struct Dat2BuildCache* cache,
    enum Dat2BuildCache_Kind kind,
    size_t bytes)
{
    if( !cache || kind >= DAT2_BUILDCACHE_KIND_COUNT )
        return;
    cache->asset_bytes[kind] -= bytes;
}

static size_t
dat2_buildcache_map_buffer_size(struct ToriDraw_Map* map)
{
    if( !map )
        return 0;
    return ToriDraw_MapBufferSizeFor(ToriDraw_MapEntrySize(map), ToriDraw_MapCapacity(map));
}

static struct ToriDraw_Map*
dat2_buildcache_map_new(
    struct Dat2BuildCache* cache,
    int entry_size,
    int capacity)
{
    int buffer_size = ToriDraw_MapBufferSizeFor(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
        .capacity = (size_t)capacity,
    };
    struct ToriDraw_Map* map = ToriDraw_MapNew(&config, 0);
    if( cache && map )
        cache->map_buffer_bytes += (size_t)buffer_size;
    return map;
}

static void
dat2_buildcache_map_free(
    struct Dat2BuildCache* cache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    if( cache )
        cache->map_buffer_bytes -= dat2_buildcache_map_buffer_size(map);
    free(ToriDraw_MapBufferPtr(map));
    ToriDraw_MapFree(map);
}

static void
dat2_buildcache_map_reset(
    struct Dat2BuildCache* cache,
    struct ToriDraw_Map** map_out,
    int entry_size,
    int capacity)
{
    if( !map_out || !*map_out )
        return;

    dat2_buildcache_map_free(cache, *map_out);
    *map_out = dat2_buildcache_map_new(cache, entry_size, capacity);
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

    dat2_buildcache->models_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_CacheModel), 8192);
    dat2_buildcache->map_terrain_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_Terrain), 512);
    dat2_buildcache->map_scenery_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_Scenery), 512);
    dat2_buildcache->sequences_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_Sequence), 65536);
    dat2_buildcache->flotype_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_Flotype), 8192);
    dat2_buildcache->underlay_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_Underlay), 4096);
    dat2_buildcache->config_loc_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_ConfigLoc), 8192);
    dat2_buildcache->frames_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_FramesArchive), 4096);
    dat2_buildcache->skeletal_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_Skeletal), 4096);
    dat2_buildcache->identkit_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_ConfigIdentkit), 512);
    dat2_buildcache->object_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_ConfigObject), 8192);
    dat2_buildcache->npctype_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_ConfigNpctype), 8192);

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
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->models_hmap);
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
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->map_terrain_hmap);
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
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->map_scenery_hmap);
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
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->sequences_hmap);
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
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->flotype_hmap);
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
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->underlay_hmap);
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
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->config_loc_hmap);
    }

    if( dat2_buildcache->frames_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->frames_hmap);
        struct MapEntry_FramesArchive* entry = NULL;
        while( (entry = (struct MapEntry_FramesArchive*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->archive )
                Dat2BuildCache_FramesArchiveFree(entry->archive);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->frames_hmap);
    }

    if( dat2_buildcache->skeletal_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->skeletal_hmap);
        struct MapEntry_Skeletal* entry = NULL;
        while( (entry = (struct MapEntry_Skeletal*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->maya )
                RSCacheDat2A_AnimMayaFree(entry->maya);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->skeletal_hmap);
    }

    if( dat2_buildcache->identkit_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->identkit_hmap);
        struct MapEntry_ConfigIdentkit* entry = NULL;
        while( (entry = (struct MapEntry_ConfigIdentkit*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->idk )
                RSCacheDat2A_ConfigIdkFree(entry->idk);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->identkit_hmap);
    }

    if( dat2_buildcache->object_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->object_hmap);
        struct MapEntry_ConfigObject* entry = NULL;
        while( (entry = (struct MapEntry_ConfigObject*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->object )
                RSCacheDat2A_ConfigObjectFree(entry->object);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->object_hmap);
    }

    if( dat2_buildcache->npctype_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->npctype_hmap);
        struct MapEntry_ConfigNpctype* entry = NULL;
        while( (entry = (struct MapEntry_ConfigNpctype*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->npc )
                RSCacheDat2A_ConfigNpctypeFree(entry->npc);
        }
        ToriDraw_MapIterFree(iter);
        dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->npctype_hmap);
    }

    memset(dat2_buildcache->asset_bytes, 0, sizeof(dat2_buildcache->asset_bytes));
    dat2_buildcache->map_buffer_bytes = 0;
    free(dat2_buildcache);
}

void
Dat2BuildCache_FramesArchiveFree(struct Dat2BuildCache_FramesArchive* fa)
{
    if( !fa )
        return;

    RSCacheDat2A_FramemapFree(fa->framemap);
    if( fa->frames )
    {
        for( int i = 0; i < fa->frame_count; i++ )
            RSCacheDat2A_FrameFree(fa->frames[i]);
        free(fa->frames);
    }
    free(fa);
}

size_t
Dat2BuildCache_FramesArchiveSizeOf(const struct Dat2BuildCache_FramesArchive* fa)
{
    if( !fa )
        return 0;

    size_t bytes = sizeof(*fa);
    bytes += RSCacheDat2A_FramemapSizeOf(fa->framemap);
    if( fa->frames )
    {
        bytes += (size_t)fa->frame_count * sizeof(*fa->frames);
        for( int i = 0; i < fa->frame_count; i++ )
            bytes += RSCacheDat2A_FrameSizeOf(fa->frames[i]);
    }
    return bytes;
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

    if( entry->model )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_MODEL,
            RSCacheDat2A_ModelSizeOf(entry->model));
        RSCacheDat2A_ModelFree(entry->model);
    }

    entry->id = model_id;
    entry->model = model;
    dat2_buildcache_mem_track_add(
        dat2_buildcache, DAT2_BUILDCACHE_KIND_MODEL, RSCacheDat2A_ModelSizeOf(model));
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
dat2_buildcache_model_remove(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id)
{
    if( !dat2_buildcache )
        return;

    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)ToriDraw_MapSearch(
        dat2_buildcache->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry || !entry->model )
        return;

    dat2_buildcache_mem_track_sub(
        dat2_buildcache,
        DAT2_BUILDCACHE_KIND_MODEL,
        RSCacheDat2A_ModelSizeOf(entry->model));
    RSCacheDat2A_ModelFree(entry->model);
    entry->model = NULL;
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

    if( entry->terrain )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_MAP_TERRAIN,
            RSCacheDat2A_MapTerrainSizeOf(entry->terrain));
        RSCacheDat2A_MapTerrainFree(entry->terrain);
    }

    entry->id = map_id;
    entry->terrain = terrain;
    dat2_buildcache_mem_track_add(
        dat2_buildcache,
        DAT2_BUILDCACHE_KIND_MAP_TERRAIN,
        RSCacheDat2A_MapTerrainSizeOf(terrain));
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

    if( entry->locs )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_MAP_SCENERY,
            RSCacheDat2A_MapLocsSizeOf(entry->locs));
        RSCacheDat2A_MapLocsFree(entry->locs);
    }

    entry->id = map_id;
    entry->locs = locs;
    dat2_buildcache_mem_track_add(
        dat2_buildcache,
        DAT2_BUILDCACHE_KIND_MAP_SCENERY,
        RSCacheDat2A_MapLocsSizeOf(locs));
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
dat2_id_wanted(
    int id,
    const int* wanted_ids,
    int wanted_count)
{
    if( !wanted_ids )
        return true;

    for( int i = 0; i < wanted_count; i++ )
    {
        if( wanted_ids[i] == id )
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
        struct RSCacheDat2A_ConfigUnderlay* underlay =
            malloc(sizeof(struct RSCacheDat2A_ConfigUnderlay));
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
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_UNDERLAY,
                RSCacheDat2A_ConfigUnderlaySizeOf(entry->underlay));
            config_floortype_underlay_free(entry->underlay);
        }

        entry->id = id;
        entry->underlay = underlay;
        dat2_buildcache_mem_track_add(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_UNDERLAY,
            RSCacheDat2A_ConfigUnderlaySizeOf(underlay));
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
        struct RSCacheDat2A_ConfigOverlay* flotype =
            malloc(sizeof(struct RSCacheDat2A_ConfigOverlay));
        if( !flotype )
            continue;

        memset(flotype, 0, sizeof(struct RSCacheDat2A_ConfigOverlay));
        flotype->_id = id;
        config_floortype_overlay_decode_inplace(
            flotype, filelist->files[i], filelist->file_sizes[i]);

        struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)ToriDraw_MapSearch(
            dat2_buildcache->flotype_hmap, &id, TORIDRAW_MAP_INSERT);
        if( !entry )
        {
            config_floortype_overlay_free(flotype);
            continue;
        }

        if( entry->flotype )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_FLOTYPE,
                RSCacheDat2A_ConfigOverlaySizeOf(entry->flotype));
            config_floortype_overlay_free(entry->flotype);
        }

        entry->id = id;
        entry->flotype = flotype;
        dat2_buildcache_mem_track_add(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_FLOTYPE,
            RSCacheDat2A_ConfigOverlaySizeOf(flotype));
    }
}

static bool
dat2_buildcache_sequence_insert(
    struct Dat2BuildCache* dat2_buildcache,
    int id,
    struct RSCacheDat2A_ConfigSequence* sequence)
{
    struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)ToriDraw_MapSearch(
        dat2_buildcache->sequences_hmap, &id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        RSCacheDat2A_ConfigSequenceFree(sequence);
        return false;
    }

    if( entry->sequence )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_SEQUENCE,
            RSCacheDat2A_ConfigSequenceSizeOf(entry->sequence));
        RSCacheDat2A_ConfigSequenceFree(entry->sequence);
    }

    entry->id = id;
    entry->sequence = sequence;
    dat2_buildcache_mem_track_add(
        dat2_buildcache,
        DAT2_BUILDCACHE_KIND_SEQUENCE,
        RSCacheDat2A_ConfigSequenceSizeOf(sequence));
    return true;
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
        struct RSCacheDat2A_ConfigSequence* sequence =
            calloc(1, sizeof(struct RSCacheDat2A_ConfigSequence));
        if( !sequence )
            continue;

        RSCacheDat2A_ConfigSequenceDecodeInplace(
            sequence, revision, filelist->files[i], filelist->file_sizes[i]);
        sequence->id = id;

        if( !dat2_buildcache_sequence_insert(dat2_buildcache, id, sequence) )
            continue;
    }
}

bool
dat2_buildcache_sequence_load_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int seq_id)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;

    if( ToriDraw_MapSearch(dat2_buildcache->sequences_hmap, &seq_id, TORIDRAW_MAP_FIND) )
        return true;
    if( !archive || !cache )
        return false;

    archive_ref = dat2_buildcache_archive_reference(cache, archive);
    if( !archive_ref )
        return false;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return false;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        if( id != seq_id )
            continue;

        struct RSCacheDat2A_ConfigSequence* sequence =
            calloc(1, sizeof(struct RSCacheDat2A_ConfigSequence));
        if( !sequence )
            break;

        RSCacheDat2A_ConfigSequenceDecodeInplace(
            sequence, archive->revision, filelist->files[i], filelist->file_sizes[i]);
        sequence->id = id;
        dat2_buildcache_sequence_insert(dat2_buildcache, id, sequence);
        break;
    }

    RSCacheShared_FileListFree(filelist);
    return ToriDraw_MapSearch(dat2_buildcache->sequences_hmap, &seq_id, TORIDRAW_MAP_FIND) != NULL;
}

struct RSCacheDat2A_ConfigSequence*
dat2_buildcache_sequence_get(
    struct Dat2BuildCache* dat2_buildcache,
    int seq_id)
{
    struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)ToriDraw_MapSearch(
        dat2_buildcache->sequences_hmap, &seq_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->sequence;
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
        if( !dat2_id_wanted(id, wanted_loc_ids, wanted_loc_count) )
            continue;
        if( dat2_buildcache_config_loc_get(dat2_buildcache, id) )
            continue;

        struct RSCacheDat2A_ConfigLocation* config_loc =
            calloc(1, sizeof(struct RSCacheDat2A_ConfigLocation));
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

        if( entry->config_loc )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_CONFIG_LOC,
                RSCacheDat2A_ConfigLocationSizeOf(entry->config_loc));
            config_locs_free(entry->config_loc);
        }

        entry->id = id;
        entry->config_loc = config_loc;
        dat2_buildcache_mem_track_add(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_CONFIG_LOC,
            RSCacheDat2A_ConfigLocationSizeOf(config_loc));
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
dat2_buildcache_identkit_add(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id,
    struct RSCacheDat2A_ConfigIdk* idk)
{
    struct MapEntry_ConfigIdentkit* entry = (struct MapEntry_ConfigIdentkit*)ToriDraw_MapSearch(
        dat2_buildcache->identkit_hmap, &idk_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->idk )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_IDENTKIT,
            RSCacheDat2A_ConfigIdkSizeOf(entry->idk));
        RSCacheDat2A_ConfigIdkFree(entry->idk);
    }

    entry->id = idk_id;
    entry->idk = idk;
    dat2_buildcache_mem_track_add(
        dat2_buildcache, DAT2_BUILDCACHE_KIND_IDENTKIT, RSCacheDat2A_ConfigIdkSizeOf(idk));
}

struct RSCacheDat2A_ConfigIdk*
dat2_buildcache_identkit_get(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id)
{
    struct MapEntry_ConfigIdentkit* entry = (struct MapEntry_ConfigIdentkit*)ToriDraw_MapSearch(
        dat2_buildcache->identkit_hmap, &idk_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->idk;
}

void
dat2_buildcache_object_add(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id,
    struct RSCacheDat2A_ConfigObject* object)
{
    struct MapEntry_ConfigObject* entry = (struct MapEntry_ConfigObject*)ToriDraw_MapSearch(
        dat2_buildcache->object_hmap, &obj_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->object )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_OBJECT,
            RSCacheDat2A_ConfigObjectSizeOf(entry->object));
        RSCacheDat2A_ConfigObjectFree(entry->object);
    }

    entry->id = obj_id;
    entry->object = object;
    dat2_buildcache_mem_track_add(
        dat2_buildcache, DAT2_BUILDCACHE_KIND_OBJECT, RSCacheDat2A_ConfigObjectSizeOf(object));
}

struct RSCacheDat2A_ConfigObject*
dat2_buildcache_object_get(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id)
{
    struct MapEntry_ConfigObject* entry = (struct MapEntry_ConfigObject*)ToriDraw_MapSearch(
        dat2_buildcache->object_hmap, &obj_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->object;
}

void
dat2_buildcache_npctype_add(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id,
    struct RSCacheDat2A_ConfigNpctype* npc)
{
    struct MapEntry_ConfigNpctype* entry = (struct MapEntry_ConfigNpctype*)ToriDraw_MapSearch(
        dat2_buildcache->npctype_hmap, &npc_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->npc )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_NPCTYPE,
            RSCacheDat2A_ConfigNpctypeSizeOf(entry->npc));
        RSCacheDat2A_ConfigNpctypeFree(entry->npc);
    }

    entry->id = npc_id;
    entry->npc = npc;
    dat2_buildcache_mem_track_add(
        dat2_buildcache, DAT2_BUILDCACHE_KIND_NPCTYPE, RSCacheDat2A_ConfigNpctypeSizeOf(npc));
}

struct RSCacheDat2A_ConfigNpctype*
dat2_buildcache_npctype_get(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id)
{
    struct MapEntry_ConfigNpctype* entry = (struct MapEntry_ConfigNpctype*)ToriDraw_MapSearch(
        dat2_buildcache->npctype_hmap, &npc_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->npc;
}

static void
dat2_buildcache_identkits_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheShared_FileList* filelist,
    struct RSCacheDat2Disk_ArchiveReference* archive_ref,
    const int* wanted_ids,
    int wanted_count)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        if( dat2_buildcache_identkit_get(dat2_buildcache, id) )
            continue;

        struct RSCacheDat2A_ConfigIdk* idk = calloc(1, sizeof(struct RSCacheDat2A_ConfigIdk));
        if( !idk )
            continue;

        RSCacheDat2A_ConfigIdkDecodeInplace(idk, filelist->files[i], filelist->file_sizes[i]);
        idk->_id = id;
        dat2_buildcache_identkit_add(dat2_buildcache, id, idk);
    }
}

static void
dat2_buildcache_objects_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheShared_FileList* filelist,
    struct RSCacheDat2Disk_ArchiveReference* archive_ref,
    const int* wanted_ids,
    int wanted_count)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        if( dat2_buildcache_object_get(dat2_buildcache, id) )
            continue;

        struct RSCacheDat2A_ConfigObject* object =
            calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
        if( !object )
            continue;

        RSCacheDat2A_ConfigObjectDecodeInplace(object, filelist->files[i], filelist->file_sizes[i]);
        object->_id = id;
        dat2_buildcache_object_add(dat2_buildcache, id, object);
    }
}

static void
dat2_buildcache_npctypes_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheShared_FileList* filelist,
    struct RSCacheDat2Disk_ArchiveReference* archive_ref,
    struct RSCacheDat2Disk_Archive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        if( dat2_buildcache_npctype_get(dat2_buildcache, id) )
            continue;

        struct RSCacheDat2A_ConfigNpctype* npc = RSCacheDat2A_ConfigNpctypeNewDecode(
            archive->revision, filelist->files[i], filelist->file_sizes[i]);
        if( !npc )
            continue;

        dat2_buildcache_npctype_add(dat2_buildcache, id, npc);
    }
}

void
dat2_buildcache_identkits_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;

    archive_ref = dat2_buildcache_archive_reference(cache, archive);
    if( !archive_ref )
        return;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return;

    dat2_buildcache_identkits_init_from_filelist(
        dat2_buildcache, filelist, archive_ref, wanted_ids, wanted_count);
    RSCacheShared_FileListFree(filelist);
}

void
dat2_buildcache_objects_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;

    archive_ref = dat2_buildcache_archive_reference(cache, archive);
    if( !archive_ref )
        return;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return;

    dat2_buildcache_objects_init_from_filelist(
        dat2_buildcache, filelist, archive_ref, wanted_ids, wanted_count);
    RSCacheShared_FileListFree(filelist);
}

void
dat2_buildcache_npctypes_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;

    archive_ref = dat2_buildcache_archive_reference(cache, archive);
    if( !archive_ref )
        return;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return;

    dat2_buildcache_npctypes_init_from_filelist(
        dat2_buildcache, filelist, archive_ref, archive, wanted_ids, wanted_count);
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
dat2_int_cmp(
    const void* a,
    const void* b)
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
    struct RSCacheDat2A_ConfigLocation* config_loc =
        dat2_buildcache_config_loc_get(dat2_buildcache, loc_id);
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

void
dat2_buildcache_foreach_skeletal(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheSkeletalCallback callback,
    void* user_data)
{
    if( !dat2_buildcache || !callback || !dat2_buildcache->skeletal_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->skeletal_hmap);
    struct MapEntry_Skeletal* entry = NULL;
    while( (entry = (struct MapEntry_Skeletal*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->maya )
            callback(entry->id, entry->maya, user_data);
    }
    ToriDraw_MapIterFree(iter);
}

/* ---- Classic frame/framemap (idx0 + idx1) ---- */

void
dat2_buildcache_frames_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    int archive_id)
{
    if( !dat2_buildcache || !cache || archive_id < 0 )
        return;
    if( dat2_buildcache_frames_has(dat2_buildcache, archive_id) )
        return;

    struct RSCacheDat2Disk_Archive* idx0_archive =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Animations, archive_id);
    if( !idx0_archive )
        return;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, idx0_archive);

    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Animations];
    struct RSCacheDat2Disk_ArchiveReference* archive_ref = NULL;
    if( table && archive_id >= 0 && archive_id < table->archive_count )
        archive_ref = &table->archives[archive_id];

    struct RSCacheShared_FileList* filelist =
        RSCacheShared_FileListNewFromCacheArchive(idx0_archive);
    RSCacheDat2Disk_ArchiveFree(idx0_archive);

    if( !filelist || filelist->file_count <= 0 )
    {
        RSCacheShared_FileListFree(filelist);
        return;
    }

    struct Dat2BuildCache_FramesArchive* fa =
        calloc(1, sizeof(struct Dat2BuildCache_FramesArchive));
    if( !fa )
    {
        RSCacheShared_FileListFree(filelist);
        return;
    }

    fa->frame_count = filelist->file_count;
    fa->frames = calloc((size_t)fa->frame_count, sizeof(struct RSCacheDat2A_Frame*));
    if( !fa->frames )
    {
        free(fa);
        RSCacheShared_FileListFree(filelist);
        return;
    }

    struct RSCacheDat2A_Framemap* shared_framemap = NULL;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        char* data = filelist->files[i];
        int data_size = filelist->file_sizes[i];
        if( !data || data_size < 2 )
            continue;

        int framemap_id = RSCacheDat2A_FrameFramemapIdFromFile(data, data_size);
        if( !shared_framemap || shared_framemap->id != framemap_id )
        {
            if( !shared_framemap )
            {
                shared_framemap = RSCacheDat2A_FramemapNewFromCache(cache, framemap_id);
            }
        }
        if( !shared_framemap )
            continue;

        int file_id = archive_ref ? archive_ref->children.files[i].id : i;
        int frame_id = (archive_id << 16) | (file_id & 0xFFFF);
        struct RSCacheDat2A_Frame* frame =
            RSCacheDat2A_FrameNewDecode2(frame_id, shared_framemap, data, data_size);
        fa->frames[i] = frame;
    }

    fa->framemap = shared_framemap;
    RSCacheShared_FileListFree(filelist);

    struct MapEntry_FramesArchive* entry = (struct MapEntry_FramesArchive*)ToriDraw_MapSearch(
        dat2_buildcache->frames_hmap, &archive_id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        Dat2BuildCache_FramesArchiveFree(fa);
        return;
    }
    if( entry->archive )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_FRAMES,
            Dat2BuildCache_FramesArchiveSizeOf(entry->archive));
        Dat2BuildCache_FramesArchiveFree(entry->archive);
    }
    entry->id = archive_id;
    entry->archive = fa;
    dat2_buildcache_mem_track_add(
        dat2_buildcache, DAT2_BUILDCACHE_KIND_FRAMES, Dat2BuildCache_FramesArchiveSizeOf(fa));
}

struct Dat2BuildCache_FramesArchive*
dat2_buildcache_frames_get(
    struct Dat2BuildCache* dat2_buildcache,
    int archive_id)
{
    struct MapEntry_FramesArchive* entry = (struct MapEntry_FramesArchive*)ToriDraw_MapSearch(
        dat2_buildcache->frames_hmap, &archive_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->archive;
}

struct Dat2BuildCache_FramesArchive*
dat2_buildcache_frames_take(
    struct Dat2BuildCache* dat2_buildcache,
    int archive_id)
{
    struct MapEntry_FramesArchive* entry = (struct MapEntry_FramesArchive*)ToriDraw_MapSearch(
        dat2_buildcache->frames_hmap, &archive_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    struct Dat2BuildCache_FramesArchive* fa = entry->archive;
    if( fa )
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_FRAMES,
            Dat2BuildCache_FramesArchiveSizeOf(fa));
    entry->archive = NULL;
    return fa;
}

bool
dat2_buildcache_frames_has(
    struct Dat2BuildCache* dat2_buildcache,
    int archive_id)
{
    return dat2_buildcache_frames_get(dat2_buildcache, archive_id) != NULL;
}

/* ---- Skeletal Animaya (idx22) ---- */

void
dat2_buildcache_skeletal_add(
    struct Dat2BuildCache* dat2_buildcache,
    int anim_maya_id,
    struct RSCacheDat2A_AnimMaya* maya)
{
    if( !dat2_buildcache || !maya )
        return;
    struct MapEntry_Skeletal* entry = (struct MapEntry_Skeletal*)ToriDraw_MapSearch(
        dat2_buildcache->skeletal_hmap, &anim_maya_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;
    if( entry->maya )
    {
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_SKELETAL,
            RSCacheDat2A_AnimMayaSizeOf(entry->maya));
        RSCacheDat2A_AnimMayaFree(entry->maya);
    }
    entry->id = anim_maya_id;
    entry->maya = maya;
    dat2_buildcache_mem_track_add(
        dat2_buildcache, DAT2_BUILDCACHE_KIND_SKELETAL, RSCacheDat2A_AnimMayaSizeOf(maya));
}

struct RSCacheDat2A_AnimMaya*
dat2_buildcache_skeletal_get(
    struct Dat2BuildCache* dat2_buildcache,
    int anim_maya_id)
{
    struct MapEntry_Skeletal* entry = (struct MapEntry_Skeletal*)ToriDraw_MapSearch(
        dat2_buildcache->skeletal_hmap, &anim_maya_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->maya;
}

struct RSCacheDat2A_AnimMaya*
dat2_buildcache_skeletal_take(
    struct Dat2BuildCache* dat2_buildcache,
    int anim_maya_id)
{
    struct MapEntry_Skeletal* entry = (struct MapEntry_Skeletal*)ToriDraw_MapSearch(
        dat2_buildcache->skeletal_hmap, &anim_maya_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    struct RSCacheDat2A_AnimMaya* maya = entry->maya;
    if( maya )
        dat2_buildcache_mem_track_sub(
            dat2_buildcache,
            DAT2_BUILDCACHE_KIND_SKELETAL,
            RSCacheDat2A_AnimMayaSizeOf(maya));
    entry->maya = NULL;
    return maya;
}

bool
dat2_buildcache_skeletal_has(
    struct Dat2BuildCache* dat2_buildcache,
    int anim_maya_id)
{
    return dat2_buildcache_skeletal_get(dat2_buildcache, anim_maya_id) != NULL;
}

void
dat2_buildcache_mem_bytes(
    struct Dat2BuildCache* dat2_buildcache,
    struct Dat2BuildCache_MemReport* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( !dat2_buildcache )
        return;

    out->map_buffer_bytes = dat2_buildcache->map_buffer_bytes;
    for( int i = 0; i < DAT2_BUILDCACHE_KIND_COUNT; i++ )
        out->asset_bytes[i] = dat2_buildcache->asset_bytes[i];
    for( int i = 0; i < DAT2_BUILDCACHE_KIND_COUNT; i++ )
        out->asset_bytes_total += dat2_buildcache->asset_bytes[i];
    out->bytes_total = out->asset_bytes_total + out->map_buffer_bytes;
}

size_t
dat2_buildcache_bytes_total(struct Dat2BuildCache* dat2_buildcache)
{
    struct Dat2BuildCache_MemReport report;
    dat2_buildcache_mem_bytes(dat2_buildcache, &report);
    return report.bytes_total;
}

void
dat2_buildcache_map_terrain_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->map_terrain_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->map_terrain_hmap);
    struct MapEntry_Terrain* entry = NULL;
    while( (entry = (struct MapEntry_Terrain*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->terrain )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_MAP_TERRAIN,
                RSCacheDat2A_MapTerrainSizeOf(entry->terrain));
            RSCacheDat2A_MapTerrainFree(entry->terrain);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->map_terrain_hmap,
        sizeof(struct MapEntry_Terrain),
        512);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_MAP_TERRAIN] = 0;
}

void
dat2_buildcache_map_scenery_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->map_scenery_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->map_scenery_hmap);
    struct MapEntry_Scenery* entry = NULL;
    while( (entry = (struct MapEntry_Scenery*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->locs )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_MAP_SCENERY,
                RSCacheDat2A_MapLocsSizeOf(entry->locs));
            RSCacheDat2A_MapLocsFree(entry->locs);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->map_scenery_hmap,
        sizeof(struct MapEntry_Scenery),
        512);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_MAP_SCENERY] = 0;
}

void
dat2_buildcache_sequences_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->sequences_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->sequences_hmap);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sequence )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_SEQUENCE,
                RSCacheDat2A_ConfigSequenceSizeOf(entry->sequence));
            RSCacheDat2A_ConfigSequenceFree(entry->sequence);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->sequences_hmap,
        sizeof(struct MapEntry_Sequence),
        65536);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_SEQUENCE] = 0;
}

void
dat2_buildcache_flotypes_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->flotype_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->flotype_hmap);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->flotype )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_FLOTYPE,
                RSCacheDat2A_ConfigOverlaySizeOf(entry->flotype));
            config_floortype_overlay_free(entry->flotype);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->flotype_hmap,
        sizeof(struct MapEntry_Flotype),
        8192);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_FLOTYPE] = 0;
}

void
dat2_buildcache_underlays_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->underlay_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->underlay_hmap);
    struct MapEntry_Underlay* entry = NULL;
    while( (entry = (struct MapEntry_Underlay*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->underlay )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_UNDERLAY,
                RSCacheDat2A_ConfigUnderlaySizeOf(entry->underlay));
            config_floortype_underlay_free(entry->underlay);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->underlay_hmap,
        sizeof(struct MapEntry_Underlay),
        4096);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_UNDERLAY] = 0;
}

void
dat2_buildcache_scenery_configs_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->config_loc_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->config_loc_hmap);
    struct MapEntry_ConfigLoc* entry = NULL;
    while( (entry = (struct MapEntry_ConfigLoc*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->config_loc )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_CONFIG_LOC,
                RSCacheDat2A_ConfigLocationSizeOf(entry->config_loc));
            config_locs_free(entry->config_loc);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->config_loc_hmap,
        sizeof(struct MapEntry_ConfigLoc),
        8192);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_CONFIG_LOC] = 0;
}

void
dat2_buildcache_models_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->models_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->models_hmap);
    struct MapEntry_CacheModel* entry = NULL;
    while( (entry = (struct MapEntry_CacheModel*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->model )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_MODEL,
                RSCacheDat2A_ModelSizeOf(entry->model));
            RSCacheDat2A_ModelFree(entry->model);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->models_hmap,
        sizeof(struct MapEntry_CacheModel),
        8192);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_MODEL] = 0;
}

void
dat2_buildcache_frames_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->frames_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->frames_hmap);
    struct MapEntry_FramesArchive* entry = NULL;
    while( (entry = (struct MapEntry_FramesArchive*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->archive )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_FRAMES,
                Dat2BuildCache_FramesArchiveSizeOf(entry->archive));
            Dat2BuildCache_FramesArchiveFree(entry->archive);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->frames_hmap,
        sizeof(struct MapEntry_FramesArchive),
        4096);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_FRAMES] = 0;
}

void
dat2_buildcache_skeletal_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache || !dat2_buildcache->skeletal_hmap )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(dat2_buildcache->skeletal_hmap);
    struct MapEntry_Skeletal* entry = NULL;
    while( (entry = (struct MapEntry_Skeletal*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->maya )
        {
            dat2_buildcache_mem_track_sub(
                dat2_buildcache,
                DAT2_BUILDCACHE_KIND_SKELETAL,
                RSCacheDat2A_AnimMayaSizeOf(entry->maya));
            RSCacheDat2A_AnimMayaFree(entry->maya);
        }
    }
    ToriDraw_MapIterFree(iter);
    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->skeletal_hmap,
        sizeof(struct MapEntry_Skeletal),
        4096);
    dat2_buildcache->asset_bytes[DAT2_BUILDCACHE_KIND_SKELETAL] = 0;
}

static void
dat2_buildcache_map_prune(
    struct Dat2BuildCache* dat2_buildcache,
    struct ToriDraw_Map** map_out,
    int entry_size)
{
    dat2_buildcache_map_reset(
        dat2_buildcache, map_out, entry_size, DAT2_BUILDCACHE_PRUNE_CAPACITY);
}

void
dat2_buildcache_prune(struct Dat2BuildCache* dat2_buildcache)
{
    if( !dat2_buildcache )
        return;

    dat2_buildcache_map_terrain_cleanup(dat2_buildcache);
    dat2_buildcache_map_scenery_cleanup(dat2_buildcache);
    dat2_buildcache_models_cleanup(dat2_buildcache);
    dat2_buildcache_sequences_cleanup(dat2_buildcache);
    dat2_buildcache_flotypes_cleanup(dat2_buildcache);
    dat2_buildcache_underlays_cleanup(dat2_buildcache);
    dat2_buildcache_scenery_configs_cleanup(dat2_buildcache);
    dat2_buildcache_frames_cleanup(dat2_buildcache);
    dat2_buildcache_skeletal_cleanup(dat2_buildcache);

    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->models_hmap, sizeof(struct MapEntry_CacheModel));
    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->map_terrain_hmap, sizeof(struct MapEntry_Terrain));
    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->map_scenery_hmap, sizeof(struct MapEntry_Scenery));
    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->sequences_hmap, sizeof(struct MapEntry_Sequence));
    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->flotype_hmap, sizeof(struct MapEntry_Flotype));
    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->underlay_hmap, sizeof(struct MapEntry_Underlay));
    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->config_loc_hmap, sizeof(struct MapEntry_ConfigLoc));
    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->frames_hmap, sizeof(struct MapEntry_FramesArchive));
    dat2_buildcache_map_prune(
        dat2_buildcache, &dat2_buildcache->skeletal_hmap, sizeof(struct MapEntry_Skeletal));
}
