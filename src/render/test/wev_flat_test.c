/*
 * The flatten strategy table (render/wev_flat.h) without an App: the name
 * lookup, the frame transform each strategy writes, and a hull bound through
 * each strategy against a real shared scene and worldview registry.
 *
 * The deck here is an empty World, so the outline strategy builds nothing --
 * it must say so (no element claimed, no element bound) and must not rebuild
 * on a second bind of the same geometry serial.
 */
#include "render/wev_flat.h"

#include "engine/world_builder/world_builder.h"
#include "mem/torirs_mem.h"
#include "painters/painters.h"
#include "render/torirs_frame.h"
#include "toridraw_scene.h"
#include "world/wev.h"
#include "world/world.h"
#include "world/worldview.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_ops_by_name(void)
{
    CHECK(WevFlat_OpsByName(NULL) == &g_wev_flat_outline, "no name selects outline");
    CHECK(WevFlat_OpsByName("") == &g_wev_flat_outline, "an empty name selects outline");
    CHECK(WevFlat_OpsByName("live") == &g_wev_flat_live, "\"live\" selects live");
    CHECK(WevFlat_OpsByName("outline") == &g_wev_flat_outline, "\"outline\" selects outline");
    CHECK(strcmp(g_wev_flat_live.name, "live") == 0, "live is named live");
    CHECK(strcmp(g_wev_flat_outline.name, "outline") == 0, "outline is named outline");
}

static void
test_bind_frame(void)
{
    struct WevConfig cfg;
    struct Wev wev;
    struct ToriRS_FrameViewXform xf;
    struct WevFlat flat = { 0 };

    memset(&cfg, 0, sizeof(cfg));
    memset(&wev, 0, sizeof(wev));
    cfg.flat_hsl = 1234;
    wev.id = 1;
    wev.config = &cfg;

    WevFlat_Init(&flat, &g_wev_flat_live);
    CHECK(flat.ops == &g_wev_flat_live, "init keeps the table");
    CHECK(flat.state == NULL, "live keeps no state");
    memset(&xf, 0, sizeof(xf));
    flat.ops->bind_frame(flat.state, &wev, &xf);
    CHECK(xf.flatten_scale == 0.01f, "live: Y scale 0.01");
    CHECK(xf.flatten_y_offset == -1200, "live: offset -1200");
    CHECK(xf.flat_hsl == 1234, "live: the config's flat HSL");
    CHECK(xf.flat, "live: flat");
    WevFlat_Free(&flat, NULL);
    CHECK(flat.ops == NULL && flat.state == NULL, "free clears the table");
    WevFlat_Free(&flat, NULL); /* a freed flat frees again as a no-op */

    WevFlat_Init(&flat, &g_wev_flat_outline);
    CHECK(flat.state != NULL, "outline keeps state");
    memset(&xf, 0, sizeof(xf));
    flat.ops->bind_frame(flat.state, &wev, &xf);
    CHECK(xf.flatten_scale == 0.01f, "outline: Y scale 0.01");
    CHECK(xf.flatten_y_offset == -1200, "outline: offset -1200");
    CHECK(xf.flat_hsl == -1, "outline: no per-draw recolour");
    CHECK(xf.flat, "outline: flat");
    {
        int ids[4] = { 7, 7, 7, 7 };
        CHECK(flat.ops->claim_elements(flat.state, ids, 4) == 0, "outline: nothing built, nothing claimed");
    }
    /* Nothing built: neither a release nor the free needs a scene. */
    flat.ops->release_view(flat.state, NULL, 1);
    WevFlat_Free(&flat, NULL);
    CHECK(flat.ops == NULL, "outline free clears the table");
}

static void
test_bind_painter(struct WevFlatOps const* ops)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    struct WorldviewRegistry reg;
    struct World* root = World_New();
    struct World* deck = World_New();
    struct WevConfig cfg;
    struct Wev wev;
    struct WevFlat flat = { 0 };
    struct WorldBuilder* root_builder;
    int ids[4];

    assert(scene);
    World_ResetScene(root, 50, 50, 16);
    World_SetLoadComplete(root, true);
    World_ResetScene(deck, 0, 0, 8);
    World_SetLoadComplete(deck, true);
    WorldviewRegistry_Init(&reg);
    root_builder = WorldBuilder_New(root, NULL, scene, NULL);
    WorldviewRegistry_RegisterRoot(&reg, root, root_builder);
    WorldviewRegistry_Register(
        &reg, 1, 1, deck, WorldBuilder_New(deck, NULL, scene, NULL), 0, 0, 8, 8, WORLDVIEW_ROOT);
    CHECK(WorldviewRegistry_IsLive(&reg, 1), "the deck view is live");

    memset(&cfg, 0, sizeof(cfg));
    memset(&wev, 0, sizeof(wev));
    cfg.flat_hsl = 99;
    wev.live = true;
    wev.id = 1;
    wev.config = &cfg;
    wev.flattened = true;

    WevFlat_Init(&flat, ops);
    flat.ops->bind_painter(flat.state, scene, &reg, &wev, root->painter, 3, 4, false);
    flat.ops->bind_painter(flat.state, scene, &reg, &wev, root->painter, 3, 4, false);
    CHECK(flat.ops->claim_elements(flat.state, ids, 4) == 0, "an empty deck claims no element");
    flat.ops->release_view(flat.state, scene, 1);
    WevFlat_Free(&flat, scene);

    WorldviewRegistry_Free(&reg); /* frees the deck and its builder */
    WorldBuilder_Free(root_builder);
    World_Free(root);
    ToriDraw_SceneFree(scene);
}

int
main(void)
{
    test_ops_by_name();
    test_bind_frame();
    test_bind_painter(&g_wev_flat_live);
    test_bind_painter(&g_wev_flat_outline);
    if( g_failures )
    {
        fprintf(stderr, "wev_flat: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("wev_flat: name lookup, both frame transforms, both strategies bound against a scene and registry PASS");
    return 0;
}
