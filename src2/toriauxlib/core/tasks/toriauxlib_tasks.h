#ifndef TORIAUXLIB_TASKS_H
#define TORIAUXLIB_TASKS_H

#include "component_load_types.h"
#include "instance_revconfig_context.h"
#include "ioqueue/libtorirs_io.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "ui/uitree.h"

struct ToriDraw_Scene;
struct GameRunescape;
struct GameRunescapeCS2Host;
struct CS2VMX;
struct Dat2BuildCache;
struct Dat2BuildCache_InterfaceArchive;
struct ToriAuxLibTD;

/* -------------------------------------------------------------------------- */
/* Dat1 tasks                                                                 */
/* -------------------------------------------------------------------------- */

struct LibToriRS_Task*
Task_Dat1ModelLoad_New(struct ToriAuxLibCache* c, int model_id);

struct LibToriRS_Task*
Task_Dat1Animate_New(struct ToriAuxLibCache* c, int anim_id);

struct LibToriRS_Task*
Task_Dat1NpcAdd_New(struct ToriAuxLibCache* c, int npc_id);

struct LibToriRS_Task*
Task_Dat1PlayerAdd_New(
    struct ToriAuxLibCache* c,
    const int appearance[12],
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l);

struct LibToriRS_Task*
Task_Dat1ProjectileAdd_New(struct ToriAuxLibCache* c, int model_id, int anim_id);

struct LibToriRS_Task*
Task_Dat1WorldRebuildNormalCenterzone_New(struct ToriAuxLibCache* c, int zonex, int zonez);

struct LibToriRS_Task*
Task_Dat1WorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count);

struct LibToriRS_Task*
Task_Dat1ComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks);

struct LibToriRS_Task*
Task_Dat1InvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks);

struct LibToriRS_Task*
Task_Dat1SpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item);

struct LibToriRS_Task*
Task_Dat1FontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item);

struct LibToriRS_Task*
Task_Dat1TdModelLoad_New(struct ToriAuxLibTD* tdx);

struct LibToriRS_Task*
Task_Dat1TdTexturesLoad_New(struct ToriAuxLibTD* tdx);

struct LibToriRS_Task*
Task_Dat1TdAnimationsLoad_New(struct ToriAuxLibTD* tdx);

/* -------------------------------------------------------------------------- */
/* Dat2 tasks                                                                 */
/* -------------------------------------------------------------------------- */

struct LibToriRS_Task*
Task_Dat2ModelLoad_New(struct ToriAuxLibCache* c, int model_id);

struct LibToriRS_Task*
Task_Dat2Animate_New(struct ToriAuxLibCache* c, int anim_id);

struct LibToriRS_Task*
Task_Dat2NpcAdd_New(struct ToriAuxLibCache* c, int npc_id);

struct LibToriRS_Task*
Task_Dat2PlayerAdd_New(
    struct ToriAuxLibCache* c,
    const int appearance[12],
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l);

struct LibToriRS_Task*
Task_Dat2ProjectileAdd_New(struct ToriAuxLibCache* c, int model_id, int anim_id);

struct LibToriRS_Task*
Task_Dat2WorldRebuildCenterzone_New(struct ToriAuxLibCache* c, int zonex, int zonez);

struct LibToriRS_Task*
Task_Dat2WorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count);

struct LibToriRS_Task*
Task_Dat2AnimResolve_New(
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache* bc,
    const int* seq_ids,
    int seq_count);

struct LibToriRS_Task*
Task_Dat2TexturesLoad_New(struct ToriAuxLibCache* cache);

struct LibToriRS_Task*
Task_Dat2ConfigEntryLoad_New(
    struct ToriAuxLibCache* cache,
    int config_kind,
    const int* ids,
    int id_count);

struct LibToriRS_Task*
Task_Dat2ClientScriptLoad_New(
    struct ToriAuxLibCache* cache,
    const int* script_ids,
    int script_count);

struct LibToriRS_Task*
Task_Dat2CS2Run_New(
    struct GameRunescape* game,
    struct CS2VMX* vm,
    struct GameRunescapeCS2Host* host,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* str_args,
    int str_arg_count);

struct LibToriRS_Task*
Task_Dat2ComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks);

struct LibToriRS_Task*
Task_Dat2InvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks);

struct LibToriRS_Task*
Task_Dat2SpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item);

struct LibToriRS_Task*
Task_Dat2FontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item);

/* -------------------------------------------------------------------------- */
/* Cache-mode wrappers                                                        */
/* -------------------------------------------------------------------------- */

struct LibToriRS_Task*
Task_CacheModelLoad_New(struct ToriAuxLibCache* c, int model_id);

struct LibToriRS_Task*
Task_CacheAnimate_New(struct ToriAuxLibCache* c, int anim_id);

struct LibToriRS_Task*
Task_CacheNpcAdd_New(struct ToriAuxLibCache* c, int npc_id);

struct LibToriRS_Task*
Task_CachePlayerAdd_New(
    struct ToriAuxLibCache* c,
    const int appearance[12],
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l);

struct LibToriRS_Task*
Task_CacheProjectileAdd_New(struct ToriAuxLibCache* c, int model_id, int anim_id);

struct LibToriRS_Task*
Task_CacheWorldRebuildNormalCenterzone_New(struct ToriAuxLibCache* c, int zonex, int zonez);

struct LibToriRS_Task*
Task_CacheWorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count);

struct LibToriRS_Task*
Task_CacheComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks);

struct LibToriRS_Task*
Task_CacheInvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks);

struct LibToriRS_Task*
Task_CacheSpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item);

struct LibToriRS_Task*
Task_CacheFontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item);

struct LibToriRS_Task*
Task_CacheTexturesLoad_New(struct ToriAuxLibCache* cache);

struct LibToriRS_Task*
Task_CacheConfigEntryLoad_New(
    struct ToriAuxLibCache* cache,
    int config_kind,
    const int* ids,
    int id_count);

struct LibToriRS_Task*
Task_CacheClientScriptLoad_New(
    struct ToriAuxLibCache* cache,
    const int* script_ids,
    int script_count);

/* -------------------------------------------------------------------------- */
/* Revconfig orchestrator                                                     */
/* -------------------------------------------------------------------------- */

#define INSTANCE_RC_MAX_CONFIG_FILES 8

struct Task_InstanceRevConfigLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* cache;
    struct ToriDraw_Scene* scene;
    struct UITree* tree;
    struct GameRunescape* game;
    char const* config_files[INSTANCE_RC_MAX_CONFIG_FILES];
    int config_file_count;
    char const* layout_group;
    struct RevConfigItemBuffer* items;
    int item_index;
    struct InstanceRevConfigContext rc_ctx;
    int config_fetch_index;
    bool ui_ready;
};

struct Task_InstanceRevConfigLoad*
Task_InstanceRevConfigLoad_FromTask(struct LibToriRS_Task* base);

struct LibToriRS_Task*
Task_InstanceRevConfigLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game,
    char const* const* config_files,
    int config_file_count,
    char const* layout_group);

void
Task_InstanceRevConfigLoad_Free(struct LibToriRS_Task* base);

bool
Task_InstanceRevConfigLoad_IsReady(struct LibToriRS_Task* base);

struct LibToriRS_Task*
Task_InstanceOnRCUIComponent_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigUIComponentItem const* item);

struct LibToriRS_Task*
Task_InstanceOnRCUILayout_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigUILayoutItem const* item);

struct LibToriRS_Task*
Task_InstanceOnRCInv_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigInvItem const* item);

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

int
toriauxlibcache_collect_component_hook_script_ids(
    struct Dat2BuildCache_InterfaceArchive* iface,
    int** script_ids_out,
    int* script_count_out);

#endif
