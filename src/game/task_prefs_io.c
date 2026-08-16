/*
 * The preferences file's two trips across the platform seam.
 *
 * Both are tasks rather than fopen calls in the App because that is where file
 * access lives in this client: the platform owns the disk, the IO queue is how
 * anything asks it for work, and the web build has no disk at all behind that
 * seam. A settings write in particular happens *during play* — the player drags
 * a slider — and a synchronous write inside the logic tick is a stall in the
 * frame loop on whatever filesystem the player has.
 *
 * The encode/decode either side of the IO is in rs_prefs.c and is pure; these
 * two are only the plumbing.
 */

#include "game/rs_prefs.h"

#include "asyncio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One slot, one item, no overlap: each of these tasks issues a single request
 * and parks on it. */
#define PREFS_IO_SLOT 0

struct Task_PrefsLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_Prefs* prefs;
    char path[TORIRS_IOITEM_MAX_PATH];
};

static int
Task_PrefsLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_PrefsLoad* task = (struct Task_PrefsLoad*)task_base;
    struct ToriRS_IOItem* item;

    PT_BEGIN(&task->pt);

    /* Defaults first, so a client whose file is missing or unreadable is a
     * client at its fresh settings rather than one reading uninitialised
     * memory — and so an early exit below needs no cleanup. */
    RS_Prefs_Defaults(task->prefs);

    ToriRS_IO_QueueFileRead(io, PREFS_IO_SLOT, task->path);
    PT_YIELD(&task->pt);

    item = &io->io_slots[PREFS_IO_SLOT];
    if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) )
        RS_Prefs_Decode(task->prefs, IOITEM_DATA(item), IOITEM_DATA_SIZE(item));
    /* No file is a first launch, not an error, so nothing is reported here.
     * The slot is shared with every other task on this queue and the next one
     * to use it asserts that it was left empty. */
    ToriRS_IO_ClearItem(item);

    PT_END(&task->pt);
}

static struct ToriRS_TaskVTable Task_PrefsLoad_VTable = {
    .run = Task_PrefsLoad_Run,
    .free = NULL,
};

struct ToriRS_Task*
CreateTask_PrefsLoad(
    struct RS_Prefs* prefs,
    char const* path)
{
    struct Task_PrefsLoad* task;

    assert(prefs);
    assert(path && *path);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PrefsLoad_VTable;
    strcpy(task->task.name, "PrefsLoad");
    task->prefs = prefs;
    snprintf(task->path, sizeof(task->path), "%s", path);
    PT_INIT(&task->pt);
    return &task->task;
}

struct Task_PrefsSave
{
    struct ToriRS_Task task;
    struct pt pt;
    char path[TORIRS_IOITEM_MAX_PATH];
    /* Read only after the yield; a protothread local would not survive it. */
    struct ToriRS_IOItem* item;
    /** The encoded file, owned by this task: the write borrows it and it must
     *  outlive the yield. */
    void* data;
    int size;
};

static int
Task_PrefsSave_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_PrefsSave* task = (struct Task_PrefsSave*)task_base;

    PT_BEGIN(&task->pt);

    if( !task->data )
        PT_EXIT(&task->pt); /* encode failed; already reported */

    ToriRS_IO_QueueFileWrite(io, PREFS_IO_SLOT, task->path, task->data, task->size);
    PT_YIELD(&task->pt);

    task->item = &io->io_slots[PREFS_IO_SLOT];
    if( IOITEM_ERROR_CODE(task->item) != 0 )
        fprintf(stderr, "prefs: could not write %s\n", task->path);
    /* Hand the borrowed payload back before releasing the slot: ClearItem frees
     * whatever `data` points at, and this buffer is the task's. */
    task->item->data = NULL;
    task->item->data_size = 0;
    ToriRS_IO_ClearItem(task->item);

    PT_END(&task->pt);
}

/* The payload is this task's, not the item's — the platform only borrowed it,
 * so freeing it belongs here, on every exit path including PT_EXIT. */
static void
Task_PrefsSave_Free(struct ToriRS_Task* task_base)
{
    struct Task_PrefsSave* task = (struct Task_PrefsSave*)task_base;

    free(task->data);
    free(task);
}

static struct ToriRS_TaskVTable Task_PrefsSave_VTable = {
    .run = Task_PrefsSave_Run,
    .free = Task_PrefsSave_Free,
};

struct ToriRS_Task*
CreateTask_PrefsSave(
    struct RS_Prefs const* prefs,
    char const* path)
{
    struct Task_PrefsSave* task;

    assert(prefs);
    assert(path && *path);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PrefsSave_VTable;
    strcpy(task->task.name, "PrefsSave");
    snprintf(task->path, sizeof(task->path), "%s", path);
    /* Encoded now, while the caller's snapshot is the one it asked to save. */
    if( !RS_Prefs_Encode(prefs, &task->data, &task->size) )
        fprintf(stderr, "prefs: could not encode settings for %s\n", path);
    PT_INIT(&task->pt);
    return &task->task;
}
