#include "toriauxlibc.h"

#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "t_modelviewer_dat1_model.h"
#include "t_modelviewer_dat2_model.h"
#include "t_runescape_dat1_animate.h"
#include "t_runescape_dat1_npc_add.h"
#include "t_runescape_dat1_player_add.h"
#include "t_runescape_dat1_projectile_add.h"
#include "t_runescape_dat1_world.h"
#include "t_runescape_dat2_animate.h"
#include "t_runescape_dat2_npc_add.h"
#include "t_runescape_dat2_player_add.h"
#include "t_runescape_dat2_projectile_add.h"
#include "t_runescape_dat2_world.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriAuxLibC
{
    struct ToriAuxLibCore* core;
    enum ToriAuxLibCMode mode;
    struct Dat1BuildCache* dat1_buildcache;
    struct Dat2BuildCache* dat2_buildcache;
    struct RSCacheDat2Disk* dat2_disk;
    struct VarPVarBitManager* varp_varbit;
};

struct ToriAuxLibC*
ToriAuxLibC_New(
    enum ToriAuxLibCMode mode,
    struct ToriAuxLibCore* core)
{
    struct ToriAuxLibC* c = calloc(1, sizeof(struct ToriAuxLibC));
    if( !c )
        return NULL;

    c->mode = mode;
    c->core = core;

    switch( mode )
    {
    case TORIAUXLIBC_MODE_DAT1:
        c->dat1_buildcache = dat1_buildcache_new();
        if( !c->dat1_buildcache )
        {
            free(c);
            return NULL;
        }
        break;
    case TORIAUXLIBC_MODE_DAT2:
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
ToriAuxLibC_Free(struct ToriAuxLibC* c)
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
ToriAuxLibC_Dat1BuildCache(struct ToriAuxLibC* c)
{
    assert(c->mode == TORIAUXLIBC_MODE_DAT1);
    return c->dat1_buildcache;
}

struct Dat2BuildCache*
ToriAuxLibC_Dat2BuildCache(struct ToriAuxLibC* c)
{
    assert(c->mode == TORIAUXLIBC_MODE_DAT2);
    return c->dat2_buildcache;
}

void
ToriAuxLibC_SetDat2Disk(
    struct ToriAuxLibC* c,
    struct RSCacheDat2Disk* disk)
{
    if( !c )
        return;
    c->dat2_disk = disk;
}

struct RSCacheDat2Disk*
ToriAuxLibC_Dat2Disk(struct ToriAuxLibC* c)
{
    return c ? c->dat2_disk : NULL;
}

struct ToriAuxLibCore*
ToriAuxLibC_Core(struct ToriAuxLibC* c)
{
    return c ? c->core : NULL;
}

enum ToriAuxLibCMode
ToriAuxLibC_Mode(struct ToriAuxLibC* c)
{
    return c->mode;
}

void
ToriAuxLibC_SetVarPVarBit(
    struct ToriAuxLibC* c,
    struct VarPVarBitManager* varp_varbit)
{
    if( !c )
        return;
    c->varp_varbit = varp_varbit;
}

struct VarPVarBitManager*
ToriAuxLibC_VarPVarBit(struct ToriAuxLibC* c)
{
    return c ? c->varp_varbit : NULL;
}

struct Task_ToriAuxLibC_ModelLoad
{
    struct ToriAuxLibC* c;
    int model_id;
    struct Task_Dat1ModelLoad* task_dat1;
    struct Task_Dat2ModelLoad* task_dat2;
};

struct Task_ToriAuxLibC_ModelLoad*
Task_ToriAuxLibC_ModelLoad_New(
    struct ToriAuxLibC* c,
    int model_id)
{
    struct Task_ToriAuxLibC_ModelLoad* task = calloc(1, sizeof(struct Task_ToriAuxLibC_ModelLoad));
    if( !task )
        return NULL;
    task->c = c;
    task->model_id = model_id;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1ModelLoad_New(c, model_id);
    else if( c->mode == TORIAUXLIBC_MODE_DAT2 )
        task->task_dat2 = Task_Dat2ModelLoad_New(c, model_id);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibC_ModelLoad_Free(struct Task_ToriAuxLibC_ModelLoad* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 && task->task_dat1 )
        Task_Dat1ModelLoad_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 && task->task_dat2 )
        Task_Dat2ModelLoad_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibC_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_ModelLoad* task = (struct Task_ToriAuxLibC_ModelLoad*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1ModelLoad_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 )
        return Task_Dat2ModelLoad_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone
{
    struct ToriAuxLibC* c;
    struct Task_Dat1WorldRebuildNormalCenterzone* task_dat1;
    struct Task_Dat2WorldRebuildNormalCenterzone* task_dat2;
};

struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone*
Task_ToriAuxLibC_WorldRebuildNormalCenterzone_New(
    struct ToriAuxLibC* c,
    int zonex,
    int zonez)
{
    struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone* task =
        calloc(1, sizeof(struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1WorldRebuildNormalCenterzone_New(c, zonex, zonez);
    else if( c->mode == TORIAUXLIBC_MODE_DAT2 )
        task->task_dat2 = Task_Dat2WorldRebuildNormalCenterzone_New(c, zonex, zonez);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibC_WorldRebuildNormalCenterzone_Free(
    struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 && task->task_dat1 )
        Task_Dat1WorldRebuildNormalCenterzone_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 && task->task_dat2 )
        Task_Dat2WorldRebuildNormalCenterzone_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibC_WorldRebuildNormalCenterzone_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone* task =
        (struct Task_ToriAuxLibC_WorldRebuildNormalCenterzone*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1WorldRebuildNormalCenterzone_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 )
        return Task_Dat2WorldRebuildNormalCenterzone_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibC_WorldRebuildChunkList
{
    struct ToriAuxLibC* c;
    struct Task_Dat1WorldRebuildChunkList* task_dat1;
    struct Task_Dat2WorldRebuildChunkList* task_dat2;
};

struct Task_ToriAuxLibC_WorldRebuildChunkList*
Task_ToriAuxLibC_WorldRebuildChunkList_New(
    struct ToriAuxLibC* c,
    const int* chunks_x,
    const int* chunks_z,
    int count)
{
    struct Task_ToriAuxLibC_WorldRebuildChunkList* task =
        calloc(1, sizeof(struct Task_ToriAuxLibC_WorldRebuildChunkList));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1WorldRebuildChunkList_New(c, chunks_x, chunks_z, count);
    else if( c->mode == TORIAUXLIBC_MODE_DAT2 )
        task->task_dat2 = Task_Dat2WorldRebuildChunkList_New(c, chunks_x, chunks_z, count);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibC_WorldRebuildChunkList_Free(struct Task_ToriAuxLibC_WorldRebuildChunkList* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 && task->task_dat1 )
        Task_Dat1WorldRebuildChunkList_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 && task->task_dat2 )
        Task_Dat2WorldRebuildChunkList_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibC_WorldRebuildChunkList_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_WorldRebuildChunkList* task =
        (struct Task_ToriAuxLibC_WorldRebuildChunkList*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1WorldRebuildChunkList_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 )
        return Task_Dat2WorldRebuildChunkList_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibC_PlayerAdd
{
    struct ToriAuxLibC* c;
    struct Task_Dat1PlayerAdd* task_dat1;
    struct Task_Dat2PlayerAdd* task_dat2;
};

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
    int walkanim_l)
{
    struct Task_ToriAuxLibC_PlayerAdd* task =
        calloc(1, sizeof(struct Task_ToriAuxLibC_PlayerAdd));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1PlayerAdd_New(
            c, appearance, readyanim, walkanim, turnanim, runanim, walkanim_b, walkanim_r, walkanim_l);
    else if( c->mode == TORIAUXLIBC_MODE_DAT2 )
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
Task_ToriAuxLibC_PlayerAdd_Free(struct Task_ToriAuxLibC_PlayerAdd* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 && task->task_dat1 )
        Task_Dat1PlayerAdd_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 && task->task_dat2 )
        Task_Dat2PlayerAdd_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibC_PlayerAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_PlayerAdd* task = (struct Task_ToriAuxLibC_PlayerAdd*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1PlayerAdd_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 )
        return Task_Dat2PlayerAdd_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibC_NpcAdd
{
    struct ToriAuxLibC* c;
    struct Task_Dat1NpcAdd* task_dat1;
    struct Task_Dat2NpcAdd* task_dat2;
};

struct Task_ToriAuxLibC_NpcAdd*
Task_ToriAuxLibC_NpcAdd_New(
    struct ToriAuxLibC* c,
    int npc_id)
{
    struct Task_ToriAuxLibC_NpcAdd* task = calloc(1, sizeof(struct Task_ToriAuxLibC_NpcAdd));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1NpcAdd_New(c, npc_id);
    else if( c->mode == TORIAUXLIBC_MODE_DAT2 )
        task->task_dat2 = Task_Dat2NpcAdd_New(c, npc_id);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibC_NpcAdd_Free(struct Task_ToriAuxLibC_NpcAdd* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 && task->task_dat1 )
        Task_Dat1NpcAdd_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 && task->task_dat2 )
        Task_Dat2NpcAdd_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibC_NpcAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_NpcAdd* task = (struct Task_ToriAuxLibC_NpcAdd*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1NpcAdd_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 )
        return Task_Dat2NpcAdd_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibC_ProjectileAdd
{
    struct ToriAuxLibC* c;
    struct Task_Dat1ProjectileAdd* task_dat1;
    struct Task_Dat2ProjectileAdd* task_dat2;
};

struct Task_ToriAuxLibC_ProjectileAdd*
Task_ToriAuxLibC_ProjectileAdd_New(
    struct ToriAuxLibC* c,
    int model_id,
    int anim_id)
{
    struct Task_ToriAuxLibC_ProjectileAdd* task =
        calloc(1, sizeof(struct Task_ToriAuxLibC_ProjectileAdd));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1ProjectileAdd_New(c, model_id, anim_id);
    else if( c->mode == TORIAUXLIBC_MODE_DAT2 )
        task->task_dat2 = Task_Dat2ProjectileAdd_New(c, model_id, anim_id);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibC_ProjectileAdd_Free(struct Task_ToriAuxLibC_ProjectileAdd* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 && task->task_dat1 )
        Task_Dat1ProjectileAdd_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 && task->task_dat2 )
        Task_Dat2ProjectileAdd_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibC_ProjectileAdd_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_ProjectileAdd* task = (struct Task_ToriAuxLibC_ProjectileAdd*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1ProjectileAdd_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 )
        return Task_Dat2ProjectileAdd_Run(task->task_dat2, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibC_Animate
{
    struct ToriAuxLibC* c;
    struct Task_Dat1Animate* task_dat1;
    struct Task_Dat2Animate* task_dat2;
};

struct Task_ToriAuxLibC_Animate*
Task_ToriAuxLibC_Animate_New(
    struct ToriAuxLibC* c,
    int anim_id)
{
    struct Task_ToriAuxLibC_Animate* task = calloc(1, sizeof(struct Task_ToriAuxLibC_Animate));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1Animate_New(c, anim_id);
    else if( c->mode == TORIAUXLIBC_MODE_DAT2 )
        task->task_dat2 = Task_Dat2Animate_New(c, anim_id);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibC_Animate_Free(struct Task_ToriAuxLibC_Animate* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 && task->task_dat1 )
        Task_Dat1Animate_Free(task->task_dat1);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 && task->task_dat2 )
        Task_Dat2Animate_Free(task->task_dat2);
    free(task);
}

int
Task_ToriAuxLibC_Animate_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_Animate* task = (struct Task_ToriAuxLibC_Animate*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1Animate_Run(task->task_dat1, ctx);
    if( task->c->mode == TORIAUXLIBC_MODE_DAT2 )
        return Task_Dat2Animate_Run(task->task_dat2, ctx);
    return PT_ENDED;
}
