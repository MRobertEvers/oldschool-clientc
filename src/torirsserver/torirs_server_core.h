#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_CORE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_CORE_H
/*
 * The core loop's scheduler: coroutines, the ready queue, and the two things
 * that make a coroutine ready -- a socket event (torirs_server_poll.h) and a
 * finished pool job (torirs_server_pool.h).
 *
 * A coroutine is a protothread (3rd/minipt.h): its state is a struct on the
 * heap and a resume point, never a stack. That is what makes thousands of them
 * cheap (one per connection), and what will let a worker thread resume one
 * later without anything here changing: nothing about a coroutine is tied to
 * the thread that last ran it.
 *
 * Rules a coroutine body keeps:
 *   - One job in flight at a time (TORIRSSERVER_CO_AWAIT_JOB). It is resumed
 *     only once that job is done, so a resumed coroutine never finds its IO
 *     half-finished.
 *   - Locals that must survive a suspension live in the coroutine's struct.
 *   - No `switch` between PT_BEGIN and PT_END (CLAUDE.md): every suspension
 *     point is a case label, and an inner switch swallows it.
 *   - No world state. A coroutine reads and writes its own struct and hands
 *     the world messages; the tick is the only thing that touches the world.
 */
#define MINIPT_ENABLE_USER_PTR 1
#include <3rd/minipt.h>

#include "torirs_server_pool.h"

#include <stdint.h>

struct ToriRSServerCore;
struct ToriRSServerCo;
struct ToriRSServerPoll;
struct ToriRSServerPollEvent;

/** One resume. Returns a PT_* value: PT_ENDED / PT_EXITED mean finished. */
typedef int (*ToriRSServerCoStep)(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co);

/** Frees a coroutine the core was told to drop while its job was in flight. */
typedef void (*ToriRSServerCoFree)(struct ToriRSServerCo* co);

struct ToriRSServerCo
{
    struct pt pt;
    ToriRSServerCoStep step;
    struct ToriRSServerCo* ready_next;
    /** The job in flight, or NULL. */
    struct ToriRSServerPoolJob* job;
    /** Dropped by its owner while `job` was in flight: freed through
     *  `free_fn` when the job comes back instead of being resumed. */
    ToriRSServerCoFree free_fn;
    uint8_t ready;
    uint8_t finished;
    uint8_t orphaned;
    /** `job` has come back through the pool's done list. Until it has, the
     *  job's memory (inside the owner) is still the pool's to hand back. */
    uint8_t job_collected;
    /** The last socket event that readied it (sticky until the next resume). */
    uint8_t readable;
    uint8_t writable;
    uint8_t closed;
};

/**
 * A finished job with no coroutine waiting for it (fire-and-forget: a logout
 * save). Runs on the loop thread when the job is collected.
 */
typedef void (*ToriRSServerJobFinish)(
    struct ToriRSServerCore* core,
    struct ToriRSServerPoolJob* job);

struct ToriRSServerCoreJob
{
    struct ToriRSServerPoolJob job; /* first: the pool hands back this pointer */
    ToriRSServerJobFinish finish;
};

struct ToriRSServerCore
{
    struct ToriRSServerPoll* poll;
    struct ToriRSServerPool* pool;
    struct ToriRSServerCo* ready_head;
    struct ToriRSServerCo* ready_tail;
};

/** `pool_threads` workers for blocking calls (0 = inline; see the pool). */
void
ToriRSServer_CoreInit(
    struct ToriRSServerCore* core,
    int pool_threads);

/** Waits out every job still in flight (resuming or finishing each, as a
 *  step would), then frees the pool and the poller. */
void
ToriRSServer_CoreShutdown(struct ToriRSServerCore* core);

void
ToriRSServer_CoInit(
    struct ToriRSServerCo* co,
    ToriRSServerCoStep step);

/** Put `co` on the ready queue (idempotent). */
void
ToriRSServer_CoreReady(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co);

/**
 * Take `co` off the ready queue for good. Returns 1 when the caller may free
 * it now; 0 when a job is still in flight, in which case the core frees it
 * through `free_fn` once the job comes back.
 */
int
ToriRSServer_CoreDrop(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co,
    ToriRSServerCoFree free_fn);

/** Hand `job` to the pool on behalf of `co`; `co` is readied when it is done. */
void
ToriRSServer_CoreSubmit(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co,
    struct ToriRSServerPoolJob* job);

/** Hand a fire-and-forget job to the pool; `finish` runs on the loop thread
 *  once it is done. */
void
ToriRSServer_CoreSubmitDetached(
    struct ToriRSServerCore* core,
    struct ToriRSServerCoreJob* job,
    ToriRSServerJobFinish finish);

/**
 * One pass of the loop's IO half: wait up to `timeout_ms` (0 when anything is
 * already ready), route socket events and finished jobs to their owners, then
 * resume every coroutine that was ready when the pass began. A coroutine
 * readied during the pass runs on the next one, so nothing can starve the
 * caller's tick.
 */
void
ToriRSServer_CoreStep(
    struct ToriRSServerCore* core,
    long timeout_ms);

/** Whether a resume is already queued (the caller's wait should not sleep). */
int
ToriRSServer_CoreHasReady(const struct ToriRSServerCore* core);

/**
 * Submit `job` and suspend until it is done. A suspension point: only between
 * PT_BEGIN and PT_END, and never inside a switch.
 */
#define TORIRSSERVER_CO_AWAIT_JOB(core, co, job_ptr)                                            \
    do                                                                                         \
    {                                                                                          \
        ToriRSServer_CoreSubmit((core), (co), (job_ptr));                                     \
        PT_WAIT_UNTIL(&(co)->pt, (job_ptr)->done);                                             \
        (co)->job = NULL;                                                                      \
    } while( 0 )

#endif
