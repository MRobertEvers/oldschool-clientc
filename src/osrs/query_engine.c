#include "query_engine.h"

#include "datastruct/vec.h"
#include "graphics/dash.h"

#include <stdlib.h>
#include <string.h>

struct SetEntry
{
    int id;

    int arg0;
    int arg1;
    int arg2;
    int arg3;
    int arg4;

    void* value;
};

struct QESet
{
    int dt;

    struct DashMapIter* iter_nullable;
    struct DashMap* map;
};

struct QESet*
qeset_new(int dt)
{
    struct QESet* qeset = malloc(sizeof(struct QESet));
    memset(qeset, 0, sizeof(struct QESet));
    qeset->dt = dt;

    int buffer_size = 1024 * sizeof(struct SetEntry);
    struct DashMapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SetEntry),
    };
    qeset->map = dashmap_new(&config, 0);
    return qeset;
}

void
qeset_free(struct QESet* qeset)
{
    if( !qeset )
        return;
    free(dashmap_buffer_ptr(qeset->map));
    dashmap_free(qeset->map);
    free(qeset);
}

struct QueryEngine
{
    struct QESet* sets[10];
};

struct QueryEngine*
query_engine_new(void)
{
    struct QueryEngine* query_engine = malloc(sizeof(struct QueryEngine));
    memset(query_engine, 0, sizeof(struct QueryEngine));

    return query_engine;
}

void
query_engine_free(struct QueryEngine* query_engine)
{
    for( int i = 0; i < 10; i++ )
    {
        qeset_free(query_engine->sets[i]);
    }
    free(query_engine);
}

void
query_engine_reset(struct QueryEngine* query_engine)
{
    for( int i = 0; i < 10; i++ )
    {
        if( query_engine->sets[i] )
            qeset_free(query_engine->sets[i]);
        query_engine->sets[i] = NULL;
    }
}

struct QEQuery
{
    enum QEQueryState state;

    uint32_t code[100];
    int code_count;

    int pc;

    int set_idx;
    int dt;

    struct Vec* reqids;
    int recv_count;
};

struct QEQuery*
query_engine_qnew(void)
{
    struct QEQuery* q = malloc(sizeof(struct QEQuery));
    memset(q, 0, sizeof(struct QEQuery));
    q->state = QE_STATE_ACTIVE;
    q->reqids = vec_new(sizeof(uint32_t), 1024);
    q->recv_count = 0;
    q->set_idx = -1;
    q->dt = 0;
    return q;
}

void
query_engine_qfree(struct QEQuery* q)
{
    vec_free(q->reqids);
    free(q);
}

static uint32_t
qe_tag(
    uint32_t val,
    int tag)
{
    assert((val & 0xF0000000) == 0);

    return (val | (tag << 28));
}

static uint32_t
qe_encode_dt(uint32_t dt)
{
    return qe_tag(dt, QE_TAG_DT);
}

static uint32_t
qe_encode_fn(uint32_t fn)
{
    return qe_tag(fn, QE_TAG_FN);
}

static uint32_t
qe_encode_action(uint32_t action)
{
    return qe_tag(action, QE_TAG_ACTION);
}

static uint32_t
qe_encode_arg(uint32_t arg)
{
    return qe_tag(arg, QE_TAG_ARG);
}

static uint32_t
qe_encode_argx_count(uint32_t argx_count)
{
    return qe_tag(argx_count, QE_TAG_ARGX_COUNT);
}

void
query_engine_qpush_op(
    struct QEQuery* q,
    int dt,
    int fn,
    int action)
{
    if( fn == QE_FN_NONE )
        return;
    if( action == QE_STORE_NONE )
        return;

    q->code[q->code_count++] = qe_encode_dt(dt);
    q->code[q->code_count++] = qe_encode_fn(fn);
    q->code[q->code_count++] = qe_encode_action(action);
}

void
query_engine_qpush_arg(
    struct QEQuery* q,
    int arg1)
{
    q->code[q->code_count++] = qe_encode_arg(arg1);
}

void
query_engine_qpush_arg2(
    struct QEQuery* q,
    int arg1,
    int arg2)
{
    q->code[q->code_count++] = qe_encode_arg(arg1);
    q->code[q->code_count++] = qe_encode_arg(arg2);
}

void
query_engine_qpush_arg4(
    struct QEQuery* q,
    int arg1,
    int arg2,
    int arg3,
    int arg4)
{
    q->code[q->code_count++] = qe_encode_arg(arg1);
    q->code[q->code_count++] = qe_encode_arg(arg2);
    q->code[q->code_count++] = qe_encode_arg(arg3);
    q->code[q->code_count++] = qe_encode_arg(arg4);
}

void
query_engine_qpush_argx(
    struct QEQuery* q,
    int* argx,
    int argx_count)
{
    q->code[q->code_count++] = qe_encode_argx_count(argx_count);
    for( int i = 0; i < argx_count; i++ )
    {
        q->code[q->code_count++] = qe_encode_arg(argx[i]);
    }
}

void
query_engine_qpush_reqid(
    struct QEQuery* q,
    uint32_t reqid)
{
    assert(q->state == QE_STATE_ACTIVE);
    vec_push(q->reqids, &reqid);
}

void
query_engine_qpop_reqid(
    struct QEQuery* q,
    uint32_t reqid)
{
    q->recv_count++;
    if( q->recv_count == vec_size(q->reqids) )
    {
        if( q->pc == q->code_count )
        {
            q->state = QE_STATE_DONE;
        }
        else
        {
            q->state = QE_STATE_ACTIVE;
        }
        vec_clear(q->reqids);
        q->recv_count = 0;
    }
}

void
query_engine_qawait(struct QEQuery* q)
{
    if( vec_size(q->reqids) == 0 )
    {
        if( q->pc == q->code_count )
        {
            q->state = QE_STATE_DONE;
        }
        else
        {
            q->state = QE_STATE_ACTIVE;
        }
        return;
    }
    q->state = QE_STATE_AWAITING_IO;
}

uint32_t
query_engine_qget_active_dt(struct QEQuery* q)
{
    return q->dt;
}

void
query_engine_qset_active_dt(
    struct QEQuery* q,
    uint32_t dt)
{
    q->dt = dt;
}

static bool
query_engine_tagged(
    int value,
    int tag)
{
    return ((value & 0xF0000000) >> 28) == tag;
}

uint32_t
query_engine_qget_dt(struct QEQuery* q)
{
    assert(q->state == QE_STATE_ACTIVE);

    uint32_t op = q->code[q->pc++];
    assert(query_engine_tagged(op, QE_TAG_DT));
    return (op & 0x0FFFFFFF);
}

uint32_t
query_engine_qget_fn(struct QEQuery* q)
{
    assert(q->state == QE_STATE_ACTIVE);

    uint32_t op = q->code[q->pc++];
    assert(query_engine_tagged(op, QE_TAG_FN));
    return (op & 0x0FFFFFFF);
}

uint32_t
query_engine_qget_action(struct QEQuery* q)
{
    assert(q->state == QE_STATE_ACTIVE);

    uint32_t op = q->code[q->pc++];
    assert(query_engine_tagged(op, QE_TAG_ACTION));
    return (op & 0x0FFFFFFF);
}

uint32_t
query_engine_qget_arg(struct QEQuery* q)
{
    assert(q->state == QE_STATE_ACTIVE);

    uint32_t op = q->code[q->pc++];
    assert(query_engine_tagged(op, QE_TAG_ARG));
    return (op & 0x0FFFFFFF);
}

uint32_t
query_engine_qget_argx_count(struct QEQuery* q)
{
    assert(q->state == QE_STATE_ACTIVE);

    uint32_t op = q->code[q->pc++];
    assert(query_engine_tagged(op, QE_TAG_ARGX_COUNT));
    return (op & 0x0FFFFFFF);
}

uint32_t
query_engine_init_set(
    struct QueryEngine* qe,
    struct QEQuery* q,
    enum QueryEngineAction action,
    int dt)
{
    if( action < QE_STORE_SET_0 || action > QE_STORE_SET_9 )
    {
        return 0;
    }

    int idx = action - QE_STORE_SET_0;

    q->set_idx = idx;

    struct QESet* set = qeset_new(dt);
    qe->sets[idx] = set;
}

bool
query_engine_qdone(struct QEQuery* q)
{
    return q->state == QE_STATE_DONE;
}

enum QEQueryState
query_engine_qstate(struct QEQuery* q)
{
    return q->state;
}

int32_t
query_engine_qget_active_set_idx(struct QEQuery* q)
{
    return q->set_idx;
}

uint32_t
query_engine_qget_set_dt(
    struct QueryEngine* query_engine,
    int set_idx)
{
    assert(set_idx >= 0 && set_idx < 10);
    assert(query_engine->sets[set_idx]);
    return query_engine->sets[set_idx]->dt;
}

void
query_engine_qset_push(
    struct QueryEngine* query_engine,
    int set_idx,
    int id,
    void* value)
{
    if( set_idx == -1 )
        return;
    assert(set_idx >= 0 && set_idx < 10);
    assert(query_engine->sets[set_idx]);
    struct SetEntry* entry =
        (struct SetEntry*)dashmap_search(query_engine->sets[set_idx]->map, &id, DASHMAP_INSERT);
    assert(entry);
    entry->id = id;
    entry->value = value;
}

void
query_engine_qget_begin(
    struct QueryEngine* query_engine,
    int set_idx)
{
    assert(set_idx >= 0 && set_idx < 10);
    assert(query_engine->sets[set_idx]);
    assert(query_engine->sets[set_idx]->iter_nullable == NULL);

    struct DashMapIter* iter = dashmap_iter_new(query_engine->sets[set_idx]->map);
    query_engine->sets[set_idx]->iter_nullable = iter;
}

void*
query_engine_qget_next(
    struct QueryEngine* query_engine,
    int set_idx)
{
    assert(set_idx >= 0 && set_idx < 10);
    assert(query_engine->sets[set_idx]);
    assert(query_engine->sets[set_idx]->iter_nullable != NULL);

    struct SetEntry* entry =
        (struct SetEntry*)dashmap_iter_next(query_engine->sets[set_idx]->iter_nullable);
    if( !entry )
        return NULL;
    return entry->value;
}

void
query_engine_qget_end(
    struct QueryEngine* query_engine,
    int set_idx)
{
    assert(set_idx >= 0 && set_idx < 10);
    assert(query_engine->sets[set_idx]);
    assert(query_engine->sets[set_idx]->iter_nullable != NULL);

    dashmap_iter_free(query_engine->sets[set_idx]->iter_nullable);
    query_engine->sets[set_idx]->iter_nullable = NULL;
}