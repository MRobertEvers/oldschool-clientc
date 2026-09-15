/*
 * The level guard in ToriRS_PickHitsClassify, pinned against a bridge deck.
 *
 * A terrain hit reports the MESH level — which cache level's floor the painter
 * drew — and on a LinkBelow column that is one above the level the player
 * standing on it is on. Comparing the mesh level against the player's level
 * therefore threw away every hit on a bridge deck: the Theatre of Blood's
 * corridors are one long deck and none of their ground could be clicked. The
 * reference compares the tile's DRAW level instead (deob class155
 * method5213/5214 guard on `field2292 <= plane`, field2292 being the draw level
 * method4161 answers), which is what these cases hold the port to.
 *
 * Mutation check: replace World_TerrainDrawLevel's body with `return
 * mesh_level;` (the defect) and the bridge and VisBelow cases go red; make it
 * `return 0` and the genuine-upper-floor case goes red.
 *
 * The same defect had a second half nobody had reached: a LOC on that deck.
 * Scenery carries the cache level it was authored on, and the guard compared it
 * raw — so the Theatre's arena barriers, authored on plane 1 above a LinkBelow
 * column, drew and animated in front of a player on level 0 and refused every
 * click. World_LocPaintLevel is the loc's answer to World_TerrainDrawLevel, and
 * is the shuffle the build itself registered the geometry with.
 *
 * Mutation check for that half: drop the World_LocPaintLevel call in the
 * classifier and "a loc on the deck under your feet" goes red; apply it to
 * every pick type and the npc/obj cases below a deck would follow.
 *
 * The third part pins the other gate in the same classifier: whether an
 * INACTIVE loc — a wall, a fence, ground decor, anything carrying no ops —
 * survives to the pickset. It must not by default (the reference records no
 * hit for a non-active typecode) and must while a loc-inspection tool is
 * running, or the footprint outline and the loc editor can only ever see the
 * handful of scenery that happens to be interactive.
 */
#include "render/torirs_pick.h"

#include "world/world.h"
#include "world/entity_scenery.h"
#include "world/world_pickset.h"
#include "world/worldview.h"
#include "world/wev.h"

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

#define SCENE_SIZE 64

static void
set_tile_flag(
    struct World* world,
    int x,
    int z,
    int level,
    unsigned char flags)
{
    world->tile_flags[x + z * SCENE_SIZE + level * SCENE_SIZE * SCENE_SIZE] = flags;
}

/* One terrain hit at (x, z) whose mesh sits on `mesh_level`, classified for a
 * player standing on `player_level`. Returns whether it survived the guard. */
static int
terrain_hit_survives(
    struct World* world,
    int x,
    int z,
    int mesh_level,
    int player_level)
{
    struct ToriRS_PickHits hits;
    struct World_PickSet pickset;
    struct ToriRS_PickResult result;

    ToriRS_PickHitsReset(&hits);
    ToriRS_PickHitsAdd(&hits, /* element_id */ 7, /* is_terrain */ true, x, z, mesh_level, 0);
    ToriRS_PickHitsClassify(world, NULL, &hits, player_level, &pickset, &result);
    return pickset.count == 1 && pickset.items[0].type == WORLD_PICK_TERRAIN &&
           pickset.items[0].tile_x == x && pickset.items[0].tile_z == z;
}

static void test_boat_click_modes(void)
{
    puts("TEST: native boat click modes and visibility");
    struct World* root = World_New();
    struct World* deck = World_New();
    World_ResetScene(root,50,50,64); World_SetLoadComplete(root,true);
    World_ResetScene(deck,480,800,8); World_SetLoadComplete(deck,true);
    char ops[5][32] = {"Steer"};
    World_SceneryRegister(deck,777,4242,3,4,1,1,1,10,0,0,"Helm",
                          (const char(*)[32])ops,1);
    struct WorldviewRegistry views = {0};
    views.views[1].live = true; views.views[1].world = deck;
    struct WevConfig cfg = {.click_mode=2};
    struct Wevs boats; Wevs_Init(&boats);
    struct Wev* boat=Wevs_Spawn(&boats,1,0,&cfg,1,3072*128,3160*128,0,0,31);
    struct ToriRS_PickHits hits;
    struct World_PickSet picks;
    struct ToriRS_PickResult result;
    ToriRS_PickHitsReset(&hits);
    ToriRS_PickHitsAdd(&hits,20,true,5,5,0,0);
    ToriRS_PickHitsAdd(&hits,888,false,-1,-1,-1,1); /* inert hull */
    ToriRS_PickHitsAdd(&hits,777,false,-1,-1,-1,1); /* native helm */
#define CLASSIFY(aboard) ToriRS_PickHitsClassifyViews(root,&views,&boats,aboard,&hits,0,&picks,&result)
    cfg.click_mode=0; CLASSIFY(0);
    CHECK(picks.count==2 && picks.items[1].type==WORLD_PICK_WEV,
          "mode0 replaces interactive and inert hits with one hull target");
    cfg.click_mode=1; CLASSIFY(0);
    CHECK(picks.count==2 && picks.items[1].type==WORLD_PICK_SCENERY,
          "mode1 keeps native contents and drops inert hull");
    cfg.click_mode=2; CLASSIFY(0);
    CHECK(picks.count==3 && picks.items[1].type==WORLD_PICK_WEV &&
          picks.items[2].type==WORLD_PICK_SCENERY,
          "mode2 falls back to boat only for noninteractive geometry");
    cfg.click_mode=3; CLASSIFY(0);
    CHECK(picks.count==0 && !result.hover_tile_valid,
          "mode3 swallows underlying root picks and hover");
    ToriRS_PickHitsAdd(&hits,21,true,6,5,0,0); CLASSIFY(0);
    CHECK(picks.count==1 && result.hover_tile_x==6,
          "a root surface nearer than blocker remains clickable");
    hits.count=3; CLASSIFY(1);
    CHECK(picks.count==2 && picks.items[1].type==WORLD_PICK_SCENERY,
          "aboard forces contents-only even for blocker configuration");
    boat->flattened=true; CLASSIFY(1);
    CHECK(picks.count==1 && picks.items[0].view_id==0,
          "flattened views contribute no actor, hull or scenery picks");
    boat->flattened=false; boat->render_visible=false; CLASSIFY(1);
    CHECK(picks.count==1,"budget-skipped views contribute no picks");
    boat->render_visible=true; cfg.click_mode=1;
    ToriRS_PickHitsReset(&hits);
    ToriRS_PickHitsAdd(&hits,55,true,3,4,1,1); CLASSIFY(1);
    CHECK(picks.count==1 && result.hover_view_valid && result.hover_view==1 &&
          result.hover_view_x==3 && result.hover_view_z==4,
          "aboard terrain remains in its deck coordinate frame");
#undef CLASSIFY
    World_Free(deck); World_Free(root);
}

int
main(void)
{
    test_boat_click_modes();
    struct World* world = World_New();

    int const bridge_x = 20; /* LinkBelow column: deck geometry on cache 1 */
    int const bridge_z = 30;
    int const flat_x = 21; /* ordinary column: a real upper storey on cache 1 */
    int const flat_z = 30;
    int const vis_x = 22; /* VisBelow tile on cache 2, revealed from level 0 */
    int const vis_z = 30;

    World_ResetScene(world, 50, 50, SCENE_SIZE);
    World_SetLoadComplete(world, true);

    /* LinkBelow is read at cache level 1 and speaks for the whole column. */
    set_tile_flag(world, bridge_x, bridge_z, 1, 0x02 /* LINK_BELOW */);
    set_tile_flag(world, vis_x, vis_z, 2, 0x08 /* VIS_BELOW */);

    printf("TEST: terrain pick level guard\n");

    CHECK(terrain_hit_survives(world, bridge_x, bridge_z, 1, 0),
          "the deck under your feet is clickable from level 0");
    CHECK(!terrain_hit_survives(world, flat_x, flat_z, 1, 0),
          "a real upper storey is not clickable from below");
    CHECK(terrain_hit_survives(world, vis_x, vis_z, 2, 0),
          "a VisBelow tile draws at level 0 and picks there");
    CHECK(terrain_hit_survives(world, flat_x, flat_z, 0, 0), "your own level always picks");
    CHECK(terrain_hit_survives(world, flat_x, flat_z, 0, 2),
          "ground below you picks — the guard is one-sided");
    CHECK(!terrain_hit_survives(world, bridge_x, bridge_z, 2, 0),
          "the deck's own upper storey is still refused from the deck");
    CHECK(terrain_hit_survives(world, bridge_x, bridge_z, 2, 1),
          "and is clickable once you are standing on it");

    printf("TEST: column readout text\n");
    {
        char buf[64];

        /* One group per cache level, in bit order, `-` for a zero byte. Three
         * consumers render this now (the Walk-here row, the loc's own tile row
         * and the loc editor's terrain panel), which is why it is one function
         * and why its exact spelling is pinned. */
        World_TileSettingsText(world, bridge_x, bridge_z, buf, (int)sizeof(buf));
        CHECK(strcmp(buf, "-|L|-|-") == 0, "a bridge column spells LinkBelow on level 1");
        World_TileSettingsText(world, vis_x, vis_z, buf, (int)sizeof(buf));
        CHECK(strcmp(buf, "-|-|V|-") == 0, "a VisBelow tile spells V on its own level");
        World_TileSettingsText(world, flat_x, flat_z, buf, (int)sizeof(buf));
        CHECK(strcmp(buf, "-|-|-|-") == 0, "a plain column is four dashes");

        /* No terrain was registered in this fixture, so every column is
         * floorless — which is the state that must read as "-" rather than as
         * an empty string a caller would print as nothing at all. */
        World_TerrainMeshLevelsText(world, flat_x, flat_z, buf, (int)sizeof(buf));
        CHECK(strcmp(buf, "-") == 0, "a floorless column says so");

        /* Truncation must still terminate: a two-byte buffer holds one char. */
        World_TileSettingsText(world, bridge_x, bridge_z, buf, 2);
        CHECK(strcmp(buf, "-") == 0, "a short buffer truncates and stays terminated");
    }

    printf("TEST: inactive locs and the loc-inspection tools\n");
    {
        char actions[5][32] = { { 0 } };
        struct ToriRS_PickHits hits;
        struct World_PickSet pickset;
        struct ToriRS_PickResult result;
        /* interactive = 0: a wall, which is what most of the scene is. */
        CHECK(World_SceneryRegister(
                  world, /* element */ 70, /* loc */ 900, /* x */ 8, /* z */ 9, /* level */ 0,
                  /* size */ 1, 1, /* shape */ 0, /* angle */ 0, /* force_approach */ 0,
                  "Wall", actions, /* interactive */ 0) >= 0,
              "the fixture wall registers");

        ToriRS_PickHitsReset(&hits);
        ToriRS_PickHitsAdd(&hits, 70, /* is_terrain */ false, -1, -1, -1, 0);
        ToriRS_PickHitsClassify(world, NULL, &hits, /* player_level */ 0, &pickset, &result);
        CHECK(pickset.count == 0, "an inactive loc is not picked by default");

        WorldEntity_SceneryDebugSetTools(true);
        ToriRS_PickHitsClassify(world, NULL, &hits, /* player_level */ 0, &pickset, &result);
        CHECK(pickset.count == 1 && pickset.items[0].type == WORLD_PICK_SCENERY,
              "a tool that inspects locs picks the inactive ones");

        WorldEntity_SceneryDebugSetTools(false);
        ToriRS_PickHitsClassify(world, NULL, &hits, /* player_level */ 0, &pickset, &result);
        CHECK(pickset.count == 0, "and stops again when the tool is switched off");
    }

    printf("TEST: a loc on a bridge deck\n");
    {
        char actions[5][32] = { { 0 } };
        struct ToriRS_PickHits hits;
        struct World_PickSet pickset;
        struct ToriRS_PickResult result;

        /* The Theatre of Blood's arena barriers: authored on cache level 1 over
         * a LinkBelow column, parked by the build on paint level 0, and clicked
         * by a player standing on level 0. Registered interactive, because that
         * is what a loc carrying "Pass" is. */
        CHECK(World_SceneryRegister(
                  world, /* element */ 71, /* loc */ 32755, bridge_x, bridge_z, /* level */ 1,
                  /* size */ 1, 1, /* shape */ 10, /* angle */ 0, /* force_approach */ 0,
                  "Barrier", actions, /* interactive */ 1) >= 0,
              "the fixture barrier registers");
        CHECK(World_SceneryRegister(
                  world, /* element */ 72, /* loc */ 32755, flat_x, flat_z, /* level */ 1,
                  /* size */ 1, 1, /* shape */ 10, /* angle */ 0, /* force_approach */ 0,
                  "Barrier", actions, /* interactive */ 1) >= 0,
              "the fixture upper-storey loc registers");

        ToriRS_PickHitsReset(&hits);
        ToriRS_PickHitsAdd(&hits, 71, /* is_terrain */ false, -1, -1, -1, 0);
        ToriRS_PickHitsClassify(world, NULL, &hits, /* player_level */ 0, &pickset, &result);
        CHECK(pickset.count == 1 && pickset.items[0].type == WORLD_PICK_SCENERY,
              "a loc on the deck under your feet is clickable from level 0");

        ToriRS_PickHitsClassify(world, NULL, &hits, /* player_level */ 1, &pickset, &result);
        CHECK(pickset.count == 0, "and is not reachable from the level it was authored on");

        ToriRS_PickHitsReset(&hits);
        ToriRS_PickHitsAdd(&hits, 72, /* is_terrain */ false, -1, -1, -1, 0);
        ToriRS_PickHitsClassify(world, NULL, &hits, /* player_level */ 0, &pickset, &result);
        CHECK(pickset.count == 0, "a loc on a genuine upper storey stays unreachable from below");
        ToriRS_PickHitsClassify(world, NULL, &hits, /* player_level */ 1, &pickset, &result);
        CHECK(pickset.count == 1, "and is clickable once you are standing up there");
    }

    World_Free(world);

    if( g_failures )
    {
        fprintf(stderr, "pick_level_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("OK: pick_level_test\n");
    return 0;
}
