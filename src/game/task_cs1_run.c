/*
 * CS1 evaluation task. Mirrors task_cs2_run: drive the VM, and when a host op
 * yields for a cache load, plan the yield in a flat switch and service it with
 * a linear chain of awaits (protothreads cannot nest switch). CS1 scripts are
 * short and stateless, so "resume" is simply re-running the component once the
 * load lands — the VM has already rolled back to the yielding opcode.
 */

#include "game/task_cs1_run.h"

#include "asyncio.h"
#include "cs1vm/cs1vm.h"
#include "engine/cache_provider.h"
#include "game/rs_cs1_host.h"
#include "ui/uitree.h"
#include <3rd/minipt.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Guards against a component whose loads never satisfy it. */
#define TASK_CS1_RETRY_MAX 4

enum TaskCS1YieldPlan
{
    TASK_CS1_YIELD_NONE = 0,
    TASK_CS1_YIELD_COMPONENT,
};

struct Task_CS1Eval
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS1Host* host;
    struct CacheProvider* provider;

    /* Walk cursor over tree->components. */
    int32_t cursor;
    int retries;

    /* Yield bookkeeping for the component at `cursor`. */
    enum CS1VM_EvalStatus status;
    struct CS1VM_HostRequest pending;
    enum TaskCS1YieldPlan yield_plan;
    int await_id;
};

/** True when the component's text carries a %N placeholder. */
static bool
task_cs1_text_has_placeholder(struct UITreeComponent const* component)
{
    if( component->type != UIELEM_RS_TEXT )
        return false;

    char const* text = component->u.rs_text.text;
    char const* active = component->u.rs_text.text_active;
    return (text && strchr(text, '%')) || (active && strchr(active, '%'));
}

/** True when the component at the cursor is live and carries CS1 scripts. */
static bool
task_cs1_component_has_scripts(struct Task_CS1Eval* self)
{
    struct UITreeComponent const* component = &self->host->tree->components[self->cursor];
    return !component->freed && component->behavior.scripts_count > 0;
}

/**
 * Evaluate one component into its result cache. Returns the CS1VM_EVAL_* status
 * of the first evaluation that did not complete, so the caller can service a
 * load and retry.
 */
static enum CS1VM_EvalStatus
task_cs1_eval_component(struct Task_CS1Eval* self)
{
    bool active = false;

    /* Resolved from the cursor rather than passed in: a pointer local in the
     * run loop would be skipped by the protothread's resume jump. */
    struct UITreeComponent* component = &self->host->tree->components[self->cursor];

    enum CS1VM_EvalStatus status = RS_CS1Host_ComponentActive(self->host, component, &active);
    if( status != CS1VM_EVAL_DONE )
        return status;

    if( component->cs1_active != (active ? 1 : 0) )
    {
        component->cs1_active = active ? 1 : 0;
        component->is_dirty = 1;
        self->host->eval_dirty = true;
    }

    if( !task_cs1_text_has_placeholder(component) )
        return CS1VM_EVAL_DONE;

    for( int i = 0; i < UITREE_CS1_VALUE_MAX; i++ )
    {
        int value = 0;
        status = RS_CS1Host_ComponentValue(self->host, component, i, &value);
        if( status != CS1VM_EVAL_DONE )
            return status;

        if( component->cs1_values[i] != value )
        {
            component->cs1_values[i] = value;
            component->is_dirty = 1;
            self->host->eval_dirty = true;
        }
    }

    return CS1VM_EVAL_DONE;
}

/** Flat switch: map the parked host request to the load the task must run. */
static void
task_cs1_plan_yield(struct Task_CS1Eval* self)
{
    assert(self);

    self->yield_plan = TASK_CS1_YIELD_NONE;
    self->await_id = -1;

    switch( self->pending.kind )
    {
    case CS1VM_HOST_REQUEST_INV_COUNT:
    case CS1VM_HOST_REQUEST_INV_CONTAINS:
        /* The opcode named a component that is not mounted; load its pack. */
        self->yield_plan = TASK_CS1_YIELD_COMPONENT;
        self->await_id = self->pending.u.inv.component_id;
        break;
    default:
        /* Every other read is served from memory and never yields. */
        break;
    }
}

static int
Task_CS1Eval_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_CS1Eval* self = (struct Task_CS1Eval*)task_base;

    PT_BEGIN(&self->pt);

    /* The scan below reads one field out of every component, so on an if3/CS2
     * tree it is a few megabytes of pointless traffic per 20ms tick. */
    if( !UITree_HasCS1Scripts(self->host->tree) )
        PT_EXIT(&self->pt);

    for( self->cursor = 0; self->cursor < (int32_t)self->host->tree->component_count;
         self->cursor++ )
    {
        if( !task_cs1_component_has_scripts(self) )
            continue;

        for( self->retries = 0; self->retries < TASK_CS1_RETRY_MAX; self->retries++ )
        {
            self->status = task_cs1_eval_component(self);
            if( self->status != CS1VM_EVAL_YIELDED )
                break;

            if( !self->host->has_pending )
            {
                fprintf(stderr, "Task_CS1Eval: yield without pending host request\n");
                break;
            }

            self->pending = self->host->pending;
            self->host->has_pending = false;

            /* Flat switch (no PT) then linear awaits — protothreads cannot
             * nest switch. */
            task_cs1_plan_yield(self);

            if( self->yield_plan == TASK_CS1_YIELD_COMPONENT )
            {
                TASK_AWAITSELF_IF(CreateTask_ComponentPackLoad(self->provider, self->await_id));
            }
            else
            {
                /* Nothing to load: retrying would spin, and the host's
                 * await-spent record makes the next pass complete anyway. */
                break;
            }
        }
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS1Eval_Free(struct ToriRS_Task* task)
{
    struct Task_CS1Eval* self = (struct Task_CS1Eval*)task;
    assert(self);
    free(self);
}

static struct ToriRS_TaskVTable Task_CS1Eval_VTable = {
    .run = Task_CS1Eval_Run,
    .free = Task_CS1Eval_Free,
};

struct ToriRS_Task*
CreateTask_CS1Eval(struct RS_CS1Host* host)
{
    struct Task_CS1Eval* self;

    if( !host || !host->tree || !host->provider )
        return NULL;
    if( host->tree->component_count == 0 )
        return NULL;

    self = calloc(1, sizeof(*self));
    assert(self);

    self->task.vtable = &Task_CS1Eval_VTable;
    strcpy(self->task.name, "CS1Eval");
    self->host = host;
    self->provider = host->provider;
    self->await_id = -1;

    PT_INIT(&self->pt);
    return &self->task;
}
