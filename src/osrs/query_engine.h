#ifndef QUERY_ENGINE_DAT_H
#define QUERY_ENGINE_DAT_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

enum QETag
{
    QE_TAG_DT = 1,
    QE_TAG_FN = 2,
    QE_TAG_ACTION = 3,
    QE_TAG_ARG = 4,
    QE_TAG_ARGX_COUNT = 5,
};

enum QueryEngineFn
{
    QE_FN_NONE,
    QE_FN_0,
    QE_FN_1,
    QE_FN_2,
    QE_FN_FROM_0,
    QE_FN_FROM_1,
    QE_FN_FROM_2,
    QE_FN_FROM_3,
    QE_FN_FROM_4,
    QE_FN_FROM_5,
    QE_FN_FROM_6,
    QE_FN_FROM_7,
    QE_FN_FROM_8,
    QE_FN_FROM_9,
    QE_FN_COUNT,
};

enum QueryEngineAction
{
    QE_STORE_NONE,
    QE_STORE_SET_0,
    QE_STORE_SET_1,
    QE_STORE_SET_2,
    QE_STORE_SET_3,
    QE_STORE_SET_4,
    QE_STORE_SET_5,
    QE_STORE_SET_6,
    QE_STORE_SET_7,
    QE_STORE_SET_8,
    QE_STORE_SET_9,
    QE_STORE_DISCARD,
    QE_ACTION_COUNT,
};

enum QEQueryState
{
    QE_STATE_ACTIVE,
    QE_STATE_AWAITING_IO,
    QE_STATE_DONE,
};

struct QueryEngine;
struct QEQuery;

struct QueryEngine*
query_engine_new(void);

void
query_engine_free(struct QueryEngine* query_engine);

void
query_engine_reset(struct QueryEngine* query_engine);

struct QEQuery*
query_engine_qnew(void);

void
query_engine_qfree(struct QEQuery* q);

void
query_engine_qpush_op(
    struct QEQuery* q,
    int dt,
    int fn,
    int action);

void
query_engine_qpush_arg(
    struct QEQuery* q,
    int arg1);
void
query_engine_qpush_arg2(
    struct QEQuery* q,
    int arg1,
    int arg2);
void
query_engine_qpush_arg4(
    struct QEQuery* q,
    int arg1,
    int arg2,
    int arg3,
    int arg4);

void
query_engine_qpush_argx(
    struct QEQuery* q,
    int* argx,
    int argx_count);

void
query_engine_qpush_reqid(
    struct QEQuery* q,
    uint32_t reqid);

void
query_engine_qpop_reqid(
    struct QEQuery* q,
    uint32_t reqid);

void
query_engine_qawait(struct QEQuery* q);

uint32_t
query_engine_qget_active_dt(struct QEQuery* q);

void
query_engine_qset_active_dt(
    struct QEQuery* q,
    uint32_t dt);

uint32_t
query_engine_qdecode_dt(struct QEQuery* q);

uint32_t
query_engine_qdecode_fn(struct QEQuery* q);

uint32_t
query_engine_qdecode_action(struct QEQuery* q);

uint32_t
query_engine_qdecode_arg(struct QEQuery* q);

uint32_t
query_engine_qdecode_argx_count(struct QEQuery* q);

uint32_t
query_engine_qreg_init_active(
    struct QueryEngine* qe,
    struct QEQuery* q,
    enum QueryEngineAction action,
    int dt);

int32_t
query_engine_qreg_get_active_idx(struct QEQuery* q);

bool
query_engine_qisdone(struct QEQuery* q);

enum QEQueryState
query_engine_qstate(struct QEQuery* q);

void
query_engine_qreg_push(
    struct QueryEngine* query_engine,
    int reg_idx,
    int id,
    void* value);

void
query_engine_qreg_iter_begin(
    struct QueryEngine* query_engine,
    int reg_idx);

void*
query_engine_qreg_iter_next(
    struct QueryEngine* query_engine,
    int reg_idx,
    int* out_id);

void
query_engine_qreg_iter_end(
    struct QueryEngine* query_engine,
    int reg_idx);

uint32_t
query_engine_qreg_get_dt(
    struct QueryEngine* query_engine,
    int reg_idx);

#endif