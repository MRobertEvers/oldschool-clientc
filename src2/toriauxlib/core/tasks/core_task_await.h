#ifndef CORE_TASK_AWAIT_H
#define CORE_TASK_AWAIT_H

#include "3rd/minipt.h"

/**
 * Drive a child protothread Run() to completion from a parent protothread.
 * Propagates PT_YIELDED / PT_WAITING so the scheduler wait_run IO logic works.
 *
 * Must not be used inside a nested switch (case __LINE__ would bind incorrectly).
 */
#define TASK_AWAIT(pt, run_expr)                                   \
    do {                                                           \
        (pt)->lc = __LINE__;                                       \
        case __LINE__: {                                           \
            int _await_res = (run_expr);                           \
            if( _await_res != PT_ENDED && _await_res != PT_EXITED )\
                return _await_res;                                 \
        }                                                          \
    } while(0)

#endif
