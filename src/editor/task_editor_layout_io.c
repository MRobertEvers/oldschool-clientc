/*
 * The editor layout file through the IO queue. task_editor_layout_io.h has
 * the contract; src/game/task_prefs_io.c is the pattern (and the reason a
 * settings write is never an fopen in the frame loop).
 */

#include "editor/task_editor_layout_io.h"

#include "log/torirs_log.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* One slot, one item: each task issues a single request and parks on it. */
#define EDITOR_LAYOUT_IO_SLOT 0

struct Task_EditorLayoutLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct EditorLayoutLoad* out;
    char path[TORIRS_IOITEM_MAX_PATH];
    /* Read only after the yield; a protothread local would not survive it. */
    struct ToriRS_IOItem* item;
};

static int
Task_EditorLayoutLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_EditorLayoutLoad* task = (struct Task_EditorLayoutLoad*)task_base;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileRead(io, EDITOR_LAYOUT_IO_SLOT, task->path);
    PT_YIELD(&task->pt);

    task->item = ToriRS_IO_TaskSlot(io, EDITOR_LAYOUT_IO_SLOT);
    if( IOITEM_ERROR_CODE(task->item) == 0 && IOITEM_DATA(task->item) && IOITEM_DATA_SIZE(task->item) > 0 )
        task->out->found =
            EditorLayout_Decode(&task->out->file, IOITEM_DATA(task->item), IOITEM_DATA_SIZE(task->item));
    /* No file is a first launch, not an error. The slot must be left empty
     * for the next task that uses it. */
    ToriRS_IO_ClearItem(task->item);
    task->out->done = 1;

    PT_END(&task->pt);
}

static void
Task_EditorLayoutLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Process(), task_base);
}

static struct ToriRS_TaskVTable Task_EditorLayoutLoad_VTable = {
    .run = Task_EditorLayoutLoad_Run,
    .free = Task_EditorLayoutLoad_Free,
};

struct ToriRS_Task*
CreateTask_EditorLayoutLoad(
    struct EditorLayoutLoad* out,
    const char* path)
{
    struct Task_EditorLayoutLoad* task;

    assert(out);
    assert(path);
    assert(path[0]);
    assert(strlen(path) < TORIRS_IOITEM_MAX_PATH);

    out->done = 0;
    out->found = 0;
    EditorLayout_Clear(&out->file);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_EditorLayoutLoad_VTable;
    snprintf(task->task.name, sizeof(task->task.name), "%s", "EditorLayoutLoad");
    task->out = out;
    snprintf(task->path, sizeof(task->path), "%s", path);
    PT_INIT(&task->pt);
    return &task->task;
}

struct Task_EditorLayoutSave
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
Task_EditorLayoutSave_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_EditorLayoutSave* task = (struct Task_EditorLayoutSave*)task_base;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileWrite(io, EDITOR_LAYOUT_IO_SLOT, task->path, task->data, task->size);
    PT_YIELD(&task->pt);

    task->item = ToriRS_IO_TaskSlot(io, EDITOR_LAYOUT_IO_SLOT);
    if( IOITEM_ERROR_CODE(task->item) != 0 )
        TORIRS_LOG("editor: could not write the layout to %s\n", task->path);
    ToriRS_IO_ClearItem(task->item);

    PT_END(&task->pt);
}

static void
Task_EditorLayoutSave_Free(struct ToriRS_Task* task_base)
{
    struct Task_EditorLayoutSave* task = (struct Task_EditorLayoutSave*)task_base;

    ToriMem_Free(ToriMem_Process(), task->data);
    ToriMem_Free(ToriMem_Process(), task);
}

static struct ToriRS_TaskVTable Task_EditorLayoutSave_VTable = {
    .run = Task_EditorLayoutSave_Run,
    .free = Task_EditorLayoutSave_Free,
};

struct ToriRS_Task*
CreateTask_EditorLayoutSave(
    const struct EditorLayoutFile* file,
    const char* path)
{
    struct Task_EditorLayoutSave* task;

    assert(file);
    assert(path);
    assert(path[0]);
    assert(strlen(path) < TORIRS_IOITEM_MAX_PATH);

    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_EditorLayoutSave_VTable;
    snprintf(task->task.name, sizeof(task->task.name), "%s", "EditorLayoutSave");
    snprintf(task->path, sizeof(task->path), "%s", path);
    EditorLayout_Encode(file, &task->data, &task->size);
    PT_INIT(&task->pt);
    return &task->task;
}
