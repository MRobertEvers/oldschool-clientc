#include "torirs_pick.h"

#include "world/world.h"
#include "world/world_pickset.h"

#include <datatypes/maps.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Entity picks report the entity's own tile. Players (including local) are
 * pickable so a tile-occupancy winner can expand co-located stackmates into
 * the minimenu (Client-TS addViewportOptions); local rows are skipped later. */
static bool
pick_classify_element(
    struct World* world,
    int element_id,
    enum World_PickType* out_type,
    int* out_tile_x,
    int* out_tile_z,
    int* out_tile_level)
{
    struct WorldEntity_NPC* npc = World_NpcGetByElementId(world, element_id, NULL);
    if( npc )
    {
        *out_type = WORLD_PICK_NPC;
        *out_tile_x = npc->grid_position.x;
        *out_tile_z = npc->grid_position.z;
        *out_tile_level = npc->grid_position.level;
        return true;
    }

    struct WorldEntity_Player* player = World_PlayerGetByElementId(world, element_id);
    if( player )
    {
        *out_type = WORLD_PICK_PLAYER;
        *out_tile_x = player->grid_position.x;
        *out_tile_z = player->grid_position.z;
        *out_tile_level = player->grid_position.level;
        return true;
    }

    struct WorldEntity_Scenery* scenery = World_SceneryGetByElementId(world, element_id);
    if( scenery )
    {
        /* LocType.active gate: the reference negates a non-active loc's
         * typecode, and Model.draw only records hits for `typecode > 0`
         * (Model.ts:1758) — so walls, gravel and floor decor never produce a
         * menu row. Without this every unnamed loc surfaced as
         * "Examine @cya@ Scenery".
         *
         * TORIRS_LOC_DEBUG lifts the gate: the locs worth inspecting for a
         * placement bug are overwhelmingly the inactive ones (a bush on the
         * wrong square is invisible to a menu that refuses to pick bushes). */
        if( !scenery->interactive && !WorldEntity_SceneryDebugEnabled() )
            return false;
        *out_type = WORLD_PICK_SCENERY;
        *out_tile_x = scenery->grid_position.x;
        *out_tile_z = scenery->grid_position.z;
        *out_tile_level = scenery->grid_position.level;
        return true;
    }

    struct WorldEntity_ObjStack* stack = World_ObjStackGetByElementId(world, element_id);
    if( stack )
    {
        *out_type = WORLD_PICK_OBJSTACK;
        *out_tile_x = stack->grid_position.x;
        *out_tile_z = stack->grid_position.z;
        *out_tile_level = stack->grid_position.level;
        return true;
    }

    return false;
}

void
ToriRS_PickHitsReset(struct ToriRS_PickHits* hits)
{
    assert(hits);
    hits->count = 0;
}

void
ToriRS_PickHitsAdd(
    struct ToriRS_PickHits* hits,
    int element_id,
    bool is_terrain,
    int tile_x,
    int tile_z,
    int tile_level)
{
    struct ToriRS_PickHit* hit;

    assert(hits);
    if( hits->count >= TORIRS_PICK_HITS_MAX )
        return;

    hit = &hits->items[hits->count++];
    hit->element_id = element_id;
    hit->is_terrain = is_terrain;
    hit->tile_x = tile_x;
    hit->tile_z = tile_z;
    hit->tile_level = tile_level;
}

/*
 * TORIRS_PICK_DEBUG=1: what the renderer says is drawn under the pointer.
 *
 * Every other diagnostic here reports what the BUILD decided. This one reports
 * what the RASTER produced: a pick hit means the projected model actually
 * covered the mouse point. Holding the two against each other is the only way
 * to separate "placed on the wrong tile" from "placed right and drawn wrong" —
 * if a loc picks over ground the terrain hits name as a different tile, the
 * geometry is landing somewhere its own slot does not claim.
 *
 * Printed only when the set changes, so a parked pointer emits one line.
 */
static void
pick_debug_dump(
    struct World* world,
    struct World_PickSet const* pickset)
{
    static int enabled = -1;
    static int every; /* TORIRS_PICK_DEBUG=all: no dedupe, one report per call */
    static unsigned last_sig;
    unsigned sig = 2166136261u;

    if( enabled < 0 )
    {
        char const* env = getenv("TORIRS_PICK_DEBUG");
        enabled = env != NULL;
        /* A sweep needs a report per sample point to pair with, and dedupe
         * silently drops the runs of identical picks that pairing depends on. */
        every = env && strcmp(env, "all") == 0;
    }
    if( !enabled )
        return;

    for( int i = 0; i < pickset->count; i++ )
    {
        sig = (sig ^ (unsigned)pickset->items[i].element_id) * 16777619u;
        sig = (sig ^ (unsigned)pickset->items[i].type) * 16777619u;
    }
    if( !every && sig == last_sig )
        return;
    last_sig = sig;

    fprintf(stderr, "pickset: %d hit(s)\n", pickset->count);
    for( int i = 0; i < pickset->count; i++ )
    {
        struct World_Picked const* p = &pickset->items[i];
        static char const* const kind[] = {
            "terrain", "scenery", "projectile", "npc", "obj", "player"
        };
        int loc_id = -1;
        if( p->type == WORLD_PICK_SCENERY )
        {
            struct WorldEntity_Scenery* sc = World_SceneryGetByElementId(world, p->element_id);
            if( sc )
                loc_id = sc->loc_id;
        }
        fprintf(
            stderr,
            "  [%d] %-10s el=%-5d tile=(%d,%d) lvl=%d%s",
            i,
            p->type <= WORLD_PICK_PLAYER ? kind[p->type] : "?",
            p->element_id,
            p->tile_x,
            p->tile_z,
            p->tile_level,
            loc_id >= 0 ? "" : "\n");
        if( loc_id >= 0 )
            fprintf(stderr, " loc=%d\n", loc_id);
    }
}

/*
 * The DRAW level of a terrain hit, which is what the level guard below compares
 * — never the mesh level the hit carries.
 *
 * The two differ on exactly the columns where the guard matters. A bridge deck
 * keeps its geometry on cache level 1 and is pushed down to paint level 0
 * (WorldBuilder_RebuildCenterzoneEnd, reference World.pushDown), so a player
 * standing on the deck is on level 0 while every tile under their feet reports
 * mesh level 1. Comparing the mesh level threw those hits away: the Theatre of
 * Blood's corridors are one long LinkBelow deck, and none of their ground was
 * clickable — no "Walk here" destination, no yellow cross, nothing.
 *
 * Same inputs the builder used for painter_tile_set_draw_level, read back from
 * the persisted land settings rather than from the painter, so this stays a
 * pure function of (tile, mesh level): the settings byte at the mesh level, and
 * whether the column carries LinkBelow at cache level 1. VIS_BELOW is folded in
 * by the same helper, which is the other way a tile draws below its own plane.
 */
static int
terrain_pick_draw_level(
    struct World* world,
    int tile_x,
    int tile_z,
    int mesh_level)
{
    int link_below;

    assert(world);
    if( mesh_level < 0 )
        return mesh_level;

    link_below = (World_TileFlagGet(world, tile_x, tile_z, 1) &
                  RSCACHE_FLOFLAG_LINK_BELOW) != 0;
    return RSCache_MapFloorVisBelowDrawLevel(
        (uint8_t)World_TileFlagGet(world, tile_x, tile_z, mesh_level), mesh_level, link_below);
}

void
ToriRS_PickHitsClassify(
    struct World* world,
    struct ToriRS_PickHits const* hits,
    int player_level,
    struct World_PickSet* out_pickset,
    struct ToriRS_PickResult* out_result)
{
    assert(hits);
    assert(out_pickset);
    assert(out_result);

    memset(out_result, 0, sizeof(*out_result));
    World_PickSetReset(out_pickset);

    if( !world )
        return;

    for( int i = 0; i < hits->count; i++ )
    {
        struct ToriRS_PickHit const* hit = &hits->items[i];

        if( hit->is_terrain )
        {
            /*
             * Ground ABOVE the player is never a walk target — that is the
             * roof-level floor of a building you are standing outside of. The
             * reference refuses it where the tile records its hit, not later:
             * deob class155 method5213/method5214 guard the `method4269` call
             * with `Statics.field2292 <= worldView.getPlane()`, and field2292
             * is the tile's own draw level, stashed by method5218/method5222
             * immediately before the paint.
             *
             * `<=`, not `==`, and the difference is load-bearing: a bridge deck
             * and every VIS_BELOW tile draw at a LOWER level than the player
             * standing on them (method4161 returns draw level 0 for a tile
             * carrying the link-below flag), so equality would make the ground
             * under your own feet unclickable.
             */
            if( player_level >= 0 &&
                terrain_pick_draw_level(world, hit->tile_x, hit->tile_z, hit->tile_level) >
                    player_level )
                continue;
            /* Hits arrive in render order, back-to-front: the last terrain
             * hit is nearest. */
            out_result->hover_tile_valid = true;
            out_result->hover_tile_x = hit->tile_x;
            out_result->hover_tile_z = hit->tile_z;
            out_result->hover_tile_level = hit->tile_level;
            World_PickSetAdd(
                out_pickset,
                hit->element_id,
                WORLD_PICK_TERRAIN,
                hit->tile_x,
                hit->tile_z,
                hit->tile_level);
        }
        else
        {
            enum World_PickType type;
            int tile_x;
            int tile_z;
            int tile_level;
            if( pick_classify_element(
                    world, hit->element_id, &type, &tile_x, &tile_z, &tile_level) )
            {
                /* Scenery/NPCs/obj stacks on a level other than the player's are
                 * unreachable — never surface them in the minimenu. */
                if( player_level >= 0 && tile_level != player_level )
                    continue;
                World_PickSetAdd(out_pickset, hit->element_id, type, tile_x, tile_z, tile_level);
            }
        }
    }

    pick_debug_dump(world, out_pickset);
}
