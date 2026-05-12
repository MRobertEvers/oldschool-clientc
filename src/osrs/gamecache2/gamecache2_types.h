#ifndef GAMECACHE2_TYPES_H
#define GAMECACHE2_TYPES_H

/*
 * GameCache2 consumer types.
 *
 * These are the public-facing types returned by the agnostic GameCache2
 * getter API.  They share layout with the existing GameCache* types so that
 * the same decoded data can be used by both the new API and the legacy World
 * pipeline without copies.  As GameCache2 matures these can diverge.
 */

#include "osrs/gamecache/gamecache_animbase.h"
#include "osrs/gamecache/gamecache_animframe.h"
#include "osrs/gamecache/gamecache_component.h"
#include "osrs/gamecache/gamecache_idk.h"
#include "osrs/gamecache/gamecache_loc.h"
#include "osrs/gamecache/gamecache_locs.h"
#include "osrs/gamecache/gamecache_model.h"
#include "osrs/gamecache/gamecache_npc.h"
#include "osrs/gamecache/gamecache_obj.h"
#include "osrs/gamecache/gamecache_overlay.h"
#include "osrs/gamecache/gamecache_sequence.h"
#include "osrs/gamecache/gamecache_spotanim.h"
#include "osrs/gamecache/gamecache_terrain.h"
#include "osrs/gamecache/gamecache_underlay.h"

typedef struct GameCacheModel       GameCache2Model;
typedef struct GameCacheLoc         GameCache2Loc;
typedef struct GameCacheMapLoc      GameCache2MapLoc;
typedef struct GameCacheMapLocs     GameCache2MapLocs;
typedef struct GameCacheOverlay     GameCache2Overlay;
typedef struct GameCacheUnderlay    GameCache2Underlay;
typedef struct GameCacheTerrainChunk GameCache2TerrainChunk;
typedef struct GameCacheFloorTile   GameCache2FloorTile;
typedef struct GameCacheSequence    GameCache2Sequence;
typedef struct GameCacheAnimBaseFrames GameCache2AnimBaseFrames;
typedef struct GameCacheAnimframe   GameCache2Animframe;
typedef struct GameCacheAnimBase    GameCache2AnimBase;
typedef struct GameCacheIdk         GameCache2Idk;
typedef struct GameCacheObj         GameCache2Obj;
typedef struct GameCacheNpc         GameCache2Npc;
typedef struct GameCacheSpotAnim    GameCache2SpotAnim;
typedef struct GameCacheComponent   GameCache2Component;

#endif /* GAMECACHE2_TYPES_H */
