/*
 * A world entity's (hull's) sequences are wanted where they change, not every
 * frame (app/app_wev.c), as the world does for players and npcs
 * (World_SeqWant):
 *
 *   - a spawn wants its config's idle bob once;
 *   - steady frames (app_wev_advance_bobs) want nothing, however long the
 *     sequence takes to load, or if it never does;
 *   - a one-shot sent for the hull (app_wev_set_seq, WORLDENTITY_INFO's seq)
 *     is wanted once, and frames that play or wait for it want nothing;
 *   - the one-shot's end (back to the idle) wants nothing either: the idle
 *     was wanted at the spawn.
 *
 * The sequence loader is a fake that answers "here" without making anything
 * resident, so the table keeps no entry and every want reaches it: the count
 * of loads it started is the count of wants.
 *
 * Run: make -C src test-wev-seq-wants
 */

#include "app.h"
#include "app/app_internal.h"
#include "engine/dat2/dat2_buildcache.h"
#include "platform/platform_x_io.h"
#include "world/wev.h"
#include "world/world.h"
#include "world/worldview.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define T_ASSERT(cond, what)                                                                       \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define IDLE_SEQ 9001
#define ONESHOT_SEQ 9002
#define CONFIG_ID 1

static struct App* g_app;
static int g_seq_asked[2];

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    (void)data;
    (void)assets;
    assert(key.kind == ASSET_KIND_SEQUENCE);
    if( key.id == IDLE_SEQ )
        g_seq_asked[0]++;
    else if( key.id == ONESHOT_SEQ )
        g_seq_asked[1]++;
    /* "Here", with nothing made resident: the table keeps no entry, so the
     * next want comes back to this loader. */
    return ASSET_START_HERE;
}

static bool
fake_resident(
    void* data,
    struct AssetKey key)
{
    (void)data;
    (void)key;
    return false;
}

static uint32_t
wants(void)
{
    return Assets_LoadsStarted(g_app->assets, ASSET_KIND_SEQUENCE);
}

static void
fixture_init(void)
{
    struct App* app;
    struct AssetLoader loader;
    struct ToriDraw_Scene* scene;
    struct Dat2BuildCache* bc;
    struct World* root;
    struct WorldBuilder* root_builder;
    struct WevConfig* configs;

    ToriDraw_Init();
    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    assert(scene);
    bc = dat2_buildcache_new();
    assert(bc);
    app = calloc(1, sizeof(*app));
    assert(app);
    g_app = app;
    app->features.name = "test";
    app->cfg.cache_kind = APP_CACHE_DAT2;
    app->provider = dat2_buildcache_as_provider(bc);
    app->scene = scene;
    VarPManager_Init(&app->varps);
    RS_EntitySync_Init(&app->esync);
    App_AssetsInit(app);
    App_PendingInit(app);
    loader.start = fake_start;
    loader.resident = fake_resident;
    loader.data = NULL;
    Assets_SetLoader(app->assets, ASSET_KIND_SEQUENCE, &loader);
    WorldSeqSourceToriDraw_Bind(&app->seq_source, scene, app->provider);

    root = World_New();
    assert(root);
    World_SetScene(root, scene);
    root_builder = WorldBuilder_New(root, app->provider, scene, &app->varps);
    assert(root_builder);
    WorldBuilder_RebuildCenterzoneBegin(root_builder, 50, 50, 64);
    World_SetLoadComplete(root, true);
    app->world = root;
    app->world_builder = root_builder;
    WorldviewRegistry_Init(&app->worldviews);
    WorldviewRegistry_RegisterRoot(&app->worldviews, root, root_builder);

    /* One hull config, ids 0 (a hole) and 1, idling on IDLE_SEQ. */
    Wevs_Init(&app->wevs);
    WevConfigTable_Init(&app->wev_configs);
    configs = calloc(CONFIG_ID + 1, sizeof(*configs));
    assert(configs);
    configs[0].id = -1;
    configs[CONFIG_ID].id = CONFIG_ID;
    configs[CONFIG_ID].anim_id = IDLE_SEQ;
    WevConfigTable_Set(&app->wev_configs, configs, CONFIG_ID + 1);
}

static void
frames(int n)
{
    for( int i = 0; i < n; i++ )
    {
        g_app->wevs.clock += 1.0;
        app_wev_advance_bobs(g_app);
    }
}

static void
test_a_hull_wants_its_sequences_where_they_change(void)
{
    struct Wev* wev;
    uint32_t before;

    printf("wev seq wants: a spawn wants its idle once, steady frames want nothing\n");
    before = wants();
    wev = App_WevSpawn(g_app, 4242, CONFIG_ID, 16, 16, 0, 6400, 6400, 0, 0);
    T_ASSERT(wev != NULL, "the hull spawned");
    if( !wev )
        return;
    T_ASSERT(wants() - before == 1, "the spawn wanted its idle once");
    T_ASSERT(g_seq_asked[0] == 1, "...and it was the config's idle");

    before = wants();
    frames(50);
    printf("  50 frames with the idle not resident: %u wants\n", (unsigned)(wants() - before));
    T_ASSERT(wants() - before == 0, "50 frames waiting for the idle wanted nothing");

    printf("wev seq wants: a one-shot is wanted once where it is set\n");
    before = wants();
    app_wev_set_seq(g_app, wev, ONESHOT_SEQ, 3);
    T_ASSERT(wants() - before == 1, "the one-shot was wanted once");
    T_ASSERT(g_seq_asked[1] == 1, "...and it was the one-shot");
    before = wants();
    frames(50);
    printf("  50 frames with the one-shot not resident: %u wants\n", (unsigned)(wants() - before));
    T_ASSERT(wants() - before == 0, "50 frames waiting for the one-shot wanted nothing");
    T_ASSERT(wev->seq_id == ONESHOT_SEQ, "the one-shot is still the hull's (it never played)");

    printf("wev seq wants: no sequence (-1) wants nothing\n");
    before = wants();
    app_wev_set_seq(g_app, wev, -1, 0);
    frames(5);
    T_ASSERT(wants() - before == 0, "a cleared one-shot wanted nothing");
}

int
main(void)
{
    fixture_init();
    test_a_hull_wants_its_sequences_where_they_change();
    if( g_failures )
    {
        printf("wev seq wants: %d FAILED\n", g_failures);
        return 1;
    }
    printf("wev seq wants: ALL PASS\n");
    return 0;
}
