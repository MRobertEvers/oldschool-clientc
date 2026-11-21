#ifndef SRC_GAME_RS_CS2_TASK_POOL_H
#define SRC_GAME_RS_CS2_TASK_POOL_H

/*
 * THE SCRIPT HOST'S TASK POOLS.
 *
 * Every client script runs as a task object, made when the script is queued
 * and dropped when it ends: 1.4 KB each, and a bank open runs 105 of them. A
 * timer handler that fires gets a smaller one, every tick it fires. The host
 * keeps finished task objects, one pool per kind (RS_CS2Host::run_task_pool,
 * ::timer_task_pool), and the next script takes one.
 *
 * A pooled object is a ToriMem_Script() block nobody has freed yet.
 * RS_CS2Host_Free frees them and closes the pools: the task queues are torn
 * down after the host, and a task released then is freed, not kept. A
 * checkpoint's copy of the host has empty pools, and a restore keeps the live
 * host's.
 *
 * Under AddressSanitizer nothing is kept, so a task used after it ended is
 * still reported.
 */

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__)
#define RS_CS2_TASK_POOL_ON 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RS_CS2_TASK_POOL_ON 0
#endif
#endif
#ifndef RS_CS2_TASK_POOL_ON
#define RS_CS2_TASK_POOL_ON 1
#endif

/** Task objects one pool keeps. */
#define RS_CS2_TASK_POOL_KEEP 128u

struct RS_CS2TaskPool
{
    /** Finished task objects, linked through their first pointer-sized bytes. */
    void* head;
    uint32_t count;
    /** Set by RS_CS2TaskPool_Free: nothing is kept any more. */
    uint32_t closed;
};

/** A zeroed task object of `size` bytes. Every object of one pool has the
 *  one size. Never NULL. */
static inline void*
RS_CS2TaskPool_New(struct RS_CS2TaskPool* pool, size_t size)
{
    void* task;
    assert(pool);
    assert(size >= sizeof(void*));
    task = pool->head;
    if( task )
    {
        memcpy(&pool->head, task, sizeof(void*));
        pool->count--;
        memset(task, 0, size);
        return task;
    }
    task = ToriMem_Calloc(ToriMem_Script(), 1, size);
    assert(task);
    return task;
}

/** Release a task object that owns nothing by now. */
static inline void
RS_CS2TaskPool_Release(struct RS_CS2TaskPool* pool, void* task)
{
    assert(pool);
    assert(task);
    if( !RS_CS2_TASK_POOL_ON || pool->closed || pool->count >= RS_CS2_TASK_POOL_KEEP )
    {
        ToriMem_Free(ToriMem_Script(), task);
        return;
    }
    memcpy(task, &pool->head, sizeof(void*));
    pool->head = task;
    pool->count++;
}

/** Free every kept object and keep none from here on. */
static inline void
RS_CS2TaskPool_Free(struct RS_CS2TaskPool* pool)
{
    assert(pool);
    while( pool->head )
    {
        void* const task = pool->head;
        memcpy(&pool->head, task, sizeof(void*));
        ToriMem_Free(ToriMem_Script(), task);
    }
    pool->count = 0;
    pool->closed = 1;
}

#endif
