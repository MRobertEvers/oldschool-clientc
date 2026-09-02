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
#include "engine/uitree_scene_bridge.h"
#include "toridraw_scene.h"

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

static struct ToriRS_Sprite*
fake_drawable_sprite(void)
{
    struct ToriRS_Sprite* sprite = calloc(1, sizeof(*sprite));
    assert(sprite);
    sprite->frames = calloc(1, sizeof(*sprite->frames));
    assert(sprite->frames);
    sprite->frame_count = 1;
    sprite->frames[0].pixels_argb = malloc(sizeof(*sprite->frames[0].pixels_argb));
    assert(sprite->frames[0].pixels_argb);
    sprite->frames[0].pixels_argb[0] = 0xff55aa33u;
    sprite->frames[0].width = 1;
    sprite->frames[0].height = 1;
    sprite->frames[0].crop_width = 1;
    sprite->frames[0].crop_height = 1;
    return sprite;
}

static struct ToriRS_Objtype*
fake_objtype(char const* name)
{
    struct ToriRS_Objtype* obj = calloc(1, sizeof(*obj));
    assert(obj);
    obj->cert_link = -1;
    obj->cert_template = -1;
    obj->placeholder_link = -1;
    obj->placeholder_template = -1;
    strncpy(obj->name, name, sizeof(obj->name) - 1);
    return obj;
}

/* A provider arrival has to invalidate retained host answers before the bridge
 * has consumed it. Same-key publication and lazy genCert synthesis are equally
 * semantic even though their map cardinality does not change. */
static void
test_ui_asset_revision_tracks_provider_answers(void)
{
    struct CacheProvider provider = { 0 };
    struct ToriRS_Objtype* obj;
    struct ToriRS_Objtype* base;
    struct ToriRS_Objtype* note;
    uint64_t revision;
    size_t objtype_count;

    CacheProvider_InitEngineCaches(&provider);
    revision = CacheProvider_UIAssetRevision(&provider);
    CHECK(CacheProvider_ObjtypeGet(&provider, 42) == NULL, "missing objtype should stay missing");
    CHECK(
        CacheProvider_UIAssetRevision(&provider) == revision,
        "a provider miss must not mutate the UI asset revision");

    obj = fake_objtype("Trout");
    CacheProvider_ObjtypeAdd(&provider, 42, obj);
    CHECK(
        CacheProvider_UIAssetRevision(&provider) != revision,
        "objtype arrival did not advance UI asset revision");
    revision = CacheProvider_UIAssetRevision(&provider);
    CHECK(CacheProvider_ObjtypeGet(&provider, 42) == obj, "resident objtype lookup failed");
    CHECK(
        CacheProvider_UIAssetRevision(&provider) == revision,
        "ordinary resident lookup advanced UI asset revision");

    objtype_count = provider.objtype_cache->size;
    strncpy(obj->name, "Salmon", sizeof(obj->name) - 1);
    CacheProvider_ObjtypeAdd(&provider, 42, obj);
    CHECK(
        provider.objtype_cache->size == objtype_count,
        "same-key objtype publication changed map cardinality");
    CHECK(
        CacheProvider_UIAssetRevision(&provider) != revision,
        "same-key objtype publication did not advance revision");

    base = fake_objtype("Apple");
    note = fake_objtype("null");
    note->cert_link = 100;
    note->cert_template = 1;
    CacheProvider_ObjtypeAdd(&provider, 100, base);
    CacheProvider_ObjtypeAdd(&provider, 101, note);
    revision = CacheProvider_UIAssetRevision(&provider);
    CHECK(CacheProvider_ObjtypeGet(&provider, 101) == note, "note objtype lookup failed");
    CHECK(strcmp(note->name, "Apple") == 0, "lazy genCert did not publish linked name");
    CHECK(note->stackable == 1, "lazy genCert did not publish stackability");
    CHECK(
        CacheProvider_UIAssetRevision(&provider) != revision,
        "lazy genCert synthesis did not advance revision");
    revision = CacheProvider_UIAssetRevision(&provider);
    (void)CacheProvider_ObjtypeGet(&provider, 101);
    CHECK(
        CacheProvider_UIAssetRevision(&provider) == revision,
        "settled genCert lookup advanced revision again");

    revision = CacheProvider_UIAssetRevision(&provider);
    CacheProvider_ModelAdd(&provider, 7, fake_model());
    CHECK(
        CacheProvider_UIAssetRevision(&provider) != revision,
        "model arrival did not advance UI asset revision");
    revision = CacheProvider_UIAssetRevision(&provider);
    CacheProvider_FontAdd(&provider, 3, calloc(1, sizeof(struct ToriRS_Font)));
    CHECK(
        CacheProvider_UIAssetRevision(&provider) != revision,
        "font arrival did not advance UI asset revision");

    CacheProvider_FreeEngineCaches(&provider);
}

static void
test_ui_asset_revision_tracks_bridge_publication(void)
{
    struct CacheProvider provider = { 0 };
    struct UITreeSceneBridge bridge;
    struct ToriDraw_Scene* scene;
    uint64_t revision;
    int scene_id;

    CacheProvider_InitEngineCaches(&provider);
    CacheProvider_SpriteAdd(&provider, 77, fake_drawable_sprite());
    scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    UITreeSceneBridge_Init(&bridge, scene, &provider);

    revision = UITreeSceneBridge_AssetRevision(&bridge);
    scene_id = UITreeSceneBridge_EnsureSprite(&bridge, 77);
    CHECK(scene_id > 0, "bridge did not publish provider sprite");
    CHECK(
        UITreeSceneBridge_AssetRevision(&bridge) != revision,
        "bridge sprite publication did not advance revision");
    revision = UITreeSceneBridge_AssetRevision(&bridge);
    CHECK(
        UITreeSceneBridge_EnsureSprite(&bridge, 77) == scene_id,
        "memoized bridge sprite changed scene id");
    CHECK(
        UITreeSceneBridge_AssetRevision(&bridge) == revision,
        "memoized bridge sprite advanced revision");

    CHECK(
        UITreeSceneBridge_EnsureStaticSprite(&bridge, STATIC_SPRITE_SCROLLBAR, 77) == scene_id,
        "static sprite slot did not bind existing scene id");
    CHECK(
        UITreeSceneBridge_AssetRevision(&bridge) != revision,
        "static sprite slot publication did not advance revision");
    revision = UITreeSceneBridge_AssetRevision(&bridge);
    (void)UITreeSceneBridge_EnsureStaticSprite(&bridge, STATIC_SPRITE_SCROLLBAR, 77);
    CHECK(
        UITreeSceneBridge_AssetRevision(&bridge) == revision,
        "memoized static sprite slot advanced revision");

    UITreeSceneBridge_Free(&bridge);
    ToriDraw_SceneFree(scene);
    CacheProvider_FreeEngineCaches(&provider);
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

static struct ToriRS_MapTerrain*
fake_terrain(int map_id)
{
    struct ToriRS_MapTerrain* terrain = calloc(1, sizeof(*terrain));
    assert(terrain);
    terrain->map_x = map_id >> 8;
    terrain->map_z = map_id & 0xff;
    return terrain;
}

static struct ToriRS_MapLocs*
fake_locs(int map_id)
{
    struct ToriRS_MapLocs* locs = calloc(1, sizeof(*locs));
    assert(locs);
    locs->chunk_mapx = map_id >> 8;
    locs->chunk_mapz = map_id & 0xff;
    locs->locs_count = 1;
    locs->locs = calloc(1, sizeof(*locs->locs));
    assert(locs->locs);
    return locs;
}

/*
 * Map squares: the same LRU, keyed by the last build that read the square.
 * Before this trim existed a session walking the map kept every square it
 * had ever loaded, which is where "running around" grew the heap without
 * bound. A 3x3 the next build is about to read has to survive; the squares
 * that were only ever read many builds ago must not.
 */
static void
test_map_square_lru_keeps_recently_used(void)
{
    struct Dat2BuildCache* dat2_buildcache = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(dat2_buildcache);
    const int total = 100;
    const int recent_first = 10;
    const int recent_count = 9;
    int terrain_survivors = 0;
    int scenery_survivors = 0;
    int recent_survivors = 0;

    for( int i = 0; i < total; i++ )
    {
        CacheProvider_MapTerrainAdd(provider, i, fake_terrain(i));
        CacheProvider_MapSceneryAdd(provider, i, fake_locs(i));
    }
    CHECK(
        provider->map_terrain_cache->size == (size_t)total,
        "expected %d squares resident, got %zu",
        total,
        provider->map_terrain_cache->size);

    /* A build reads its squares through Get; that is what makes them recent. */
    for( int i = recent_first; i < recent_first + recent_count; i++ )
    {
        CHECK(CacheProvider_MapTerrainGet(provider, i) != NULL, "terrain %d absent", i);
        CHECK(CacheProvider_MapSceneryGet(provider, i) != NULL, "scenery %d absent", i);
    }

    CacheProvider_TrimDerivedCaches(provider);

    CHECK(
        provider->map_terrain_cache->size < (size_t)total,
        "square trim evicted no terrain (size still %zu)",
        provider->map_terrain_cache->size);
    CHECK(
        provider->map_scenery_cache->size < (size_t)total,
        "square trim evicted no scenery (size still %zu)",
        provider->map_scenery_cache->size);

    for( int i = 0; i < total; i++ )
    {
        int has_terrain = CacheProvider_MapTerrainHas(provider, i);
        int has_scenery = CacheProvider_MapSceneryHas(provider, i);
        terrain_survivors += has_terrain;
        scenery_survivors += has_scenery;
        if( i >= recent_first && i < recent_first + recent_count )
            recent_survivors += has_terrain && has_scenery;
    }
    CHECK(
        recent_survivors == recent_count,
        "every square the last build read should survive: %d of %d did",
        recent_survivors,
        recent_count);
    CHECK(
        (size_t)terrain_survivors == provider->map_terrain_cache->size,
        "walk found %d terrain survivors, map reports %zu",
        terrain_survivors,
        provider->map_terrain_cache->size);
    CHECK(
        (size_t)scenery_survivors == provider->map_scenery_cache->size,
        "walk found %d scenery survivors, map reports %zu",
        scenery_survivors,
        provider->map_scenery_cache->size);

    /* Below the keep threshold nothing moves: a second trim with the working
     * set already at size is a no-op, not a slow drain to zero. */
    {
        size_t before = provider->map_terrain_cache->size;
        CacheProvider_TrimDerivedCaches(provider);
        CHECK(
            provider->map_terrain_cache->size == before,
            "repeated square trim drained %zu -> %zu",
            before,
            provider->map_terrain_cache->size);
    }

    printf(
        "  map square trim: %d of %d squares kept, all %d recently read ones among them\n",
        terrain_survivors,
        total,
        recent_count);
    dat2_buildcache_free(dat2_buildcache);
}

int
main(void)
{
    printf("cache_trim_test\n");
    test_model_lru_keeps_recently_used();
    test_map_square_lru_keeps_recently_used();
    test_trim_below_threshold_is_a_noop();
    test_repeated_trim_is_stable();
    test_sprite_lru_independent_of_models();
    test_ui_asset_revision_tracks_provider_answers();
    test_ui_asset_revision_tracks_bridge_publication();

    if( g_failures )
    {
        printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
