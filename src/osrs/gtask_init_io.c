#include "gtask_init_io.h"

#include "gtask.h"
#include "osrs/gio_assets.h"
#include "osrs/tables/model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct GTaskInitIO*
gtask_init_io_new(struct GIOQueue* io)
{
    struct GTaskInitIO* task = malloc(sizeof(struct GTaskInitIO));
    memset(task, 0, sizeof(struct GTaskInitIO));
    task->io = io;
    return task;
}

void
gtask_init_io_free(struct GTaskInitIO* task)
{
    free(task);
}

enum GTaskStatus
gtask_init_io_step(struct GTaskInitIO* task)
{
    struct GIOMessage message = { 0 };

    if( task->reqid_init == 0 )
        task->reqid_init = gioq_submit(task->io, GIO_REQ_INIT, 0, 0, 0);

    if( !gioq_poll(task->io, &message) )
        return GTASK_STATUS_PENDING;
    assert(message.message_id == task->reqid_init);

    gioq_release(task->io, &message);

    return GTASK_STATUS_COMPLETED;
}