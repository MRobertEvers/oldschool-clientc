#ifndef GAMECACHE2_H
#define GAMECACHE2_H

/*
 * GameCache2 — unified asset cache replacing BuildCacheDat + GameCache.
 *
 * Consumers use the agnostic getter API (gamecache2_model_get, …).
 * Dat1-specific loading is driven through the gamecache2_dat1 API.
 *
 * The struct owns a GameCache internally; call gamecache2_get_gamecache() to
 * get a non-owning pointer suitable for passing to world_new() or other
 * legacy consumers.
 */

#include "gamecache2_types.h"

#include <stdbool.h>

struct GameCache2;
/* ---------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------*/

/** Allocate a new GameCache2 with an internally-owned GameCache.
 *  scene2 is borrowed and must remain valid during any Dat1 WorldBuild. */
struct GameCache2*
GameCache2_New();

void
GameCache2_Free(struct GameCache2* gc2);

GameCache2Loc*
GameCache2_LocGet(
    struct GameCache2* gc2,
    int loc_id);

GameCache2MapLocs*
GameCache2_MapLocsGet(
    struct GameCache2* gc2,
    int mapx,
    int mapz);

GameCache2TerrainChunk*
GameCache2_TerrainGet(
    struct GameCache2* gc2,
    int mapx,
    int mapz);

GameCache2Overlay*
GameCache2_OverlayGet(
    struct GameCache2* gc2,
    int flotype_id);

GameCache2Sequence*
GameCache2_SequenceGet(
    struct GameCache2* gc2,
    int sequence_id);

GameCache2AnimBaseFrames*
GameCache2_AnimBaseFramesGet(
    struct GameCache2* gc2,
    int animbaseframes_id);

GameCache2Animframe*
GameCache2_AnimframeGet(
    struct GameCache2* gc2,
    int animframe_id);

GameCache2Npc*
GameCache2_NpcGet(
    struct GameCache2* gc2,
    int npc_id);

GameCache2Obj*
GameCache2_ObjGet(
    struct GameCache2* gc2,
    int obj_id);

GameCache2Idk*
GameCache2_IdkGet(
    struct GameCache2* gc2,
    int idk_id);

GameCache2SpotAnim*
GameCache2_SpotAnimGet(
    struct GameCache2* gc2,
    int spotanim_id);

GameCache2Component*
GameCache2_ComponentGet(
    struct GameCache2* gc2,
    int component_id);

/** Returns the UIScene font slot id for a named font, or -1 if not found. */
int
GameCache2_FontGetByName(
    struct GameCache2* gc2,
    const char* name);

/** Returns the UIScene element id for a named sprite, or -1 if not found. */
int
GameCache2_SpriteGetByName(
    struct GameCache2* gc2,
    const char* name);

#endif /* GAMECACHE2_H */
