/*
 * Triggers, counts and clocks: what a parked task waits on (task_trigger.h).
 *
 * Every list is intrusive -- a trigger's subscribers, a clock's sleepers and
 * a count's waiters all run through the task's own park_prev / park_next --
 * so parking allocates nothing. The one table here is the count waiters':
 * a count is a plain int in its owner's struct, so the tasks parked on one
 * are found by its address, hashed.
 */
#include "task_trigger.h"

#include "asyncio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT_BUCKETS 256

static struct ToriRS_Task* g_count_bucket[COUNT_BUCKETS];
static int g_count_parked;
static uint32_t g_wakes;

char const*
ToriRS_TaskWaitName(int wait)
{
    if( wait == TORIRS_TASK_WAIT_ASSET )
        return "asset";
    if( wait == TORIRS_TASK_WAIT_JOIN )
        return "join";
    if( wait == TORIRS_TASK_WAIT_TURN )
        return "turn";
    if( wait == TORIRS_TASK_WAIT_REBUILD )
        return "rebuild";
    if( wait == TORIRS_TASK_WAIT_CYCLE )
        return "cycle";
    if( wait == TORIRS_TASK_WAIT_TRIGGER )
        return "trigger";
    return "none";
}

void
ToriRS_TaskFatal(char const* line)
{
    assert(line);
    fprintf(stderr, "task_trigger: %s\n", line);
    fflush(stderr);
    abort();
}

static char const*
task_name(struct ToriRS_Task const* task)
{
    return task->name[0] ? task->name : "(unnamed)";
}

static char const*
trigger_name(struct ToriRS_Trigger const* trigger)
{
    return trigger->name ? trigger->name : "(unnamed trigger)";
}

/* ---- owners ------------------------------------------------------------- */

void
ToriRS_TriggerOwner_Init(
    struct ToriRS_TriggerOwner* owner,
    int kind,
    char const* name)
{
    assert(owner);
    assert(kind == TORIRS_OWNER_JOB || kind == TORIRS_OWNER_EXTERNAL);
    assert(name);
    /* Re-made only between lives: one that still holds a prime would lose
     * it here. */
    assert(!owner->primed);
    owner->kind = kind;
    owner->name = name;
    owner->task = NULL;
}

char const*
ToriRS_TriggerOwner_Name(struct ToriRS_TriggerOwner const* owner)
{
    assert(owner);
    if( owner->kind == TORIRS_OWNER_TASK )
        return task_name(owner->task);
    return owner->name ? owner->name : "(unnamed owner)";
}

static void
owner_unlink(struct ToriRS_Trigger* trigger)
{
    struct ToriRS_TriggerOwner* owner = trigger->owner;

    assert(owner);
    if( trigger->owner_prev )
        trigger->owner_prev->owner_next = trigger->owner_next;
    else
    {
        assert(owner->primed == trigger);
        owner->primed = trigger->owner_next;
    }
    if( trigger->owner_next )
        trigger->owner_next->owner_prev = trigger->owner_prev;
    trigger->owner_prev = NULL;
    trigger->owner_next = NULL;
    trigger->owner = NULL;
}

/* Unprime `trigger` with `outcome` and wake every subscriber with `why`. */
static void
trigger_end(
    struct ToriRS_Trigger* trigger,
    int outcome,
    int why)
{
    owner_unlink(trigger);
    trigger->outcome = outcome;
    trigger->ends++;
    while( trigger->subs )
        ToriRS_TaskWake(trigger->subs, why);
}

void
ToriRS_TriggerOwner_End(
    struct ToriRS_TriggerOwner* owner,
    int completed)
{
    assert(owner);
    while( owner->primed )
    {
        struct ToriRS_Trigger* trigger = owner->primed;

        if( completed )
        {
            /* A debug build stops at the owner that forgot; any other build
             * says so once and abandons, which is what its waiters need. */
            fprintf(
                stderr,
                "task_trigger: %s ran to its end with trigger '%s' still primed (it forgot to fire "
                "it); abandoned\n",
                ToriRS_TriggerOwner_Name(owner),
                trigger_name(trigger));
            fflush(stderr);
            assert(!completed && "an owner ran to its end with a trigger still primed");
        }
        trigger_end(trigger, TORIRS_TRIGGER_ABANDONED, TORIRS_WAKE_ABANDONED);
    }
}

/* ---- triggers ----------------------------------------------------------- */

void
ToriRS_Trigger_Init(
    struct ToriRS_Trigger* trigger,
    char const* name)
{
    assert(trigger);
    assert(!trigger->owner);
    assert(!trigger->subs);
    trigger->name = name;
}

void
ToriRS_Trigger_Prime(
    struct ToriRS_Trigger* trigger,
    struct ToriRS_TriggerOwner* owner)
{
    char line[256];

    assert(trigger);
    assert(owner);
    assert(owner->kind != TORIRS_OWNER_NONE);
    if( trigger->owner == owner )
    {
        snprintf(
            line,
            sizeof(line),
            "%s primed trigger '%s' twice",
            ToriRS_TriggerOwner_Name(owner),
            trigger_name(trigger));
        ToriRS_TaskFatal(line);
    }
    /* A newer prime replaces the older: what the older promised is not
     * coming, what the newer promises is. Its waiters wake, find it primed
     * again, and wait again. */
    if( trigger->owner )
        trigger_end(trigger, TORIRS_TRIGGER_ABANDONED, TORIRS_WAKE_ABANDONED);
    trigger->owner = owner;
    trigger->owner_prev = NULL;
    trigger->owner_next = owner->primed;
    if( owner->primed )
        owner->primed->owner_prev = trigger;
    owner->primed = trigger;
}

int
ToriRS_Trigger_Primed(struct ToriRS_Trigger const* trigger)
{
    assert(trigger);
    return trigger->owner != NULL;
}

void
ToriRS_Trigger_Fire(struct ToriRS_Trigger* trigger)
{
    assert(trigger);
    assert(trigger->owner && "fired a trigger that is not primed");
    trigger_end(trigger, TORIRS_TRIGGER_FIRED, TORIRS_WAKE_FIRED);
}

void
ToriRS_Trigger_FireIfPrimed(struct ToriRS_Trigger* trigger)
{
    assert(trigger);
    if( trigger->owner )
        ToriRS_Trigger_Fire(trigger);
}

int
ToriRS_Trigger_Outcome(struct ToriRS_Trigger const* trigger)
{
    assert(trigger);
    return trigger->outcome;
}

void
ToriRS_Trigger_Retire(struct ToriRS_Trigger* trigger)
{
    assert(trigger);
    if( trigger->owner )
        trigger_end(trigger, TORIRS_TRIGGER_ABANDONED, TORIRS_WAKE_ABANDONED);
    assert(!trigger->subs);
}

int
ToriRS_Trigger_Subscribers(struct ToriRS_Trigger const* trigger)
{
    int n = 0;

    assert(trigger);
    for( struct ToriRS_Task const* t = trigger->subs; t; t = t->park_next )
        n++;
    return n;
}

/* ---- clocks ------------------------------------------------------------- */

void
ToriRS_Clock_Init(
    struct ToriRS_Clock* clock,
    char const* name,
    uint32_t now)
{
    assert(clock);
    assert(!clock->head);
    clock->name = name;
    clock->now = now;
}

void
ToriRS_Clock_Advance(
    struct ToriRS_Clock* clock,
    uint32_t now)
{
    assert(clock);
    clock->now = now;
    while( clock->head && ToriRS_CycleReached(now, clock->head->park.due) )
        ToriRS_TaskWake(clock->head, TORIRS_WAKE_CYCLE);
}

void
ToriRS_Clock_Retire(struct ToriRS_Clock* clock)
{
    assert(clock);
    while( clock->head )
        ToriRS_TaskWake(clock->head, TORIRS_WAKE_CYCLE);
}

int
ToriRS_Clock_Parked(struct ToriRS_Clock const* clock)
{
    int n = 0;

    assert(clock);
    for( struct ToriRS_Task const* t = clock->head; t; t = t->park_next )
        n++;
    return n;
}

/* ---- counts ------------------------------------------------------------- */

static unsigned
count_bucket(int const* count)
{
    uintptr_t h = (uintptr_t)count;

    h ^= h >> 17;
    h *= 0x9E3779B1u;
    h ^= h >> 13;
    return (unsigned)(h & (COUNT_BUCKETS - 1));
}

void
ToriRS_CountDropped(int* count)
{
    struct ToriRS_Task* t;
    struct ToriRS_Task* next;

    assert(count);
    for( t = g_count_bucket[count_bucket(count)]; t; t = next )
    {
        next = t->park_next;
        if( t->park.count != count )
            continue;
        if( *count <= 0 || t->park.any )
            ToriRS_TaskWake(t, TORIRS_WAKE_COUNT);
    }
}

int
ToriRS_CountParked(void)
{
    return g_count_parked;
}

/* ---- parking ------------------------------------------------------------ */

/* The head pointer of the list `task` is (or will be) linked in. */
static struct ToriRS_Task**
park_list(struct ToriRS_Task const* task)
{
    if( task->park.on == TORIRS_PARK_TRIGGER )
        return &task->park.trigger->subs;
    if( task->park.on == TORIRS_PARK_CYCLE )
        return &task->park.clock->head;
    assert(task->park.on == TORIRS_PARK_COUNT);
    return &g_count_bucket[count_bucket(task->park.count)];
}

static void
park_link_front(
    struct ToriRS_Task** head,
    struct ToriRS_Task* task)
{
    task->park_prev = NULL;
    task->park_next = *head;
    if( *head )
        (*head)->park_prev = task;
    *head = task;
}

/* Into the clock's list in due order, after every task due no later (so
 * tasks due on one cycle wake in the order they parked). */
static void
park_link_clock(
    struct ToriRS_Clock* clock,
    struct ToriRS_Task* task)
{
    struct ToriRS_Task* prev = NULL;
    struct ToriRS_Task* at = clock->head;

    while( at && ToriRS_CycleReached(task->park.due, at->park.due) )
    {
        prev = at;
        at = at->park_next;
    }
    task->park_prev = prev;
    task->park_next = at;
    if( at )
        at->park_prev = task;
    if( prev )
        prev->park_next = task;
    else
        clock->head = task;
}

static void
park_unlink(struct ToriRS_Task* task)
{
    struct ToriRS_Task** head = park_list(task);

    if( task->park_prev )
        task->park_prev->park_next = task->park_next;
    else
    {
        assert(*head == task);
        *head = task->park_next;
    }
    if( task->park_next )
        task->park_next->park_prev = task->park_prev;
    task->park_prev = NULL;
    task->park_next = NULL;
    if( task->park.on == TORIRS_PARK_COUNT )
        g_count_parked--;
    task->parked = 0;
    memset(&task->park, 0, sizeof(task->park));
}

/* Append to `out` (bounded) and return the new length. */
static size_t
chain_put(
    char* out,
    size_t out_size,
    size_t len,
    char const* text)
{
    int n;

    if( len >= out_size )
        return len;
    n = snprintf(out + len, out_size - len, "%s", text);
    if( n < 0 )
        return len;
    return len + (size_t)n;
}

int
ToriRS_TaskChainFormat(
    struct ToriRS_Task const* task,
    char* out,
    size_t out_size)
{
    char piece[192];
    size_t len = 0;
    int hops = 0;

    assert(task);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( ;; )
    {
        struct ToriRS_TriggerOwner const* owner;

        if( !task->parked )
        {
            if( task->io.pending )
            {
                snprintf(piece, sizeof(piece), "%s (reading)", task_name(task));
                chain_put(out, out_size, len, piece);
                return TORIRS_CHAIN_READ;
            }
            snprintf(piece, sizeof(piece), "%s (running)", task_name(task));
            chain_put(out, out_size, len, piece);
            return hops ? TORIRS_CHAIN_RUNNING : TORIRS_CHAIN_READY;
        }
        if( task->park.on == TORIRS_PARK_COUNT )
        {
            snprintf(
                piece,
                sizeof(piece),
                "%s [%s] on a count of %d",
                task_name(task),
                ToriRS_TaskWaitName(task->park.kind),
                *task->park.count);
            chain_put(out, out_size, len, piece);
            return *task->park.count > 0 ? TORIRS_CHAIN_COUNT : TORIRS_CHAIN_UNSOUND;
        }
        if( task->park.on == TORIRS_PARK_CYCLE )
        {
            snprintf(
                piece,
                sizeof(piece),
                "%s [%s] on %s until cycle %u (now %u)",
                task_name(task),
                ToriRS_TaskWaitName(task->park.kind),
                task->park.clock->name ? task->park.clock->name : "a clock",
                (unsigned)task->park.due,
                (unsigned)task->park.clock->now);
            chain_put(out, out_size, len, piece);
            return TORIRS_CHAIN_CYCLE;
        }
        assert(task->park.on == TORIRS_PARK_TRIGGER);
        owner = task->park.trigger->owner;
        snprintf(
            piece,
            sizeof(piece),
            "%s [%s] on '%s'",
            task_name(task),
            ToriRS_TaskWaitName(task->park.kind),
            trigger_name(task->park.trigger));
        len = chain_put(out, out_size, len, piece);
        if( !owner )
        {
            chain_put(out, out_size, len, ", which is not primed");
            return TORIRS_CHAIN_UNSOUND;
        }
        if( owner->kind != TORIRS_OWNER_TASK )
        {
            snprintf(
                piece,
                sizeof(piece),
                " -> %s %s",
                owner->kind == TORIRS_OWNER_JOB ? "job" : "outside owner",
                ToriRS_TriggerOwner_Name(owner));
            chain_put(out, out_size, len, piece);
            return owner->kind == TORIRS_OWNER_JOB ? TORIRS_CHAIN_JOB : TORIRS_CHAIN_EXTERNAL;
        }
        len = chain_put(out, out_size, len, " -> ");
        task = owner->task;
        /* A loop is refused when it is made; this is the backstop. */
        if( ++hops > 64 )
        {
            chain_put(out, out_size, len, "...");
            return TORIRS_CHAIN_UNSOUND;
        }
    }
}

/* Would parking `waiter` on `trigger` close a loop? Walk the owners: a task
 * owner parked on a trigger leads to that trigger's owner. */
static int
closes_loop(
    struct ToriRS_Task const* waiter,
    struct ToriRS_Trigger const* trigger)
{
    int hops = 0;

    while( trigger && trigger->owner && trigger->owner->kind == TORIRS_OWNER_TASK )
    {
        struct ToriRS_Task const* owner = trigger->owner->task;

        if( owner == waiter )
            return 1;
        if( !owner->parked || owner->park.on != TORIRS_PARK_TRIGGER )
            return 0;
        trigger = owner->park.trigger;
        if( ++hops > 4096 )
            return 1;
    }
    return 0;
}

void
ToriRS_TaskPark(struct ToriRS_Task* task)
{
    char line[1024];
    char chain[768];

    assert(task);
    assert(!task->parked);
    assert(task->park_req.on != TORIRS_PARK_NONE);
    task->park = task->park_req;
    memset(&task->park_req, 0, sizeof(task->park_req));
    if( task->park.on == TORIRS_PARK_TRIGGER )
    {
        struct ToriRS_Trigger* trigger = task->park.trigger;

        assert(trigger);
        if( !trigger->owner )
        {
            snprintf(
                line,
                sizeof(line),
                "%s awaited trigger '%s', which is not primed: nothing is coming that would fire it",
                task_name(task),
                trigger_name(trigger));
            ToriRS_TaskFatal(line);
        }
        if( closes_loop(task, trigger) )
        {
            /* Park it, so the chain prints through it, then die. */
            task->parked = 1;
            park_link_front(&trigger->subs, task);
            (void)ToriRS_TaskChainFormat(task, chain, sizeof(chain));
            snprintf(line, sizeof(line), "a wait closes a loop: %s", chain);
            ToriRS_TaskFatal(line);
        }
        park_link_front(&trigger->subs, task);
    }
    else if( task->park.on == TORIRS_PARK_COUNT )
    {
        assert(task->park.count);
        assert(*task->park.count > 0);
        park_link_front(&g_count_bucket[count_bucket(task->park.count)], task);
        g_count_parked++;
    }
    else
    {
        assert(task->park.on == TORIRS_PARK_CYCLE);
        assert(task->park.clock);
        park_link_clock(task->park.clock, task);
    }
    task->parked = 1;
}

void
ToriRS_TaskWake(
    struct ToriRS_Task* task,
    int why)
{
    assert(task);
    assert(task->parked);
    park_unlink(task);
    task->woke = why;
    g_wakes++;
}

void
ToriRS_TaskUnparkForFree(struct ToriRS_Task* task)
{
    assert(task);
    if( task->parked )
        park_unlink(task);
}

uint32_t
ToriRS_TaskWakes(void)
{
    return g_wakes;
}
