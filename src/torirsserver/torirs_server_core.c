#include "torirs_server_core.h"

#include "torirs_server_poll.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

void
ToriRSServer_CoreInit(
    struct ToriRSServerCore* core,
    int pool_threads)
{
    assert(core);
    assert(pool_threads >= 0);
    memset(core, 0, sizeof(*core));
    core->poll = ToriRSServer_PollNew();
    core->pool = ToriRSServer_PoolNew(pool_threads, pool_threads > 0 ? core->poll : NULL);
}

static void core_collect(struct ToriRSServerCore* core);

void
ToriRSServer_CoreShutdown(struct ToriRSServerCore* core)
{
    assert(core);
    /* Every job's memory is somebody's (a save's buffer, a connection), and
     * only a collect gives it back: wait the pool out before freeing it. */
    while( ToriRSServer_PoolPending(core->pool) > 0 )
        ToriRSServer_CoreStep(core, 100);
    ToriRSServer_PoolFree(core->pool);
    core->pool = NULL;
    ToriRSServer_PollFree(core->poll);
    core->poll = NULL;
}

void
ToriRSServer_CoInit(
    struct ToriRSServerCo* co,
    ToriRSServerCoStep step)
{
    assert(co);
    assert(step);
    memset(co, 0, sizeof(*co));
    PT_INIT(&co->pt);
    co->step = step;
}

void
ToriRSServer_CoreReady(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co)
{
    assert(core);
    assert(co);
    if( co->ready || co->finished || co->orphaned )
        return;
    co->ready = 1;
    co->ready_next = NULL;
    if( core->ready_tail )
        core->ready_tail->ready_next = co;
    else
        core->ready_head = co;
    core->ready_tail = co;
}

int
ToriRSServer_CoreDrop(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co,
    ToriRSServerCoFree free_fn)
{
    assert(core);
    assert(co);
    assert(free_fn);
    if( co->ready )
    {
        struct ToriRSServerCo* prev = NULL;

        for( struct ToriRSServerCo* at = core->ready_head; at; prev = at, at = at->ready_next )
        {
            if( at != co )
                continue;
            if( prev )
                prev->ready_next = at->ready_next;
            else
                core->ready_head = at->ready_next;
            if( core->ready_tail == at )
                core->ready_tail = prev;
            break;
        }
        co->ready = 0;
    }
    co->finished = 1;
    if( co->job && !co->job_collected )
    {
        /* In flight, or finished and still on the pool's done list: either
         * way the pool will hand the job (inside this coroutine) back, so the
         * collect frees it. */
        co->orphaned = 1;
        co->free_fn = free_fn;
        return 0;
    }
    co->job = NULL;
    return 1;
}

void
ToriRSServer_CoreSubmit(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co,
    struct ToriRSServerPoolJob* job)
{
    assert(core);
    assert(co);
    assert(job);
    assert(!co->job);
    co->job = job;
    co->job_collected = 0;
    job->owner = co;
    ToriRSServer_PoolSubmit(core->pool, job);
}

void
ToriRSServer_CoreSubmitDetached(
    struct ToriRSServerCore* core,
    struct ToriRSServerCoreJob* job,
    ToriRSServerJobFinish finish)
{
    assert(core);
    assert(job);
    assert(finish);
    job->finish = finish;
    job->job.owner = NULL;
    ToriRSServer_PoolSubmit(core->pool, &job->job);
}

static void
core_collect(struct ToriRSServerCore* core)
{
    struct ToriRSServerPoolJob* job = ToriRSServer_PoolCollect(core->pool);

    while( job )
    {
        struct ToriRSServerPoolJob* next = job->next;
        struct ToriRSServerCo* owner = (struct ToriRSServerCo*)job->owner;

        if( owner )
        {
            owner->job_collected = 1;
            if( owner->orphaned )
            {
                owner->job = NULL;
                owner->free_fn(owner);
            }
            else
                ToriRSServer_CoreReady(core, owner);
        }
        else
        {
            /* Only a detached job has no owner (CoreSubmitDetached), and a
             * detached job is always a ToriRSServerCoreJob. */
            struct ToriRSServerCoreJob* detached = (struct ToriRSServerCoreJob*)job;

            assert(detached->finish);
            detached->finish(core, job);
        }
        job = next;
    }
}

int
ToriRSServer_CoreHasReady(const struct ToriRSServerCore* core)
{
    assert(core);
    return core->ready_head != NULL;
}

void
ToriRSServer_CoreStep(
    struct ToriRSServerCore* core,
    long timeout_ms)
{
    struct ToriRSServerPollEvent events[256];
    struct ToriRSServerCo* batch;
    int count;

    assert(core);
    if( core->ready_head )
        timeout_ms = 0;
    count = ToriRSServer_PollWait(core->poll, timeout_ms, events, 256);
    for( int i = 0; i < count; i++ )
    {
        struct ToriRSServerCo* co = (struct ToriRSServerCo*)events[i].owner;

        if( !co )
            continue;
        co->readable |= events[i].readable;
        co->writable |= events[i].writable;
        co->closed |= events[i].closed;
        ToriRSServer_CoreReady(core, co);
    }
    core_collect(core);

    /* This pass runs what is ready now; whatever a resume readies waits for
     * the next pass. */
    batch = core->ready_head;
    core->ready_head = NULL;
    core->ready_tail = NULL;
    while( batch )
    {
        struct ToriRSServerCo* co = batch;
        int result;

        batch = co->ready_next;
        co->ready_next = NULL;
        co->ready = 0;
        if( co->finished )
            continue;
        result = co->step(core, co);
        co->readable = 0;
        co->writable = 0;
        co->closed = 0;
        if( result == PT_ENDED || result == PT_EXITED )
            co->finished = 1;
    }
}
