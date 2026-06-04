#ifndef LIBTORIRS_IO_H
#define LIBTORIRS_IO_H

#include <stdbool.h>

// clang-format off
#define LIBTORI_TASK_BEGIN(ctx) switch((ctx)->lc) { case 0:
#define LIBTORI_TASK_WAIT_UNTIL(ctx, cond) \
    (ctx)->lc = __LINE__; case __LINE__: \
    if(!(cond)) return 0
#define LIBTORI_TASK_END(ctx) } (ctx)->lc = 0; return 1
#define LIBTORI_TASK_INIT(ctx) (ctx)->lc = 0
    
    // The Boilerplate Killer Macro
#define LIBTORI_TASK(TaskName, ...) \
    typedef struct { \
        unsigned short lc; \
        __VA_ARGS__ \
    } TaskName##_ctx; \
    int TaskName(TaskName##_ctx* ctx)
// clang-format on

#define T_BEGIN(ctx) LIBTORI_TASK_BEGIN(ctx)
#define T_WAIT_UNTIL(ctx, cond) LIBTORI_TASK_WAIT_UNTIL(ctx, cond)
#define T_END(ctx) LIBTORI_TASK_END(ctx)
#define T_INIT(ctx) LIBTORI_TASK_INIT(ctx)
#define T_TASK(TaskName, ...) LIBTORI_TASK(TaskName, __VA_ARGS__)

#endif