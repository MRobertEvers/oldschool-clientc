/*
 * CacheProvider_TrimDerivedCaches — LRU eviction of the model/sprite caches.
 *
 * The trim only fires when a session has accumulated more than one scene's
 * worth of assets, which an offline boot never does, so without this the
 * eviction path would ship unexercised. Everything here is in-memory: the
 * provider is built by dat2_buildcache_new and fed hand-made models, so no
 * cache on disk is involved.
 */

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                                                       \
    do                                                                         \
    {                                                                          \
        if( !(cond) )                                                          \
        {                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            g_failures++;                                                      \
        }                                                                      \
    } while( 0 )

/* A model small enough to make thousands of them, real enough to be freed by
 * ToriRS_ModelFree (which walks the owned arrays). */
static struct ToriRS_Model*
fake_model(void)
{
    struct ToriRS_Model* model = calloc(1, sizeof(*model));
    assert(model);
    model->vertex_count = 1;
    model->face_count = 0;
    model->vertices_x = calloc(1, sizeof(*model->vertices_x));
    model->vertices_y = calloc(1, sizeof(*model->vertices_y));
    model->vertices_z = calloc(1, sizeof(*model->vertices_z));
    assert(model->vertices_x && model->vertices_y && model->vertices_z);
    return model;
}

static struct ToriRS_Sprite*
fake_sprite(void)
{
    struct ToriRS_Sprite* sprite = calloc(1, sizeof(*sprite));
    assert(sprite);
    return sprite;
}

/*
 * Fill past the keep threshold, touch a chosen subset, trim, and check that
 * exactly the touched entries survived. "Touched" is the whole point: an
 * eviction that ignored recency would pass a count check and still throw away
 * the models the next build is about to ask for.
 */
static void
test_model_lru_keeps_recently_used(void)
{
    struct Dat2BuildCache* dat2_buildcache = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(dat2_buildcache);
    const int total = 4000;
    const int recent_first = 100;
    const int recent_count = 200;
    int survivors = 0;
    int recent_survivors = 0;

    for( int i = 0; i < total; i++ )
        CacheProvider_ModelAdd(provider, i, fake_model());
    CHECK(
        provider->model_cache->size == (size_t)total,
        "expected %d models resident, got %zu",
        total,
        provider->model_cache->size);

    /* Re-reading makes these the most recent, even though they were inserted
     * first — which is the difference between an LRU and a FIFO. */
    for( int i = recent_first; i < recent_first + recent_count; i++ )
        CHECK(
            CacheProvider_ModelGet(provider, i) != NULL,
            "model %d should be resident before the trim",
            i);

    CacheProvider_TrimDerivedCaches(provider);

    CHECK(
        provider->model_cache->size < (size_t)total,
        "trim evicted nothing (size still %zu)",
        provider->model_cache->size);

    for( int i = 0; i < total; i++ )
    {
        if( !CacheProvider_ModelHas(provider, i) )
            continue;
        survivors++;
        if( i >= recent_first && i < recent_first + recent_count )
            recent_survivors++;
    }

    CHECK(
        recent_survivors == recent_count,
        "every touched model should survive: %d of %d did",
        recent_survivors,
        recent_count);
    CHECK(
        (size_t)survivors == provider->model_cache->size,
        "walk found %d survivors, map reports %zu",
        survivors,
        provider->model_cache->size);
    printf(
        "  model trim: %d -> %d resident, all %d touched kept\n",
        total,
        survivors,
        recent_survivors);

    dat2_buildcache_free(dat2_buildcache);
}

/* Below the threshold nothing may be dropped — a trim between two small builds
 * must not cost the next one its warm cache. */
static void
test_trim_below_threshold_is_a_noop(void)
{
    struct Dat2BuildCache* dat2_buildcache = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(dat2_buildcache);
    const int total = 400; /* about what one osrs230 square preloads */

    for( int i = 0; i < total; i++ )
        CacheProvider_ModelAdd(provider, i, fake_model());
    for( int i = 0; i < total; i++ )
        CacheProvider_SpriteAdd(provider, i, fake_sprite());

    CacheProvider_TrimDerivedCaches(provider);

    for( int i = 0; i < total; i++ )
    {
        CHECK(CacheProvider_ModelHas(provider, i), "model %d was evicted", i);
        CHECK(CacheProvider_SpriteHas(provider, i), "sprite %d was evicted", i);
    }
    printf("  below-threshold trim: all %d models and sprites kept\n", total);

    dat2_buildcache_free(dat2_buildcache);
}

/* A trim must be repeatable: running it twice with no traffic in between
 * settles rather than eroding the cache further each time. */
static void
test_repeated_trim_is_stable(void)
{
    struct Dat2BuildCache* dat2_buildcache = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(dat2_buildcache);
    size_t after_first;
    size_t after_second;

    for( int i = 0; i < 4000; i++ )
        CacheProvider_ModelAdd(provider, i, fake_model());

    CacheProvider_TrimDerivedCaches(provider);
    after_first = provider->model_cache->size;
    CacheProvider_TrimDerivedCaches(provider);
    after_second = provider->model_cache->size;

    CHECK(
        after_first == after_second,
        "second trim removed more (%zu -> %zu)",
        after_first,
        after_second);
    printf("  repeated trim: stable at %zu resident\n", after_second);

    dat2_buildcache_free(dat2_buildcache);
}

/* Sprites use the same clock as models; evicting one must not disturb the
 * other's recency, since a UI bake and a world build touch them separately. */
static void
test_sprite_lru_independent_of_models(void)
{
    struct Dat2BuildCache* dat2_buildcache = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(dat2_buildcache);
    const int sprites = 3000;

    for( int i = 0; i < sprites; i++ )
        CacheProvider_SpriteAdd(provider, i, fake_sprite());
    for( int i = 0; i < 200; i++ )
        CacheProvider_ModelAdd(provider, i, fake_model());

    /* Touch the oldest sprites after every model insert, so recency and
     * insertion order disagree. */
    for( int i = 0; i < 300; i++ )
        (void)CacheProvider_SpriteGet(provider, i);

    CacheProvider_TrimDerivedCaches(provider);

    for( int i = 0; i < 300; i++ )
        CHECK(CacheProvider_SpriteHas(provider, i), "touched sprite %d evicted", i);
    for( int i = 0; i < 200; i++ )
        CHECK(CacheProvider_ModelHas(provider, i), "model %d evicted below threshold", i);
    printf("  sprite trim: 300 touched sprites kept, models untouched\n");

    dat2_buildcache_free(dat2_buildcache);
}

int
main(void)
{
    printf("cache_trim_test\n");
    test_model_lru_keeps_recently_used();
    test_trim_below_threshold_is_a_noop();
    test_repeated_trim_is_stable();
    test_sprite_lru_independent_of_models();

    if( g_failures )
    {
        printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
