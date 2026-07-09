#ifndef TORIAUXLIB_TASK_CLIENTSCRIPT_LOAD_H
#define TORIAUXLIB_TASK_CLIENTSCRIPT_LOAD_H

#include "3rd/minipt.h"
#include "ioqueue/libtorirs_io.h"

struct ToriAuxLibCache;

struct Task_ClientScriptLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    int* script_ids;
    int script_count;
    int script_index;
};

struct Task_ClientScriptLoad*
Task_ClientScriptLoad_New(
    struct ToriAuxLibCache* cache,
    const int* script_ids,
    int script_count);

void
Task_ClientScriptLoad_Free(struct Task_ClientScriptLoad* task);

int
Task_ClientScriptLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Dat2BuildCache_InterfaceArchive;

int
toriauxlibcache_collect_component_hook_script_ids(
    struct Dat2BuildCache_InterfaceArchive* iface,
    int** script_ids_out,
    int* script_count_out);

#endif
