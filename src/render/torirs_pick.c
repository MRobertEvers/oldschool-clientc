#include "torirs_pick.h"

#include "world/world.h"
#include "world/world_pickset.h"

#include <assert.h>
#include <string.h>

/* Entity picks report the entity's own tile; players are not yet minimenu
 * targets, so they are hoverable-through (terrain still hits under them). */
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
            /* Lower levels still render under the player, so their tiles keep
             * producing terrain hits — but the player only ever walks/clicks on
             * their own level's ground. */
            if( player_level >= 0 && hit->tile_level != player_level )
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
}
