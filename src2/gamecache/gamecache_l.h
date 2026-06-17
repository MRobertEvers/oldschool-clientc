#ifndef GAMECACHE_LOADER_H
#define GAMECACHE_LOADER_H

#include "../libtorirs.h"
#include "buildcache/dat1_buildcache.h"
#include "gamecache.h"

struct GameCacheL;

enum GameCacheLMode
{
    GAMECACHE_L_MODE_DAT1,
    GAMECACHE_L_MODE_DAT2,
};

struct GameCacheL*
GameCacheL_New(enum GameCacheLMode mode);

void
GameCacheL_Free(struct GameCacheL* gamecache_l);

struct Dat1BuildCache*
GameCacheL_GetDat1BuildCache(struct GameCacheL* gamecache_l);
#define dat1(gamecache_l) ((struct Dat1BuildCache*)GameCacheL_GetDat1BuildCache(gamecache_l))

struct GameCache*
GameCacheL_GetGameCache(struct GameCacheL* gamecache_l);
#define gamecache(gamecache_l) ((struct GameCache*)GameCacheL_GetGameCache(gamecache_l))

enum GameCacheLMode
GameCacheL_GetMode(struct GameCacheL* gamecache_l);

struct Task_GameCacheL_ModelLoad;

struct Task_GameCacheL_ModelLoad*
Task_GameCacheL_ModelLoad_New(
    struct GameCacheL* gamecache_l,
    int model_id);

int
Task_GameCacheL_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_GameCacheL_WorldRebuildNormal;

struct Task_GameCacheL_WorldRebuildNormal*
Task_GameCacheL_WorldRebuildNormal_New(
    struct GameCacheL* gamecache_l,
    int zonex,
    int zonez);

void
Task_GameCacheL_WorldRebuildNormal_Free(struct Task_GameCacheL_WorldRebuildNormal* task);

int
Task_GameCacheL_WorldRebuildNormal_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif