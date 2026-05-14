#ifndef GAMECACHE_H
#define GAMECACHE_H

#include "osrs/gamecache/gamecache_types.h"

#include "graphics/dash.h"

struct GameCache;

struct GameCache*
gamecache_new(void);

void
gamecache_free(struct GameCache* gc);

void
gamecache_clear(struct GameCache* gc);

struct GameCacheTerrain*
gamecache_get_map_terrain(struct GameCache* gc, int mapx, int mapz);

struct GameCacheFloorType*
gamecache_get_flotype(struct GameCache* gc, int flotype_id);

struct GameCacheLocList*
gamecache_get_scenery(struct GameCache* gc, int mapx, int mapz);

struct GameCacheModel*
gamecache_get_model(struct GameCache* gc, int model_id);

struct GameCacheLocConfig*
gamecache_get_config_loc(struct GameCache* gc, int config_loc_id);

struct GameCacheSequence*
gamecache_get_sequence(struct GameCache* gc, int sequence_id);

struct GameCacheAnimframe*
gamecache_get_animframe(struct GameCache* gc, int animframe_id);

struct GameCacheObj*
gamecache_get_obj(struct GameCache* gc, int obj_id);

struct GameCacheIdk*
gamecache_get_idk(struct GameCache* gc, int idk_id);

struct GameCacheNpc*
gamecache_get_npc(struct GameCache* gc, int npc_id);

struct GameCacheModel*
gamecache_get_obj_model(struct GameCache* gc, int obj_id);

struct GameCacheModel*
gamecache_get_idk_model(struct GameCache* gc, int idk_id);

struct GameCacheModel*
gamecache_get_npc_model(struct GameCache* gc, int npc_id);

void
gamecache_add_obj_model(struct GameCache* gc, int obj_id, struct GameCacheModel* model);

void
gamecache_add_idk_model(struct GameCache* gc, int idk_id, struct GameCacheModel* model);

void
gamecache_add_npc_model(struct GameCache* gc, int npc_id, struct GameCacheModel* model);

#endif
