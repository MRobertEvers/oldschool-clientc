/*
 * Triggers, counts and clocks: what a parked task waits on (task_trigger.h),
 * held by the task layer itself and checked here with no App.
 *
 * Pins:
 *
 *   1. PRIME, AWAIT, FIRE. A task parks on a primed trigger and is not
 *      stepped (counted in runs over many pumps); FIRE makes every
 *      subscriber ready -- one or many -- and runs none of them inline; their
 *      queue runs them at its next pass, and each reads how the wait ended.
 *   2. AN UNPRIMED TRIGGER. Awaiting one aborts, naming it and the task.
 *   3. THE OWNER ENDS. A task or a job that ends with a trigger still primed
 *      has it fired ABANDONED: a dropped owner (a queue freed, a job
 *      cancelled) quietly; one that ran to its end forgot to fire it, which
 *      asserts in a debug build. Every task's own `ended` trigger fires when
 *      it is freed, however it ends.
 *   4. PRIMED AGAIN. A second owner priming a primed trigger abandons the
 *      older prime: its subscribers wake ABANDONED and wait again. The same
 *      owner priming twice aborts.
 *   5. CANCEL. Cancelling a parked task through the pending index wakes it.
 *   6. LOOPS. A wait that closes a loop -- two tasks, or three, each waiting
 *      on the next one's end -- aborts when it is made, with the chain.
 *   7. TURNS. Tasks for one key apply in the order they were added, through
 *      the earlier tasks' `ended` triggers, whatever order their own waits
 *      end in.
 *   8. COUNTS. A task joined on several siblings parks on the count and is
 *      made ready by the decrement that takes it to zero, not before.
 *   9. CYCLES. Tasks parked on a clock wake in due order as it advances, and
 *      a scope cancel wakes them early.
 *  10. THE REBUILD. A view's built trigger, primed by a blocking rebuild's
 *      packet task (the owner is a task on the main queue, the build runs in
 *      it) or by a background job (the owner is the job, which ends at its
 *      commit or cancel): waiters on the parallel queue wake at the commit,
 *      or ABANDONED at a cancel.
 *  11. CHILDREN. A child run inline (TASK_AWAITEX) that parks parks its
 *      parent, and is told how the wait ended.
 *  12. THE CHAIN. What a parked task waits on prints down to what it bottoms
 *      out in: a read, a count, a cycle, a running owner, a job.
 *
 * The death checks run in a forked child and pass when it dies by SIGABRT,
 * which is how an assert is proved to fire without changing the code under
 * test. Built without NDEBUG (the Makefile target), so the asserts are live.
 *
 *   make -C src test-task-trigger
 */
#include "asyncio.h"
#include "engine/asset_requests.h"
#include "game/rs_parallel_task.h"
#include "game/rs_pending.h"
#include "task_runner.h"
#include "task_trigger.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_failures;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* platform_x_io.h keeps this opaque. A read of "never" stays on the wire. */
struct PlatformX_IO
{
    int process_calls;
};

void
PlatformX_IO_Pump(struct PlatformX_IO* px)
{
    (void)px;
}

int
PlatformX_IO_Process(struct PlatformX_IO* px, struct ToriRS_IOBatch* io)
{
    px->process_calls++;
    for( int i = 0; i < io->active_count; i++ )
        if( io->active[i]->kind == TORIRS_IOK_FILE_READ && strcmp(io->active[i]->u.file.path, "never") == 0 )
            io->active[i]->pending = 1;
    ToriRS_IOBatch_Reset(io);
    return 0;
}

static void
runner_init(
    struct TaskRunner* runner,
    struct PlatformX_IO* px,
    int role)
{
    memset(runner, 0, sizeof(*runner));
    runner->queue = ToriRS_TaskQueue_New();
    runner->io = ToriRS_IOBatch_New();
    runner->px = px;
    runner->role = role;
    runner->parallel = role != TASK_RUNNER_ROLE_MAIN;
}

static void
runner_free(struct TaskRunner* runner)
{
    ToriRS_TaskQueue_Free(runner->queue);
    ToriRS_IOBatch_Free(runner->io);
}

static enum TaskRunnerStat
pump(struct TaskRunner* runner)
{
    int steps = 0;
    return TaskRunner_PumpToStandstill(runner, 1000, &steps);
}

/* Runs `body` in a forked child: 1 when it died by SIGABRT. Its stderr goes
 * to `out` (up to out_size - 1 bytes) when `out` is given. */
static int
dies_by_abort_saying(
    void (*body)(void),
    char* out,
    size_t out_size)
{
    int fds[2];
    pid_t pid;
    int status = 0;

    fflush(stdout);
    fflush(stderr);
    assert(pipe(fds) == 0);
    pid = fork();
    assert(pid >= 0);
    if( pid == 0 )
    {
        close(fds[0]);
        dup2(fds[1], 2);
        body();
        _exit(0);
    }
    close(fds[1]);
    {
        /* Read to EOF either way: a child writing into a closed pipe dies by
         * SIGPIPE, not by the abort under test. */
        char sink[256];
        size_t len = 0;
        ssize_t n;
        for( ;; )
        {
            if( out && len + 1 < out_size )
                n = read(fds[0], out + len, out_size - 1 - len);
            else
                n = read(fds[0], sink, sizeof(sink));
            if( n <= 0 )
                break;
            if( out && len + 1 < out_size )
                len += (size_t)n;
        }
        if( out )
            out[len] = '\0';
    }
    close(fds[0]);
    (void)waitpid(pid, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static int
dies_by_abort(void (*body)(void))
{
    return dies_by_abort_saying(body, NULL, 0);
}

static void
task_free_libc(struct ToriRS_Task* task)
{
    free(task);
}

/* --- a task that waits on one trigger, `waits` times ------------------------ */

struct Sub
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_Trigger* trigger;
    /* Wait while the trigger is primed, or exactly once. */
    int while_primed;
    int runs;
    int woke;
    int done;
    int* order;
    int* order_len;
    int id;
    /* Where to say how the wait ended, for a Sub freed when it ends. */
    int* woke_out;
};

static int
Sub_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Sub* self = (struct Sub*)base;
    (void)io;
    self->runs++;
    PT_BEGIN(&self->pt);
    if( self->while_primed )
    {
        while( ToriRS_Trigger_Primed(self->trigger) )
            TASK_AWAIT_TRIGGER(&self->task, &self->pt, TORIRS_TASK_WAIT_TRIGGER, self->trigger);
    }
    else
        TASK_AWAIT_TRIGGER(&self->task, &self->pt, TORIRS_TASK_WAIT_TRIGGER, self->trigger);
    self->woke = self->task.woke;
    if( self->woke_out )
        *self->woke_out = self->task.woke;
    self->done = 1;
    if( self->order )
        self->order[(*self->order_len)++] = self->id;
    PT_END(&self->pt);
}

/* Shared by the tests, so a Sub outlives its queue's free in the checks
 * below: freed by hand at the end of each. */
static void
sub_free_keep(struct ToriRS_Task* task)
{
    (void)task;
}

static struct ToriRS_TaskVTable k_sub_vtable = { .run = Sub_Run, .free = sub_free_keep };

/* A Sub on the heap, freed by its queue when it ends. */
static void
sibling_free(struct ToriRS_Task* task)
{
    free(task);
}

static struct ToriRS_TaskVTable k_sibling_vtable = { .run = Sub_Run, .free = sibling_free };

static void
sub_init(
    struct Sub* sub,
    char const* name,
    struct ToriRS_Trigger* trigger)
{
    memset(sub, 0, sizeof(*sub));
    sub->task.vtable = &k_sub_vtable;
    snprintf(sub->task.name, sizeof(sub->task.name), "%s", name);
    sub->trigger = trigger;
    PT_INIT(&sub->pt);
}

/* --- 1. prime, await, fire -------------------------------------------------- */

static void
test_prime_await_fire(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct ToriRS_Trigger trigger = { 0 };
    struct ToriRS_TriggerOwner owner = { 0 };
    struct Sub subs[5];

    printf("TEST: a primed trigger parks its subscribers until it fires, and a fire runs none of them\n");
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_Trigger_Init(&trigger, "the test trigger");
    ToriRS_TriggerOwner_Init(&owner, TORIRS_OWNER_EXTERNAL, "the test owner");
    ToriRS_Trigger_Prime(&trigger, &owner);
    CHECK(ToriRS_Trigger_Primed(&trigger));

    /* One subscriber, then four more. */
    for( int i = 0; i < 5; i++ )
    {
        char name[16];
        snprintf(name, sizeof(name), "sub%d", i);
        sub_init(&subs[i], name, &trigger);
        ToriRS_TaskQueue_Add(runner.queue, &subs[i].task);
        if( i == 0 )
        {
            CHECK(pump(&runner) == TASK_RUNNER_WAITING);
            CHECK(subs[0].task.parked);
            CHECK(ToriRS_Trigger_Subscribers(&trigger) == 1);
        }
    }
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(ToriRS_Trigger_Subscribers(&trigger) == 5);

    /* A parked task costs a pump nothing: a hundred pumps run it 0 times. */
    {
        int const before = subs[0].runs + subs[4].runs;
        int const calls = px.process_calls;
        for( int i = 0; i < 100; i++ )
            CHECK(pump(&runner) == TASK_RUNNER_WAITING);
        CHECK(subs[0].runs + subs[4].runs == before);
        /* One pass a pump, each finding nothing to run. */
        CHECK(px.process_calls == calls + 100);
    }

    /* Fire: every subscriber ready, none run. */
    {
        uint32_t const wakes = ToriRS_TaskWakes();
        int runs = 0;
        for( int i = 0; i < 5; i++ )
            runs += subs[i].runs;
        ToriRS_Trigger_Fire(&trigger);
        CHECK(!ToriRS_Trigger_Primed(&trigger));
        CHECK(ToriRS_Trigger_Outcome(&trigger) == TORIRS_TRIGGER_FIRED);
        CHECK(ToriRS_TaskWakes() == wakes + 5);
        CHECK(TaskRunner_HasReady(&runner));
        for( int i = 0; i < 5; i++ )
        {
            CHECK(!subs[i].task.parked);
            runs -= subs[i].runs;
            CHECK(!subs[i].done);
        }
        CHECK(runs == 0);
    }
    CHECK(pump(&runner) == TASK_RUNNER_IDLE);
    for( int i = 0; i < 5; i++ )
    {
        CHECK(subs[i].done);
        CHECK(subs[i].woke == TORIRS_WAKE_FIRED);
    }
    runner_free(&runner);
}

/* --- 2. an unprimed trigger -------------------------------------------------- */

static void
await_unprimed(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct ToriRS_Trigger trigger = { 0 };
    struct Sub sub;

    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_Trigger_Init(&trigger, "a trigger nobody primed");
    sub_init(&sub, "lonely", &trigger);
    ToriRS_TaskQueue_Add(runner.queue, &sub.task);
    (void)pump(&runner);
}

static void
test_an_unprimed_trigger_cannot_be_awaited(void)
{
    char said[512];

    printf("TEST: awaiting a trigger nothing primed aborts, naming the trigger and the task\n");
    CHECK(dies_by_abort_saying(await_unprimed, said, sizeof(said)));
    CHECK(strstr(said, "a trigger nobody primed") != NULL);
    CHECK(strstr(said, "lonely") != NULL);
}

/* --- 3. the owner ends ------------------------------------------------------ */

/* Primes the test's trigger as itself, then waits on a trigger nothing
 * fires: it ends only when its queue is freed (a dropped owner). */
struct Primer
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_Trigger* trigger;
    struct ToriRS_Trigger* hold;
    /* 1: end at once, forgetting to fire it. */
    int forget;
};

static int
Primer_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Primer* self = (struct Primer*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    ToriRS_Trigger_Prime(self->trigger, ToriRS_TaskOwner(&self->task));
    if( !self->forget )
        TASK_AWAIT_TRIGGER(&self->task, &self->pt, TORIRS_TASK_WAIT_TRIGGER, self->hold);
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_primer_vtable = { .run = Primer_Run, .free = task_free_libc };

static struct Primer*
new_primer(
    struct ToriRS_Trigger* trigger,
    struct ToriRS_Trigger* hold,
    int forget)
{
    struct Primer* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_primer_vtable;
    strcpy(t->task.name, forget ? "forgetful" : "primer");
    t->trigger = trigger;
    t->hold = hold;
    t->forget = forget;
    PT_INIT(&t->pt);
    return t;
}

static void
owner_forgets(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct ToriRS_Trigger trigger = { 0 };

    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_Trigger_Init(&trigger, "the forgotten trigger");
    ToriRS_TaskQueue_Add(runner.queue, &new_primer(&trigger, NULL, 1)->task);
    (void)pump(&runner);
}

static void
job_forgets(void)
{
    struct ToriRS_TaskJob job = { 0 };
    struct ToriRS_Trigger trigger = { 0 };

    ToriRS_Trigger_Init(&trigger, "the job's trigger");
    ToriRS_Trigger_Prime(&trigger, ToriRS_TaskJobOwner(&job, "a job"));
    ToriRS_TaskJob_End(&job, 1);
}

static void
test_the_owner_ending_abandons(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct ToriRS_Trigger trigger = { 0 };
    struct ToriRS_Trigger hold = { 0 };
    struct ToriRS_TriggerOwner hold_owner = { 0 };
    struct ToriRS_TaskJob job = { 0 };
    struct Sub sub;
    struct Sub ender;
    struct Primer* primer;
    char said[512];

    printf("TEST: an owner that ends with a trigger primed abandons it; one that completed forgot, and "
           "asserts\n");
    /* A task owner dropped with its queue: abandoned, quietly. */
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_Trigger_Init(&trigger, "the primer's trigger");
    ToriRS_Trigger_Init(&hold, "hold");
    ToriRS_TriggerOwner_Init(&hold_owner, TORIRS_OWNER_EXTERNAL, "nobody");
    ToriRS_Trigger_Prime(&hold, &hold_owner);
    primer = new_primer(&trigger, &hold, 0);
    ToriRS_TaskQueue_Add(runner.queue, &primer->task);
    sub_init(&sub, "waits on the primer", &trigger);
    ToriRS_TaskQueue_Add(runner.queue, &sub.task);
    /* And one that waits for the primer to END. */
    sub_init(&ender, "waits for the end", ToriRS_TaskEnded(&primer->task));
    ToriRS_TaskQueue_Add(runner.queue, &ender.task);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(sub.task.parked);
    CHECK(ender.task.parked);
    /* The primer is taken off its queue without running to its end (a
     * cancel): its trigger is abandoned and its end fires. */
    ToriRS_TaskQueue_Remove(runner.queue, &primer->task);
    CHECK(!ToriRS_Trigger_Primed(&trigger));
    CHECK(ToriRS_Trigger_Outcome(&trigger) == TORIRS_TRIGGER_ABANDONED);
    CHECK(!sub.task.parked);
    CHECK(sub.task.woke == TORIRS_WAKE_ABANDONED);
    CHECK(!ender.task.parked);
    CHECK(ender.task.woke == TORIRS_WAKE_FIRED);
    CHECK(pump(&runner) == TASK_RUNNER_IDLE);
    CHECK(sub.done && ender.done);
    runner_free(&runner);
    ToriRS_TriggerOwner_End(&hold_owner, 0);

    /* A job cancelled: abandoned, quietly. */
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_Trigger_Init(&trigger, "the job's trigger");
    ToriRS_Trigger_Prime(&trigger, ToriRS_TaskJobOwner(&job, "a cancelled job"));
    sub_init(&sub, "waits on the job", &trigger);
    ToriRS_TaskQueue_Add(runner.queue, &sub.task);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    ToriRS_TaskJob_End(&job, 0);
    CHECK(sub.task.woke == TORIRS_WAKE_ABANDONED);
    CHECK(pump(&runner) == TASK_RUNNER_IDLE);
    runner_free(&runner);

    /* A task or a job that ran to its end without firing: a debug build stops
     * there, naming both. */
    CHECK(dies_by_abort_saying(owner_forgets, said, sizeof(said)));
    CHECK(strstr(said, "forgetful") != NULL);
    CHECK(strstr(said, "the forgotten trigger") != NULL);
    CHECK(dies_by_abort(job_forgets));

    /* A deallocator's trigger: retired with nobody priming it is fine;
     * retired while primed abandons. */
    memset(&trigger, 0, sizeof(trigger));
    ToriRS_Trigger_Retire(&trigger);
}

/* --- 4. primed again --------------------------------------------------------- */

static void
same_owner_twice(void)
{
    struct ToriRS_Trigger trigger = { 0 };
    struct ToriRS_TriggerOwner owner = { 0 };

    ToriRS_Trigger_Init(&trigger, "primed twice");
    ToriRS_TriggerOwner_Init(&owner, TORIRS_OWNER_EXTERNAL, "one owner");
    ToriRS_Trigger_Prime(&trigger, &owner);
    ToriRS_Trigger_Prime(&trigger, &owner);
}

static void
test_priming_again(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct ToriRS_Trigger trigger = { 0 };
    struct ToriRS_TriggerOwner first = { 0 };
    struct ToriRS_TriggerOwner second = { 0 };
    struct Sub sub;

    printf("TEST: a second owner's prime abandons the first (waiters wait again); the same owner twice "
           "aborts\n");
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_Trigger_Init(&trigger, "the rebuild");
    ToriRS_TriggerOwner_Init(&first, TORIRS_OWNER_EXTERNAL, "the first rebuild");
    ToriRS_TriggerOwner_Init(&second, TORIRS_OWNER_EXTERNAL, "the second rebuild");
    ToriRS_Trigger_Prime(&trigger, &first);
    sub_init(&sub, "waits it out", &trigger);
    sub.while_primed = 1;
    ToriRS_TaskQueue_Add(runner.queue, &sub.task);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    ToriRS_Trigger_Prime(&trigger, &second);
    CHECK(!sub.task.parked);
    CHECK(sub.task.woke == TORIRS_WAKE_ABANDONED);
    CHECK(trigger.owner == &second);
    CHECK(first.primed == NULL);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(sub.task.parked);
    CHECK(!sub.done);
    ToriRS_Trigger_Fire(&trigger);
    CHECK(pump(&runner) == TASK_RUNNER_IDLE);
    CHECK(sub.done);
    CHECK(sub.woke == TORIRS_WAKE_FIRED);
    runner_free(&runner);
    CHECK(dies_by_abort(same_owner_twice));
}

/* --- 5. cancel while parked -------------------------------------------------- */

enum
{
    KIND_A = RS_PENDING_KINDS_TEST_FIRST,
};

struct Turn
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct RS_Pending* index;
    struct Assets* assets;
    /* What it waits on before its turn (its own load, as it were). */
    struct ToriRS_Trigger own;
    struct ToriRS_TriggerOwner own_owner;
    int id;
    int* order;
    int* order_len;
    int cancelled_seen;
};

static int
Turn_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Turn* self = (struct Turn*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    /* 1. Its own wait. */
    while( !self->par.cancelled && ToriRS_Trigger_Primed(&self->own) )
        TASK_AWAIT_TRIGGER(&self->task, &self->pt, TORIRS_TASK_WAIT_ASSET, &self->own);
    /* Then its turn behind the earlier tasks for its key. */
    PT_PARALLEL_AWAIT_TURN(self->index);
    if( RS_ParallelTask_Cancelled(&self->par) )
    {
        self->cancelled_seen = 1;
        PT_EXIT(&self->pt);
    }
    self->order[(*self->order_len)++] = self->id;
    PT_END(&self->pt);
}

static void
Turn_Free(struct ToriRS_Task* base)
{
    struct Turn* self = (struct Turn*)base;
    RS_ParallelTask_Release(self->index, self->assets, &self->par);
    ToriRS_TriggerOwner_End(&self->own_owner, 0);
}

static struct ToriRS_TaskVTable k_turn_vtable = { .run = Turn_Run, .free = Turn_Free };

static void
turn_add(
    struct Turn* t,
    struct TaskRunner* runner,
    struct RS_Pending* index,
    struct Assets* assets,
    int id,
    int* order,
    int* order_len)
{
    memset(t, 0, sizeof(*t));
    t->task.vtable = &k_turn_vtable;
    snprintf(t->task.name, sizeof(t->task.name), "turn%d", id);
    t->index = index;
    t->assets = assets;
    t->id = id;
    t->order = order;
    t->order_len = order_len;
    ToriRS_Trigger_Init(&t->own, "its own load");
    ToriRS_TriggerOwner_Init(&t->own_owner, TORIRS_OWNER_EXTERNAL, "its loader");
    ToriRS_Trigger_Prime(&t->own, &t->own_owner);
    PT_INIT(&t->pt);
    RS_ParallelTask_Add(runner->queue, index, &t->task, &t->par, KIND_A, RS_PendingKey_Id(1, 0), -1, 0, 0);
}

static void
test_turns_and_cancel(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct RS_Pending* index = RS_Pending_New();
    struct Assets* assets = Assets_New();
    struct Turn turns[3];
    int order[3];
    int order_len = 0;

    printf("TEST: tasks for one key apply in the order added, through the earlier tasks' ends; a cancel "
           "wakes a parked task\n");
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    for( int i = 0; i < 3; i++ )
        turn_add(&turns[i], &runner, index, assets, i, order, &order_len);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    /* Their own waits end last-first. */
    ToriRS_Trigger_Fire(&turns[2].own);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    /* 2 waits its turn now: on 1's end. */
    CHECK(turns[2].task.parked);
    CHECK(turns[2].task.park.kind == TORIRS_TASK_WAIT_TURN);
    CHECK(turns[2].task.park.trigger == ToriRS_TaskEnded(&turns[1].task));
    CHECK(order_len == 0);
    ToriRS_Trigger_Fire(&turns[1].own);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(order_len == 0);
    ToriRS_Trigger_Fire(&turns[0].own);
    CHECK(pump(&runner) == TASK_RUNNER_IDLE);
    CHECK(order_len == 3);
    CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2);

    /* Cancel while parked: the cancel itself wakes it. */
    order_len = 0;
    turn_add(&turns[0], &runner, index, assets, 0, order, &order_len);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(turns[0].task.parked);
    CHECK(RS_Pending_CancelKey(index, KIND_A, RS_PendingKey_Id(1, 0)) == 1);
    CHECK(!turns[0].task.parked);
    CHECK(turns[0].task.woke == TORIRS_WAKE_CANCELLED);
    CHECK(pump(&runner) == TASK_RUNNER_IDLE);
    CHECK(turns[0].cancelled_seen);
    CHECK(order_len == 0);
    runner_free(&runner);
    CHECK(RS_Pending_Listed(index) == 0);
    RS_Pending_Free(index);
    Assets_Free(assets);
}

/* --- 6. loops --------------------------------------------------------------- */

/* Waits on the end of `other` (a join on one task). */
struct EndWaiter
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_Task* other;
};

static int
EndWaiter_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct EndWaiter* self = (struct EndWaiter*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    TASK_AWAIT_TRIGGER(&self->task, &self->pt, TORIRS_TASK_WAIT_TURN, ToriRS_TaskEnded(self->other));
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_end_waiter_vtable = { .run = EndWaiter_Run, .free = task_free_libc };

static void
loop_of(int n)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct EndWaiter* t[3];

    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    for( int i = 0; i < n; i++ )
    {
        t[i] = calloc(1, sizeof(*t[i]));
        assert(t[i]);
        t[i]->task.vtable = &k_end_waiter_vtable;
        snprintf(t[i]->task.name, sizeof(t[i]->task.name), "looper%c", 'A' + i);
        PT_INIT(&t[i]->pt);
    }
    for( int i = 0; i < n; i++ )
        t[i]->other = &t[(i + 1) % n]->task;
    for( int i = 0; i < n; i++ )
        ToriRS_TaskQueue_Add(runner.queue, &t[i]->task);
    (void)pump(&runner);
}

static void
loop_of_two(void)
{
    loop_of(2);
}

static void
loop_of_three(void)
{
    loop_of(3);
}

static void
test_loops_assert_with_the_chain(void)
{
    char said[1024];

    printf("TEST: a wait that closes a loop of two, or of three, aborts when it is made, printing the "
           "chain\n");
    CHECK(dies_by_abort_saying(loop_of_two, said, sizeof(said)));
    CHECK(strstr(said, "closes a loop") != NULL);
    CHECK(strstr(said, "looperA") != NULL);
    CHECK(strstr(said, "looperB") != NULL);
    CHECK(dies_by_abort_saying(loop_of_three, said, sizeof(said)));
    CHECK(strstr(said, "closes a loop") != NULL);
    CHECK(strstr(said, "looperA") != NULL);
    CHECK(strstr(said, "looperB") != NULL);
    CHECK(strstr(said, "looperC") != NULL);
    /* A chain that does not close is a wait like any other. */
    {
        struct TaskRunner runner;
        struct PlatformX_IO px = { 0 };
        struct EndWaiter* a = calloc(1, sizeof(*a));
        struct EndWaiter* b = calloc(1, sizeof(*b));
        struct Sub c;
        struct ToriRS_Trigger gate = { 0 };
        struct ToriRS_TriggerOwner gate_owner = { 0 };

        assert(a && b);
        runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
        ToriRS_Trigger_Init(&gate, "the gate");
        ToriRS_TriggerOwner_Init(&gate_owner, TORIRS_OWNER_EXTERNAL, "the outside");
        ToriRS_Trigger_Prime(&gate, &gate_owner);
        a->task.vtable = b->task.vtable = &k_end_waiter_vtable;
        strcpy(a->task.name, "chainA");
        strcpy(b->task.name, "chainB");
        PT_INIT(&a->pt);
        PT_INIT(&b->pt);
        sub_init(&c, "chainC", &gate);
        a->other = &b->task;
        b->other = &c.task;
        ToriRS_TaskQueue_Add(runner.queue, &a->task);
        ToriRS_TaskQueue_Add(runner.queue, &b->task);
        ToriRS_TaskQueue_Add(runner.queue, &c.task);
        CHECK(pump(&runner) == TASK_RUNNER_WAITING);
        /* 12. The chain, down to the outside owner it bottoms out in. */
        CHECK(ToriRS_TaskChainFormat(&a->task, said, sizeof(said)) == TORIRS_CHAIN_EXTERNAL);
        CHECK(strstr(said, "chainA") && strstr(said, "chainB") && strstr(said, "chainC") &&
              strstr(said, "the outside"));
        ToriRS_Trigger_Fire(&gate);
        CHECK(pump(&runner) == TASK_RUNNER_IDLE);
        runner_free(&runner);
    }
}

/* --- 8. counts ---------------------------------------------------------------- */

struct Joins
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_TaskQueue* queue;
    struct ToriRS_Trigger* gates;
    int pending;
    int runs;
    int done;
};

static int
Joins_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Joins* self = (struct Joins*)base;
    (void)io;
    self->runs++;
    PT_BEGIN(&self->pt);
    for( int i = 0; i < 4; i++ )
    {
        struct Sub* s = calloc(1, sizeof(*s));
        assert(s);
        sub_init(s, "sibling", &self->gates[i]);
        s->task.vtable = &k_sibling_vtable;
        ToriRS_TaskQueue_AddParallelPoolSubTask(self->queue, &s->task, &self->pending);
    }
    PT_TASK_JOIN(pending);
    self->done = 1;
    PT_END(&self->pt);
}

static void
joins_free(struct ToriRS_Task* task)
{
    (void)task;
}

static struct ToriRS_TaskVTable k_joins_vtable = { .run = Joins_Run, .free = joins_free };


static void
test_a_count_join(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct ToriRS_Trigger gates[4];
    struct ToriRS_TriggerOwner owner = { 0 };
    struct Joins joins;

    printf("TEST: a join over several siblings parks on the count and is made ready by the decrement "
           "that takes it to zero\n");
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_TriggerOwner_Init(&owner, TORIRS_OWNER_EXTERNAL, "the siblings' loads");
    memset(gates, 0, sizeof(gates));
    for( int i = 0; i < 4; i++ )
    {
        ToriRS_Trigger_Init(&gates[i], "a sibling's load");
        ToriRS_Trigger_Prime(&gates[i], &owner);
    }
    memset(&joins, 0, sizeof(joins));
    joins.task.vtable = &k_joins_vtable;
    strcpy(joins.task.name, "joins");
    joins.queue = runner.queue;
    joins.gates = gates;
    PT_INIT(&joins.pt);
    ToriRS_TaskQueue_Add(runner.queue, &joins.task);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(joins.pending == 4);
    CHECK(joins.task.parked);
    CHECK(joins.task.park.on == TORIRS_PARK_COUNT);
    CHECK(ToriRS_CountParked() >= 1);
    /* The siblings end one by one; the joiner is not made ready, and not run,
     * until the last of them has. */
    {
        int const runs = joins.runs;

        for( int i = 0; i < 4; i++ )
        {
            ToriRS_Trigger_Fire(&gates[i]);
            CHECK(pump(&runner) == (i < 3 ? TASK_RUNNER_WAITING : TASK_RUNNER_IDLE));
            CHECK(joins.pending == 3 - i);
            if( i < 3 )
            {
                CHECK(joins.task.parked);
                CHECK(joins.runs == runs);
            }
        }
        CHECK(joins.task.woke == TORIRS_WAKE_COUNT);
        CHECK(joins.runs == runs + 1);
    }
    CHECK(joins.done);
    runner_free(&runner);
    ToriRS_TriggerOwner_End(&owner, 0);
}

/* --- 9. cycles ----------------------------------------------------------------- */

struct Sleeper
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct ToriRS_Clock* clock;
    struct RS_Pending* index;
    struct Assets* assets;
    uint32_t due;
    int id;
    int* order;
    int* order_len;
    int cancelled_seen;
};

static int
Sleeper_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Sleeper* self = (struct Sleeper*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    PT_PARALLEL_AWAIT_CYCLE(self->clock, self->due);
    if( RS_ParallelTask_Cancelled(&self->par) )
    {
        self->cancelled_seen = 1;
        PT_EXIT(&self->pt);
    }
    self->order[(*self->order_len)++] = self->id;
    PT_END(&self->pt);
}

static void
Sleeper_Free(struct ToriRS_Task* base)
{
    struct Sleeper* self = (struct Sleeper*)base;
    RS_ParallelTask_Release(self->index, self->assets, &self->par);
}

static struct ToriRS_TaskVTable k_sleeper_vtable = { .run = Sleeper_Run, .free = Sleeper_Free };

static void
test_cycle_waits(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct RS_Pending* index = RS_Pending_New();
    struct Assets* assets = Assets_New();
    struct ToriRS_Clock clock = { 0 };
    struct Sleeper sleepers[4];
    /* Added out of due order; two share a cycle. */
    uint32_t const due[4] = { 30, 10, 20, 20 };
    int order[4];
    int order_len = 0;

    printf("TEST: cycle waits wake in due order as the clock advances, and on a scope cancel\n");
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_Clock_Init(&clock, "the test cycle", 0);
    for( int i = 0; i < 4; i++ )
    {
        struct Sleeper* s = &sleepers[i];
        memset(s, 0, sizeof(*s));
        s->task.vtable = &k_sleeper_vtable;
        snprintf(s->task.name, sizeof(s->task.name), "sleeper%d", i);
        s->clock = &clock;
        s->index = index;
        s->assets = assets;
        s->due = due[i];
        s->id = i;
        s->order = order;
        s->order_len = &order_len;
        PT_INIT(&s->pt);
        RS_ParallelTask_Add(runner.queue, index, &s->task, &s->par, KIND_A, RS_PendingKey_Id(i, 0), 7, 0, 0);
    }
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(ToriRS_Clock_Parked(&clock) == 4);
    /* The list is in due order: 10, 20 (added first), 20, 30. */
    CHECK(clock.head == &sleepers[1].task);
    CHECK(clock.head->park_next == &sleepers[2].task);
    CHECK(clock.head->park_next->park_next == &sleepers[3].task);
    ToriRS_Clock_Advance(&clock, 9);
    CHECK(ToriRS_Clock_Parked(&clock) == 4);
    ToriRS_Clock_Advance(&clock, 20);
    CHECK(ToriRS_Clock_Parked(&clock) == 1);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(order_len == 3);
    CHECK(order[0] == 1 && order[1] == 2 && order[2] == 3);
    /* A scope cancel (every task of view 7) wakes the last one early. */
    CHECK(RS_Pending_Cancel(index, RS_PendingScope_View(7)) == 1);
    CHECK(ToriRS_Clock_Parked(&clock) == 0);
    CHECK(pump(&runner) == TASK_RUNNER_IDLE);
    CHECK(sleepers[0].cancelled_seen);
    CHECK(order_len == 3);
    runner_free(&runner);
    RS_Pending_Free(index);
    Assets_Free(assets);
}

/* --- 10. the rebuild trigger, both modes ------------------------------------- */

/* A blocking rebuild: a task on the main queue primes the view's trigger as
 * itself, gathers (parked on a count of loaders on the parallel queue), then
 * builds and fires. */
struct BlockingRebuild
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_Trigger* view_built;
    struct ToriRS_TaskQueue* parallel;
    struct ToriRS_Trigger* loads;
    int pending;
    int* built;
};

static int
BlockingRebuild_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct BlockingRebuild* self = (struct BlockingRebuild*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    ToriRS_Trigger_Prime(self->view_built, ToriRS_TaskOwner(&self->task));
    for( int i = 0; i < 3; i++ )
    {
        struct Sub* s = calloc(1, sizeof(*s));
        assert(s);
        sub_init(s, "map square", self->loads);
        s->task.vtable = &k_sibling_vtable;
        ToriRS_TaskQueue_AddParallelPoolSubTask(self->parallel, &s->task, &self->pending);
    }
    PT_TASK_JOIN(pending);
    *self->built = 1;
    ToriRS_Trigger_Fire(self->view_built);
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_blocking_rebuild_vtable = { .run = BlockingRebuild_Run, .free = task_free_libc };

/* A parallel task waiting out the view's rebuild, as the ground and loc
 * tasks do. */
struct ViewWaiter
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct ToriRS_Trigger* view_built;
    int* built;
    struct RS_Pending* index;
    struct Assets* assets;
    int applied;
    int woke;
};

static int
ViewWaiter_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct ViewWaiter* self = (struct ViewWaiter*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    PT_PARALLEL_AWAIT_REBUILD(self->view_built, *self->built != 0);
    self->woke = self->task.woke;
    self->applied = 1;
    PT_END(&self->pt);
}

static void
ViewWaiter_Free(struct ToriRS_Task* base)
{
    struct ViewWaiter* self = (struct ViewWaiter*)base;
    RS_ParallelTask_Release(self->index, self->assets, &self->par);
}

static struct ToriRS_TaskVTable k_view_waiter_vtable = { .run = ViewWaiter_Run, .free = ViewWaiter_Free };

static void
view_waiter_add(
    struct ViewWaiter* w,
    struct TaskRunner* parallel,
    struct RS_Pending* index,
    struct Assets* assets,
    struct ToriRS_Trigger* view_built,
    int* built)
{
    memset(w, 0, sizeof(*w));
    w->task.vtable = &k_view_waiter_vtable;
    strcpy(w->task.name, "ground model");
    w->view_built = view_built;
    w->built = built;
    w->index = index;
    w->assets = assets;
    PT_INIT(&w->pt);
    RS_ParallelTask_Add(parallel->queue, index, &w->task, &w->par, KIND_A, RS_PendingKey_Id(9, 0), 0, 0, 0);
}

static void
test_the_rebuild_trigger(void)
{
    struct TaskRunner main_q;
    struct TaskRunner parallel;
    struct TaskRunner background;
    struct PlatformX_IO px = { 0 };
    struct RS_Pending* index = RS_Pending_New();
    struct Assets* assets = Assets_New();
    struct ToriRS_Trigger view_built = { 0 };
    struct ToriRS_Trigger loads = { 0 };
    struct ToriRS_TriggerOwner loader = { 0 };
    struct ViewWaiter w;
    int built = 1;

    printf("TEST: the view's built trigger, primed by a blocking rebuild's packet task and by a "
           "background job, wakes its waiters at the commit or ABANDONED at a cancel\n");
    runner_init(&main_q, &px, TASK_RUNNER_ROLE_MAIN);
    runner_init(&parallel, &px, TASK_RUNNER_ROLE_PARALLEL);
    runner_init(&background, &px, TASK_RUNNER_ROLE_BACKGROUND);
    ToriRS_Trigger_Init(&view_built, "view 0 built");
    ToriRS_Trigger_Init(&loads, "the map squares' reads");
    ToriRS_TriggerOwner_Init(&loader, TORIRS_OWNER_EXTERNAL, "the disk");

    /* Blocking: the packet task primes, gathers on the parallel queue, builds
     * in place and fires. The waiter, added while it is under way, parks on
     * the trigger with the old scene still built. */
    {
        struct BlockingRebuild* r = calloc(1, sizeof(*r));
        assert(r);
        r->task.vtable = &k_blocking_rebuild_vtable;
        strcpy(r->task.name, "GameProtoExec");
        r->view_built = &view_built;
        r->parallel = parallel.queue;
        r->loads = &loads;
        r->built = &built;
        PT_INIT(&r->pt);
        ToriRS_Trigger_Prime(&loads, &loader);
        TaskRunner_AddRenderBlockingSerialTask(&main_q, &r->task);
        CHECK(TaskRunner_SettleFrame(&main_q) == TASK_RUNNER_WAITING);
        CHECK(TaskRunner_HeadParked(&main_q));
        built = 0;
        view_waiter_add(&w, &parallel, index, assets, &view_built, &built);
        CHECK(pump(&parallel) == TASK_RUNNER_WAITING);
        CHECK(w.task.parked);
        CHECK(w.task.park.kind == TORIRS_TASK_WAIT_REBUILD);
        /* The chain runs from the waiter through the packet task to the
         * count it is joined on. */
        {
            char chain[512];
            CHECK(ToriRS_TaskChainFormat(&w.task, chain, sizeof(chain)) == TORIRS_CHAIN_COUNT);
            CHECK(strstr(chain, "GameProtoExec") != NULL);
        }
        /* The loads land; the parallel queue ends the loaders, the last one
         * readies the packet task, which builds and fires. */
        ToriRS_Trigger_Fire(&loads);
        CHECK(pump(&parallel) == TASK_RUNNER_WAITING);
        CHECK(w.task.parked);
        CHECK(!TaskRunner_HeadParked(&main_q));
        CHECK(TaskRunner_SettleFrame(&main_q) == TASK_RUNNER_IDLE);
        CHECK(built == 1);
        CHECK(!w.task.parked);
        CHECK(pump(&parallel) == TASK_RUNNER_IDLE);
        CHECK(w.applied);
        CHECK(w.woke == TORIRS_WAKE_FIRED);
    }

    /* Background: the job primes at its start and its owner commits after
     * the gather -- the fire, then the job's end. */
    {
        struct ToriRS_TaskJob job = { 0 };

        ToriRS_Trigger_Prime(&view_built, ToriRS_TaskJobOwner(&job, "the world job"));
        view_waiter_add(&w, &parallel, index, assets, &view_built, &built);
        CHECK(pump(&parallel) == TASK_RUNNER_WAITING);
        CHECK(w.task.parked);
        {
            char chain[512];
            CHECK(ToriRS_TaskChainFormat(&w.task, chain, sizeof(chain)) == TORIRS_CHAIN_JOB);
            CHECK(strstr(chain, "the world job") != NULL);
        }
        /* The commit. */
        ToriRS_Trigger_Fire(&view_built);
        ToriRS_TaskJob_End(&job, 1);
        CHECK(pump(&parallel) == TASK_RUNNER_IDLE);
        CHECK(w.applied);

        /* A cancelled job: its end abandons, the waiter goes on against the
         * old scene. */
        ToriRS_Trigger_Prime(&view_built, ToriRS_TaskJobOwner(&job, "the world job"));
        view_waiter_add(&w, &parallel, index, assets, &view_built, &built);
        CHECK(pump(&parallel) == TASK_RUNNER_WAITING);
        ToriRS_TaskJob_End(&job, 0);
        CHECK(pump(&parallel) == TASK_RUNNER_IDLE);
        CHECK(w.applied);
        CHECK(w.woke == TORIRS_WAKE_ABANDONED);
    }

    runner_free(&main_q);
    runner_free(&parallel);
    runner_free(&background);
    ToriRS_TriggerOwner_End(&loader, 0);
    CHECK(RS_Pending_Listed(index) == 0);
    RS_Pending_Free(index);
    Assets_Free(assets);
}

/* --- 11. a child's park is its parent's ------------------------------------- */

struct Parent
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_Trigger* trigger;
    int* done_out;
    int* child_woke_out;
};

static int
Parent_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Parent* self = (struct Parent*)base;
    PT_BEGIN(&self->pt);
    {
        struct Sub* child = calloc(1, sizeof(*child));
        assert(child);
        sub_init(child, "child", self->trigger);
        child->task.vtable = &k_sibling_vtable;
        child->woke_out = self->child_woke_out;
        self->pt.user = &child->task;
    }
    PT_TASK_AWAITSELF(self->pt.user);
    *self->done_out = 1;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_parent_vtable = { .run = Parent_Run, .free = task_free_libc };

static void
test_a_childs_park_is_the_parents(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct ToriRS_Trigger trigger = { 0 };
    struct ToriRS_TriggerOwner owner = { 0 };
    struct Parent* parent = calloc(1, sizeof(*parent));
    int done = 0;
    int child_woke = -1;

    printf("TEST: a child run inline that parks parks its parent, and learns how the wait ended\n");
    assert(parent);
    runner_init(&runner, &px, TASK_RUNNER_ROLE_MAIN);
    ToriRS_Trigger_Init(&trigger, "what the child waits on");
    ToriRS_TriggerOwner_Init(&owner, TORIRS_OWNER_EXTERNAL, "the outside");
    ToriRS_Trigger_Prime(&trigger, &owner);
    parent->task.vtable = &k_parent_vtable;
    strcpy(parent->task.name, "parent");
    parent->trigger = &trigger;
    parent->done_out = &done;
    parent->child_woke_out = &child_woke;
    PT_INIT(&parent->pt);
    ToriRS_TaskQueue_Add(runner.queue, &parent->task);
    CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_PROGRESSED);
    CHECK(parent->task.parked);
    CHECK(ToriRS_Trigger_Subscribers(&trigger) == 1);
    CHECK(trigger.subs == &parent->task);
    CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_WAITING);
    ToriRS_TriggerOwner_End(&owner, 0);
    CHECK(parent->task.woke == TORIRS_WAKE_ABANDONED);
    /* The parent is freed when it ends: read through what it set. */
    CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
    CHECK(done == 1);
    CHECK(child_woke == TORIRS_WAKE_ABANDONED);
    CHECK(runner.queue->head == NULL);
    runner_free(&runner);
}

/* --- 12. the chain bottoms out ----------------------------------------------- */

struct Reader
{
    struct ToriRS_Task task;
    struct pt pt;
};

static int
Reader_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Reader* self = (struct Reader*)base;
    PT_BEGIN(&self->pt);
    ToriRS_IO_QueueFileRead(io, 0, "never");
    PT_YIELD(&self->pt);
    PT_END(&self->pt);
}

static void
reader_free(struct ToriRS_Task* task)
{
    task->io.pending = 0;
    task->io.kind = TORIRS_IOK_NONE;
    free(task);
}

static struct ToriRS_TaskVTable k_reader_vtable = { .run = Reader_Run, .free = reader_free };

static void
test_the_chain_bottoms_out_in_a_read(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct Reader* reader = calloc(1, sizeof(*reader));
    struct EndWaiter* waiter = calloc(1, sizeof(*waiter));
    char chain[512];

    printf("TEST: the chain of a task waiting on a reader's end bottoms out in the read\n");
    assert(reader && waiter);
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    reader->task.vtable = &k_reader_vtable;
    strcpy(reader->task.name, "reader");
    PT_INIT(&reader->pt);
    waiter->task.vtable = &k_end_waiter_vtable;
    strcpy(waiter->task.name, "after the reader");
    waiter->other = &reader->task;
    PT_INIT(&waiter->pt);
    ToriRS_TaskQueue_Add(runner.queue, &reader->task);
    ToriRS_TaskQueue_Add(runner.queue, &waiter->task);
    CHECK(pump(&runner) == TASK_RUNNER_WAITING);
    CHECK(ToriRS_TaskChainFormat(&waiter->task, chain, sizeof(chain)) == TORIRS_CHAIN_READ);
    CHECK(strstr(chain, "after the reader") && strstr(chain, "reader (reading)"));
    CHECK(TaskRunner_PrintParked(&runner, stdout) == 2);
    runner_free(&runner);
}

/* --- 12b. a long chain is a chain, not a loop --------------------------------- */

/*
 * `n` tasks in a line behind one reader, each waiting on the end of the one
 * before it (the turn behind an earlier task, n deep): every wait is on a
 * primed trigger whose owner is alive, and the line bottoms out in a read.
 * Nothing about its length makes it a loop. Returns the chain's end for the
 * last task; the tasks are left on `runner`.
 */
static int
long_chain(
    struct TaskRunner* runner,
    int n,
    char* chain,
    size_t chain_size)
{
    struct Reader* reader = calloc(1, sizeof(*reader));
    struct ToriRS_Task* prev;
    struct ToriRS_Task* last = NULL;

    assert(reader);
    reader->task.vtable = &k_reader_vtable;
    strcpy(reader->task.name, "reader");
    PT_INIT(&reader->pt);
    ToriRS_TaskQueue_Add(runner->queue, &reader->task);
    prev = &reader->task;
    for( int i = 0; i < n; i++ )
    {
        struct EndWaiter* w = calloc(1, sizeof(*w));

        assert(w);
        w->task.vtable = &k_end_waiter_vtable;
        snprintf(w->task.name, sizeof(w->task.name), "turn%d", i);
        w->other = prev;
        PT_INIT(&w->pt);
        ToriRS_TaskQueue_Add(runner->queue, &w->task);
        prev = &w->task;
        last = &w->task;
    }
    CHECK(pump(runner) == TASK_RUNNER_WAITING);
    CHECK(last->parked);
    return ToriRS_TaskChainFormat(last, chain, chain_size);
}

/* Parks 5000 tasks in one line: each wait walks the owners behind it. */
static void
park_a_line_of_5000(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    char chain[256];

    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    (void)long_chain(&runner, 5000, chain, sizeof(chain));
    _exit(0);
}

static void
test_a_long_chain_is_not_a_loop(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    char chain[512];
    char said[1024];

    printf("TEST: a line of 100 turns behind a read bottoms out in the read; 5000 park without a loop\n");
    runner_init(&runner, &px, TASK_RUNNER_ROLE_PARALLEL);
    /* What the strict audit (TaskRunner_PumpEnd) reads: UNSOUND here aborts
     * a TORIRS_QUEUE_CONTRACT_STRICT run over a state that is legitimate. */
    CHECK(long_chain(&runner, 100, chain, sizeof(chain)) == TORIRS_CHAIN_READ);
    CHECK(strstr(chain, "turn99") != NULL);
    /* The text is bounded by its buffer, not by refusing to look further. */
    CHECK(strlen(chain) < sizeof(chain));
    runner_free(&runner);
    /* A wait is refused as a loop only when it closes one. */
    CHECK(!dies_by_abort_saying(park_a_line_of_5000, said, sizeof(said)));
    CHECK(strstr(said, "closes a loop") == NULL);
    if( said[0] )
        fprintf(stderr, "  the line of 5000 said: %.200s\n", said);
}

/* --- 13. a task woken behind the walk still gets its turn -------------------- */

struct Clocked
{
    uint64_t now_us;
};

static uint64_t
clocked_now(void* ud)
{
    return ((struct Clocked*)ud)->now_us;
}

/* A sibling that spends the slice: it moves the clock past the deadline. */
struct Spender
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Clocked* clock;
    struct ToriRS_Trigger* gate;
};

static int
Spender_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Spender* self = (struct Spender*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    /* Its load first (a pump later), then the work that spends the slice. */
    while( ToriRS_Trigger_Primed(self->gate) )
        TASK_AWAIT_TRIGGER(&self->task, &self->pt, TORIRS_TASK_WAIT_ASSET, self->gate);
    self->clock->now_us += 5000;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_spender_vtable = { .run = Spender_Run, .free = task_free_libc };

/* Fans one spender out behind itself and joins it. */
struct Fanner
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_TaskQueue* queue;
    struct Clocked* clock;
    struct ToriRS_Trigger* gate;
    int pending;
    int* done;
};

static int
Fanner_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Fanner* self = (struct Fanner*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    {
        struct Spender* sp = calloc(1, sizeof(*sp));
        assert(sp);
        sp->task.vtable = &k_spender_vtable;
        strcpy(sp->task.name, "spender");
        sp->clock = self->clock;
        sp->gate = self->gate;
        PT_INIT(&sp->pt);
        ToriRS_TaskQueue_AddParallelPoolSubTask(self->queue, &sp->task, &self->pending);
    }
    PT_TASK_JOIN(pending);
    *self->done = 1;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_fanner_vtable = { .run = Fanner_Run, .free = task_free_libc };

static void
test_a_task_woken_behind_the_walk_gets_its_turn(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct Clocked clock = { 0 };
    struct Fanner* f = calloc(1, sizeof(*f));
    struct ToriRS_Trigger gate = { 0 };
    struct ToriRS_TriggerOwner loader = { 0 };
    int done = 0;

    printf("TEST: a task a wake readies after the walk passed it gets its turn in that pump, the slice "
           "notwithstanding\n");
    assert(f);
    runner_init(&runner, &px, TASK_RUNNER_ROLE_BACKGROUND);
    TaskRunner_SetClock(&runner, clocked_now, &clock);
    f->task.vtable = &k_fanner_vtable;
    strcpy(f->task.name, "fanner");
    f->queue = runner.queue;
    f->clock = &clock;
    f->done = &done;
    f->gate = &gate;
    PT_INIT(&f->pt);
    ToriRS_Trigger_Init(&gate, "the spender's load");
    ToriRS_TriggerOwner_Init(&loader, TORIRS_OWNER_EXTERNAL, "the disk");
    ToriRS_Trigger_Prime(&gate, &loader);
    ToriRS_TaskQueue_Add(runner.queue, &f->task);
    /* Pump 1: the fanner fans out and parks on its join; the spender parks
     * on its load. */
    CHECK(TaskRunner_RunUntil(&runner, clock.now_us + 1000) == TASK_RUNNER_WAITING);
    CHECK(f->task.parked);
    /* Pump 2: the load lands; the spender runs, spends the slice and ends,
     * which readies the fanner after the walk has passed it. The pump owes
     * it its turn (PumpEnd asserts otherwise), and it ends in this pump. */
    ToriRS_Trigger_Fire(&gate);
    (void)TaskRunner_RunUntil(&runner, clock.now_us + 1000);
    CHECK(done == 1);
    CHECK(runner.queue->head == NULL);
    runner_free(&runner);
}

int
main(void)
{
    test_prime_await_fire();
    test_an_unprimed_trigger_cannot_be_awaited();
    test_the_owner_ending_abandons();
    test_priming_again();
    test_turns_and_cancel();
    test_loops_assert_with_the_chain();
    test_a_count_join();
    test_cycle_waits();
    test_the_rebuild_trigger();
    test_a_childs_park_is_the_parents();
    test_the_chain_bottoms_out_in_a_read();
    test_a_long_chain_is_not_a_loop();
    test_a_task_woken_behind_the_walk_gets_its_turn();
    if( g_failures )
    {
        printf("task_trigger_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("task_trigger_test: all passed\n");
    return 0;
}
