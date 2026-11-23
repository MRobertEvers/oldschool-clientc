/*
 * The CS2 jit pack's boot read and write-behind store. task_cs2_jit_pack.h
 * says what they promise; engine/cs2_jit_cache.c holds every decision, so
 * these are only the IO and the yields around it.
 */

#include "engine/task_cs2_jit_pack.h"

#include "asyncio.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <string.h>

#define CS2JIT_PACK_IO_SLOT 0

/* ---- the boot read ---------------------------------------------------------- */

struct Task_CS2JitPackLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct CS2JitCache* cache;
};

static int
Task_CS2JitPackLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_CS2JitPackLoad* self = (struct Task_CS2JitPackLoad*)task_base;
    struct ToriRS_IOItem* item;

    PT_BEGIN(&self->pt);

    CS2JitCache_ReadIssued(self->cache);
    ToriRS_IO_QueueDerivedRead(io, CS2JIT_PACK_IO_SLOT, CS2JitCache_PackName(self->cache));
    PT_YIELD(&self->pt);

    /* Answered (the runner resumes a task only once its item is). Absent is
     * the first run; a read error was logged once by the executor. Either
     * way every script is built in memory, and the first store writes one. */
    item = ToriRS_IO_TaskSlot(io, CS2JIT_PACK_IO_SLOT);
    if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) )
        CS2JitCache_PackLanded(self->cache, IOITEM_DATA(item), IOITEM_DATA_SIZE(item));
    else
        CS2JitCache_PackAbsent(self->cache);
    ToriRS_IO_ClearItem(item);

    PT_END(&self->pt);
}

static void
Task_CS2JitPackLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Process(), task_base);
}

static struct ToriRS_TaskVTable Task_CS2JitPackLoad_VTable = {
    .run = Task_CS2JitPackLoad_Run,
    .free = Task_CS2JitPackLoad_Free,
};

struct ToriRS_Task*
CreateTask_CS2JitPackLoad(struct CS2JitCache* cache)
{
    struct Task_CS2JitPackLoad* task;

    assert(cache);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_CS2JitPackLoad_VTable;
    strcpy(task->task.name, "CS2JitPackLoad");
    task->cache = cache;
    PT_INIT(&task->pt);
    return &task->task;
}

/* ---- the write-behind store --------------------------------------------------- */

struct Task_CS2JitPackStore
{
    struct ToriRS_Task task;
    struct pt pt;
    struct CS2JitCache* cache;
    CS2JitScriptLookup lookup;
    void* lookup_user;
    /* Across the yields: protothread locals do not survive one. */
    int complete;
    void* bytes;
    int size;
};

static int
Task_CS2JitPackStore_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_CS2JitPackStore* self = (struct Task_CS2JitPackStore*)task_base;
    struct ToriRS_IOItem* item;
    bool ok;

    PT_BEGIN(&self->pt);

    /* Nothing new against the pack: the store is over and nothing is
     * written (the plan has ended it). */
    if( CS2JitCache_StorePlan(self->cache, self->lookup, self->lookup_user) == 0 )
        PT_EXIT(&self->pt);

    /* Bounded units, each one an ask of the background queue's slice. The
     * cache holds the plan and the buffer; nothing here points at a script
     * across a yield. */
    self->complete = 0;
    while( !self->complete )
    {
        self->complete =
            CS2JitCache_StoreUnit(self->cache, self->lookup, self->lookup_user, CS2JIT_STORE_UNIT_SCRIPTS);
        if( !self->complete )
            PT_TASK_YIELD_IF_SPENT();
    }

    /* The cache lends the finished pack until StoreDone, which is after this
     * write has been answered. */
    self->bytes = CS2JitCache_StoreBytes(self->cache, &self->size);
    ToriRS_IO_QueueDerivedWrite(
        io, CS2JIT_PACK_IO_SLOT, CS2JitCache_PackName(self->cache), self->bytes, self->size);
    PT_YIELD(&self->pt);

    item = ToriRS_IO_TaskSlot(io, CS2JIT_PACK_IO_SLOT);
    ok = IOITEM_ERROR_CODE(item) == 0;
    ToriRS_IO_ClearItem(item);
    self->bytes = NULL;
    CS2JitCache_StoreDone(self->cache, ok);
    if( CS2JitCache_Timing(self->cache) )
        CS2JitCache_Report(self->cache, "store");

    PT_END(&self->pt);
}

static void
Task_CS2JitPackStore_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Process(), task_base);
}

static struct ToriRS_TaskVTable Task_CS2JitPackStore_VTable = {
    .run = Task_CS2JitPackStore_Run,
    .free = Task_CS2JitPackStore_Free,
};

struct ToriRS_Task*
CreateTask_CS2JitPackStore(
    struct CS2JitCache* cache,
    CS2JitScriptLookup lookup,
    void* lookup_user)
{
    struct Task_CS2JitPackStore* task;

    assert(cache);
    assert(lookup);
    assert(CS2JitCache_StoreInFlight(cache));
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_CS2JitPackStore_VTable;
    strcpy(task->task.name, "CS2JitPackStore");
    task->cache = cache;
    task->lookup = lookup;
    task->lookup_user = lookup_user;
    PT_INIT(&task->pt);
    return &task->task;
}

bool
CS2JitCache_MaybeStore(
    struct CS2JitCache* cache,
    uint64_t now_ms,
    struct ToriRS_TaskQueue* background,
    CS2JitScriptLookup lookup,
    void* lookup_user)
{
    assert(cache);
    assert(background);
    assert(lookup);
    if( !CS2JitCache_StoreWanted(cache, now_ms) )
        return false;
    CS2JitCache_StoreBegin(cache);
    ToriRS_TaskQueue_Add(background, CreateTask_CS2JitPackStore(cache, lookup, lookup_user));
    return true;
}
