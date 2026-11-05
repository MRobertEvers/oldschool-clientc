/*
 * editor_prefs.ini through the IO queue. task_editor_prefs_io.h has the
 * contract; task_editor_layout_io.c is the same shape for the layout file.
 */

#include "editor/task_editor_prefs_io.h"

#include "log/torirs_log.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* One slot, one item: each task issues a single request and parks on it. */
#define EDITOR_PREFS_IO_SLOT 0

struct Task_EditorPrefsLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct EditorPrefsLoad* out;
    char path[TORIRS_IOITEM_MAX_PATH];
    /* Read only after the yield; a protothread local would not survive it. */
    struct ToriRS_IOItem* item;
};

static int
Task_EditorPrefsLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_EditorPrefsLoad* task = (struct Task_EditorPrefsLoad*)task_base;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileRead(io, EDITOR_PREFS_IO_SLOT, task->path);
    PT_YIELD(&task->pt);

    task->item = ToriRS_IO_TaskSlot(io, EDITOR_PREFS_IO_SLOT);
    if( IOITEM_ERROR_CODE(task->item) == 0 && IOITEM_DATA(task->item) && IOITEM_DATA_SIZE(task->item) > 0 )
    {
        task->out->skipped = Editor_PrefsDecode(
            &task->out->prefs, IOITEM_DATA(task->item), IOITEM_DATA_SIZE(task->item));
        task->out->found = 1;
    }
    /* No file is a first launch, not an error. The slot must be left empty
     * for the next task that uses it. */
    ToriRS_IO_ClearItem(task->item);
    task->out->done = 1;

    PT_END(&task->pt);
}

static void
Task_EditorPrefsLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Process(), task_base);
}

static struct ToriRS_TaskVTable Task_EditorPrefsLoad_VTable = {
    .run = Task_EditorPrefsLoad_Run,
    .free = Task_EditorPrefsLoad_Free,
};

struct ToriRS_Task*
CreateTask_EditorPrefsLoad(
    struct EditorPrefsLoad* out,
    const char* path)
{
    struct Task_EditorPrefsLoad* task;

    assert(out);
    assert(path);
    assert(path[0]);
    assert(strlen(path) < TORIRS_IOITEM_MAX_PATH);

    out->done = 0;
    out->found = 0;
    Editor_PrefsInit(&out->prefs);
    out->skipped = 0;
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_EditorPrefsLoad_VTable;
    snprintf(task->task.name, sizeof(task->task.name), "%s", "EditorPrefsLoad");
    task->out = out;
    snprintf(task->path, sizeof(task->path), "%s", path);
    PT_INIT(&task->pt);
    return &task->task;
}

struct Task_EditorPrefsSave
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
Task_EditorPrefsSave_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_EditorPrefsSave* task = (struct Task_EditorPrefsSave*)task_base;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileWrite(io, EDITOR_PREFS_IO_SLOT, task->path, task->data, task->size);
    PT_YIELD(&task->pt);

    task->item = ToriRS_IO_TaskSlot(io, EDITOR_PREFS_IO_SLOT);
    if( IOITEM_ERROR_CODE(task->item) != 0 )
        TORIRS_LOG("editor: could not write the view preferences to %s\n", task->path);
    ToriRS_IO_ClearItem(task->item);

    PT_END(&task->pt);
}

static void
Task_EditorPrefsSave_Free(struct ToriRS_Task* task_base)
{
    struct Task_EditorPrefsSave* task = (struct Task_EditorPrefsSave*)task_base;

    ToriMem_Free(ToriMem_Process(), task->data);
    ToriMem_Free(ToriMem_Process(), task);
}

static struct ToriRS_TaskVTable Task_EditorPrefsSave_VTable = {
    .run = Task_EditorPrefsSave_Run,
    .free = Task_EditorPrefsSave_Free,
};

struct ToriRS_Task*
CreateTask_EditorPrefsSave(
    const struct Editor_Prefs* prefs,
    const char* path)
{
    struct Task_EditorPrefsSave* task;

    assert(prefs);
    assert(path);
    assert(path[0]);
    assert(strlen(path) < TORIRS_IOITEM_MAX_PATH);

    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_EditorPrefsSave_VTable;
    snprintf(task->task.name, sizeof(task->task.name), "%s", "EditorPrefsSave");
    snprintf(task->path, sizeof(task->path), "%s", path);
    Editor_PrefsEncode(prefs, &task->data, &task->size);
    PT_INIT(&task->pt);
    return &task->task;
}
