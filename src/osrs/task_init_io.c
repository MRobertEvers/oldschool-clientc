#include "task_init_io.h"

#include "gametask.h"
#include "osrs/gio_assets.h"
#include "osrs/rscache/tables/model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct TaskInitIO*
task_init_io_new(struct GIOQueue* io)
{
    struct TaskInitIO* task = malloc(sizeof(struct TaskInitIO));
    memset(task, 0, sizeof(struct TaskInitIO));
    task->io = io;
    return task;
}

void
task_init_io_free(struct TaskInitIO* task)
{
    free(task);
}

enum GameTaskStatus
task_init_io_step(struct TaskInitIO* task)
{
    struct GIOMessage message = { 0 };

    if( task->reqid_init == 0 )
        task->reqid_init = gioq_submit(task->io, GIO_REQ_INIT, 0, 0, 0);

    if( !gioq_poll(task->io, &message) )
        return GAMETASK_STATUS_PENDING;
    assert(message.message_id == task->reqid_init);

    gioq_release(task->io, &message);

    return GAMETASK_STATUS_COMPLETED;
}