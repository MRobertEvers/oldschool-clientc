#include "rs_component_state.h"

#include "graphics/dash.h"
#include "graphics/dashmap.h"
#include "osrs/buildcachedat.h"
#include "osrs/rscache/tables_dat/config_component.h"

#include <stdlib.h>
#include <string.h>

void
rs_component_state_seed_from_buildcachedat(
    struct RSComponentStatePool* pool,
    struct BuildCacheDat* buildcachedat)
{
    if( !pool || !buildcachedat || !buildcachedat->component_hmap )
        return;

    struct DashMapIter* iter = buildcachedat_component_iter_new(buildcachedat);
    if( !iter )
        return;

    int id = 0;
    struct CacheDatConfigComponent* c = NULL;
    while( (c = buildcachedat_component_iter_next(iter, &id)) != NULL )
    {
        if( c->type != COMPONENT_TYPE_MODEL )
            continue;
        struct RSModelDynState* m = rs_model_state(pool, id);
        if( m )
        {
            m->seq_frame = c->seqFrame;
            m->seq_cycle = c->seqCycle;
        }
    }
    dashmap_iter_free(iter);
}

struct RSComponentStatePool*
rs_component_state_pool_new(int max_component_id_exclusive)
{
    if( max_component_id_exclusive <= 0 )
        return NULL;

    struct RSComponentStatePool* pool = malloc(sizeof(struct RSComponentStatePool));
    if( !pool )
        return NULL;
    memset(pool, 0, sizeof(struct RSComponentStatePool));
    pool->count = max_component_id_exclusive;

    size_t n = (size_t)max_component_id_exclusive;
    pool->layer = calloc(n, sizeof(struct RSLayerDynState));
    pool->model = calloc(n, sizeof(struct RSModelDynState));
    pool->text = calloc(n, sizeof(struct RSTextDynState));
    pool->inv = calloc(n, sizeof(struct RSInvDynState));
    if( !pool->layer || !pool->model || !pool->text || !pool->inv )
    {
        rs_component_state_pool_free(pool);
        return NULL;
    }
    return pool;
}

void
rs_component_state_pool_free(struct RSComponentStatePool* pool)
{
    if( !pool )
        return;
    if( pool->layer )
    {
        for( int i = 0; i < pool->count; i++ )
            dashsprite_free(pool->layer[i].cached_sprite);
    }
    if( pool->model )
    {
        for( int i = 0; i < pool->count; i++ )
            dashsprite_free(pool->model[i].cached_sprite);
    }
    if( pool->text )
    {
        for( int i = 0; i < pool->count; i++ )
        {
            free(pool->text[i].text_override);
            dashsprite_free(pool->text[i].cached_sprite);
        }
    }
    if( pool->inv )
    {
        for( int i = 0; i < pool->count; i++ )
            dashsprite_free(pool->inv[i].cached_sprite);
    }
    free(pool->layer);
    free(pool->model);
    free(pool->text);
    free(pool->inv);
    free(pool);
}

struct RSLayerDynState*
rs_layer_state(
    struct RSComponentStatePool* pool,
    int component_id)
{
    if( !pool || component_id < 0 || component_id >= pool->count )
        return NULL;
    return &pool->layer[component_id];
}

struct RSModelDynState*
rs_model_state(
    struct RSComponentStatePool* pool,
    int component_id)
{
    if( !pool || component_id < 0 || component_id >= pool->count )
        return NULL;
    return &pool->model[component_id];
}

struct RSTextDynState*
rs_text_state(
    struct RSComponentStatePool* pool,
    int component_id)
{
    if( !pool || component_id < 0 || component_id >= pool->count )
        return NULL;
    return &pool->text[component_id];
}

struct RSInvDynState*
rs_inv_state(
    struct RSComponentStatePool* pool,
    int component_id)
{
    if( !pool || component_id < 0 || component_id >= pool->count )
        return NULL;
    return &pool->inv[component_id];
}
