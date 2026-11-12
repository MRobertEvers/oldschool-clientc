/*
 * The Building tool, headless (editor/editor_building.c plans, editor/panels/
 * edpanel_ops_building.c turns the plan into commands): an in-memory document
 * of two empty squares side by side, every command applied through
 * Editor_CmdApply as the authority would and undone the same way.
 *
 *   plan       a 9x7 footprint: 24 walls and 4 corners on the ring, 35 floor
 *              tiles, 28 eaves, 20 slopes, 15 flat tops, each piece at the
 *              shape and rotation the content's houses have it (the smithy
 *              and the house of m50_50, whose rows are quoted below)
 *   narrow     a 3-wide footprint has no slope ring; a 4-wide one is all
 *              slopes (a ridge)
 *   masks      a wall with no shape 3 takes the post (1), one with neither
 *              no corner; an edge with no 21 takes 19
 *   storeys    walls and floor per level, the ladder's foot and top on one
 *              tile, the roof above the last; refusals (too small, too
 *              large, above level 3, not a wall, not a roof, nothing asked)
 *   commands   built on bare ground; built again nothing changes; a floor
 *              keeps the tile's height and underlay and adds the flag; a
 *              wall of another shape on a ring tile leaves; undone, the
 *              squares are as they were
 *   closed     pieces outside the open squares are left out and counted
 *   door       a wall piece becomes the door and back, the wall's id read
 *              off its neighbour; found from the floor beside it; a room's
 *              corner tile is ambiguous
 *   window     both faces, on the wall's tile and the tile across; again
 *              takes both off
 *   fence      a fence stands on the rectangle's own outer edges with L
 *              corners (an 8x8 drag is an 8x8 pen); a gate is two leaves,
 *              opened and closed from either
 *   wall line  along x on the south edge, along z on the west edge
 *   taller     the same footprint a storey taller takes the old roof off
 */

#include "editor/editor_building.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/panels/edpanel_ops_building.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                                     \
    do                                                                                                       \
    {                                                                                                        \
        g_checks++;                                                                                          \
        if( !(cond) )                                                                                        \
        {                                                                                                    \
            g_failed++;                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                      \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
        }                                                                                                    \
    } while( 0 )

#define CMDS_MAX 8192

/* Squares (50, 50) and (51, 50): world tiles x 3200..3327, z 3200..3263. */
#define WALL 1602
#define DOOR 1540
#define WINDOW 1823
#define ROOF 1926
#define EDGE 1792
#define OTHER_WALL 1902

#define MASK_WALL ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 9))
#define MASK_ROOF ((1u << 12) | (1u << 13) | (1u << 14) | (1u << 15) | (1u << 16) | (1u << 17))
#define MASK_EDGE ((1u << 18) | (1u << 19) | (1u << 20) | (1u << 21))

static struct Editor_Doc g_doc;
static struct Editor_Cmd g_cmds[CMDS_MAX];
static struct Editor_BuildingPlan g_plan;

static struct Editor_Square*
square_at(
    int world_x,
    int world_z)
{
    struct Editor_Square* square =
        Editor_DocFindSquare(&g_doc, world_x / EDITOR_SQUARE_X, world_z / EDITOR_SQUARE_Z);
    return square && square->loaded ? square : NULL;
}

static struct Editor_Tile*
tile_at(
    int world_x,
    int world_z,
    int level)
{
    struct Editor_Square* square = square_at(world_x, world_z);
    if( !square )
        return NULL;
    return &square->tiles[Editor_TileIndex(world_x % EDITOR_SQUARE_X, world_z % EDITOR_SQUARE_Z, level)];
}

/** The loc of `shape` on a world tile, or NULL. */
static const struct Editor_Loc*
loc_at(
    int world_x,
    int world_z,
    int level,
    int shape)
{
    struct Editor_Square* square = square_at(world_x, world_z);
    int index;
    if( !square )
        return NULL;
    index = Editor_SquareLocFind(square, level, world_x % EDITOR_SQUARE_X, world_z % EDITOR_SQUARE_Z, shape);
    return index < 0 ? NULL : &square->locs[index];
}

static int
loc_is(
    int world_x,
    int world_z,
    int level,
    int shape,
    int loc_id,
    int rotation)
{
    const struct Editor_Loc* loc = loc_at(world_x, world_z, level, shape);
    return loc && loc->loc_id == loc_id && (loc->rotation & 3) == rotation;
}

static int
loc_total(void)
{
    int total = 0;
    for( int i = 0; i < g_doc.square_count; i++ )
        total += g_doc.squares[i].loc_count;
    return total;
}

static void
apply(
    const struct Editor_Cmd* cmds,
    int count)
{
    for( int i = 0; i < count; i++ )
        CHECK(Editor_CmdApply(&g_doc, &cmds[i], EDITOR_CMD_FORWARD), "command %d of %d applies", i, count);
}

static void
revert(
    const struct Editor_Cmd* cmds,
    int count)
{
    for( int i = count - 1; i >= 0; i-- )
        CHECK(Editor_CmdApply(&g_doc, &cmds[i], EDITOR_CMD_INVERSE), "command %d of %d reverts", i, count);
}

static void
clear_squares(void)
{
    for( int i = 0; i < g_doc.square_count; i++ )
    {
        memset(g_doc.squares[i].tiles, 0, sizeof(g_doc.squares[i].tiles));
        g_doc.squares[i].loc_count = 0;
    }
}

static struct Editor_BuildingSpec
cottage(void)
{
    struct Editor_BuildingSpec spec;
    memset(&spec, 0, sizeof(spec));
    /* 9 x 7, given upside down: the plan normalises it. */
    spec.x0 = 3218;
    spec.z0 = 3216;
    spec.x1 = 3210;
    spec.z1 = 3210;
    spec.level = 0;
    spec.storeys = 1;
    spec.wall_id = WALL;
    spec.wall_mask = MASK_WALL;
    spec.floor_overlay = 5;
    spec.roof_flag = 1;
    spec.roof_id = ROOF;
    spec.roof_mask = MASK_ROOF;
    spec.edge_id = EDGE;
    spec.edge_mask = MASK_EDGE;
    spec.ladder_up_id = -1;
    spec.ladder_down_id = -1;
    return spec;
}

/** The plan's loc piece on a tile and level, or NULL. */
static const struct Editor_BuildingPiece*
plan_loc_at(
    int x,
    int z,
    int level)
{
    for( int i = 0; i < g_plan.count; i++ )
        if( g_plan.pieces[i].kind == EDITOR_BUILDING_LOC && g_plan.pieces[i].x == x && g_plan.pieces[i].z == z
            && g_plan.pieces[i].level == level )
            return &g_plan.pieces[i];
    return NULL;
}

static int
plan_is(
    int x,
    int z,
    int level,
    int loc_id,
    int shape,
    int rotation)
{
    const struct Editor_BuildingPiece* p = plan_loc_at(x, z, level);
    return p && p->loc_id == loc_id && p->shape == shape && p->rotation == rotation;
}

static void
test_plan(void)
{
    struct Editor_BuildingSpec spec = cottage();
    int const count = Editor_BuildingPlanMake(&spec, &g_plan);
    int floors = 0;

    CHECK(g_plan.refusal == EDITOR_BUILDING_OK, "a 9x7 cottage plans");
    CHECK(g_plan.x0 == 3210 && g_plan.z0 == 3210 && g_plan.x1 == 3218 && g_plan.z1 == 3216, "the footprint normalised");
    CHECK(g_plan.parts[EDITOR_BUILDING_PART_WALL] == 24, "24 straight walls (%d)", g_plan.parts[EDITOR_BUILDING_PART_WALL]);
    CHECK(g_plan.parts[EDITOR_BUILDING_PART_CORNER] == 4, "4 corners");
    CHECK(g_plan.parts[EDITOR_BUILDING_PART_FLOOR] == 35, "the 7x5 room's 35 floor tiles (%d)",
        g_plan.parts[EDITOR_BUILDING_PART_FLOOR]);
    CHECK(g_plan.parts[EDITOR_BUILDING_PART_ROOF_EDGE] == 28, "28 eaves (%d)", g_plan.parts[EDITOR_BUILDING_PART_ROOF_EDGE]);
    CHECK(g_plan.parts[EDITOR_BUILDING_PART_ROOF] == 35, "20 slopes and 15 flat tops (%d)",
        g_plan.parts[EDITOR_BUILDING_PART_ROOF]);
    CHECK(count == 24 + 4 + 35 + 28 + 35, "every piece counted (%d)", count);

    /* Walls face indoors. m50_50's house: `0 25 41: 1902 0 2` on its west
     * column, `0 27 43: 1902 0 3` on its north row, `0 32 35: 1902 0 1` on
     * its south row. */
    CHECK(plan_is(3210, 3213, 0, WALL, 0, 2), "the west column's walls stand on their east edge");
    CHECK(plan_is(3214, 3216, 0, WALL, 0, 3), "the north row's on their south edge");
    CHECK(plan_is(3218, 3213, 0, WALL, 0, 0), "the east column's on their west edge");
    CHECK(plan_is(3214, 3210, 0, WALL, 0, 1), "the south row's on their north edge");
    /* Its corners: `3/1` south-west, `3/2` north-west, `3/3` north-east. */
    CHECK(plan_is(3210, 3210, 0, WALL, 3, 1), "the south-west corner piece");
    CHECK(plan_is(3210, 3216, 0, WALL, 3, 2), "the north-west corner piece");
    CHECK(plan_is(3218, 3216, 0, WALL, 3, 3), "the north-east corner piece");
    CHECK(plan_is(3218, 3210, 0, WALL, 3, 0), "the south-east corner piece");
    CHECK(plan_loc_at(3211, 3211, 0) == NULL, "no loc in the room");

    /* The smithy's roof: eaves `18/2` west, `18/3` north, `18/1` south and
     * `21/2` `21/1` `21/3` on the corners; slopes `12/2` `12/3` `12/1` with
     * `16/2` `16/1` `16/3`; `17` inside. */
    CHECK(plan_is(3210, 3213, 1, EDGE, 18, 2) && plan_is(3214, 3216, 1, EDGE, 18, 3)
              && plan_is(3218, 3213, 1, EDGE, 18, 0) && plan_is(3214, 3210, 1, EDGE, 18, 1),
        "the eaves take their wall's rotation");
    CHECK(plan_is(3210, 3210, 1, EDGE, 21, 1) && plan_is(3210, 3216, 1, EDGE, 21, 2)
              && plan_is(3218, 3216, 1, EDGE, 21, 3) && plan_is(3218, 3210, 1, EDGE, 21, 0),
        "the eaves' corners");
    CHECK(plan_is(3211, 3213, 1, ROOF, 12, 2) && plan_is(3214, 3215, 1, ROOF, 12, 3)
              && plan_is(3217, 3213, 1, ROOF, 12, 0) && plan_is(3214, 3211, 1, ROOF, 12, 1),
        "the slopes, one ring in");
    CHECK(plan_is(3211, 3211, 1, ROOF, 16, 1) && plan_is(3211, 3215, 1, ROOF, 16, 2)
              && plan_is(3217, 3215, 1, ROOF, 16, 3) && plan_is(3217, 3211, 1, ROOF, 16, 0),
        "the slopes' convex corners");
    CHECK(plan_is(3212, 3212, 1, ROOF, 17, 0) && plan_is(3216, 3214, 1, ROOF, 17, 0), "flat tops inside");

    for( int i = 0; i < g_plan.count; i++ )
    {
        const struct Editor_BuildingPiece* p = &g_plan.pieces[i];
        if( p->kind != EDITOR_BUILDING_FLOOR )
            continue;
        floors++;
        CHECK(p->x > 3210 && p->x < 3218 && p->z > 3210 && p->z < 3216 && p->level == 0, "a floor tile is in the room");
        CHECK(p->overlay == 5 && p->flags == EDITOR_BUILDING_FLAG_REMOVE_ROOF, "floor and flag");
    }
    CHECK(floors == 35, "the floor pieces");
}

static void
test_narrow_and_masks(void)
{
    struct Editor_BuildingSpec spec = cottage();

    /* 3 wide: walls, one column of room, and over it eaves and a flat top. */
    spec.x0 = 3210;
    spec.x1 = 3212;
    spec.z0 = 3210;
    spec.z1 = 3216;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) > 0, "a 3-wide building plans");
    CHECK(plan_is(3211, 3213, 1, ROOF, 17, 0), "its one inner column is flat top");
    CHECK(g_plan.parts[EDITOR_BUILDING_PART_ROOF] == 5, "five flat tops, no slopes (%d)",
        g_plan.parts[EDITOR_BUILDING_PART_ROOF]);

    /* 4 wide: the slope ring is the whole inside -- two slopes meeting. */
    spec.x1 = 3213;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) > 0, "a 4-wide building plans");
    CHECK(plan_is(3211, 3213, 1, ROOF, 12, 2) && plan_is(3212, 3213, 1, ROOF, 12, 0), "a ridge: two slopes back to back");

    /* A wall with a post but no corner; a wall with neither. */
    spec = cottage();
    spec.wall_mask = (1u << 0) | (1u << 1);
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) > 0, "a wall with shape 1 only plans");
    CHECK(plan_is(3210, 3210, 0, WALL, 1, 1), "its corners are posts");
    spec.wall_mask = 1u << 0;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) > 0, "a wall with no corner shape plans");
    CHECK(plan_loc_at(3210, 3210, 0) == NULL && g_plan.parts[EDITOR_BUILDING_PART_CORNER] == 0,
        "and leaves its corners empty");

    /* An edge with no square corner takes the diagonal one. */
    spec = cottage();
    spec.edge_mask = (1u << 18) | (1u << 19);
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) > 0, "an edge with no shape 21 plans");
    CHECK(plan_is(3210, 3216, 1, EDGE, 19, 2), "its corners are shape 19");
}

static void
test_storeys_and_refusals(void)
{
    struct Editor_BuildingSpec spec = cottage();

    spec.storeys = 2;
    spec.ladder_up_id = EDITOR_BUILDING_LADDER_UP;
    spec.ladder_down_id = EDITOR_BUILDING_LADDER_DOWN;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) > 0, "two storeys plan");
    CHECK(g_plan.parts[EDITOR_BUILDING_PART_WALL] == 48 && g_plan.parts[EDITOR_BUILDING_PART_FLOOR] == 70,
        "walls and floor on both levels");
    CHECK(plan_is(3214, 3210, 1, WALL, 0, 1), "the upper storey's walls");
    CHECK(plan_is(3217, 3215, 0, EDITOR_BUILDING_LADDER_UP, 10, 0), "the ladder's foot in the room's north-east corner");
    CHECK(g_plan.parts[EDITOR_BUILDING_PART_LADDER] == 2, "a foot and a top");
    CHECK(plan_is(3214, 3216, 2, EDGE, 18, 3), "the roof above the upper storey");
    {
        int tops = 0;
        for( int i = 0; i < g_plan.count; i++ )
            tops += g_plan.pieces[i].kind == EDITOR_BUILDING_LOC && g_plan.pieces[i].level == 1
                    && g_plan.pieces[i].loc_id == EDITOR_BUILDING_LADDER_DOWN && g_plan.pieces[i].x == 3217
                    && g_plan.pieces[i].z == 3215;
        CHECK(tops == 1, "the ladder's top on the same tile a level up");
    }

    spec.storeys = 3;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) > 0, "three storeys from level 0 plan (the roof on level 3)");
    CHECK(plan_is(3216, 3215, 1, EDITOR_BUILDING_LADDER_UP, 10, 0), "the second flight starts a tile west");
    spec.level = 1;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 0 && g_plan.refusal == EDITOR_BUILDING_NO_LEVEL,
        "three storeys from level 1 have no level for the roof");
    spec.roof_id = -1;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) > 0, "without a roof they fit");

    spec = cottage();
    spec.x1 = spec.x0 - 1;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 0 && g_plan.refusal == EDITOR_BUILDING_TOO_SMALL, "2 wide is too small");
    spec = cottage();
    spec.x1 = spec.x0 + EDITOR_BUILDING_SIDE_MAX;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 0 && g_plan.refusal == EDITOR_BUILDING_TOO_LARGE, "33 wide is too large");
    spec = cottage();
    spec.wall_mask = 1u << 10;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 0 && g_plan.refusal == EDITOR_BUILDING_NOT_A_WALL,
        "a centrepiece is not a wall");
    spec = cottage();
    spec.edge_mask = 1u << 10;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 0 && g_plan.refusal == EDITOR_BUILDING_NOT_A_ROOF,
        "a centrepiece is not a roof edge");
    spec = cottage();
    spec.wall_id = -1;
    spec.roof_id = -1;
    spec.floor_overlay = -1;
    spec.roof_flag = 0;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 0 && g_plan.refusal == EDITOR_BUILDING_EMPTY, "nothing asked for");
    /* A pen: walls only. */
    spec.wall_id = 980;
    spec.wall_mask = MASK_WALL;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 28 && g_plan.parts[EDITOR_BUILDING_PART_FLOOR] == 0
              && g_plan.parts[EDITOR_BUILDING_PART_ROOF] == 0,
        "walls only: a fenced pen");
}

static void
test_commands(void)
{
    struct Editor_BuildingSpec spec = cottage();
    struct Editor_BuildingApplyResult result;
    struct Editor_Tile* tile;
    struct Editor_Cmd stray;
    int count;
    int again;

    clear_squares();
    /* Ground the room stands on: a height and an underlay that must stay. */
    tile = tile_at(3212, 3212, 0);
    tile->has_height = 1;
    tile->height = 30;
    tile->underlay_id = 49;
    tile->settings = 1;
    /* A wall of another loc, as an L, on a ring tile: the world keeps one
     * wall a tile, so it must go. */
    Editor_CmdInitSquare(&stray, EDITOR_CMD_LOC, 50, 50);
    stray.has_after = 1;
    stray.u.loc.after.loc_id = OTHER_WALL;
    stray.u.loc.after.shape = 2;
    stray.u.loc.after.rotation = 1;
    stray.u.loc.after.level = 0;
    stray.u.loc.after.x = 3214 % 64;
    stray.u.loc.after.z = 3210 % 64;
    apply(&stray, 1);

    (void)Editor_BuildingPlanMake(&spec, &g_plan);
    count = Editor_BuildingCommands(&g_doc, g_plan.pieces, g_plan.count, g_cmds, CMDS_MAX, &result);
    CHECK(result.placed == 24 + 4 + 28 + 35 && result.tiles == 35 && result.removed == 1 && result.closed == 0,
        "placed %d, tiles %d, removed %d, closed %d", result.placed, result.tiles, result.removed, result.closed);
    CHECK(count == result.placed + result.tiles + result.removed, "a command a change (%d)", count);
    CHECK(g_cmds[0].kind == EDITOR_CMD_TILE, "tiles first");
    apply(g_cmds, count);

    CHECK(loc_is(3214, 3210, 0, 0, WALL, 1), "the south wall stands");
    CHECK(loc_at(3214, 3210, 0, 2) == NULL, "the other wall's L left its tile");
    CHECK(loc_is(3210, 3216, 0, 3, WALL, 2), "a corner stands");
    CHECK(loc_is(3212, 3212, 1, 17, ROOF, 0), "the roof is a level up");
    tile = tile_at(3212, 3212, 0);
    CHECK(tile->has_height && tile->height == 30 && tile->underlay_id == 49, "the floor kept the ground's height and underlay");
    CHECK(tile->has_overlay && tile->overlay_id == 5 && tile->shape == 0, "and took the floor overlay, whole tile");
    CHECK(tile->settings == (1 | EDITOR_BUILDING_FLAG_REMOVE_ROOF), "the flag joins the flags that were there");
    CHECK(tile_at(3210, 3210, 0)->has_overlay == 0 && tile_at(3210, 3210, 0)->settings == 0, "the ring is left as it was");
    CHECK(loc_total() == 24 + 4 + 28 + 35, "every piece in the document (%d)", loc_total());

    again = Editor_BuildingCommands(&g_doc, g_plan.pieces, g_plan.count, g_cmds + count, CMDS_MAX - count, &result);
    CHECK(again == 0 && result.kept == 24 + 4 + 28 + 35 && result.placed == 0, "built again, nothing changes");

    revert(g_cmds, count);
    CHECK(loc_total() == 1 && loc_is(3214, 3210, 0, 2, OTHER_WALL, 1), "undone: only the wall that was there");
    tile = tile_at(3212, 3212, 0);
    CHECK(!tile->has_overlay && tile->settings == 1 && tile->height == 30, "undone: the tile as it was");

    /* Outside the open squares (x under 3200): left out, counted. */
    clear_squares();
    spec.x0 = 3196;
    spec.x1 = 3204;
    (void)Editor_BuildingPlanMake(&spec, &g_plan);
    count = Editor_BuildingCommands(&g_doc, g_plan.pieces, g_plan.count, g_cmds, CMDS_MAX, &result);
    CHECK(result.closed > 0 && result.closed_x < 3200 && count > 0, "pieces in a closed square are left out (%d)",
        result.closed);
    for( int i = 0; i < count; i++ )
        CHECK(strcmp(g_cmds[i].doc_key, "m50_50") == 0, "every command is the open square's");
}

static void
test_door_and_window(void)
{
    struct Editor_BuildingSpec spec = cottage();
    struct Editor_BuildingApplyResult result;
    struct Editor_BuildingSwapResult swap;
    struct Editor_LocRef wall;
    struct Editor_LocRef other;
    int count;

    clear_squares();
    (void)Editor_BuildingPlanMake(&spec, &g_plan);
    count = Editor_BuildingCommands(&g_doc, g_plan.pieces, g_plan.count, g_cmds, CMDS_MAX, &result);
    apply(g_cmds, count);

    /* The south wall at 3214,3210, found from its own tile and from the
     * floor north of it. */
    CHECK(Editor_BuildingFindWall(&g_doc, 3214, 3210, 0, &wall) == EDITOR_BUILDING_WALL_FOUND && wall.rotation == 1,
        "the wall on its own tile");
    CHECK(Editor_BuildingFindWall(&g_doc, 3214, 3211, 0, &other) == EDITOR_BUILDING_WALL_FOUND
              && other.x == wall.x && other.z == wall.z,
        "the same wall from the floor beside it");
    CHECK(Editor_BuildingFindWall(&g_doc, 3214, 3213, 0, &other) == EDITOR_BUILDING_WALL_NONE, "none in mid-room");
    CHECK(Editor_BuildingFindWall(&g_doc, 3211, 3211, 0, &other) == EDITOR_BUILDING_WALL_AMBIGUOUS,
        "the room's corner tile touches two walls");
    CHECK(Editor_BuildingFindWall(&g_doc, 3210, 3210, 0, &other) == EDITOR_BUILDING_WALL_NONE,
        "a corner piece is not a straight wall");

    count = Editor_BuildingDoorCommands(&g_doc, &wall, DOOR, -1, OTHER_WALL, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 1 && swap.added && swap.loc_id == DOOR, "a door planned");
    apply(g_cmds, count);
    CHECK(loc_is(3214, 3210, 0, 0, DOOR, 1), "the door stands in the wall, same rotation");
    wall.loc_id = DOOR;
    count = Editor_BuildingDoorCommands(&g_doc, &wall, DOOR, -1, OTHER_WALL, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 1 && !swap.added && swap.loc_id == WALL, "clicked again: the wall its neighbours are (%d)", swap.loc_id);
    apply(g_cmds, count);
    CHECK(loc_is(3214, 3210, 0, 0, WALL, 1), "the wall is back");
    wall.loc_id = WALL;

    count = Editor_BuildingWindowCommands(&g_doc, &wall, WINDOW, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 2 && swap.added, "a window is two faces");
    apply(g_cmds, count);
    /* m50_50: wall `0 27 43: 1902 0 3` carries `0 27 43: 1821 5 3` and
     * `0 27 42: 1821 4 1`. */
    CHECK(loc_is(3214, 3210, 0, 5, WINDOW, 1), "the outside face on the wall's tile, its rotation");
    CHECK(loc_is(3214, 3211, 0, 4, WINDOW, 3), "the inside face across the wall, turned half round");
    CHECK(loc_is(3214, 3210, 0, 0, WALL, 1), "the wall stays");
    count = Editor_BuildingWindowCommands(&g_doc, &wall, WINDOW, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 2 && !swap.added, "again: both faces come off");
    apply(g_cmds, count);
    CHECK(loc_at(3214, 3210, 0, 5) == NULL && loc_at(3214, 3211, 0, 4) == NULL, "the window is gone");

    wall.loc_id = 99999;
    CHECK(Editor_BuildingDoorCommands(&g_doc, &wall, DOOR, -1, WALL, g_cmds, CMDS_MAX, &swap) == 0,
        "a ref that no longer resolves makes no command");

    /* A painting placed in the room turns to the wall its tile is under
     * (Editor_LocFitDecorPose): the room's north row is 3215, its walls on
     * 3216's south edge. */
    {
        uint32_t const decor = (1u << 4) | (1u << 5);
        int shape = 4;
        int rotation = 0;
        CHECK(Editor_LocFitDecorPose(&g_doc, decor, 3214, 3215, 0, &shape, &rotation) && rotation == 1 && shape == 4,
            "under the north wall a decoration turns north, an inside face (shape %d rot %d)", shape, rotation);
        shape = 4;
        rotation = 1;
        CHECK(!Editor_LocFitDecorPose(&g_doc, decor, 3214, 3215, 0, &shape, &rotation) && rotation == 1,
            "already on the walled edge: nothing to change");
        shape = 4;
        rotation = 3;
        CHECK(Editor_LocFitDecorPose(&g_doc, decor, 3211, 3215, 0, &shape, &rotation) && rotation == 0,
            "in the room's north-west corner, from south the first walled edge clockwise is west (%d)", rotation);
        shape = 4;
        rotation = 1;
        CHECK(!Editor_LocFitDecorPose(&g_doc, decor, 3211, 3215, 0, &shape, &rotation) && rotation == 1,
            "and a pose already on one of the corner's two walls is kept");
        shape = 4;
        rotation = 2;
        CHECK(!Editor_LocFitDecorPose(&g_doc, decor, 3214, 3213, 0, &shape, &rotation) && rotation == 2 && shape == 4,
            "in mid-room nothing turns it");
        shape = 4;
        rotation = 0;
        CHECK(Editor_LocFitDecorPose(&g_doc, decor, 3214, 3216, 0, &shape, &rotation) && rotation == 3 && shape == 5,
            "on the wall's own tile it takes the wall's edge and the offset shape (shape %d rot %d)", shape, rotation);
        shape = 10;
        rotation = 0;
        CHECK(!Editor_LocFitDecorPose(&g_doc, decor, 3214, 3215, 0, &shape, &rotation) && shape == 10,
            "a centrepiece is not a decoration");
    }
}

#define FENCE 980
#define GATE_L 1558
#define GATE_R 1560

static void
test_fence_and_gate(void)
{
    struct Editor_BuildingSpec spec = cottage();
    struct Editor_BuildingApplyResult result;
    struct Editor_BuildingSwapResult swap;
    struct Editor_LocRef wall;
    int count;

    /* An 8x8 pen at 3220..3227 x 3220..3227: no floor, no flag, no roof. */
    spec.x0 = 3220;
    spec.z0 = 3220;
    spec.x1 = 3227;
    spec.z1 = 3227;
    spec.wall_id = FENCE;
    spec.outward = 1;
    spec.floor_overlay = -1;
    spec.roof_flag = 0;
    spec.roof_id = -1;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 28, "a fence is 24 runs and 4 corners (%d)", g_plan.count);
    CHECK(plan_is(3220, 3223, 0, FENCE, 0, 0) && plan_is(3223, 3227, 0, FENCE, 0, 1)
              && plan_is(3227, 3223, 0, FENCE, 0, 2) && plan_is(3223, 3220, 0, FENCE, 0, 3),
        "each side on its own outer edge");
    CHECK(plan_is(3220, 3220, 0, FENCE, 2, 3) && plan_is(3220, 3227, 0, FENCE, 2, 0)
              && plan_is(3227, 3227, 0, FENCE, 2, 1) && plan_is(3227, 3220, 0, FENCE, 2, 2),
        "an L on each corner tile, turned to its two outer edges");
    /* With a floor asked for, a fence's floor is the whole rectangle. */
    spec.floor_overlay = 5;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 28 + 64 && g_plan.parts[EDITOR_BUILDING_PART_FLOOR] == 64,
        "a fenced floor is all 64 tiles");
    spec.floor_overlay = -1;
    /* A fence loc with no L is laid as walls are: inside the ring. */
    spec.wall_mask = (1u << 0) | (1u << 3);
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 28 && plan_is(3220, 3223, 0, FENCE, 0, 2)
              && plan_is(3220, 3220, 0, FENCE, 3, 1),
        "no L: the house's ring");
    spec.wall_mask = MASK_WALL;
    spec.x1 = 3221;
    spec.z1 = 3221;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 4, "a 2x2 fence is four corners");
    spec.x1 = 3220;
    CHECK(Editor_BuildingPlanMake(&spec, &g_plan) == 0 && g_plan.refusal == EDITOR_BUILDING_TOO_SMALL,
        "a fence one tile wide is too small");

    /* The pen in the document, and a gate in its south side. */
    clear_squares();
    spec.x1 = 3227;
    spec.z1 = 3227;
    (void)Editor_BuildingPlanMake(&spec, &g_plan);
    count = Editor_BuildingCommands(&g_doc, g_plan.pieces, g_plan.count, g_cmds, CMDS_MAX, &result);
    apply(g_cmds, count);
    CHECK(Editor_BuildingFindWall(&g_doc, 3224, 3220, 0, &wall) == EDITOR_BUILDING_WALL_FOUND && wall.rotation == 3,
        "the south fence piece");
    count = Editor_BuildingDoorCommands(&g_doc, &wall, GATE_L, GATE_R, FENCE, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 2 && swap.added, "a gate is two leaves (%d)", count);
    apply(g_cmds, count);
    /* The content: `fencegate_l 0 3` with `fencegate_r 0 3` a tile west. */
    CHECK(loc_is(3224, 3220, 0, 0, GATE_L, 3) && loc_is(3223, 3220, 0, 0, GATE_R, 3), "the second leaf a tile west");
    /* Closed from the second leaf: both go back to fence. */
    CHECK(Editor_BuildingFindWall(&g_doc, 3223, 3220, 0, &wall) == EDITOR_BUILDING_WALL_FOUND && wall.loc_id == GATE_R,
        "the second leaf is found");
    count = Editor_BuildingDoorCommands(&g_doc, &wall, GATE_L, GATE_R, FENCE, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 2 && !swap.added && swap.loc_id == FENCE, "either leaf closes the gate (%d, %d)", count, swap.loc_id);
    apply(g_cmds, count);
    CHECK(loc_is(3224, 3220, 0, 0, FENCE, 3) && loc_is(3223, 3220, 0, 0, FENCE, 3), "both are fence again");

    /* Beside a corner there is no straight piece for the second leaf on one
     * side: the west column's gate at its north end has an L north of it, so
     * it opens the other way -- the piece south of the click is the first
     * leaf, the click the second. */
    CHECK(Editor_BuildingFindWall(&g_doc, 3220, 3226, 0, &wall) == EDITOR_BUILDING_WALL_FOUND && wall.rotation == 0,
        "the west fence's last straight piece");
    count = Editor_BuildingDoorCommands(&g_doc, &wall, GATE_L, GATE_R, FENCE, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 2 && swap.added && !swap.no_leaf, "two leaves where the next tile is the corner (%d)", count);
    apply(g_cmds, count);
    CHECK(loc_is(3220, 3225, 0, 0, GATE_L, 0) && loc_is(3220, 3226, 0, 0, GATE_R, 0),
        "the gate hangs the other way: its first leaf south of the click");
    /* A click on the piece the gate stands beside: clockwise is the gate's
     * first leaf, so this gate too opens the other way. */
    CHECK(Editor_BuildingFindWall(&g_doc, 3220, 3224, 0, &wall) == EDITOR_BUILDING_WALL_FOUND && wall.loc_id == FENCE,
        "the fence piece below the gate");
    count = Editor_BuildingDoorCommands(&g_doc, &wall, GATE_L, GATE_R, FENCE, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 2 && loc_is(3220, 3224, 0, 0, FENCE, 0), "no lone leaf against the gate (%d)", count);
    apply(g_cmds, count);
    CHECK(loc_is(3220, 3223, 0, 0, GATE_L, 0) && loc_is(3220, 3224, 0, 0, GATE_R, 0) && loc_is(3220, 3225, 0, 0, GATE_L, 0),
        "a second whole gate beside the first");
    /* A straight piece with a corner on both sides has nowhere to open. */
    clear_squares();
    spec.x0 = 3220;
    spec.z0 = 3220;
    spec.x1 = 3221;
    spec.z1 = 3222;
    (void)Editor_BuildingPlanMake(&spec, &g_plan);
    count = Editor_BuildingCommands(&g_doc, g_plan.pieces, g_plan.count, g_cmds, CMDS_MAX, &result);
    apply(g_cmds, count);
    CHECK(Editor_BuildingFindWall(&g_doc, 3220, 3221, 0, &wall) == EDITOR_BUILDING_WALL_FOUND, "the lone piece");
    count = Editor_BuildingDoorCommands(&g_doc, &wall, GATE_L, GATE_R, FENCE, g_cmds, CMDS_MAX, &swap);
    CHECK(count == 0 && swap.no_leaf && loc_is(3220, 3221, 0, 0, FENCE, 0), "no gate, and it says why (%d)", count);
    {
        int bx;
        int bz;
        Editor_BuildingGateBeside(10, 10, 0, &bx, &bz);
        CHECK(bx == 10 && bz == 11, "a west edge's second leaf is a tile north");
        Editor_BuildingGateBeside(10, 10, 1, &bx, &bz);
        CHECK(bx == 11 && bz == 10, "a north edge's a tile east");
        Editor_BuildingGateBeside(10, 10, 2, &bx, &bz);
        CHECK(bx == 10 && bz == 9, "an east edge's a tile south");
        Editor_BuildingGateBeside(10, 10, 3, &bx, &bz);
        CHECK(bx == 9 && bz == 10, "a south edge's a tile west");
    }
}

static void
test_wall_line_and_taller(void)
{
    struct Editor_BuildingSpec spec = cottage();
    struct Editor_BuildingApplyResult result;
    struct Editor_BuildingPiece line[8];
    int cleared;
    int count;

    clear_squares();
    CHECK(Editor_BuildingWallLine(WALL, 3214, 3211, 0, 5, 0, line) == 5, "a wall line of five");
    CHECK(line[0].x == 3214 && line[0].z == 3211 && line[4].z == 3215 && line[2].shape == 0 && line[2].rotation == 0,
        "along z: the west edge");
    CHECK(Editor_BuildingWallLine(WALL, 3211, 3213, 0, 3, 1, line) == 3 && line[2].x == 3213 && line[2].z == 3213
              && line[1].rotation == 3,
        "along x: the south edge");
    count = Editor_BuildingCommands(&g_doc, line, 3, g_cmds, CMDS_MAX, &result);
    CHECK(count == 3 && result.placed == 3, "a wall line is three adds");

    /* One storey, then the same footprint with two: the level-1 roof goes. */
    clear_squares();
    (void)Editor_BuildingPlanMake(&spec, &g_plan);
    count = Editor_BuildingCommands(&g_doc, g_plan.pieces, g_plan.count, g_cmds, CMDS_MAX, &result);
    apply(g_cmds, count);
    spec.storeys = 2;
    (void)Editor_BuildingPlanMake(&spec, &g_plan);
    cleared = Editor_BuildingClearRoofCommands(&g_doc, g_plan.x0, g_plan.z0, g_plan.x1, g_plan.z1, 0, 1, g_cmds, CMDS_MAX);
    CHECK(cleared == 63, "the old roof's 63 pieces come off level 1 (%d)", cleared);
    count = cleared + Editor_BuildingCommands(&g_doc, g_plan.pieces, g_plan.count, g_cmds + cleared, CMDS_MAX - cleared,
                          &result);
    apply(g_cmds, count);
    CHECK(loc_at(3214, 3216, 1, 18) == NULL && loc_is(3214, 3216, 1, 0, WALL, 3), "level 1 is walls now");
    CHECK(loc_is(3214, 3216, 2, 18, EDGE, 3), "the roof moved up");
    CHECK(result.kept == 28, "the ground floor's walls were already there (%d)", result.kept);

    CHECK(Editor_BuildingSideRotation(0) == 2 && Editor_BuildingSideRotation(3) == 1, "a side's pieces face indoors");
    CHECK(Editor_BuildingCornerRotation(0) == 1 && Editor_BuildingCornerRotation(3) == 0, "the corner rotations");
    CHECK(Editor_BuildingWallStyleCount() >= 6 && Editor_BuildingRoofStyleCount() >= 4
              && Editor_BuildingFloorStyleCount() >= 3,
        "the styles are listed");
    for( int i = 0; i < Editor_BuildingWallStyleCount(); i++ )
    {
        const struct Editor_BuildingWallStyle* style = Editor_BuildingWallStyleAt(i);
        CHECK(style->name && strlen(style->name) < 32 && style->wall_id >= 0
                  && style->roof_style < Editor_BuildingRoofStyleCount(),
            "wall style %d is whole", i);
        /* A Door or Window click finds the style from the piece clicked. */
        CHECK(Editor_BuildingWallStyleOf(style->wall_id) == i, "wall style %d is found by its wall", i);
        if( style->door2_id >= 0 )
            CHECK(Editor_BuildingWallStyleOf(style->door2_id) == i, "wall style %d by its gate's second leaf", i);
    }
    CHECK(Editor_BuildingWallStyleOf(WALL) == 0, "loc 1602 is Timber and plaster");
    CHECK(Editor_BuildingWallStyleOf(FENCE) >= 0 && Editor_BuildingWallStyleAt(Editor_BuildingWallStyleOf(FENCE))->fence,
        "the pen's fence is a fence style");
    CHECK(Editor_BuildingWallStyleOf(DOOR) == 0, "the cottage's door names its style");
    CHECK(Editor_BuildingWallStyleOf(12345) == -1 && Editor_BuildingWallStyleOf(-1) == -1, "a loc no style names");
}

int
main(void)
{
    Editor_DocInit(&g_doc, NULL);
    for( int mx = 50; mx <= 51; mx++ )
    {
        struct Editor_Square* square = Editor_DocOpenSquare(&g_doc, mx, 50);
        CHECK(square != NULL, "square opens");
        square->loaded = 1;
    }
    test_plan();
    test_narrow_and_masks();
    test_storeys_and_refusals();
    test_commands();
    test_door_and_window();
    test_fence_and_gate();
    test_wall_line_and_taller();
    Editor_DocFree(&g_doc);
    printf("editor_building_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
