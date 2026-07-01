#include "toriauxlib/c/toriauxlibcache.h"

#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "toriauxlib/core/tasks/task_modelviewer_dat1_model.h"
#include "toriauxlib/core/tasks/task_modelviewer_dat2_model.h"
#include "toriauxlib/core/tasks/task_runescape_dat1_animate.h"
#include "toriauxlib/core/tasks/task_runescape_dat1_npc_add.h"
#include "toriauxlib/core/tasks/task_runescape_dat1_player_add.h"
#include "toriauxlib/core/tasks/task_runescape_dat1_projectile_add.h"
#include "toriauxlib/core/tasks/task_runescape_dat1_world.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_animate.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_npc_add.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_player_add.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_projectile_add.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_world.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriAuxLibCache
{
    struct ToriAuxLibCore* core;
    enum ToriAuxLibCacheMode mode;
    struct Dat1BuildCache* dat1_buildcache;
    struct Dat2BuildCache* dat2_buildcache;
    struct RSCacheDat2Disk* dat2_disk;
    struct VarPVarBitManager* varp_varbit;
};

struct ToriAuxLibCache*
ToriAuxLibCache_New(
    enum ToriAuxLibCacheMode mode,
    struct ToriAuxLibCore* core)
{
    struct ToriAuxLibCache* c = calloc(1, sizeof(struct ToriAuxLibCache));
    if( !c )
        return NULL;

    c->mode = mode;
    c->core = core;

    switch( mode )
    {
    case TORIAUXLIBCACHE_MODE_DAT1:
        c->dat1_buildcache = dat1_buildcache_new();
        if( !c->dat1_buildcache )
        {
            free(c);
            return NULL;
        }
        break;
    case TORIAUXLIBCACHE_MODE_DAT2:
        c->dat2_buildcache = dat2_buildcache_new();
        if( !c->dat2_buildcache )
        {
            free(c);
            return NULL;
        }
        break;
    default:
        free(c);
        return NULL;
    }

    return c;
}

void
ToriAuxLibCache_Free(struct ToriAuxLibCache* c)
{
    if( !c )
        return;
    if( c->dat1_buildcache )
        dat1_buildRSCacheDat2Disk_Free(c->dat1_buildcache);
    if( c->dat2_buildcache )
        dat2_buildcache_free(c->dat2_buildcache);
    free(c);
}

struct Dat1BuildCache*
ToriAuxLibCache_Dat1BuildCache(struct ToriAuxLibCache* c)
{
    assert(c->mode == TORIAUXLIBCACHE_MODE_DAT1);
    return c->dat1_buildcache;
}

struct Dat2BuildCache*
ToriAuxLibCache_Dat2BuildCache(struct ToriAuxLibCache* c)
{
    assert(c->mode == TORIAUXLIBCACHE_MODE_DAT2);
    return c->dat2_buildcache;
}

void
ToriAuxLibCache_SetDat2Disk(
    struct ToriAuxLibCache* c,
    struct RSCacheDat2Disk* disk)
{
    if( !c )
        return;
    c->dat2_disk = disk;
}

struct RSCacheDat2Disk*
ToriAuxLibCache_Dat2Disk(struct ToriAuxLibCache* c)
{
    return c ? c->dat2_disk : NULL;
}

struct ToriAuxLibCore*
ToriAuxLibCache_Core(struct ToriAuxLibCache* c)
{
    return c ? c->core : NULL;
}

enum ToriAuxLibCacheMode
ToriAuxLibCache_Mode(struct ToriAuxLibCache* c)
{
    return c->mode;
}

void
ToriAuxLibCache_SetVarPVarBit(
    struct ToriAuxLibCache* c,
    struct VarPVarBitManager* varp_varbit)
{
    if( !c )
        return;
    c->varp_varbit = varp_varbit;
}

void
ToriAuxLibCache_SetClientKronos(
    struct ToriAuxLibCache* c,
    bool kronos)
{
    if( !c || c->mode != TORIAUXLIBCACHE_MODE_DAT2 || !c->dat2_buildcache )
        return;
    dat2_buildcache_set_loc_decode_flags(
        c->dat2_buildcache, kronos ? CONFIG_LOC_DECODE_KRONOS : 0);
}

struct VarPVarBitManager*
ToriAuxLibCache_VarPVarBit(struct ToriAuxLibCache* c)
{
    return c ? c->varp_varbit : NULL;
}

void
ToriAuxLibCache_MemReport(
    struct ToriAuxLibCache* c,
    struct ToriAuxLibCache_MemReport* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( !c )
        return;

    if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 && c->dat2_buildcache )
    {
        dat2_buildcache_mem_bytes(c->dat2_buildcache, &out->l1_dat2);
        out->l1_bytes = out->l1_dat2.bytes_total;
    }
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 && c->dat1_buildcache )
    {
        out->l1_bytes = dat1_buildcache_bytes_total(c->dat1_buildcache);
    }

    if( c->core )
    {
        ToriAuxLibCore_MemBytes(c->core, &out->l2_core);
        out->l2_bytes = out->l2_core.bytes_total;
    }

    out->total_bytes = out->l1_bytes + out->l2_bytes;
}

size_t
ToriAuxLibCache_BytesTotal(struct ToriAuxLibCache* c)
{
    struct ToriAuxLibCache_MemReport report;
    ToriAuxLibCache_MemReport(c, &report);
    return report.total_bytes;
}

bool
ToriAuxLibCache_MemBudgetExceeded(
    struct ToriAuxLibCache* c,
    size_t budget_bytes)
{
    return ToriAuxLibCache_BytesTotal(c) > budget_bytes;
}

void
ToriAuxLibCache_PruneBuildCaches(struct ToriAuxLibCache* c)
{
    if( !c )
        return;

    if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 && c->dat1_buildcache )
        dat1_buildcache_prune(c->dat1_buildcache);
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 && c->dat2_buildcache )
        dat2_buildcache_prune(c->dat2_buildcache);
}

struct Task_ToriAuxLibCache_ModelLoad
{
    struct ToriAuxLibCache* c;
    int model_id;
    struct Task_Dat1ModelLoad* task_dat1;
    struct Task_Dat2ModelLoad* task_dat2;
};

struct Task_ToriAuxLibCache_ModelLoad*
Task_ToriAuxLibCache_ModelLoad_New(
    struct ToriAuxLibCache* c,
    int model_id)
{
    struct Task_ToriAuxLibCache_ModelLoad* task = calloc(1, sizeof(struct Task_ToriAuxLibCache_ModelLoad));
    if( !task )
        return NULL;
    task->c = c;
    task->model_id = model_id;
    if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        task->task_dat1 = Task_Dat1ModelLoad_New(c, model_id);
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        task->task_dat2 = Task_Dat2ModelLoad_New(c, model_id);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibCache_ModelLoad_Free(struct Task_ToriAuxLibCache_ModelLoad* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 && task->task_dat1 )
        Task_Dat1ModelLoad_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 && task->task_dat2 )
        Task_Dat2ModelLoad_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibCache_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibCache_ModelLoad* task = (struct Task_ToriAuxLibCache_ModelLoad*)task_state;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1ModelLoad_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2ModelLoad_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone
{
    struct ToriAuxLibCache* c;
    struct Task_Dat1WorldRebuildNormalCenterzone* task_dat1;
    struct Task_Dat2WorldRebuildNormalCenterzone* task_dat2;
};

struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone*
Task_ToriAuxLibCache_WorldRebuildNormalCenterzone_New(
    struct ToriAuxLibCache* c,
    int zonex,
    int zonez)
{
    struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone* task =
        calloc(1, sizeof(struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        task->task_dat1 = Task_Dat1WorldRebuildNormalCenterzone_New(c, zonex, zonez);
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        task->task_dat2 = Task_Dat2WorldRebuildNormalCenterzone_New(c, zonex, zonez);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibCache_WorldRebuildNormalCenterzone_Free(
    struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 && task->task_dat1 )
        Task_Dat1WorldRebuildNormalCenterzone_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 && task->task_dat2 )
        Task_Dat2WorldRebuildNormalCenterzone_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibCache_WorldRebuildNormalCenterzone_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone* task =
        (struct Task_ToriAuxLibCache_WorldRebuildNormalCenterzone*)task_state;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1WorldRebuildNormalCenterzone_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2WorldRebuildNormalCenterzone_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibCache_WorldRebuildChunkList
{
    struct ToriAuxLibCache* c;
    struct Task_Dat1WorldRebuildChunkList* task_dat1;
    struct Task_Dat2WorldRebuildChunkList* task_dat2;
};

struct Task_ToriAuxLibCache_WorldRebuildChunkList*
Task_ToriAuxLibCache_WorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count)
{
    struct Task_ToriAuxLibCache_WorldRebuildChunkList* task =
        calloc(1, sizeof(struct Task_ToriAuxLibCache_WorldRebuildChunkList));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        task->task_dat1 = Task_Dat1WorldRebuildChunkList_New(c, chunks_x, chunks_z, count);
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        task->task_dat2 = Task_Dat2WorldRebuildChunkList_New(c, chunks_x, chunks_z, count);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibCache_WorldRebuildChunkList_Free(struct Task_ToriAuxLibCache_WorldRebuildChunkList* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 && task->task_dat1 )
        Task_Dat1WorldRebuildChunkList_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 && task->task_dat2 )
        Task_Dat2WorldRebuildChunkList_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibCache_WorldRebuildChunkList_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibCache_WorldRebuildChunkList* task =
        (struct Task_ToriAuxLibCache_WorldRebuildChunkList*)task_state;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1WorldRebuildChunkList_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2WorldRebuildChunkList_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibCache_PlayerAdd
{
    struct ToriAuxLibCache* c;
    struct Task_Dat1PlayerAdd* task_dat1;
    struct Task_Dat2PlayerAdd* task_dat2;
};

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
    int walkanim_l)
{
    struct Task_ToriAuxLibCache_PlayerAdd* task =
        calloc(1, sizeof(struct Task_ToriAuxLibCache_PlayerAdd));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        task->task_dat1 = Task_Dat1PlayerAdd_New(
            c, appearance, readyanim, walkanim, turnanim, runanim, walkanim_b, walkanim_r, walkanim_l);
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        task->task_dat2 = Task_Dat2PlayerAdd_New(
            c, appearance, readyanim, walkanim, turnanim, runanim, walkanim_b, walkanim_r, walkanim_l);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibCache_PlayerAdd_Free(struct Task_ToriAuxLibCache_PlayerAdd* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 && task->task_dat1 )
        Task_Dat1PlayerAdd_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 && task->task_dat2 )
        Task_Dat2PlayerAdd_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibCache_PlayerAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibCache_PlayerAdd* task = (struct Task_ToriAuxLibCache_PlayerAdd*)task_state;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1PlayerAdd_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2PlayerAdd_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibCache_NpcAdd
{
    struct ToriAuxLibCache* c;
    struct Task_Dat1NpcAdd* task_dat1;
    struct Task_Dat2NpcAdd* task_dat2;
};

struct Task_ToriAuxLibCache_NpcAdd*
Task_ToriAuxLibCache_NpcAdd_New(
    struct ToriAuxLibCache* c,
    int npc_id)
{
    struct Task_ToriAuxLibCache_NpcAdd* task = calloc(1, sizeof(struct Task_ToriAuxLibCache_NpcAdd));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        task->task_dat1 = Task_Dat1NpcAdd_New(c, npc_id);
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        task->task_dat2 = Task_Dat2NpcAdd_New(c, npc_id);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibCache_NpcAdd_Free(struct Task_ToriAuxLibCache_NpcAdd* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 && task->task_dat1 )
        Task_Dat1NpcAdd_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 && task->task_dat2 )
        Task_Dat2NpcAdd_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibCache_NpcAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibCache_NpcAdd* task = (struct Task_ToriAuxLibCache_NpcAdd*)task_state;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1NpcAdd_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2NpcAdd_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibCache_ProjectileAdd
{
    struct ToriAuxLibCache* c;
    struct Task_Dat1ProjectileAdd* task_dat1;
    struct Task_Dat2ProjectileAdd* task_dat2;
};

struct Task_ToriAuxLibCache_ProjectileAdd*
Task_ToriAuxLibCache_ProjectileAdd_New(
    struct ToriAuxLibCache* c,
    int model_id,
    int anim_id)
{
    struct Task_ToriAuxLibCache_ProjectileAdd* task =
        calloc(1, sizeof(struct Task_ToriAuxLibCache_ProjectileAdd));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        task->task_dat1 = Task_Dat1ProjectileAdd_New(c, model_id, anim_id);
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        task->task_dat2 = Task_Dat2ProjectileAdd_New(c, model_id, anim_id);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibCache_ProjectileAdd_Free(struct Task_ToriAuxLibCache_ProjectileAdd* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 && task->task_dat1 )
        Task_Dat1ProjectileAdd_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 && task->task_dat2 )
        Task_Dat2ProjectileAdd_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibCache_ProjectileAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibCache_ProjectileAdd* task = (struct Task_ToriAuxLibCache_ProjectileAdd*)task_state;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1ProjectileAdd_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2ProjectileAdd_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibCache_Animate
{
    struct ToriAuxLibCache* c;
    struct Task_Dat1Animate* task_dat1;
    struct Task_Dat2Animate* task_dat2;
};

struct Task_ToriAuxLibCache_Animate*
Task_ToriAuxLibCache_Animate_New(
    struct ToriAuxLibCache* c,
    int anim_id)
{
    struct Task_ToriAuxLibCache_Animate* task = calloc(1, sizeof(struct Task_ToriAuxLibCache_Animate));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        task->task_dat1 = Task_Dat1Animate_New(c, anim_id);
    else if( c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        task->task_dat2 = Task_Dat2Animate_New(c, anim_id);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibCache_Animate_Free(struct Task_ToriAuxLibCache_Animate* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 && task->task_dat1 )
        Task_Dat1Animate_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 && task->task_dat2 )
        Task_Dat2Animate_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibCache_Animate_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibCache_Animate* task = (struct Task_ToriAuxLibCache_Animate*)task_state;
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1Animate_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2Animate_Run(task->task_dat2, ctx);
    return PT_ENDED;
}
