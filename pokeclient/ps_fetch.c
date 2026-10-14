#include "ps_fetch.h"

#include "platform/platform_x_http.h"
#include "pokesprite/pokesprite_format.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PS_FETCH_SLOTS 8192 /* open addressing; a session sees a few thousand keys */

struct Entry
{
    char key[128];
    enum PS_FetchState state;
    struct PS_Sheet* sheet;
    int used;
    int queued; /* waiting for the worker */
};

struct PS_Fetch
{
    char host[128];
    int port;
    struct Entry* table;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_t worker;
    int stop;
};

static unsigned
hash(const char* s)
{
    unsigned h = 2166136261u;
    for( ; *s; s++ )
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

/* Caller holds the lock. */
static struct Entry*
find(
    struct PS_Fetch* f,
    const char* key,
    int create)
{
    unsigned i = hash(key) & (PS_FETCH_SLOTS - 1);
    for( int probe = 0; probe < PS_FETCH_SLOTS; probe++, i = (i + 1) & (PS_FETCH_SLOTS - 1) )
    {
        struct Entry* e = &f->table[i];
        if( e->used && strcmp(e->key, key) == 0 )
            return e;
        if( !e->used )
        {
            if( !create )
                return NULL;
            e->used = 1;
            snprintf(e->key, sizeof(e->key), "%s", key);
            e->state = PS_FETCH_PENDING;
            e->queued = 1;
            return e;
        }
    }
    /* A full table is a session that asked for 8192 distinct sheets: raise
     * PS_FETCH_SLOTS rather than start dropping answers. */
    assert(!"PS_Fetch table full");
    return NULL;
}

static void*
worker_main(void* arg)
{
    struct PS_Fetch* f = (struct PS_Fetch*)arg;
    for( ;; )
    {
        char key[128];
        pthread_mutex_lock(&f->lock);
        struct Entry* job = NULL;
        while( !f->stop )
        {
            for( int i = 0; i < PS_FETCH_SLOTS && !job; i++ )
                if( f->table[i].used && f->table[i].queued )
                    job = &f->table[i];
            if( job )
                break;
            pthread_cond_wait(&f->wake, &f->lock);
        }
        if( f->stop )
        {
            pthread_mutex_unlock(&f->lock);
            return NULL;
        }
        job->queued = 0;
        snprintf(key, sizeof(key), "%s", job->key);
        pthread_mutex_unlock(&f->lock);

        char route[192];
        int size = 0, status = 0;
        snprintf(route, sizeof(route), "/pokesprite/%s", key);
        char* body = PlatformX_HttpGetStatus(f->host, f->port, route, &size, &status);
        struct PS_Sheet* sheet = body ? PS_SheetDecode((const uint8_t*)body, size) : NULL;
        free(body);
        enum PS_FetchState state =
            sheet ? PS_FETCH_READY : (status == 404 ? PS_FETCH_MISSING : PS_FETCH_FAILED);

        pthread_mutex_lock(&f->lock);
        struct Entry* e = find(f, key, 0);
        assert(e);
        e->sheet = sheet;
        e->state = state;
        pthread_mutex_unlock(&f->lock);
    }
}

struct PS_Fetch*
PS_FetchNew(
    const char* host,
    int port)
{
    assert(host);
    struct PS_Fetch* f = (struct PS_Fetch*)calloc(1, sizeof(*f));
    assert(f);
    snprintf(f->host, sizeof(f->host), "%s", host);
    f->port = port;
    f->table = (struct Entry*)calloc(PS_FETCH_SLOTS, sizeof(struct Entry));
    assert(f->table);
    pthread_mutex_init(&f->lock, NULL);
    pthread_cond_init(&f->wake, NULL);
    int rc = pthread_create(&f->worker, NULL, worker_main, f);
    assert(rc == 0);
    (void)rc;
    return f;
}

void
PS_FetchFree(struct PS_Fetch* f)
{
    if( !f )
        return;
    pthread_mutex_lock(&f->lock);
    f->stop = 1;
    pthread_cond_broadcast(&f->wake);
    pthread_mutex_unlock(&f->lock);
    pthread_join(f->worker, NULL);
    for( int i = 0; i < PS_FETCH_SLOTS; i++ )
        PS_SheetFree(f->table[i].sheet);
    free(f->table);
    pthread_mutex_destroy(&f->lock);
    pthread_cond_destroy(&f->wake);
    free(f);
}

const struct PS_Sheet*
PS_FetchGet(
    struct PS_Fetch* f,
    const char* key,
    enum PS_FetchState* out_state)
{
    assert(f);
    assert(key);
    assert(out_state);
    assert(PokeSprite_KeyValid(key));
    pthread_mutex_lock(&f->lock);
    struct Entry* e = find(f, key, 1);
    int fresh = e->queued && e->state == PS_FETCH_PENDING;
    enum PS_FetchState state = e->state;
    const struct PS_Sheet* sheet = e->sheet;
    if( fresh )
        pthread_cond_signal(&f->wake);
    pthread_mutex_unlock(&f->lock);
    *out_state = state;
    return state == PS_FETCH_READY ? sheet : NULL;
}

void
PS_FetchCounts(
    struct PS_Fetch* f,
    int out[4])
{
    assert(f);
    assert(out);
    out[0] = out[1] = out[2] = out[3] = 0;
    pthread_mutex_lock(&f->lock);
    for( int i = 0; i < PS_FETCH_SLOTS; i++ )
        if( f->table[i].used )
            out[f->table[i].state == PS_FETCH_READY     ? 0
                : f->table[i].state == PS_FETCH_PENDING ? 1
                : f->table[i].state == PS_FETCH_MISSING ? 2
                                                        : 3]++;
    pthread_mutex_unlock(&f->lock);
}
