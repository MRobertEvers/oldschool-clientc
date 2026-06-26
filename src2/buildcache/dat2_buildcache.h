#ifndef DAT2_BUILDCACHE_H
#define DAT2_BUILDCACHE_H

#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2a/dat2a_framemap.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"

#include <stdbool.h>
#include <stdint.h>

struct VarPVarBitManager;
struct ToriDraw_Map;
struct RSCacheDat2Disk;
struct RSCacheDat2Disk_Archive;
struct RSCacheDat2A_ConfigOverlay;
struct RSCacheDat2A_ConfigUnderlay;
struct RSCacheDat2A_ConfigLocation;
struct RSCacheDat2A_ConfigSequence;
struct RSCacheDat2A_AnimMaya;

/**
 * Decoded frames from one idx0 animation archive.
 * All frames share one framemap (owned by this struct).
 */
struct Dat2BuildCache_FramesArchive
{
    struct RSCacheDat2A_Framemap* framemap;
    struct RSCacheDat2A_Frame**   frames;
    int                           frame_count;
};

void
Dat2BuildCache_FramesArchiveFree(struct Dat2BuildCache_FramesArchive* fa);

struct Dat2BuildCache
{
    struct ToriDraw_Map* models_hmap;
    struct ToriDraw_Map* map_terrain_hmap;
    struct ToriDraw_Map* map_scenery_hmap;
    struct ToriDraw_Map* sequences_hmap;
    struct ToriDraw_Map* flotype_hmap;
    struct ToriDraw_Map* underlay_hmap;
    struct ToriDraw_Map* config_loc_hmap;
    struct ToriDraw_Map* frames_hmap;     /* archive_id -> Dat2BuildCache_FramesArchive* */
    struct ToriDraw_Map* skeletal_hmap;   /* anim_maya_id -> RSCacheDat2A_AnimMaya*      */
};

struct Dat2BuildCache*
dat2_buildcache_new(void);

void
dat2_buildcache_free(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_model_add(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id,
    struct RSCacheDat2A_Model* model);

struct RSCacheDat2A_Model*
dat2_buildcache_model_get(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id);

void
dat2_buildcache_map_terrain_add(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id,
    struct RSCacheDat2A_MapTerrain* terrain);

struct RSCacheDat2A_MapTerrain*
dat2_buildcache_map_terrain_get(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id);

bool
dat2_buildcache_map_terrain_has(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id);

void
dat2_buildcache_map_scenery_add(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id,
    struct RSCacheDat2A_MapLocs* locs);

struct RSCacheDat2A_MapLocs*
dat2_buildcache_map_scenery_get(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id);

bool
dat2_buildcache_map_scenery_has(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id);

void
dat2_buildcache_underlays_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive);

void
dat2_buildcache_overlays_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive);

void
dat2_buildcache_sequences_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive);

/* Load a single sequence config by id from an already-decoded sequences archive. */
bool
dat2_buildcache_sequence_load_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int seq_id);

void
dat2_buildcache_scenery_configs_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    struct VarPVarBitManager* varp_mgr);

/* Classic frame/framemap (idx0 + idx1) */

void
dat2_buildcache_frames_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    int archive_id);

struct Dat2BuildCache_FramesArchive*
dat2_buildcache_frames_get(
    struct Dat2BuildCache* dat2_buildcache,
    int archive_id);

struct Dat2BuildCache_FramesArchive*
dat2_buildcache_frames_take(
    struct Dat2BuildCache* dat2_buildcache,
    int archive_id);

bool
dat2_buildcache_frames_has(
    struct Dat2BuildCache* dat2_buildcache,
    int archive_id);

/* Skeletal Animaya (idx22) */

void
dat2_buildcache_skeletal_add(
    struct Dat2BuildCache* dat2_buildcache,
    int anim_maya_id,
    struct RSCacheDat2A_AnimMaya* maya);

struct RSCacheDat2A_AnimMaya*
dat2_buildcache_skeletal_get(
    struct Dat2BuildCache* dat2_buildcache,
    int anim_maya_id);

struct RSCacheDat2A_AnimMaya*
dat2_buildcache_skeletal_take(
    struct Dat2BuildCache* dat2_buildcache,
    int anim_maya_id);

bool
dat2_buildcache_skeletal_has(
    struct Dat2BuildCache* dat2_buildcache,
    int anim_maya_id);

struct RSCacheDat2A_ConfigLocation*
dat2_buildcache_config_loc_get(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id);

int
dat2_buildcache_get_scenery_model_ids(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id,
    int** model_ids_out);

int
dat2_buildcache_get_all_unique_scenery_model_ids(
    struct Dat2BuildCache* dat2_buildcache,
    int** model_ids_out);

typedef void (*Dat2BuildCacheSequenceCallback)(
    int seq_id,
    struct RSCacheDat2A_ConfigSequence* sequence,
    void* user_data);

typedef void (*Dat2BuildCacheFlotypeCallback)(
    int flo_id,
    struct RSCacheDat2A_ConfigOverlay* flotype,
    void* user_data);

typedef void (*Dat2BuildCacheUnderlayCallback)(
    int underlay_id,
    struct RSCacheDat2A_ConfigUnderlay* underlay,
    void* user_data);

typedef void (*Dat2BuildCacheLocationCallback)(
    int loc_id,
    struct RSCacheDat2A_ConfigLocation* config_loc,
    void* user_data);

void
dat2_buildcache_foreach_sequence(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheSequenceCallback callback,
    void* user_data);

void
dat2_buildcache_foreach_flotype(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheFlotypeCallback callback,
    void* user_data);

void
dat2_buildcache_foreach_underlay(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheUnderlayCallback callback,
    void* user_data);

void
dat2_buildcache_foreach_config_loc(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheLocationCallback callback,
    void* user_data);

#endif
