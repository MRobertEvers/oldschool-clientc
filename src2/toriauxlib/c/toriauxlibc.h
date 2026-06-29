#ifndef TORIAUXLIBC_H
#define TORIAUXLIBC_H

#include "../../libtorirs.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/core/toriauxlibcore.h"

struct ToriAuxLibC;
struct VarPVarBitManager;

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

struct Dat2BuildCache*
ToriAuxLibC_Dat2BuildCache(struct ToriAuxLibC* c);
#define dat2(c) ((struct Dat2BuildCache*)ToriAuxLibC_Dat2BuildCache(c))

void
ToriAuxLibC_SetDat2Disk(
    struct ToriAuxLibC* c,
    struct RSCacheDat2Disk* disk);

struct RSCacheDat2Disk*
ToriAuxLibC_Dat2Disk(struct ToriAuxLibC* c);

struct ToriAuxLibCore*
ToriAuxLibC_Core(struct ToriAuxLibC* c);

enum ToriAuxLibCMode
ToriAuxLibC_Mode(struct ToriAuxLibC* c);

void
ToriAuxLibC_SetVarPVarBit(
    struct ToriAuxLibC* c,
    struct VarPVarBitManager* varp_varbit);

struct VarPVarBitManager*
ToriAuxLibC_VarPVarBit(struct ToriAuxLibC* c);

struct ToriAuxLibCore_Model*
ToriAuxLibC_ModelNewFromCacheModel(const void* cache_model);

struct ToriAuxLibCore_Animation*
ToriAuxLibC_AnimationNewFromCacheDatAnimbaseframes(const void* abf);

struct ToriAuxLibCore_Texture*
ToriAuxLibC_TextureNewFromCacheDatTexture(
    const void* cache_texture,
    int animation_direction,
    int animation_speed);

struct RSCacheDat2A_Texture;
struct RSCacheDat2A_SpritePack;

struct ToriAuxLibCore_Texture*
ToriAuxLibC_TextureNewFromDat2Definition(
    struct RSCacheDat2A_Texture* def,
    struct RSCacheDat2A_SpritePack** packs,
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

struct ToriAuxLibCore_Sequence*
ToriAuxLibC_SequenceNewFromCacheDat2Sequence(
    const void* cache_seq,
    int id);

struct ToriAuxLibCore_Flotype*
ToriAuxLibC_UnderlayNewFromCacheConfigUnderlay(
    const void* cache_underlay,
    int id);

struct ToriAuxLibCore_Component*
ToriAuxLibC_ComponentNewFromCacheComponent(const void* cache_component);

struct Dat2BuildCache_FramesArchive;
struct ToriAuxLibCore_Animation*
ToriAuxLibC_AnimationNewFromDat2FramesArchive(
    const struct Dat2BuildCache_FramesArchive* fa,
    int archive_id);

struct RSCacheDat2A_AnimMaya;
/**
 * Build a ToriAuxLibCore_SkeletalAnim from a pre-baked matrix palette.
 * palette must be a heap-allocated [frame_count * bone_count * 16] float array;
 * ownership is transferred (freed by ToriAuxLibCore_SkeletalAnimFree).
 */
struct ToriAuxLibCore_SkeletalAnim*
ToriAuxLibC_SkeletalAnimNewFromBakedPalette(
    int   anim_id,
    float* palette,
    int    frame_count,
    int    bone_count);

struct Task_ToriAuxLibC_ModelLoad;

struct Task_ToriAuxLibC_ModelLoad*
Task_ToriAuxLibC_ModelLoad_New(
    struct ToriAuxLibC* c,
    int model_id);

int
Task_ToriAuxLibC_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

void
Task_ToriAuxLibC_ModelLoad_Free(struct Task_ToriAuxLibC_ModelLoad* task);

struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone;

struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone*
Task_ToriAuxLibC_WorldRebuildNormalCenterzone_New(
    struct ToriAuxLibC* c,
    int zonex,
    int zonez);

void
Task_ToriAuxLibC_WorldRebuildNormalCenterzone_Free(
    struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone* task);

int
Task_ToriAuxLibC_WorldRebuildNormalCenterzone_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibC_WorldRebuildChunkList;

struct Task_ToriAuxLibC_WorldRebuildChunkList*
Task_ToriAuxLibC_WorldRebuildChunkList_New(
    struct ToriAuxLibC* c,
    const int* chunks_x,
    const int* chunks_z,
    int count);

void
Task_ToriAuxLibC_WorldRebuildChunkList_Free(struct Task_ToriAuxLibC_WorldRebuildChunkList* task);

int
Task_ToriAuxLibC_WorldRebuildChunkList_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibC_PlayerAdd;

struct Task_ToriAuxLibC_PlayerAdd*
Task_ToriAuxLibC_PlayerAdd_New(
    struct ToriAuxLibC* c,
    const int appearance[12],
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l);

void
Task_ToriAuxLibC_PlayerAdd_Free(struct Task_ToriAuxLibC_PlayerAdd* task);

int
Task_ToriAuxLibC_PlayerAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibC_NpcAdd;

struct Task_ToriAuxLibC_NpcAdd*
Task_ToriAuxLibC_NpcAdd_New(
    struct ToriAuxLibC* c,
    int npc_id);

void
Task_ToriAuxLibC_NpcAdd_Free(struct Task_ToriAuxLibC_NpcAdd* task);

int
Task_ToriAuxLibC_NpcAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibC_ProjectileAdd;

struct Task_ToriAuxLibC_ProjectileAdd*
Task_ToriAuxLibC_ProjectileAdd_New(
    struct ToriAuxLibC* c,
    int model_id,
    int anim_id);

void
Task_ToriAuxLibC_ProjectileAdd_Free(struct Task_ToriAuxLibC_ProjectileAdd* task);

int
Task_ToriAuxLibC_ProjectileAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibC_Animate;

struct Task_ToriAuxLibC_Animate*
Task_ToriAuxLibC_Animate_New(
    struct ToriAuxLibC* c,
    int anim_id);

void
Task_ToriAuxLibC_Animate_Free(struct Task_ToriAuxLibC_Animate* task);

int
Task_ToriAuxLibC_Animate_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
