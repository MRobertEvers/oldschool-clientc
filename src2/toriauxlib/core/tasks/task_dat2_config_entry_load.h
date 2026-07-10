#ifndef TORIAUXLIB_TASK_DAT2_CONFIG_ENTRY_LOAD_H
#define TORIAUXLIB_TASK_DAT2_CONFIG_ENTRY_LOAD_H

#include "3rd/minipt.h"
#include "ioqueue/libtorirs_io.h"

struct ToriAuxLibCache;

struct Task_Dat2ConfigEntryLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    int config_kind;
    int* ids;
    int id_count;
};

struct Task_Dat2ConfigEntryLoad*
Task_Dat2ConfigEntryLoad_New(
    struct ToriAuxLibCache* cache,
    int config_kind,
    const int* ids,
    int id_count);

void
Task_Dat2ConfigEntryLoad_Free(struct Task_Dat2ConfigEntryLoad* task);

int
Task_Dat2ConfigEntryLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
