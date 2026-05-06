#ifndef GAMECACHE_FROM_BUILDCACHEDAT_H
#define GAMECACHE_FROM_BUILDCACHEDAT_H

struct BuildCacheDat;
struct GameCache;

/** Deep-copy everything currently present in BuildCacheDat into GameCache (merge / overwrite by
 * id). */
void
gamecache_convert_all_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_reftables_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_floortypes_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_sequences_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_animbaseframes_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_npcs_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_objs_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_idks_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_spotanims_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_components_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_map_terrain_chunk_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz);

void
gamecache_convert_scenery_chunk_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz);

/** Copies loc configs referenced by the scenery chunk at (mapx,mapz), plus merge of entire loc
 * table if needed. */
void
gamecache_convert_loc_configs_chunk_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz);

/** Merge all decoded models currently in BuildCacheDat into GameCache. mapx/mapz unused (reserved).
 */
void
gamecache_convert_models_chunk_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz);

void
gamecache_convert_npc_models_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_obj_models_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

void
gamecache_convert_idk_models_from_buildcachedat(
    struct GameCache* gamecache,
    struct BuildCacheDat* buildcachedat);

#endif
