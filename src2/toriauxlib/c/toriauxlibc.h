#ifndef TORIAUXLIBC_H
#define TORIAUXLIBC_H

#include "../../libtorirs.h"
#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/core/toriauxlibcore.h"

struct ToriAuxLibC;

enum ToriAuxLibCMode
{
    TORIAUXLIBC_MODE_DAT1,
    TORIAUXLIBC_MODE_DAT2,
};

struct ToriAuxLibC*
ToriAuxLibC_New(
    enum ToriAuxLibCMode mode,
    struct ToriAuxLibCore* core);

void
ToriAuxLibC_Free(struct ToriAuxLibC* c);

struct Dat1BuildCache*
ToriAuxLibC_Dat1BuildCache(struct ToriAuxLibC* c);
#define dat1(c) ((struct Dat1BuildCache*)ToriAuxLibC_Dat1BuildCache(c))

struct ToriAuxLibCore*
ToriAuxLibC_Core(struct ToriAuxLibC* c);

enum ToriAuxLibCMode
ToriAuxLibC_Mode(struct ToriAuxLibC* c);

struct ToriAuxLibCore_Model*
ToriAuxLibC_ModelNewFromCacheModel(const void* cache_model);

struct ToriAuxLibCore_Animation*
ToriAuxLibC_AnimationNewFromCacheDatAnimbaseframes(const void* abf);

struct ToriAuxLibCore_Texture*
ToriAuxLibC_TextureNewFromCacheDatTexture(
    const void* cache_texture,
    int animation_direction,
    int animation_speed);

struct ToriAuxLibCore_MapTerrain*
ToriAuxLibC_MapTerrainNewFromCacheMapTerrain(const void* cache_terrain);

struct ToriAuxLibCore_MapLocs*
ToriAuxLibC_MapLocsNewFromCacheMapLocs(const void* cache_locs);

struct ToriAuxLibCore_Flotype*
ToriAuxLibC_FlotypeNewFromCacheConfigOverlay(
    const void* cache_overlay,
    int id);

struct ToriAuxLibCore_Location*
ToriAuxLibC_LocationNewFromCacheConfigLocation(const void* cache_loc);

struct ToriAuxLibCore_Sequence*
ToriAuxLibC_SequenceNewFromCacheDatSequence(
    const void* cache_seq,
    int id);

struct Task_ToriAuxLibC_ModelLoad;

struct Task_ToriAuxLibC_ModelLoad*
Task_ToriAuxLibC_ModelLoad_New(
    struct ToriAuxLibC* c,
    int model_id);

int
Task_ToriAuxLibC_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibC_WorldRebuildNormal;

struct Task_ToriAuxLibC_WorldRebuildNormal*
Task_ToriAuxLibC_WorldRebuildNormal_New(
    struct ToriAuxLibC* c,
    int zonex,
    int zonez);

void
Task_ToriAuxLibC_WorldRebuildNormal_Free(struct Task_ToriAuxLibC_WorldRebuildNormal* task);

int
Task_ToriAuxLibC_WorldRebuildNormal_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
