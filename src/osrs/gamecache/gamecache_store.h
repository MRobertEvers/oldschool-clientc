#ifndef GAMECACHE_STORE_H
#define GAMECACHE_STORE_H

/** Write-side API for `buildcachedat_loader` (and tests). Takes ownership of heap pointers. */

#include "osrs/gamecache/gamecache_types.h"

struct GameCache;

void
gamecache_store_put_terrain(struct GameCache* gc, int mapx, int mapz, struct GameCacheTerrain* t);

void
gamecache_store_put_scenery(struct GameCache* gc, int mapx, int mapz, struct GameCacheLocList* locs);

void
gamecache_store_put_flotype(struct GameCache* gc, int flotype_id, struct GameCacheFloorType* ft);

void
gamecache_store_put_config_loc(struct GameCache* gc, int id, struct GameCacheLocConfig* loc);

void
gamecache_store_put_model(struct GameCache* gc, int model_id, struct GameCacheModel* model);

void
gamecache_store_put_sequence(struct GameCache* gc, int sequence_id, struct GameCacheSequence* seq);

void
gamecache_store_put_animframe(struct GameCache* gc, int animframe_id, struct GameCacheAnimframe* af);

void
gamecache_store_put_obj(struct GameCache* gc, int obj_id, struct GameCacheObj* obj);

void
gamecache_store_put_idk(struct GameCache* gc, int idk_id, struct GameCacheIdk* idk);

void
gamecache_store_put_npc(struct GameCache* gc, int npc_id, struct GameCacheNpc* npc);

#endif
