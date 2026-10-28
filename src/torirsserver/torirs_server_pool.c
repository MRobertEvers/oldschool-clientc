#include "torirs_server_pool.h"

#include "mem/torirs_mem.h"
#include "torirs_server_poll.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

/*
 * One queue per worker, picked by key, which is what makes a key's jobs FIFO:
 * they all land on one thread and that thread takes them in order. The
 * finished list is shared and guarded by its own lock.
 *
 * C11 <threads.h> would be the portable spelling, but macOS does not ship it,
 * so this file carries the two-call shim it needs.
 */

#ifdef _WIN32
typedef CRITICAL_SECTION pool_mutex;
typedef CONDITION_VARIABLE pool_cond;
typedef HANDLE pool_thread;
#define pool_mutex_init(m) InitializeCriticalSection(m)
#define pool_mutex_destroy(m) DeleteCriticalSection(m)
#define pool_lock(m) EnterCriticalSection(m)
#define pool_unlock(m) LeaveCriticalSection(m)
#define pool_cond_init(c) InitializeConditionVariable(c)
#define pool_cond_destroy(c) ((void)(c))
#define pool_cond_wait(c, m) SleepConditionVariableCS((c), (m), INFINITE)
#define pool_cond_signal(c) WakeConditionVariable(c)
#else
typedef pthread_mutex_t pool_mutex;
typedef pthread_cond_t pool_cond;
typedef pthread_t pool_thread;
#define pool_mutex_init(m) pthread_mutex_init((m), NULL)
#define pool_mutex_destroy(m) pthread_mutex_destroy(m)
#define pool_lock(m) pthread_mutex_lock(m)
#define pool_unlock(m) pthread_mutex_unlock(m)
#define pool_cond_init(c) pthread_cond_init((c), NULL)
#define pool_cond_destroy(c) pthread_cond_destroy(c)
#define pool_cond_wait(c, m) pthread_cond_wait((c), (m))
#define pool_cond_signal(c) pthread_cond_signal(c)
#endif

struct PoolWorker
{
    struct ToriRSServerPool* pool;
    pool_thread thread;
    pool_mutex lock;
    pool_cond ready;
    struct ToriRSServerPoolJob* head;
    struct ToriRSServerPoolJob* tail;
    int stopping;
};

struct ToriRSServerPool
{
    struct ToriRSServerPoll* wake;
    struct PoolWorker* workers;
    int worker_count;

    pool_mutex done_lock;
    struct ToriRSServerPoolJob* done_head;
    struct ToriRSServerPoolJob* done_tail;

    /* Loop thread only. */
    int pending;
};

static void
pool_finish(
    struct ToriRSServerPool* pool,
    struct ToriRSServerPoolJob* job)
{
    job->next = NULL;
    pool_lock(&pool->done_lock);
    job->done = 1;
    if( pool->done_tail )
        pool->done_tail->next = job;
    else
        pool->done_head = job;
    pool->done_tail = job;
    pool_unlock(&pool->done_lock);
    if( pool->wake )
        ToriRSServer_PollWake(pool->wake);
}

#ifdef _WIN32
static DWORD WINAPI
pool_worker_main(LPVOID arg)
#else
static void*
pool_worker_main(void* arg)
#endif
{
    struct PoolWorker* worker = (struct PoolWorker*)arg;

    for( ;; )
    {
        struct ToriRSServerPoolJob* job;

        pool_lock(&worker->lock);
        while( !worker->head && !worker->stopping )
            pool_cond_wait(&worker->ready, &worker->lock);
        job = worker->head;
        if( job )
        {
            worker->head = job->next;
            if( !worker->head )
                worker->tail = NULL;
        }
        pool_unlock(&worker->lock);
        if( !job )
            break; /* stopping, and the queue is empty */
        job->fn(job);
        pool_finish(worker->pool, job);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

struct ToriRSServerPool*
ToriRSServer_PoolNew(
    int threads,
    struct ToriRSServerPoll* wake)
{
    struct ToriRSServerPool* pool = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*pool));

    assert(pool);
    assert(threads >= 0);
    assert(threads == 0 || wake);
    pool->wake = wake;
    pool_mutex_init(&pool->done_lock);
    pool->worker_count = threads;
    if( threads == 0 )
        return pool;
    pool->workers = ToriMem_Calloc(ToriMem_ServerNet(), (size_t)threads, sizeof(*pool->workers));
    assert(pool->workers);
    for( int i = 0; i < threads; i++ )
    {
        struct PoolWorker* worker = &pool->workers[i];

        worker->pool = pool;
        pool_mutex_init(&worker->lock);
        pool_cond_init(&worker->ready);
#ifdef _WIN32
        worker->thread = CreateThread(NULL, 0, pool_worker_main, worker, 0, NULL);
        if( !worker->thread )
#else
        if( pthread_create(&worker->thread, NULL, pool_worker_main, worker) != 0 )
#endif
        {
            fprintf(stderr, "torirsserver: cannot start pool thread %d\n", i);
            abort();
        }
    }
    return pool;
}

void
ToriRSServer_PoolFree(struct ToriRSServerPool* pool)
{
    if( !pool )
        return;
    for( int i = 0; i < pool->worker_count; i++ )
    {
        struct PoolWorker* worker = &pool->workers[i];

        pool_lock(&worker->lock);
        worker->stopping = 1;
        pool_cond_signal(&worker->ready);
        pool_unlock(&worker->lock);
#ifdef _WIN32
        WaitForSingleObject(worker->thread, INFINITE);
        CloseHandle(worker->thread);
#else
        pthread_join(worker->thread, NULL);
#endif
        pool_cond_destroy(&worker->ready);
        pool_mutex_destroy(&worker->lock);
    }
    pool_mutex_destroy(&pool->done_lock);
    ToriMem_Free(ToriMem_ServerNet(), pool->workers);
    ToriMem_Free(ToriMem_ServerNet(), pool);
}

void
ToriRSServer_PoolSubmit(
    struct ToriRSServerPool* pool,
    struct ToriRSServerPoolJob* job)
{
    struct PoolWorker* worker;

    assert(pool);
    assert(job);
    assert(job->fn);
    job->done = 0;
    job->next = NULL;
    pool->pending++;
    if( pool->worker_count == 0 )
    {
        job->fn(job);
        pool_finish(pool, job);
        return;
    }
    worker = &pool->workers[job->key % (uint32_t)pool->worker_count];
    pool_lock(&worker->lock);
    if( worker->tail )
        worker->tail->next = job;
    else
        worker->head = job;
    worker->tail = job;
    pool_cond_signal(&worker->ready);
    pool_unlock(&worker->lock);
}

struct ToriRSServerPoolJob*
ToriRSServer_PoolCollect(struct ToriRSServerPool* pool)
{
    struct ToriRSServerPoolJob* list;

    assert(pool);
    pool_lock(&pool->done_lock);
    list = pool->done_head;
    pool->done_head = NULL;
    pool->done_tail = NULL;
    pool_unlock(&pool->done_lock);
    for( struct ToriRSServerPoolJob* job = list; job; job = job->next )
        pool->pending--;
    assert(pool->pending >= 0);
    return list;
}

int
ToriRSServer_PoolPending(const struct ToriRSServerPool* pool)
{
    assert(pool);
    return pool->pending;
}

uint32_t
ToriRSServer_PoolKey(const char* name)
{
    uint32_t hash = 2166136261u;

    assert(name);
    for( ; *name; name++ )
        hash = (hash ^ (uint32_t)tolower((unsigned char)*name)) * 16777619u;
    return hash;
}
