#ifndef CORE_TASK_AWAIT_H
#define CORE_TASK_AWAIT_H

#include "3rd/minipt.h"

struct LibToriRS_Task;
struct LibToriRS_IOContext;

/**
 * Drive a child protothread Run() to completion from a parent protothread.
 * Propagates PT_YIELDED / PT_WAITING so the scheduler wait_run IO logic works.
 *
 * Must not be used inside a nested switch (case __LINE__ would bind incorrectly).
 */
#define TASK_AWAIT(pt, run_expr)                                                                   \
    do                                                                                             \
    {                                                                                              \
        (pt)->lc = __LINE__;                                                                       \
    case __LINE__:                                                                                 \
    {                                                                                              \
        int _await_res = (run_expr);                                                               \
        if( _await_res != PT_ENDED && _await_res != PT_EXITED )                                    \
            return _await_res;                                                                     \
    }                                                                                              \
    } while( 0 )

/**
 * Drive a heap-allocated child LibToriRS_Task to completion from a parent protothread.
 * child_expr is evaluated once and stored in (pt)->user across yields.
 */
#define TASK_AWAITEX(pt, ctx, child_expr)                                                          \
    do                                                                                             \
    {                                                                                              \
        (pt)->lc = __LINE__;                                                                       \
        if( !(pt)->user )                                                                          \
            (pt)->user = (child_expr);                                                             \
    case __LINE__:                                                                                 \
    {                                                                                              \
        struct LibToriRS_Task* _child = (pt)->user;                                                \
        int _await_res = _child->vtable->run_fn(_child, ctx);                                      \
        if( _await_res != PT_ENDED && _await_res != PT_EXITED )                                    \
            return _await_res;                                                                     \
        if( _child->vtable->free_fn )                                                               \
            _child->vtable->free_fn(_child);                                                       \
        (pt)->user = NULL;                                                                         \
    }                                                                                              \
    } while( 0 )

#endif
