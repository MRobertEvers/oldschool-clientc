#ifndef TORIAUXLIB_TASK_DAT2_CONFIG_META_LOAD_H
#define TORIAUXLIB_TASK_DAT2_CONFIG_META_LOAD_H

#include "3rd/minipt.h"
#include "ioqueue/libtorirs_io.h"

struct ToriAuxLibCache;

struct Task_Dat2ConfigMetaLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
};

struct Task_Dat2ConfigMetaLoad*
Task_Dat2ConfigMetaLoad_New(struct ToriAuxLibCache* cache);

void
Task_Dat2ConfigMetaLoad_Free(struct Task_Dat2ConfigMetaLoad* task);

int
Task_Dat2ConfigMetaLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
