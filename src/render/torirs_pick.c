#include "torirs_pick.h"

#include "toridraw_element_id.h"
#include "world/world.h"
#include "world/world_pickset.h"
#include "world/worldview.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* Entity picks report the entity's own tile. Players (including local) are
 * pickable so a tile-occupancy winner can expand co-located stackmates into
 * the minimenu (Client-TS addViewportOptions); local rows are skipped later. */
/*
 * Which entity is this, and where is it standing?
 *
 * This used to try the npc pool, then players, then scenery, then
 * objstacks, and each of those is a linear walk of a pool's active list
 * chasing World_EntityPoolNext -- so a frame paid O(hits x scene) to answer
 * a question the emitter already knew the answer to. The element id now
 * carries its kind (see toridraw_element_id.h), so this asks one pool.
 *
 * An untagged id is kind NONE and still falls through to the old search:
 * ids reach here from paths that predate the tag (a plugin-placed object,
 * anything a test builds by hand), and reading NONE as "no entity" would
 * silently stop those picking.
 */
static bool
pick_classify_element(
    struct World* world,
    int element_id,
    enum World_PickType* out_type,
    int* out_tile_x,
    int* out_tile_z,
    int* out_tile_level)
{
    enum ToriDraw_ElementKind const kind =
        ElementId_Kind(ElementId_FromRaw(element_id));

    if( kind == TORIDRAW_ELEMENT_KIND_NPC || kind == TORIDRAW_ELEMENT_KIND_NONE )
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
        if( kind == TORIDRAW_ELEMENT_KIND_NPC )
            return false;
    }

    if( kind == TORIDRAW_ELEMENT_KIND_PLAYER || kind == TORIDRAW_ELEMENT_KIND_NONE )
    {
        struct WorldEntity_Player* player = World_PlayerGetByElementId(world, element_id);
        if( player )
        {
            *out_type = WORLD_PICK_PLAYER;
            *out_tile_x = player->grid_position.x;
            *out_tile_z = player->grid_position.z;
            *out_tile_level = player->grid_position.level;
            return true;
        }
        if( kind == TORIDRAW_ELEMENT_KIND_PLAYER )
            return false;
    }

    if( kind == TORIDRAW_ELEMENT_KIND_SCENERY || kind == TORIDRAW_ELEMENT_KIND_NONE )
    {
        struct WorldEntity_Scenery* scenery = World_SceneryGetByElementId(world, element_id);
        if( scenery )
        {
            /* LocType.active gate: the reference negates a non-active loc's
             * typecode, and Model.draw only records hits for `typecode > 0`
             * (Model.ts:1758) -- so walls, gravel and floor decor never
             * produce a menu row. Without this every unnamed loc surfaced as
             * "Examine @cya@Scenery".
             *
             * The loc-inspection tools lift the gate, because the locs worth
             * inspecting for a placement bug are overwhelmingly the inactive
             * ones -- a wall, a fence or a patch of ground decor on the wrong
             * square is invisible to a menu that refuses to pick them, and so
             * is its footprint. See WorldEntity_SceneryPickInactive. */
            if( !scenery->interactive && !WorldEntity_SceneryPickInactive() )
                return false;
            *out_type = WORLD_PICK_SCENERY;
            *out_tile_x = scenery->grid_position.x;
            *out_tile_z = scenery->grid_position.z;
            *out_tile_level = scenery->grid_position.level;
            return true;
        }
        if( kind == TORIDRAW_ELEMENT_KIND_SCENERY )
            return false;
    }

    if( kind == TORIDRAW_ELEMENT_KIND_OBJSTACK || kind == TORIDRAW_ELEMENT_KIND_NONE )
    {
        struct WorldEntity_ObjStack* stack = World_ObjStackGetByElementId(world, element_id);
        if( stack )
        {
            *out_type = WORLD_PICK_OBJSTACK;
            *out_tile_x = stack->grid_position.x;
            *out_tile_z = stack->grid_position.z;
            *out_tile_level = stack->grid_position.level;
            return true;
        }
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
    int tile_level,
    int view_id)
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
    hit->view_id = view_id;
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

    TORIRS_LOG("pickset: %d hit(s)\n", pickset->count);
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
        TORIRS_LOG("  [%d] %-10s el=%-5d tile=(%d,%d) lvl=%d%s",
            i,
            p->type <= WORLD_PICK_PLAYER ? kind[p->type] : "?",
            p->element_id,
            p->tile_x,
            p->tile_z,
            p->tile_level,
            loc_id >= 0 ? "" : "\n");
        if( loc_id >= 0 )
            TORIRS_LOG(" loc=%d\n", loc_id);
    }
}

void
ToriRS_PickHitsClassify(
    struct World* world,
    struct WorldviewRegistry* views,
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
             *
             * Hence World_TerrainDrawLevel and not `hit->tile_level`: the hit
             * carries the MESH level, the plane the floor was authored on, and
             * on a deck that is one ABOVE the player standing on it. Comparing
             * it directly discarded every hit on a bridge — which is what made
             * the whole of the Theatre of Blood's corridors unclickable.
             *
             * A WORLD-ENTITY view's tiles are the view's own coordinates —
             * this world's draw levels say nothing about them, so the guard
             * does not apply; the view's walkable planes are the server's
             * (deck collision) problem, exactly as the deob leaves per-view
             * plane resolution to the menu layer (class108.method3786).
             */
            if( hit->view_id == 0 && player_level >= 0 &&
                World_TerrainDrawLevel(world, hit->tile_x, hit->tile_z, hit->tile_level) >
                    player_level )
                continue;
            /* Hits arrive in render order, back-to-front: the last terrain
             * hit is nearest. Only root tiles feed the hover latch — a
             * deck-local coordinate drawn as a root-scene hover box lands in
             * the wrong ocean. */
            if( hit->view_id == 0 )
            {
                out_result->hover_tile_valid = true;
                out_result->hover_tile_x = hit->tile_x;
                out_result->hover_tile_z = hit->tile_z;
                out_result->hover_tile_level = hit->tile_level;
            }
            World_PickSetAdd(
                out_pickset,
                hit->element_id,
                WORLD_PICK_TERRAIN,
                hit->tile_x,
                hit->tile_z,
                hit->tile_level,
                hit->view_id);
        }
        else if( hit->view_id != 0 )
        {
            enum World_PickType type;
            int tile_x;
            int tile_z;
            int tile_level;

            /* An actor RIDING the view first: aboard players and npcs are
             * retagged into the view's dynamic pool but keep their ROOT
             * entity records, so they classify like any shore actor — a
             * click on a fellow passenger must offer their own rows (Attack,
             * Talk-to), not the hull's. No reach-level filter here: an
             * aboard actor's level is a deck plane, incomparable with the
             * viewer's root level. */
            if( pick_classify_element(
                    world, hit->element_id, &type, &tile_x, &tile_z, &tile_level) &&
                (type == WORLD_PICK_NPC || type == WORLD_PICK_PLAYER) )
            {
                World_PickSetAdd(
                    out_pickset, hit->element_id, type, tile_x, tile_z, tile_level,
                    hit->view_id);
            }
            else if(
                views && WorldviewRegistry_IsLive(views, hit->view_id) &&
                WorldviewRegistry_Get(views, hit->view_id)->world &&
                pick_classify_element(
                    WorldviewRegistry_Get(views, hit->view_id)->world, hit->element_id,
                    &type, &tile_x, &tile_z, &tile_level) &&
                type == WORLD_PICK_SCENERY )
            {
                /* A DECK LOC: it lives in the VIEW world's scenery table, so
                 * classification runs against that world — same interactive
                 * gate as the root's. The pick carries deck-local tiles and
                 * the view id; the menu layer resolves the loc through the
                 * same view world, and its op dispatch sends view.base+local
                 * (the wire shape every aboard interaction uses). No reach-
                 * level filter: a deck plane is incomparable with the
                 * viewer's root level — reachability is the server's deck
                 * collision's problem, like every deck walk. */
                World_PickSetAdd(
                    out_pickset, hit->element_id, WORLD_PICK_SCENERY, tile_x, tile_z,
                    tile_level, hit->view_id);
            }
            else
            {
                /* A sub-scene MODEL that classifies as nothing (hull side,
                 * mast, deck terrain skirt): what a click on it MEANS is "the
                 * boat" — surface the view id and let the menu layer offer
                 * the hull's config ops (the deob's menu hash carries the
                 * world-view id for exactly this). */
                World_PickSetAdd(
                    out_pickset, hit->element_id, WORLD_PICK_WEV, -1, -1, -1, hit->view_id);
            }
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
                 * unreachable — never surface them in the minimenu.
                 *
                 * A LOC's level is not read raw, for the same reason the
                 * terrain guard above does not read `hit->tile_level`: the
                 * scenery pool holds the CACHE level a loc was authored on,
                 * and on a LinkBelow column the build parks its geometry one
                 * level down (World_LocPaintLevel — the same shuffle
                 * world_builder and world_cycle register it into the painter
                 * with). Every ToB corridor is such a deck, so the Nylocas
                 * room's barrier was authored at plane 1, drawn at paint level
                 * 0 in front of a player standing on level 0 — and thrown away
                 * by a guard comparing 1 against 0. The red gate drew, lit,
                 * animated, and could not be clicked.
                 *
                 * NPCs, players and obj stacks are already positioned on the
                 * level they are WALKED on (the server shifts them), so only
                 * scenery takes the conversion — running it on the rest would
                 * push a bridge-deck npc from level 0 to the underside. */
                int reach_level = tile_level;
                if( type == WORLD_PICK_SCENERY )
                    reach_level = World_LocPaintLevel(world, tile_x, tile_z, tile_level);
                if( player_level >= 0 && reach_level != player_level )
                    continue;
                /* Non-terrain classification resolves through the ROOT world's
                 * entity tables, so these are root picks by construction —
                 * view-scene locs/actors do not classify here (yet). */
                World_PickSetAdd(
                    out_pickset, hit->element_id, type, tile_x, tile_z, tile_level, 0);
            }
        }
    }

    pick_debug_dump(world, out_pickset);
}
