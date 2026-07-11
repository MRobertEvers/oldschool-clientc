#ifndef TORIAUXLIB_TASK_DAT2_ANIM_IO_H
#define TORIAUXLIB_TASK_DAT2_ANIM_IO_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_anim_load.h"

#define TASK_DAT2_ANIM_RESOLVE_MAX_SEQS 16
#define TASK_DAT2_ANIM_MAX_PENDING_FRAMEMAPS 32

struct Task_Dat2AnimResolve
{
    struct pt thread;
    struct ToriAuxLibCache* c;
    struct Dat2BuildCache* bc;
    struct Dat2AnimArchiveSet aset;
    int seq_ids[TASK_DAT2_ANIM_RESOLVE_MAX_SEQS];
    int seq_count;
    int seq_index;
    int archive_index;
    int pending_framemap_ids[TASK_DAT2_ANIM_MAX_PENDING_FRAMEMAPS];
    int pending_framemap_count;
    int pending_framemap_index;
    struct RSCacheDat2Disk_Archive* held_idx0;
    int skeletal_maya_id;
    int skeletal_seq_id;
    int skeletal_animaya_aid;
    int skeletal_base_id;
    int current_aid;
};

void
Task_Dat2AnimResolve_Init(
    struct Task_Dat2AnimResolve* task,
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache* bc);

void
Task_Dat2AnimResolve_SetArchiveSet(
    struct Task_Dat2AnimResolve* task,
    const struct Dat2AnimArchiveSet* aset);

void
Task_Dat2AnimResolve_AddSequenceId(
    struct Task_Dat2AnimResolve* task,
    int seq_id);

int
Task_Dat2AnimResolve_Run(
    struct Task_Dat2AnimResolve* task,
    struct LibToriRS_IOContext* ctx);

void
Task_Dat2AnimResolve_Destroy(struct Task_Dat2AnimResolve* task);

#endif
