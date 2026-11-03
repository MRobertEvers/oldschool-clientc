/*
 * editor_recent.ini through the IO queue. task_editor_recent_io.h has the
 * contract; task_editor_hotkeys_io.c is the same shape for the hotkeys file.
 */

#include "editor/task_editor_recent_io.h"

#include "log/torirs_log.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* One slot, one item: each task issues a single request and parks on it. */
#define EDITOR_RECENT_IO_SLOT 0

struct Task_EditorRecentLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct EditorRecentLoad* out;
    char path[TORIRS_IOITEM_MAX_PATH];
    /* Read only after the yield; a protothread local would not survive it. */
    struct ToriRS_IOItem* item;
};

static int
Task_EditorRecentLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_EditorRecentLoad* task = (struct Task_EditorRecentLoad*)task_base;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileRead(io, EDITOR_RECENT_IO_SLOT, task->path);
    PT_YIELD(&task->pt);

    task->item = ToriRS_IO_TaskSlot(io, EDITOR_RECENT_IO_SLOT);
    if( IOITEM_ERROR_CODE(task->item) == 0 && IOITEM_DATA(task->item) && IOITEM_DATA_SIZE(task->item) > 0 )
    {
        (void)Editor_RecentDecode(&task->out->recent, IOITEM_DATA(task->item), IOITEM_DATA_SIZE(task->item));
        task->out->found = 1;
    }
    /* No file is a first launch, not an error. The slot must be left empty
     * for the next task that uses it. */
    ToriRS_IO_ClearItem(task->item);
    task->out->done = 1;

    PT_END(&task->pt);
}

static void
Task_EditorRecentLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Process(), task_base);
}

static struct ToriRS_TaskVTable Task_EditorRecentLoad_VTable = {
    .run = Task_EditorRecentLoad_Run,
    .free = Task_EditorRecentLoad_Free,
};

struct ToriRS_Task*
CreateTask_EditorRecentLoad(
    struct EditorRecentLoad* out,
    const char* path)
{
    struct Task_EditorRecentLoad* task;

    assert(out);
    assert(path);
    assert(path[0]);
    assert(strlen(path) < TORIRS_IOITEM_MAX_PATH);

    out->done = 0;
    out->found = 0;
    Editor_RecentInit(&out->recent);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_EditorRecentLoad_VTable;
    snprintf(task->task.name, sizeof(task->task.name), "%s", "EditorRecentLoad");
    task->out = out;
    snprintf(task->path, sizeof(task->path), "%s", path);
    PT_INIT(&task->pt);
    return &task->task;
}

struct Task_EditorRecentSave
{
    struct ToriRS_Task task;
    struct pt pt;
    char path[TORIRS_IOITEM_MAX_PATH];
    struct ToriRS_IOItem* item;
    /** The encoded file, this task's: the write borrows it across the yield. */
    char* data;
    int size;
};

static int
Task_EditorRecentSave_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_EditorRecentSave* task = (struct Task_EditorRecentSave*)task_base;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileWrite(io, EDITOR_RECENT_IO_SLOT, task->path, task->data, task->size);
    PT_YIELD(&task->pt);

    task->item = ToriRS_IO_TaskSlot(io, EDITOR_RECENT_IO_SLOT);
    if( IOITEM_ERROR_CODE(task->item) != 0 )
        TORIRS_LOG("editor: could not write the recent squares to %s\n", task->path);
    ToriRS_IO_ClearItem(task->item);

    PT_END(&task->pt);
}

static void
Task_EditorRecentSave_Free(struct ToriRS_Task* task_base)
{
    struct Task_EditorRecentSave* task = (struct Task_EditorRecentSave*)task_base;

    ToriMem_Free(ToriMem_Process(), task->data);
    ToriMem_Free(ToriMem_Process(), task);
}

static struct ToriRS_TaskVTable Task_EditorRecentSave_VTable = {
    .run = Task_EditorRecentSave_Run,
    .free = Task_EditorRecentSave_Free,
};

struct ToriRS_Task*
CreateTask_EditorRecentSave(
    const struct Editor_Recent* recent,
    const char* path)
{
    struct Task_EditorRecentSave* task;

    assert(recent);
    assert(path);
    assert(path[0]);
    assert(strlen(path) < TORIRS_IOITEM_MAX_PATH);

    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_EditorRecentSave_VTable;
    snprintf(task->task.name, sizeof(task->task.name), "%s", "EditorRecentSave");
    snprintf(task->path, sizeof(task->path), "%s", path);
    Editor_RecentEncode(recent, &task->data, &task->size);
    PT_INIT(&task->pt);
    return &task->task;
}
