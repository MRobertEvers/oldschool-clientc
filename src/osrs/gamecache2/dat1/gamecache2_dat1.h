#ifndef GAMECACHE2_DAT1_H
#define GAMECACHE2_DAT1_H

#include "../gamecache2_iorequest.h"

/*
 * GameCache2Dat1 — Dat1 (legacy .dat cache) loading subsystem for GameCache2.
 *
 * Typical WorldBuild flow (driven by the CHost Lua script):
 *
 *   gamecache2dat1_worldbuild_begin(state, gc2)
 *   gamecache2dat1_worldbuild_chunks_begin(state, zone_x, zone_z, scene_size)
 *
 *   -- Global assets (once) --
 *   gamecache2dat1_worldbuild_set_config_jagfile(state, data, size)
 *   gamecache2dat1_worldbuild_set_versionlist_jagfile(state, data, size)
 *   gamecache2dat1_worldbuild_textures_load(state, data, size)     -- sprites archive
 *   gamecache2dat1_worldbuild_floortypes_init(state)
 *   gamecache2dat1_worldbuild_sequences_init(state)
 *   gamecache2dat1_worldbuild_animbaseframes_cache_add(state, id, data, size)  x N
 *   gamecache2dat1_worldbuild_animbaseframes_convert(state)
 *
 *   -- Per-chunk --
 *   for each (mapx, mapz):
 *     if not terrain_cached:
 *       gamecache2dat1_worldbuild_chunk_terrain_load(state, mapx, mapz, data, size)
 *     if not scenery_cached:
 *       gamecache2dat1_worldbuild_chunk_scenery_load(state, mapx, mapz, data, size)
 *     gamecache2dat1_worldbuild_chunk_loc_configs_load(state, mapx, mapz)
 *     for each needed model_id:
 *       gamecache2dat1_worldbuild_model_cache_add(state, model_id, data, size)
 *     gamecache2dat1_worldbuild_convert_chunk(state, mapx, mapz)
 *     gamecache2dat1_worldbuild_clear_model_cache(state)
 *     gamecache2dat1_worldbuild_clear_terrain_cache(state)
 *     gamecache2dat1_worldbuild_clear_scenery_cache(state)
 *
 *   gamecache2dat1_worldbuild_chunks_end(state)
 *   gamecache2dat1_worldbuild_clear_config_jagfile(state)
 *   gamecache2dat1_worldbuild_clear_versionlist_jagfile(state)
 *   gamecache2dat1_worldbuild_end(state)
 *
 * Note: "state" refers to the GameCache2Dat1State embedded in the GameCache2
 * struct; callers obtain it via gamecache2_dat1_state(gc2).
 */

#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Dat1 loader state (embedded in GameCache2)
 * -------------------------------------------------------------------------*/

struct GameCache2;
struct GameCache2Dat1;

struct GameCache2Dat1*
GameCache2Dat1_New();
void
GameCache2Dat1_Free(struct GameCache2Dat1* dat1);

void
GameCache2Dat1_WorldBuild_BeginFetch(
    struct GameCache2* gc2,
    struct GameCache2IORequestBuffer* request_buffer);

void
GameCache2Dat1_WorldBuild_BeginLoad(
    struct GameCache2* gc2,
    struct GameCache2Archive* archives);

void
GameCache2Dat1_WorldBuild_End(struct GameCache2* gc2);

#endif /* GAMECACHE2_DAT1_H */
