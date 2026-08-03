#include "engine/torirs_model_inst_cache.h"

#include "perf/torirs_perf.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Capacities match Client-TS LruCache sizes. */
static uint32_t const g_kind_keep[TORIRS_MODEL_INST_KIND_COUNT] = {
    30,  /* spot */
    30,  /* npc */
    500, /* loc base */
    30,  /* loc shape */
    50,  /* obj */
    200, /* player */
};

bool
TorirsModelInstCache_Init(struct TorirsModelInstCache* cache)
{
    int i;
    assert(cache);
    memset(cache, 0, sizeof(*cache));
    for( i = 0; i < TORIRS_MODEL_INST_KIND_COUNT; i++ )
    {
        if( !TorirsLru_Init(&cache->lrus[i], g_kind_keep[i] * 2u, g_kind_keep[i]) )
        {
            TorirsModelInstCache_Free(cache);
            return false;
        }
    }
    cache->inited = 1;
    return true;
}

static void
torirs_model_inst_free_entry(struct TorirsModelInstEntry* e)
{
    if( !e )
        return;
    if( e->model )
        ToriDraw_ModelFree(e->model);
    free(e);
}

void
TorirsModelInstCache_Clear(struct TorirsModelInstCache* cache)
{
    int k;
    assert(cache);
    if( !cache->inited )
        return;
    for( k = 0; k < TORIRS_MODEL_INST_KIND_COUNT; k++ )
    {
        struct TorirsLruNode* n;
        while( (n = TorirsLru_EvictOldest(&cache->lrus[k])) != NULL )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_MODEL_INST_EVICT, 1);
            torirs_model_inst_free_entry((struct TorirsModelInstEntry*)n);
        }
    }
}

void
TorirsModelInstCache_Free(struct TorirsModelInstCache* cache)
{
    int k;
    if( !cache )
        return;
    TorirsModelInstCache_Clear(cache);
    for( k = 0; k < TORIRS_MODEL_INST_KIND_COUNT; k++ )
        TorirsLru_Free(&cache->lrus[k]);
    cache->inited = 0;
}

struct ToriDraw_Model*
TorirsModelInstCache_Get(
    struct TorirsModelInstCache* cache,
    enum TorirsModelInstKind kind,
    int64_t key)
{
    struct TorirsLruNode* n;
    assert(cache && cache->inited);
    assert(kind >= 0 && kind < TORIRS_MODEL_INST_KIND_COUNT);
    n = TorirsLru_Find(&cache->lrus[kind], key);
    if( !n )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_MODEL_INST_MISS, 1);
        return NULL;
    }
    TorirsLru_Touch(&cache->lrus[kind], n);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_MODEL_INST_HIT, 1);
    return ((struct TorirsModelInstEntry*)n)->model;
}

void
TorirsModelInstCache_Put(
    struct TorirsModelInstCache* cache,
    enum TorirsModelInstKind kind,
    int64_t key,
    struct ToriDraw_Model* model)
{
    struct TorirsModelInstEntry* e;
    struct TorirsLruNode* evicted = NULL;
    assert(cache && cache->inited && model);
    assert(kind >= 0 && kind < TORIRS_MODEL_INST_KIND_COUNT);

    /* Replace existing. */
    {
        struct TorirsLruNode* existing = TorirsLru_Find(&cache->lrus[kind], key);
        if( existing )
        {
            TorirsLru_Remove(&cache->lrus[kind], existing);
            torirs_model_inst_free_entry((struct TorirsModelInstEntry*)existing);
        }
    }

    e = (struct TorirsModelInstEntry*)calloc(1, sizeof(*e));
    if( !e )
    {
        ToriDraw_ModelFree(model);
        return;
    }
    e->model = model;
    e->node.map_slot = -1;
    TorirsLru_Insert(&cache->lrus[kind], &e->node, key, &evicted);
    if( evicted )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_MODEL_INST_EVICT, 1);
        torirs_model_inst_free_entry((struct TorirsModelInstEntry*)evicted);
    }
}

struct ToriDraw_Model*
TorirsModelInstCache_CopyGet(
    struct TorirsModelInstCache* cache,
    enum TorirsModelInstKind kind,
    int64_t key)
{
    struct ToriDraw_Model* base = TorirsModelInstCache_Get(cache, kind, key);
    if( !base )
        return NULL;
    return ToriDraw_ModelCopy(base);
}
