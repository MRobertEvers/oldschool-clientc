#ifndef TORIAUXLIBCACHE_H
#define TORIAUXLIBCACHE_H

#include "../../libtorirs.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/core/toriauxlibcore.h"

struct ToriAuxLibCache;
struct VarPVarBitManager;

enum ToriAuxLibCacheMode
{
    TORIAUXLIBCACHE_MODE_DAT1,
    TORIAUXLIBCACHE_MODE_DAT2,
};

struct ToriAuxLibCache*
ToriAuxLibCache_New(
    enum ToriAuxLibCacheMode mode,
    struct ToriAuxLibCore* core);

void
ToriAuxLibCache_Free(struct ToriAuxLibCache* c);

struct Dat1BuildCache*
ToriAuxLibCache_Dat1BuildCache(struct ToriAuxLibCache* c);
#define dat1(c) ((struct Dat1BuildCache*)ToriAuxLibCache_Dat1BuildCache(c))

struct Dat2BuildCache*
ToriAuxLibCache_Dat2BuildCache(struct ToriAuxLibCache* c);
#define dat2(c) ((struct Dat2BuildCache*)ToriAuxLibCache_Dat2BuildCache(c))

void
ToriAuxLibCache_SetDat2Disk(
    struct ToriAuxLibCache* c,
    struct RSCacheDat2Disk* disk);

struct RSCacheDat2Disk*
ToriAuxLibCache_Dat2Disk(struct ToriAuxLibCache* c);

struct ToriAuxLibCore*
ToriAuxLibCache_Core(struct ToriAuxLibCache* c);

enum ToriAuxLibCacheMode
ToriAuxLibCache_Mode(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SetVarPVarBit(
    struct ToriAuxLibCache* c,
    struct VarPVarBitManager* varp_varbit);

void
ToriAuxLibCache_SetClientKronos(
    struct ToriAuxLibCache* c,
    bool kronos);

void
ToriAuxLibCache_SetClientscriptDecodeFlags(
    struct ToriAuxLibCache* c,
    int flags);

int
ToriAuxLibCache_ClientscriptDecodeFlags(struct ToriAuxLibCache* c);

struct VarPVarBitManager*
ToriAuxLibCache_VarPVarBit(struct ToriAuxLibCache* c);

struct ToriAuxLibCache_MemReport
{
    struct Dat2BuildCache_MemReport l1_dat2;
    struct ToriAuxLibCore_MemReport l2_core;
    size_t l1_bytes;
    size_t l2_bytes;
    size_t total_bytes;
};

void
ToriAuxLibCache_MemReport(
    struct ToriAuxLibCache* c,
    struct ToriAuxLibCache_MemReport* out);

size_t
ToriAuxLibCache_BytesTotal(struct ToriAuxLibCache* c);

bool
ToriAuxLibCache_MemBudgetExceeded(
    struct ToriAuxLibCache* c,
    size_t budget_bytes);

void
ToriAuxLibCache_PruneBuildCaches(struct ToriAuxLibCache* c);

struct ToriAuxLibCore_Model*
ToriAuxLibCache_ModelNewFromCacheModel(const void* cache_model);

struct ToriAuxLibCore_Animation*
ToriAuxLibCache_AnimationNewFromCacheDatAnimbaseframes(const void* abf);

struct ToriAuxLibCore_Texture*
ToriAuxLibCache_TextureNewFromCacheDatTexture(
    const void* cache_texture,
    int animation_direction,
    int animation_speed);

struct RSCacheDat2A_Texture;
struct RSCacheDat2A_SpritePack;

struct ToriAuxLibCore_Texture*
ToriAuxLibCache_TextureNewFromDat2Definition(
    struct RSCacheDat2A_Texture* def,
    struct RSCacheDat2A_SpritePack** packs,
    int animation_direction,
    int animation_speed);

struct ToriAuxLibCore_MapTerrain*
ToriAuxLibCache_MapTerrainNewFromCacheMapTerrain(const void* cache_terrain);

struct ToriAuxLibCore_MapLocs*
ToriAuxLibCache_MapLocsNewFromCacheMapLocs(const void* cache_locs);

struct ToriAuxLibCore_Flotype*
ToriAuxLibCache_FlotypeNewFromCacheConfigOverlay(
    const void* cache_overlay,
    int id);

struct ToriAuxLibCore_Location*
ToriAuxLibCache_LocationNewFromCacheConfigLocation(const void* cache_loc);

struct ToriAuxLibCore_Npctype*
ToriAuxLibCache_NpctypeNewFromDat1ConfigNpc(const void* cache_npc, int npc_id);

struct ToriAuxLibCore_Npctype*
ToriAuxLibCache_NpctypeNewFromDat2ConfigNpctype(const void* cache_npc, int npc_id);

struct ToriAuxLibCore_Objtype*
ToriAuxLibCache_ObjtypeNewFromDat1ConfigObj(const void* cache_obj, int obj_id);

struct ToriAuxLibCore_Objtype*
ToriAuxLibCache_ObjtypeNewFromDat2ConfigObject(const void* cache_obj, int obj_id);

struct ToriAuxLibCore_Sequence*
ToriAuxLibCache_SequenceNewFromCacheDatSequence(
    const void* cache_seq,
    int id);

struct ToriAuxLibCore_Sequence*
ToriAuxLibCache_SequenceNewFromCacheDat2Sequence(
    const void* cache_seq,
    int id);

struct ToriAuxLibCore_Flotype*
ToriAuxLibCache_UnderlayNewFromCacheConfigUnderlay(
    const void* cache_underlay,
    int id);

struct ToriAuxLibCore_Component*
ToriAuxLibCache_ComponentNewFromCacheComponent(const void* cache_component);

struct ToriAuxLibCore_Component*
ToriAuxLibCache_ComponentNewFromCacheDat2Component(const void* cache_component);

struct Dat2BuildCache_FramesArchive;
struct ToriAuxLibCore_Animation*
ToriAuxLibCache_AnimationNewFromDat2FramesArchive(
    const struct Dat2BuildCache_FramesArchive* fa,
    int archive_id);

struct RSCacheDat2A_AnimMaya;
/**
 * Build a ToriAuxLibCore_SkeletalAnim from a pre-baked matrix palette.
 * palette must be a heap-allocated [frame_count * bone_count * 16] float array;
 * ownership is transferred (freed by ToriAuxLibCore_SkeletalAnimFree).
 */
struct ToriAuxLibCore_SkeletalAnim*
ToriAuxLibCache_SkeletalAnimNewFromBakedPalette(
    int   anim_id,
    float* palette,
    int    frame_count,
    int    bone_count);

struct Task_ToriAuxLibCache_ModelLoad;

struct Task_ToriAuxLibCache_ModelLoad*
Task_ToriAuxLibCache_ModelLoad_New(
    struct ToriAuxLibCache* c,
    int model_id);

int
Task_ToriAuxLibCache_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

void
Task_ToriAuxLibCache_ModelLoad_Free(struct Task_ToriAuxLibCache_ModelLoad* task);

struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone;

struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone*
Task_ToriAuxLibCache_WorldRebuildNormalCenterzone_New(
    struct ToriAuxLibCache* c,
    int zonex,
    int zonez);

void
Task_ToriAuxLibCache_WorldRebuildNormalCenterzone_Free(
    struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone* task);

int
Task_ToriAuxLibCache_WorldRebuildNormalCenterzone_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibCache_WorldRebuildChunkList;

struct Task_ToriAuxLibCache_WorldRebuildChunkList*
Task_ToriAuxLibCache_WorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count);

void
Task_ToriAuxLibCache_WorldRebuildChunkList_Free(struct Task_ToriAuxLibCache_WorldRebuildChunkList* task);

int
Task_ToriAuxLibCache_WorldRebuildChunkList_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibCache_PlayerAdd;

struct Task_ToriAuxLibCache_PlayerAdd*
Task_ToriAuxLibCache_PlayerAdd_New(
    struct ToriAuxLibCache* c,
    const int appearance[12],
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l);

void
Task_ToriAuxLibCache_PlayerAdd_Free(struct Task_ToriAuxLibCache_PlayerAdd* task);

int
Task_ToriAuxLibCache_PlayerAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibCache_NpcAdd;

struct Task_ToriAuxLibCache_NpcAdd*
Task_ToriAuxLibCache_NpcAdd_New(
    struct ToriAuxLibCache* c,
    int npc_id);

void
Task_ToriAuxLibCache_NpcAdd_Free(struct Task_ToriAuxLibCache_NpcAdd* task);

int
Task_ToriAuxLibCache_NpcAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibCache_ProjectileAdd;

struct Task_ToriAuxLibCache_ProjectileAdd*
Task_ToriAuxLibCache_ProjectileAdd_New(
    struct ToriAuxLibCache* c,
    int model_id,
    int anim_id);

void
Task_ToriAuxLibCache_ProjectileAdd_Free(struct Task_ToriAuxLibCache_ProjectileAdd* task);

int
Task_ToriAuxLibCache_ProjectileAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibCache_Animate;

struct Task_ToriAuxLibCache_Animate*
Task_ToriAuxLibCache_Animate_New(
    struct ToriAuxLibCache* c,
    int anim_id);

void
Task_ToriAuxLibCache_Animate_Free(struct Task_ToriAuxLibCache_Animate* task);

int
Task_ToriAuxLibCache_Animate_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
