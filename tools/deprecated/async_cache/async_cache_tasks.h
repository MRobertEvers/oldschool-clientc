#ifndef ASYNC_CACHE_TASKS_H
#define ASYNC_CACHE_TASKS_H

#include "../../src2/ioqueue/libtorirs_io.h"
#include "cachedat2.h"

struct LibToriRS_Task*
Task_AsyncCacheDat2_Model_Load_New(
    int id,
    struct CacheDat2* cachedat2);

struct LibToriRS_Task*
Task_AsyncCacheDat2_ObjectModel_Load_New(
    int object_id,
    struct CacheDat2* cachedat2);

typedef void (*ReferenceTableLoadCallback)(
    void* user,
    struct RSCacheDat2Disk_ReferenceTable* reference_table);

struct LibToriRS_Task*
Task_AsyncCacheDat2_ReferenceTable_Ensure_New(
    int table_id,
    struct CacheDat2* cachedat2,
    void* user,
    ReferenceTableLoadCallback callback);

#endif
