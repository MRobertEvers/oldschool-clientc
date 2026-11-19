#include "game/rs_pending.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
 * Two intrusive lists through the headers themselves: every listed header in
 * listing order (what a scope walks), and one chain per key bucket (what a
 * find walks). Nothing is allocated per task; the index is the two heads.
 */

#define RS_PENDING_BUCKETS 256

struct RS_Pending
{
    struct RS_ParallelTask* head;
    struct RS_ParallelTask* tail;
    struct RS_ParallelTask* bucket[RS_PENDING_BUCKETS];
    int listed;
    uint32_t next_order;
};

static uint32_t
bucket_of(
    int kind,
    struct RS_PendingKey key)
{
    uint32_t h = (uint32_t)kind * 0x9E3779B1u;
    h ^= (uint32_t)key.shape * 0x85EBCA77u;
    h ^= (uint32_t)key.sub * 0xC2B2AE3Du;
    h ^= (uint32_t)key.id * 0x27D4EB2Fu;
    h ^= (uint32_t)key.x * 0x165667B1u;
    h ^= (uint32_t)key.z * 0xD3A2646Cu;
    h ^= ((uint32_t)key.level << 8 | key.layer) * 0xFD7046C5u;
    h ^= key.gen * 0xB55A4F09u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h & (RS_PENDING_BUCKETS - 1);
}

/* Is `a` listed before `b`? Orders are compared as a signed difference. */
static bool
order_before(
    uint32_t a,
    uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static bool
is_task_for(
    struct RS_ParallelTask const* task,
    int kind,
    struct RS_PendingKey key)
{
    return task->kind == (uint16_t)kind && RS_PendingKey_Equal(task->key, key);
}

static void
check_kind(int kind)
{
    (void)kind;
    assert(kind > 0);
    assert(kind < RS_PENDING_KIND_MAX);
}

struct RS_Pending*
RS_Pending_New(void)
{
    struct RS_Pending* index = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*index));

    assert(index);
    index->next_order = 1;
    return index;
}

void
RS_Pending_Free(struct RS_Pending* index)
{
    if( !index )
        return;
    assert(index->listed == 0 && "a parallel task outlived the index");
    ToriMem_Free(ToriMem_Process(), index);
}

void
RS_Pending_List(
    struct RS_Pending* index,
    struct RS_ParallelTask* task,
    int kind,
    struct RS_PendingKey key,
    int view,
    uint32_t scene_serial,
    uint32_t arrived_cycle)
{
    uint32_t b;

    assert(index);
    assert(task);
    assert(!task->listed);
    check_kind(kind);
    memset(task, 0, sizeof(*task));
    task->kind = (uint16_t)kind;
    task->key = key;
    task->view = view;
    task->scene_serial = scene_serial;
    task->arrived_cycle = arrived_cycle;
    task->order = index->next_order++;
    task->outcome = ASSET_OUTCOME_DROPPED;
    task->listed = 1;

    task->prev = index->tail;
    if( index->tail )
        index->tail->next = task;
    else
        index->head = task;
    index->tail = task;

    b = bucket_of(kind, key);
    task->bucket_next = index->bucket[b];
    if( index->bucket[b] )
        index->bucket[b]->bucket_prev = task;
    index->bucket[b] = task;
    index->listed++;
}

void
RS_Pending_Unlist(
    struct RS_Pending* index,
    struct RS_ParallelTask* task)
{
    uint32_t b;

    assert(index);
    assert(task);
    assert(task->listed);
    assert(index->listed > 0);

    if( task->prev )
        task->prev->next = task->next;
    else
        index->head = task->next;
    if( task->next )
        task->next->prev = task->prev;
    else
        index->tail = task->prev;

    b = bucket_of(task->kind, task->key);
    if( task->bucket_prev )
        task->bucket_prev->bucket_next = task->bucket_next;
    else
    {
        assert(index->bucket[b] == task);
        index->bucket[b] = task->bucket_next;
    }
    if( task->bucket_next )
        task->bucket_next->bucket_prev = task->bucket_prev;

    task->prev = NULL;
    task->next = NULL;
    task->bucket_prev = NULL;
    task->bucket_next = NULL;
    task->listed = 0;
    index->listed--;
}

struct RS_ParallelTask*
RS_Pending_Find(
    struct RS_Pending const* index,
    int kind,
    struct RS_PendingKey key)
{
    struct RS_ParallelTask* newest = NULL;

    assert(index);
    check_kind(kind);
    for( struct RS_ParallelTask* t = index->bucket[bucket_of(kind, key)]; t; t = t->bucket_next )
    {
        if( t->cancelled || !is_task_for(t, kind, key) )
            continue;
        if( !newest || order_before(newest->order, t->order) )
            newest = t;
    }
    return newest;
}

int
RS_Pending_CancelKey(
    struct RS_Pending* index,
    int kind,
    struct RS_PendingKey key)
{
    int cancelled = 0;

    assert(index);
    check_kind(kind);
    for( struct RS_ParallelTask* t = index->bucket[bucket_of(kind, key)]; t; t = t->bucket_next )
    {
        if( t->cancelled || !is_task_for(t, kind, key) )
            continue;
        t->cancelled = 1;
        cancelled++;
    }
    return cancelled;
}

bool
RS_Pending_EarlierPending(
    struct RS_Pending const* index,
    struct RS_ParallelTask const* task)
{
    assert(index);
    assert(task);
    assert(task->listed);
    for( struct RS_ParallelTask const* t = index->bucket[bucket_of(task->kind, task->key)]; t;
         t = t->bucket_next )
    {
        if( t == task || t->cancelled || !is_task_for(t, task->kind, task->key) )
            continue;
        if( order_before(t->order, task->order) )
            return true;
    }
    return false;
}

static bool
in_scope(
    struct RS_ParallelTask const* t,
    struct RS_PendingScope const* scope)
{
    if( scope->kind == RS_PENDING_SCOPE_ALL )
        return true;
    if( scope->kind == RS_PENDING_SCOPE_SCENE )
        return t->view == scope->view && t->scene_serial == scope->serial;
    if( scope->kind == RS_PENDING_SCOPE_VIEW )
        return t->view == scope->view;
    if( scope->kind == RS_PENDING_SCOPE_ENTITY )
        return t->key.shape == RS_PENDING_KEY_ENTITY && t->key.sub == scope->sub &&
               t->key.id == scope->id;
    if( scope->kind == RS_PENDING_SCOPE_GROUP )
        return t->key.shape == RS_PENDING_KEY_COMPONENT &&
               (int32_t)((uint32_t)t->key.id >> 16) == scope->id;
    assert(scope->kind == RS_PENDING_SCOPE_KIND);
    return t->kind == (uint16_t)scope->id;
}

int
RS_Pending_Cancel(
    struct RS_Pending* index,
    struct RS_PendingScope scope)
{
    int cancelled = 0;

    assert(index);
    if( scope.kind == RS_PENDING_SCOPE_KIND )
        check_kind(scope.id);
    for( struct RS_ParallelTask* t = index->head; t; t = t->next )
    {
        if( t->cancelled || !in_scope(t, &scope) )
            continue;
        t->cancelled = 1;
        cancelled++;
    }
    return cancelled;
}

int
RS_Pending_Count(
    struct RS_Pending const* index,
    int kind)
{
    int count = 0;

    assert(index);
    if( kind != 0 )
        check_kind(kind);
    for( struct RS_ParallelTask const* t = index->head; t; t = t->next )
        if( !t->cancelled && (kind == 0 || t->kind == (uint16_t)kind) )
            count++;
    return count;
}

int
RS_Pending_Listed(struct RS_Pending const* index)
{
    assert(index);
    return index->listed;
}
