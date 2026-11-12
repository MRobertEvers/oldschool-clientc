#include "edpanel_ops_building.h"

#include <assert.h>
#include <string.h>

/* The placement shapes this file tells apart. */
#define SHAPE_WALL 0
#define SHAPE_WALL_LAYER_LAST 3
#define SHAPE_DECOR_INSIDE 4
#define SHAPE_DECOR_OUTSIDE 5
#define SHAPE_ROOF_FIRST 12
#define SHAPE_ROOF_LAST 21
#define SHAPE_GROUND_DECOR 22

/** The open square holding world tile (x, z), and the tile's local coords. */
static struct Editor_Square*
building_square_at(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int* out_x,
    int* out_z)
{
    struct Editor_Square* square;

    if( world_x < 0 || world_z < 0 )
        return NULL;
    square = Editor_DocFindSquare(doc, world_x / EDITOR_SQUARE_X, world_z / EDITOR_SQUARE_Z);
    if( !square || !square->loaded )
        return NULL;
    *out_x = world_x % EDITOR_SQUARE_X;
    *out_z = world_z % EDITOR_SQUARE_Z;
    return square;
}

static void
building_loc(
    struct Editor_Loc* loc,
    int loc_id,
    int shape,
    int rotation,
    int level,
    int x,
    int z)
{
    loc->loc_id = loc_id;
    loc->shape = shape;
    loc->rotation = rotation & 3;
    loc->level = level;
    loc->x = x;
    loc->z = z;
}

/** A LOC command on `square`: `before` (may be NULL: an add) becomes `after`
 *  (may be NULL: a remove). */
static void
building_loc_command(
    struct Editor_Cmd* command,
    const struct Editor_Square* square,
    const struct Editor_Loc* before,
    const struct Editor_Loc* after)
{
    Editor_CmdInitSquare(command, EDITOR_CMD_LOC, square->map_x, square->map_z);
    if( before )
    {
        command->has_before = 1;
        command->u.loc.before = *before;
    }
    if( after )
    {
        command->has_after = 1;
        command->u.loc.after = *after;
    }
}

/** The shapes that share a tile's layer with `shape` where the world keeps
 *  one loc: the wall layer, the roof. Any other shape is its own key. */
static void
building_layer(
    int shape,
    int* out_first,
    int* out_last)
{
    *out_first = shape;
    *out_last = shape;
    if( shape >= SHAPE_WALL && shape <= SHAPE_WALL_LAYER_LAST )
    {
        *out_first = SHAPE_WALL;
        *out_last = SHAPE_WALL_LAYER_LAST;
    }
    else if( shape >= SHAPE_ROOF_FIRST && shape <= SHAPE_ROOF_LAST )
    {
        *out_first = SHAPE_ROOF_FIRST;
        *out_last = SHAPE_ROOF_LAST;
    }
}

int
Editor_BuildingCommands(
    struct Editor_Doc* doc,
    const struct Editor_BuildingPiece* pieces,
    int count,
    struct Editor_Cmd* out,
    int max,
    struct Editor_BuildingApplyResult* result)
{
    int written = 0;

    assert(doc);
    assert(pieces || count == 0);
    assert(out);
    assert(result);

    memset(result, 0, sizeof(*result));
    /* Tiles first, so a loc lands on the new floor in the history as it
     * will in the world. */
    for( int pass = 0; pass < 2; pass++ )
        for( int i = 0; i < count; i++ )
        {
            const struct Editor_BuildingPiece* p = &pieces[i];
            struct Editor_Square* square;
            int x;
            int z;

            if( (p->kind == EDITOR_BUILDING_FLOOR) != (pass == 0) )
                continue;
            assert(p->level >= 0);
            assert(p->level < EDITOR_SQUARE_LEVELS);
            square = building_square_at(doc, p->x, p->z, &x, &z);
            if( !square )
            {
                if( result->closed++ == 0 )
                {
                    result->closed_x = p->x;
                    result->closed_z = p->z;
                }
                continue;
            }
            if( p->kind == EDITOR_BUILDING_FLOOR )
            {
                const struct Editor_Tile* now = &square->tiles[Editor_TileIndex(x, z, p->level)];
                struct Editor_Tile after = *now;

                if( p->overlay >= 0 )
                {
                    after.has_overlay = 1;
                    after.overlay_id = (uint16_t)(p->overlay & 0xffff);
                    after.shape = 0;
                    after.rotation = 0;
                }
                after.settings = (uint8_t)(after.settings | (p->flags & 0xff));
                if( memcmp(now, &after, sizeof(after)) == 0 )
                    continue;
                if( written >= max )
                    return written;
                Editor_CmdInitSquare(&out[written], EDITOR_CMD_TILE, square->map_x, square->map_z);
                out[written].has_before = 1;
                out[written].has_after = 1;
                out[written].u.tile.level = p->level;
                out[written].u.tile.x = x;
                out[written].u.tile.z = z;
                out[written].u.tile.before = *now;
                out[written].u.tile.after = after;
                written++;
                result->tiles++;
            }
            else
            {
                struct Editor_Loc after;
                int first;
                int last;
                int same;

                building_loc(&after, p->loc_id, p->shape, p->rotation, p->level, x, z);
                building_layer(p->shape, &first, &last);
                /* The other shapes of the layer leave before the piece
                 * arrives: the world would keep only one of them. */
                for( int shape = first; shape <= last; shape++ )
                {
                    int const held = shape == p->shape ? -1 : Editor_SquareLocFind(square, p->level, x, z, shape);
                    if( held < 0 )
                        continue;
                    if( written >= max )
                        return written;
                    building_loc_command(&out[written++], square, &square->locs[held], NULL);
                    result->removed++;
                }
                same = Editor_SquareLocFind(square, p->level, x, z, p->shape);
                if( same >= 0 && square->locs[same].loc_id == after.loc_id
                    && (square->locs[same].rotation & 3) == after.rotation )
                {
                    result->kept++;
                    continue;
                }
                if( written >= max )
                    return written;
                building_loc_command(&out[written++], square, same >= 0 ? &square->locs[same] : NULL, &after);
                result->placed++;
            }
        }
    return written;
}

int
Editor_BuildingClearRoofCommands(
    struct Editor_Doc* doc,
    int x0,
    int z0,
    int x1,
    int z1,
    int level_lo,
    int level_hi,
    struct Editor_Cmd* out,
    int max)
{
    int written = 0;

    assert(doc);
    assert(out);
    assert(x0 <= x1);
    assert(z0 <= z1);

    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        int const base_x = square->map_x * EDITOR_SQUARE_X;
        int const base_z = square->map_z * EDITOR_SQUARE_Z;

        if( !square->loaded || base_x > x1 || base_x + EDITOR_SQUARE_X <= x0 || base_z > z1
            || base_z + EDITOR_SQUARE_Z <= z0 )
            continue;
        for( int k = 0; k < square->loc_count; k++ )
        {
            const struct Editor_Loc* loc = &square->locs[k];
            if( loc->shape < SHAPE_ROOF_FIRST || loc->shape > SHAPE_ROOF_LAST || loc->level < level_lo
                || loc->level > level_hi || base_x + loc->x < x0 || base_x + loc->x > x1 || base_z + loc->z < z0
                || base_z + loc->z > z1 )
                continue;
            if( written >= max )
                return written;
            building_loc_command(&out[written++], square, loc, NULL);
        }
    }
    return written;
}

int
Editor_BuildingClearDecorCommands(
    struct Editor_Doc* doc,
    int x0,
    int z0,
    int x1,
    int z1,
    int level,
    struct Editor_Cmd* out,
    int max)
{
    int written = 0;

    assert(doc);
    assert(out);
    assert(x0 <= x1);
    assert(z0 <= z1);

    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        int const base_x = square->map_x * EDITOR_SQUARE_X;
        int const base_z = square->map_z * EDITOR_SQUARE_Z;

        if( !square->loaded || base_x > x1 || base_x + EDITOR_SQUARE_X <= x0 || base_z > z1
            || base_z + EDITOR_SQUARE_Z <= z0 )
            continue;
        for( int k = 0; k < square->loc_count; k++ )
        {
            const struct Editor_Loc* loc = &square->locs[k];
            if( loc->shape != SHAPE_GROUND_DECOR || loc->level != level || base_x + loc->x < x0
                || base_x + loc->x > x1 || base_z + loc->z < z0 || base_z + loc->z > z1 )
                continue;
            if( written >= max )
                return written;
            building_loc_command(&out[written++], square, loc, NULL);
        }
    }
    return written;
}

/** The straight wall on world tile (x, z) standing on edge `rotation` (-1:
 *  any edge), as a ref. */
static int
building_wall_on(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int level,
    int rotation,
    struct Editor_LocRef* out)
{
    struct Editor_Square* square;
    int x;
    int z;
    int index;

    square = building_square_at(doc, world_x, world_z, &x, &z);
    if( !square )
        return 0;
    index = Editor_SquareLocFind(square, level, x, z, SHAPE_WALL);
    if( index < 0 || (rotation >= 0 && (square->locs[index].rotation & 3) != rotation) )
        return 0;
    Editor_LocRefAt(out, world_x, world_z, level, SHAPE_WALL, square->locs[index].loc_id,
        square->locs[index].rotation & 3);
    return 1;
}

enum Editor_BuildingWallFind
Editor_BuildingFindWall(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int level,
    struct Editor_LocRef* out)
{
    struct Editor_LocRef found;
    int count = 0;

    assert(doc);
    assert(out);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);

    if( building_wall_on(doc, world_x, world_z, level, -1, out) )
        return EDITOR_BUILDING_WALL_FOUND;
    /* The wall on the far side of each of the tile's edges stands on the
     * neighbour's edge facing back: across edge e, on edge e + 2. */
    for( int edge = 0; edge < 4; edge++ )
    {
        int nx;
        int nz;
        Editor_BuildingAcross(world_x, world_z, edge, &nx, &nz);
        if( building_wall_on(doc, nx, nz, level, (edge + 2) & 3, &found) )
        {
            if( count++ == 0 )
                *out = found;
        }
    }
    if( count == 0 )
        return EDITOR_BUILDING_WALL_NONE;
    return count == 1 ? EDITOR_BUILDING_WALL_FOUND : EDITOR_BUILDING_WALL_AMBIGUOUS;
}

/** The id of a wall beside world tile (x, z) along the line of edge
 *  `rotation`, that is neither leaf of the door: what a door goes back to.
 *  `fallback` when there is none. */
static int
building_wall_beside(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int level,
    int rotation,
    int door_id,
    int door2_id,
    int fallback)
{
    int const along_z = (rotation & 1) == 0;
    struct Editor_LocRef beside;

    for( int reach = 1; reach <= 2; reach++ )
        for( int step = -reach; step <= reach; step += 2 * reach )
            if( building_wall_on(doc, world_x + (along_z ? 0 : step), world_z + (along_z ? step : 0), level,
                    rotation & 3, &beside)
                && beside.loc_id != door_id && beside.loc_id != door2_id )
                return beside.loc_id;
    return fallback;
}

int
Editor_BuildingDoorCommands(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* wall,
    int door_id,
    int door2_id,
    int wall_id,
    struct Editor_Cmd* out,
    int max,
    struct Editor_BuildingSwapResult* result)
{
    struct Editor_Square* square;
    struct Editor_Square* other_square;
    struct Editor_Loc after;
    struct Editor_LocRef first = *wall;
    struct Editor_LocRef other;
    int index;
    int other_index = -1;
    int rotation;
    int world_x;
    int world_z;
    int other_x;
    int other_z;
    int back;
    int written = 0;

    assert(doc);
    assert(wall);
    assert(out);
    assert(result);
    assert(door_id >= 0);
    assert(wall_id >= 0);

    memset(result, 0, sizeof(*result));
    index = Editor_LocRefResolve(doc, wall);
    if( index < 0 || max < 2 )
        return 0;
    square = Editor_DocFindSquare(doc, wall->map_x, wall->map_z);
    rotation = square->locs[index].rotation & 3;
    Editor_LocRefWorld(wall, &world_x, &world_z);

    /* A click on a gate's second leaf is a click on the gate: its first leaf
     * is the piece this one stands beside. */
    if( door2_id >= 0 && square->locs[index].loc_id == door2_id )
    {
        Editor_BuildingGateBeside(world_x, world_z, rotation + 2, &other_x, &other_z);
        if( building_wall_on(doc, other_x, other_z, wall->level, rotation, &other) && other.loc_id == door_id )
        {
            first = other;
            world_x = other_x;
            world_z = other_z;
            index = Editor_LocRefResolve(doc, &first);
            square = Editor_DocFindSquare(doc, first.map_x, first.map_z);
        }
    }
    back = square->locs[index].loc_id == door_id || square->locs[index].loc_id == door2_id;
    /* The second leaf's tile: a straight piece on the same edge that is a
     * wall to open (or, going back, the leaf to close). */
    other_square = NULL;
    if( door2_id >= 0 )
    {
        Editor_BuildingGateBeside(world_x, world_z, rotation, &other_x, &other_z);
        if( building_wall_on(doc, other_x, other_z, wall->level, rotation, &other)
            && (back ? other.loc_id == door2_id : other.loc_id != door_id && other.loc_id != door2_id) )
        {
            other_square = Editor_DocFindSquare(doc, other.map_x, other.map_z);
            other_index = Editor_LocRefResolve(doc, &other);
        }
        if( !back && other_index < 0 )
        {
            /* Nothing to open clockwise: the gate hangs the other way, the
             * piece before the click its first leaf, the click its second. */
            Editor_BuildingGateBeside(world_x, world_z, rotation + 2, &other_x, &other_z);
            if( !building_wall_on(doc, other_x, other_z, wall->level, rotation, &other) || other.loc_id == door_id
                || other.loc_id == door2_id )
            {
                result->no_leaf = 1;
                return 0;
            }
            other_square = square;
            other_index = index;
            square = Editor_DocFindSquare(doc, other.map_x, other.map_z);
            index = Editor_LocRefResolve(doc, &other);
            first = other;
            world_x = other_x;
            world_z = other_z;
        }
    }

    after = square->locs[index];
    after.loc_id =
        back ? building_wall_beside(doc, world_x, world_z, wall->level, rotation, door_id, door2_id, wall_id) : door_id;
    building_loc_command(&out[written++], square, &square->locs[index], &after);
    result->added = !back;
    result->loc_id = after.loc_id;
    Editor_LocRefAt(&result->ref, world_x, world_z, first.level, SHAPE_WALL, after.loc_id, rotation);
    if( other_square && other_index >= 0 )
    {
        struct Editor_Loc leaf = other_square->locs[other_index];
        leaf.loc_id = back ? after.loc_id : door2_id;
        building_loc_command(&out[written++], other_square, &other_square->locs[other_index], &leaf);
    }
    return written;
}

int
Editor_BuildingWindowCommands(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* wall,
    int window_id,
    struct Editor_Cmd* out,
    int max,
    struct Editor_BuildingSwapResult* result)
{
    struct Editor_Square* square;
    struct Editor_Square* across;
    struct Editor_Loc face;
    int rotation;
    int world_x;
    int world_z;
    int far_x;
    int far_z;
    int x;
    int z;
    int held;
    int written = 0;

    assert(doc);
    assert(wall);
    assert(out);
    assert(result);
    assert(window_id >= 0);

    memset(result, 0, sizeof(*result));
    square = Editor_DocFindSquare(doc, wall->map_x, wall->map_z);
    if( !square || Editor_LocRefResolve(doc, wall) < 0 || max < 2 )
        return 0;
    rotation = wall->rotation & 3;
    Editor_LocRefWorld(wall, &world_x, &world_z);
    Editor_BuildingAcross(world_x, world_z, rotation, &far_x, &far_z);
    across = building_square_at(doc, far_x, far_z, &x, &z);

    held = Editor_SquareLocFind(square, wall->level, wall->x, wall->z, SHAPE_DECOR_OUTSIDE);
    result->loc_id = window_id;
    Editor_LocRefAt(&result->ref, world_x, world_z, wall->level, SHAPE_DECOR_OUTSIDE, window_id, rotation);
    if( held >= 0 && square->locs[held].loc_id == window_id && (square->locs[held].rotation & 3) == rotation )
    {
        /* The window is there: both faces come off. */
        building_loc_command(&out[written++], square, &square->locs[held], NULL);
        if( across )
        {
            int const inside = Editor_SquareLocFind(across, wall->level, x, z, SHAPE_DECOR_INSIDE);
            if( inside >= 0 && across->locs[inside].loc_id == window_id
                && (across->locs[inside].rotation & 3) == ((rotation + 2) & 3) )
                building_loc_command(&out[written++], across, &across->locs[inside], NULL);
        }
        return written;
    }
    result->added = 1;
    building_loc(&face, window_id, SHAPE_DECOR_OUTSIDE, rotation, wall->level, wall->x, wall->z);
    building_loc_command(&out[written++], square, held >= 0 ? &square->locs[held] : NULL, &face);
    if( across )
    {
        int const inside = Editor_SquareLocFind(across, wall->level, x, z, SHAPE_DECOR_INSIDE);
        building_loc(&face, window_id, SHAPE_DECOR_INSIDE, rotation + 2, wall->level, x, z);
        building_loc_command(&out[written++], across, inside >= 0 ? &across->locs[inside] : NULL, &face);
    }
    return written;
}
