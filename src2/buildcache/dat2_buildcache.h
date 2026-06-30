#ifndef DAT2_BUILDCACHE_H
#define DAT2_BUILDCACHE_H

#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2a/dat2a_framemap.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2disk/dat2disk.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ToriAuxLibCore_Sprite;

enum Dat2BuildCache_Kind
{
    DAT2_BUILDCACHE_KIND_MODEL,
    DAT2_BUILDCACHE_KIND_MAP_TERRAIN,
    DAT2_BUILDCACHE_KIND_MAP_SCENERY,
    DAT2_BUILDCACHE_KIND_SEQUENCE,
    DAT2_BUILDCACHE_KIND_FLOTYPE,
    DAT2_BUILDCACHE_KIND_UNDERLAY,
    DAT2_BUILDCACHE_KIND_CONFIG_LOC,
    DAT2_BUILDCACHE_KIND_FRAMES,
    DAT2_BUILDCACHE_KIND_SKELETAL,
    DAT2_BUILDCACHE_KIND_IDENTKIT,
    DAT2_BUILDCACHE_KIND_OBJECT,
    DAT2_BUILDCACHE_KIND_NPCTYPE,
    DAT2_BUILDCACHE_KIND_COUNT,
};

struct Dat2BuildCache_MemReport
{
    size_t asset_bytes[DAT2_BUILDCACHE_KIND_COUNT];
    size_t asset_bytes_total;
    size_t map_buffer_bytes;
    size_t bytes_total;
};

struct VarPVarBitManager;
struct ToriDraw_Map;
struct ToriDraw_Sprite;
struct RSCacheDat2Disk;
struct RSCacheDat2Disk_Archive;
struct RSCacheDat2A_ConfigOverlay;
struct RSCacheDat2A_ConfigUnderlay;
struct RSCacheDat2A_ConfigLocation;
struct RSCacheDat2A_ConfigSequence;
struct RSCacheDat2A_AnimMaya;
struct RSCacheDat2A_ConfigIdk;
struct RSCacheDat2A_ConfigObject;
struct RSCacheDat2A_ConfigNpctype;

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

/**
 * All decoded widgets from one interfaces archive (idx=iface_id).
 */
struct Dat2BuildCache_InterfaceArchive
{
    Component** components;
    int component_count;
};

void
Dat2BuildCache_InterfaceArchiveFree(struct Dat2BuildCache_InterfaceArchive* archive);

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
    struct ToriDraw_Map* identkit_hmap;
    struct ToriDraw_Map* object_hmap;
    struct ToriDraw_Map* npctype_hmap;
    struct ToriDraw_Map* interfaces_hmap;
    struct ToriDraw_Map* dynamic_sprites_hmap;
    struct RSCacheDat2Disk_ReferenceTable* reference_tables[RSCacheDat2Disk_Table_Count];
    size_t asset_bytes[DAT2_BUILDCACHE_KIND_COUNT];
    size_t map_buffer_bytes;
};

struct Dat2BuildCache*
dat2_buildcache_new(void);

void
dat2_buildcache_free(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_mem_bytes(
    struct Dat2BuildCache* dat2_buildcache,
    struct Dat2BuildCache_MemReport* out);

size_t
dat2_buildcache_bytes_total(struct Dat2BuildCache* dat2_buildcache);

size_t
Dat2BuildCache_FramesArchiveSizeOf(const struct Dat2BuildCache_FramesArchive* fa);

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
dat2_buildcache_model_remove(
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

struct RSCacheDat2A_ConfigSequence*
dat2_buildcache_sequence_get(
    struct Dat2BuildCache* dat2_buildcache,
    int seq_id);

void
dat2_buildcache_scenery_configs_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    struct VarPVarBitManager* varp_mgr);

void
dat2_buildcache_identkit_add(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id,
    struct RSCacheDat2A_ConfigIdk* idk);

struct RSCacheDat2A_ConfigIdk*
dat2_buildcache_identkit_get(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id);

void
dat2_buildcache_object_add(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id,
    struct RSCacheDat2A_ConfigObject* object);

struct RSCacheDat2A_ConfigObject*
dat2_buildcache_object_get(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id);

void
dat2_buildcache_npctype_add(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id,
    struct RSCacheDat2A_ConfigNpctype* npc);

struct RSCacheDat2A_ConfigNpctype*
dat2_buildcache_npctype_get(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id);

void
dat2_buildcache_identkits_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_objects_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_npctypes_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* wanted_ids,
    int wanted_count);

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

typedef void (*Dat2BuildCacheSkeletalCallback)(
    int anim_maya_id,
    struct RSCacheDat2A_AnimMaya* maya,
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

void
dat2_buildcache_foreach_skeletal(
    struct Dat2BuildCache* dat2_buildcache,
    Dat2BuildCacheSkeletalCallback callback,
    void* user_data);

void
dat2_buildcache_map_terrain_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_map_scenery_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_sequences_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_flotypes_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_underlays_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_scenery_configs_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_models_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_frames_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_skeletal_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_interface_archive_add(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id,
    struct Dat2BuildCache_InterfaceArchive* archive);

struct Dat2BuildCache_InterfaceArchive*
dat2_buildcache_interface_archive_get(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id);

bool
dat2_buildcache_interface_archive_has(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id);

void
dat2_buildcache_interfaces_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_reference_table_add(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id,
    struct RSCacheDat2Disk_ReferenceTable* table);

struct RSCacheDat2Disk_ReferenceTable*
dat2_buildcache_reference_table_get(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id);

bool
dat2_buildcache_reference_table_has(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id);

void
dat2_buildcache_reference_tables_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_dynamic_sprite_add(
    struct Dat2BuildCache* dat2_buildcache,
    int sprite_id,
    struct ToriAuxLibCore_Sprite* sprite);

struct ToriAuxLibCore_Sprite*
dat2_buildcache_dynamic_sprite_get(
    struct Dat2BuildCache* dat2_buildcache,
    int sprite_id);

struct ToriAuxLibCore_Sprite*
dat2_buildcache_dynamic_sprite_release(
    struct Dat2BuildCache* dat2_buildcache,
    int sprite_id);

bool
dat2_buildcache_dynamic_sprite_has(
    struct Dat2BuildCache* dat2_buildcache,
    int sprite_id);

void
dat2_buildcache_dynamic_sprites_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_prune(struct Dat2BuildCache* dat2_buildcache);

#endif
