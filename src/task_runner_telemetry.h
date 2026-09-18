#ifndef SRC_TASK_RUNNER_TELEMETRY_H
#define SRC_TASK_RUNNER_TELEMETRY_H

#include "asyncio.h"

#include <assert.h>
#include <string.h>

/*
 * What the runner can say about how a queue is being driven.
 *
 * Counted, never sampled, so the numbers answer a question exactly: how many
 * passes ran nothing because every answer was still on the wire (the frame
 * had to end), how many reads each KIND of task issued and how many of those
 * were issued one after another by a task that had just been resumed on the
 * previous one landing. That last number is the serialized-request count --
 * a CS2 script resolving sprites one per yield, a map load asking for a
 * reference table then a square then a loc -- and it is what says whether
 * the platform is slow or the client is asking one thing at a time.
 *
 * Read by boot_telemetry.c (the report and the JSON the browser page pulls)
 * and by nothing else; it changes no decision. Kept out of task_runner.h so
 * the runner reads as the runner: every call below is bookkeeping the pass
 * would be correct without.
 */
#define TASK_RUNNER_TELEMETRY_TASKS 48

struct TaskRunnerTaskTelemetry
{
    char name[32];
    long runs;
    long reads;
    /** Reads issued by a task resumed on its previous read landing. */
    long chained_reads;
    int max_chain;
};

struct TaskRunnerTelemetry
{
    long passes;
    long tasks_run;
    long reads_issued;
    /** Process calls that were handed at least one item. */
    long batches;
    int max_batch;
    /** Passes that stepped nothing while reads were still out. */
    long passes_waiting;
    /** Passes driven by the platform's landed hook rather than the frame. */
    long pump_passes;
    /** Set by the host around a pump-driven step; see App_PumpAsync. */
    int in_pump;
    struct TaskRunnerTaskTelemetry by_task[TASK_RUNNER_TELEMETRY_TASKS];
    int by_task_count;
    int by_task_overflow;
};

/* The row for a task's NAME, made on first sight; NULL once the table is
 * full, which is counted rather than asserted because a name is not a bug.
 * Taken BEFORE the run: a task that ends is freed, and its name with it. */
static inline struct TaskRunnerTaskTelemetry*
TaskRunnerTelemetry_Row(
    struct TaskRunnerTelemetry* telemetry,
    struct ToriRS_Task const* task)
{
    struct TaskRunnerTaskTelemetry* row;

    assert(telemetry);
    assert(task);
    for( int i = 0; i < telemetry->by_task_count; i++ )
    {
        if( strcmp(telemetry->by_task[i].name, task->name) == 0 )
            return &telemetry->by_task[i];
    }
    if( telemetry->by_task_count == TASK_RUNNER_TELEMETRY_TASKS )
    {
        telemetry->by_task_overflow++;
        return NULL;
    }
    row = &telemetry->by_task[telemetry->by_task_count++];
    memset(row, 0, sizeof(*row));
    strncpy(row->name, task->name, sizeof(row->name) - 1);
    return row;
}

static inline void
TaskRunnerTelemetry_Pass(struct TaskRunnerTelemetry* telemetry)
{
    assert(telemetry);
    telemetry->passes++;
    if( telemetry->in_pump )
        telemetry->pump_passes++;
}

/** A task is about to run. `row` may be NULL (table full). */
static inline void
TaskRunnerTelemetry_Run(
    struct TaskRunnerTelemetry* telemetry,
    struct TaskRunnerTaskTelemetry* row)
{
    assert(telemetry);
    telemetry->tasks_run++;
    if( row )
        row->runs++;
}

/** A task yielded and is still queued: did it queue a read? Counted per task
 *  name, and as a chain when the task was resumed on its previous read
 *  landing and immediately asked for the next. */
static inline void
TaskRunnerTelemetry_Yielded(
    struct TaskRunnerTaskTelemetry* row,
    struct ToriRS_Task* task)
{
    assert(task);
    if( task->io.kind == TORIRS_IOK_NONE )
    {
        task->read_chain = 0;
        return;
    }
    task->read_chain++;
    if( !row )
        return;
    row->reads++;
    if( task->read_chain >= 2 )
        row->chained_reads++;
    if( task->read_chain > row->max_chain )
        row->max_chain = task->read_chain;
}

/** The pass is over: `batch` reads go to the platform together, and the pass
 *  either advanced something or sat waiting on the wire. */
static inline void
TaskRunnerTelemetry_PassEnd(
    struct TaskRunnerTelemetry* telemetry,
    int batch,
    int ran,
    int waiting)
{
    assert(telemetry);
    if( batch > 0 )
    {
        telemetry->batches++;
        telemetry->reads_issued += batch;
        if( batch > telemetry->max_batch )
            telemetry->max_batch = batch;
    }
    if( !ran && waiting )
        telemetry->passes_waiting++;
}

#endif
