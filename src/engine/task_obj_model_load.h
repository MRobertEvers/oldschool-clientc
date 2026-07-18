#ifndef TASK_OBJ_MODEL_LOAD_H
#define TASK_OBJ_MODEL_LOAD_H

#include "asyncio.h"

struct CacheProvider;

/**
 * Load objtypes and their inventory models (count-variant aware).
 * counts may be NULL (every stack count treated as 1).
 * Returns NULL if n <= 0 or nothing needs loading — callers use TASK_AWAITSELF_IF.
 */
struct ToriRS_Task*
CreateTask_ObjModelLoad(
    struct CacheProvider* provider,
    int const* obj_ids,
    int const* counts,
    int n);

#endif
