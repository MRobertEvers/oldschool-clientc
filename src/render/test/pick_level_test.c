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
 * Mutation check: replace terrain_pick_draw_level's body with `return
 * mesh_level;` (the defect) and the bridge and VisBelow cases go red; make it
 * `return 0` and the genuine-upper-floor case goes red.
 */
#include "render/torirs_pick.h"

#include "world/world.h"
#include "world/world_pickset.h"

#include <stdio.h>

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
    ToriRS_PickHitsAdd(&hits, /* element_id */ 7, /* is_terrain */ true, x, z, mesh_level);
    ToriRS_PickHitsClassify(world, &hits, player_level, &pickset, &result);
    return pickset.count == 1 && pickset.items[0].type == WORLD_PICK_TERRAIN &&
           pickset.items[0].tile_x == x && pickset.items[0].tile_z == z;
}

int
main(void)
{
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

    World_Free(world);

    if( g_failures )
    {
        fprintf(stderr, "pick_level_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("OK: pick_level_test\n");
    return 0;
}
