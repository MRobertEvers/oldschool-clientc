#include "edpanel_ops.h"

#include "editor/editor_derive.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPS_UNIT 8
#define OPS_UNITS_MAX 255

/* ---- addressing ------------------------------------------------------------- */

static int
ops_split(
    int world_x,
    int world_z,
    int* out_map_x,
    int* out_map_z,
    int* out_x,
    int* out_z)
{
    if( world_x < 0 || world_z < 0 )
        return 0;
    *out_map_x = world_x / EDITOR_SQUARE_X;
    *out_map_z = world_z / EDITOR_SQUARE_Z;
    *out_x = world_x % EDITOR_SQUARE_X;
    *out_z = world_z % EDITOR_SQUARE_Z;
    return 1;
}

/** The open square holding world tile (x, z), and the tile's local coords. */
static struct Editor_Square*
ops_square_at(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int* out_x,
    int* out_z)
{
    struct Editor_Square* square;
    int map_x;
    int map_z;

    if( !ops_split(world_x, world_z, &map_x, &map_z, out_x, out_z) )
        return NULL;
    square = Editor_DocFindSquare(doc, map_x, map_z);
    if( !square || !square->loaded )
        return NULL;
    return square;
}

/* ---- heights ---------------------------------------------------------------- */

int
Editor_OpsDerivedHeight(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int level,
    int* out_height)
{
    struct ToriRS_MapFloor floor;
    struct Editor_Square* square;
    int x;
    int z;

    assert(doc);
    assert(out_height);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);

    square = ops_square_at(doc, world_x, world_z, &x, &z);
    if( !square )
        return 0;
    Editor_TileDerive(square, x, z, level, &floor);
    *out_height = floor.height;
    return 1;
}

/** The base a level's `h` is relative to: 0 on level 0, the derived height of
 *  the tile below above that. */
static int
ops_base(
    const struct Editor_Square* square,
    int x,
    int z,
    int level)
{
    struct ToriRS_MapFloor column[EDITOR_SQUARE_LEVELS];

    if( level == 0 )
        return 0;
    Editor_TileDeriveColumn(square, x, z, column);
    return column[level - 1].height;
}

/** Height above `base` in representable units: 0, or 2..255 (8 world units
 *  each). A tie, and the unspellable 1, go up. */
static int
ops_units_nearest(
    int base,
    int height)
{
    double const exact = (double)(base - height) / (double)OPS_UNIT;
    int units;

    if( exact <= 0.0 )
        return 0;
    units = (int)floor(exact + 0.5);
    if( units == 1 )
        units = exact >= 1.0 ? 2 : 0;
    if( units > OPS_UNITS_MAX )
        units = OPS_UNITS_MAX;
    return units;
}

static int
ops_raw_of_units(int units)
{
    assert(units >= 0);
    assert(units <= OPS_UNITS_MAX);
    return units == 0 ? 1 : units;
}

/** A unit count moved by `delta`, stepping over the unspellable 1. */
static int
ops_units_step(
    int units,
    int delta)
{
    int next = units + delta;

    if( next == 1 )
        next = delta > 0 ? 2 : 0;
    if( next < 0 )
        next = 0;
    if( next > OPS_UNITS_MAX )
        next = OPS_UNITS_MAX;
    return next;
}

int
Editor_OpsRawForHeight(
    int base,
    int height)
{
    return ops_raw_of_units(ops_units_nearest(base, height));
}

/** The tile's current height in units: exact for an authored tile, the
 *  nearest representable step for a generated one. */
static int
ops_tile_units(
    const struct Editor_Square* square,
    int x,
    int z,
    int level)
{
    const struct Editor_Tile* tile = &square->tiles[Editor_TileIndex(x, z, level)];
    struct ToriRS_MapFloor floor;

    if( tile->has_height && tile->height != 0 )
        return tile->height == 1 ? 0 : tile->height;
    Editor_TileDerive(square, x, z, level, &floor);
    return ops_units_nearest(ops_base(square, x, z, level), floor.height);
}

/* ---- brushes ---------------------------------------------------------------- */

double
Editor_BrushWeight(
    const struct Editor_Brush* brush,
    int dx,
    int dz)
{
    double distance;
    double reach;

    assert(brush);

    distance = sqrt((double)(dx * dx + dz * dz));
    /* The disc includes every tile whose centre is within radius + half a
     * tile, so radius 1 is the plus-shaped 3x3 minus its corners... plus the
     * corners at sqrt(2) < 1.5. */
    if( distance > (double)brush->radius + 0.5 )
        return 0.0;
    if( brush->falloff == EDITOR_BRUSH_FALLOFF_FLAT || brush->radius == 0 )
        return 1.0;
    reach = (double)brush->radius + 1.0;
    if( brush->falloff == EDITOR_BRUSH_FALLOFF_LINEAR )
        return 1.0 - distance / reach;
    return 0.5 * (1.0 + cos(3.14159265358979323846 * distance / reach));
}

/** The command for one tile, appended when it changes anything. */
static int
ops_emit_tile(
    const struct Editor_Square* square,
    int x,
    int z,
    int level,
    const struct Editor_Tile* after,
    struct Editor_Cmd* out,
    int* count,
    int max)
{
    const struct Editor_Tile* before = &square->tiles[Editor_TileIndex(x, z, level)];
    struct Editor_Cmd* command;

    if( memcmp(before, after, sizeof(*before)) == 0 )
        return 0;
    if( *count >= max )
        return 0;
    command = &out[(*count)++];
    Editor_CmdInitSquare(command, EDITOR_CMD_TILE, square->map_x, square->map_z);
    command->u.tile.level = level;
    command->u.tile.x = x;
    command->u.tile.z = z;
    command->u.tile.before = *before;
    command->u.tile.after = *after;
    return 1;
}

int
Editor_BrushCommands(
    struct Editor_Doc* doc,
    const struct Editor_BrushOp* op,
    struct Editor_Cmd* out,
    int max)
{
    int const radius = op ? op->brush.radius : 0;
    int count = 0;

    assert(doc);
    assert(op);
    assert(out);
    assert(op->level >= 0);
    assert(op->level < EDITOR_SQUARE_LEVELS);
    assert(radius >= 0);
    assert(radius <= EDITOR_BRUSH_RADIUS_MAX);

    for( int dz = -radius; dz <= radius; dz++ )
    {
        for( int dx = -radius; dx <= radius; dx++ )
        {
            double const weight = Editor_BrushWeight(&op->brush, dx, dz);
            int const world_x = op->world_x + dx;
            int const world_z = op->world_z + dz;
            struct Editor_Square* square;
            struct Editor_Tile after;
            int x;
            int z;

            if( weight <= 0.0 )
                continue;
            square = ops_square_at(doc, world_x, world_z, &x, &z);
            if( !square )
                continue;
            after = square->tiles[Editor_TileIndex(x, z, op->level)];

            switch( op->tool )
            {
            case EDITOR_TOOL_RAISE:
            case EDITOR_TOOL_LOWER:
            {
                int delta = (int)floor((double)op->brush.step * weight + 0.5);
                if( delta < 1 )
                    delta = 1;
                if( op->tool == EDITOR_TOOL_LOWER )
                    delta = -delta;
                after.has_height = 1;
                after.height = (uint8_t)ops_raw_of_units(
                    ops_units_step(ops_tile_units(square, x, z, op->level), delta));
                break;
            }
            case EDITOR_TOOL_FLATTEN:
                after.has_height = 1;
                after.height = (uint8_t)Editor_OpsRawForHeight(
                    ops_base(square, x, z, op->level), op->flatten_height);
                break;
            case EDITOR_TOOL_SMOOTH:
            {
                static const int k_near[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                int here;
                int sum = 0;
                int seen = 0;
                double target;

                (void)Editor_OpsDerivedHeight(doc, world_x, world_z, op->level, &here);
                for( int k = 0; k < 4; k++ )
                {
                    int there;
                    if( Editor_OpsDerivedHeight(
                            doc, world_x + k_near[k][0], world_z + k_near[k][1], op->level,
                            &there) )
                    {
                        sum += there;
                        seen++;
                    }
                }
                if( seen == 0 )
                    continue;
                target = (double)here
                         + ((double)sum / seen - (double)here) * (double)op->brush.strength
                               / 100.0 * weight;
                /* Under half a step from where it is: leave a generated tile
                 * generated rather than authoring it to the value it has. */
                if( fabs(target - (double)here) < OPS_UNIT / 2.0 )
                    continue;
                after.has_height = 1;
                after.height = (uint8_t)Editor_OpsRawForHeight(
                    ops_base(square, x, z, op->level), (int)floor(target + 0.5));
                break;
            }
            case EDITOR_TOOL_PAINT_UNDERLAY:
                assert(op->underlay_id >= 0);
                assert(op->underlay_id <= 0xff);
                after.underlay_id = (uint8_t)op->underlay_id;
                break;
            case EDITOR_TOOL_PAINT_OVERLAY:
                if( op->overlay_none )
                {
                    after.has_overlay = 0;
                    after.overlay_id = 0;
                    after.shape = 0;
                    after.rotation = 0;
                }
                else
                {
                    assert(op->overlay_id >= 0);
                    assert(op->overlay_id <= 0xffff);
                    after.has_overlay = 1;
                    after.overlay_id = (uint16_t)op->overlay_id;
                    after.shape = (uint8_t)(op->overlay_shape & 0xff);
                    after.rotation = (uint8_t)(op->overlay_rotation & 3);
                }
                break;
            case EDITOR_TOOL_PAINT_FLAGS:
                after.settings = (uint8_t)(op->flags_value & 0xff);
                break;
            default:
                /* Not a brush: the caller asked a brush of a click tool. */
                assert(0 && "Editor_BrushCommands: not a brush tool");
                return 0;
            }
            ops_emit_tile(square, x, z, op->level, &after, out, &count, max);
        }
    }
    return count;
}

/* ---- the rectangle ------------------------------------------------------------ */

int
Editor_TileRectNormalise(
    const struct Editor_TileRect* in,
    struct Editor_TileRect* out)
{
    assert(in);
    assert(out);

    if( !in->valid )
        return 0;
    *out = *in;
    if( out->x0 > out->x1 )
    {
        int const t = out->x0;
        out->x0 = out->x1;
        out->x1 = t;
    }
    if( out->z0 > out->z1 )
    {
        int const t = out->z0;
        out->z0 = out->z1;
        out->z1 = t;
    }
    if( out->level < 0 || out->level >= EDITOR_SQUARE_LEVELS )
        return 0;
    if( (out->x1 - out->x0 + 1) * (out->z1 - out->z0 + 1) > EDITOR_RECT_TILES_MAX )
        return 0;
    return 1;
}

int
Editor_TileClipCopy(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    struct Editor_TileClip* clip)
{
    struct Editor_TileRect r;
    int copied = 0;

    assert(doc);
    assert(rect);
    assert(clip);

    memset(clip, 0, sizeof(*clip));
    if( !Editor_TileRectNormalise(rect, &r) )
        return 0;
    clip->w = r.x1 - r.x0 + 1;
    clip->h = r.z1 - r.z0 + 1;
    for( int dz = 0; dz < clip->h; dz++ )
    {
        for( int dx = 0; dx < clip->w; dx++ )
        {
            int x;
            int z;
            struct Editor_Square* square = ops_square_at(doc, r.x0 + dx, r.z0 + dz, &x, &z);
            int const at = dz * clip->w + dx;
            if( !square )
                continue;
            clip->tiles[at] = square->tiles[Editor_TileIndex(x, z, r.level)];
            clip->present[at] = 1;
            if( dx == 0 && dz == 0 )
                clip->anchor_units = ops_tile_units(square, x, z, r.level);
            copied++;
        }
    }
    clip->valid = copied > 0;
    return copied;
}

int
Editor_TileClipPaste(
    struct Editor_Doc* doc,
    const struct Editor_TileClip* clip,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max)
{
    int count = 0;
    int delta = 0;

    assert(doc);
    assert(clip);
    assert(out);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);

    if( !clip->valid )
        return 0;
    {
        int x;
        int z;
        struct Editor_Square* anchor = ops_square_at(doc, world_x, world_z, &x, &z);
        if( anchor )
            delta = ops_tile_units(anchor, x, z, level) - clip->anchor_units;
    }

    for( int dz = 0; dz < clip->h; dz++ )
    {
        for( int dx = 0; dx < clip->w; dx++ )
        {
            int const at = dz * clip->w + dx;
            struct Editor_Square* square;
            struct Editor_Tile after;
            int x;
            int z;

            if( !clip->present[at] )
                continue;
            square = ops_square_at(doc, world_x + dx, world_z + dz, &x, &z);
            if( !square )
                continue;
            after = clip->tiles[at];
            if( after.has_height && after.height != 0 && delta != 0 )
            {
                int const units = after.height == 1 ? 0 : after.height;
                int next = units + delta;
                if( next == 1 )
                    next = delta > 0 ? 2 : 0;
                if( next < 0 )
                    next = 0;
                if( next > OPS_UNITS_MAX )
                    next = OPS_UNITS_MAX;
                after.height = (uint8_t)ops_raw_of_units(next);
            }
            ops_emit_tile(square, x, z, level, &after, out, &count, max);
        }
    }
    return count;
}

int
Editor_TileRectFill(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    const struct EditorUiTools* tools,
    struct Editor_Cmd* out,
    int max)
{
    struct Editor_TileRect r;
    int count = 0;

    assert(doc);
    assert(rect);
    assert(tools);
    assert(out);

    if( !Editor_TileRectNormalise(rect, &r) )
        return 0;
    for( int wz = r.z0; wz <= r.z1; wz++ )
    {
        for( int wx = r.x0; wx <= r.x1; wx++ )
        {
            int x;
            int z;
            struct Editor_Square* square = ops_square_at(doc, wx, wz, &x, &z);
            struct Editor_Tile after;
            if( !square )
                continue;
            after = square->tiles[Editor_TileIndex(x, z, r.level)];
            after.underlay_id = (uint8_t)(tools->underlay_id & 0xff);
            if( tools->overlay_none )
            {
                after.has_overlay = 0;
                after.overlay_id = 0;
                after.shape = 0;
                after.rotation = 0;
            }
            else
            {
                after.has_overlay = 1;
                after.overlay_id = (uint16_t)(tools->overlay_id & 0xffff);
                after.shape = (uint8_t)(tools->overlay_shape & 0xff);
                after.rotation = (uint8_t)(tools->overlay_rotation & 3);
            }
            ops_emit_tile(square, x, z, r.level, &after, out, &count, max);
        }
    }
    return count;
}

int
Editor_TileRectClear(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    struct Editor_Cmd* out,
    int max)
{
    struct Editor_TileRect r;
    struct Editor_Tile const empty = { 0 };
    int count = 0;

    assert(doc);
    assert(rect);
    assert(out);

    if( !Editor_TileRectNormalise(rect, &r) )
        return 0;
    for( int wz = r.z0; wz <= r.z1; wz++ )
    {
        for( int wx = r.x0; wx <= r.x1; wx++ )
        {
            int x;
            int z;
            struct Editor_Square* square = ops_square_at(doc, wx, wz, &x, &z);
            if( square )
                ops_emit_tile(square, x, z, r.level, &empty, out, &count, max);
        }
    }
    return count;
}

/* ---- locs ----------------------------------------------------------------------- */

void
Editor_LocRefAt(
    struct Editor_LocRef* ref,
    int world_x,
    int world_z,
    int level,
    int shape,
    int loc_id,
    int rotation)
{
    assert(ref);
    assert(world_x >= 0);
    assert(world_z >= 0);

    ref->map_x = world_x / EDITOR_SQUARE_X;
    ref->map_z = world_z / EDITOR_SQUARE_Z;
    ref->x = world_x % EDITOR_SQUARE_X;
    ref->z = world_z % EDITOR_SQUARE_Z;
    ref->level = level;
    ref->shape = shape;
    ref->loc_id = loc_id;
    ref->rotation = rotation;
}

void
Editor_LocRefWorld(
    const struct Editor_LocRef* ref,
    int* out_world_x,
    int* out_world_z)
{
    assert(ref);
    assert(out_world_x);
    assert(out_world_z);

    *out_world_x = ref->map_x * EDITOR_SQUARE_X + ref->x;
    *out_world_z = ref->map_z * EDITOR_SQUARE_Z + ref->z;
}

int
Editor_LocRefResolve(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* ref)
{
    struct Editor_Square* square;
    int index;

    assert(doc);
    assert(ref);

    square = Editor_DocFindSquare(doc, ref->map_x, ref->map_z);
    if( !square || !square->loaded )
        return -1;
    index = Editor_SquareLocFind(square, ref->level, ref->x, ref->z, ref->shape);
    if( index < 0 )
        return -1;
    if( ref->loc_id >= 0 && square->locs[index].loc_id != ref->loc_id )
        return -1;
    return index;
}

int
Editor_LocShapeCycle(int shape)
{
    static const int k_walls[] = { 0, 1, 2, 3, 9 };
    static const int k_decor[] = { 4, 5, 6, 7, 8 };
    static const int k_ground[] = { 10, 11 };

    for( int i = 0; i < 5; i++ )
        if( k_walls[i] == shape )
            return k_walls[(i + 1) % 5];
    for( int i = 0; i < 5; i++ )
        if( k_decor[i] == shape )
            return k_decor[(i + 1) % 5];
    for( int i = 0; i < 2; i++ )
        if( k_ground[i] == shape )
            return k_ground[(i + 1) % 2];
    if( shape >= 12 && shape <= 21 )
        return shape == 21 ? 12 : shape + 1;
    return shape;
}

static void
ops_loc_of_ref(
    const struct Editor_LocRef* ref,
    const struct Editor_Loc* in_doc,
    struct Editor_LocRef* out)
{
    *out = *ref;
    out->loc_id = in_doc->loc_id;
    out->rotation = in_doc->rotation;
    out->shape = in_doc->shape;
}

/** One loc moved / turned / reshaped / copied: the commands for it. A copy
 *  only adds; a change of square removes here and adds there. */
static int
ops_loc_emit(
    struct Editor_Doc* doc,
    const struct Editor_Loc* before,
    int from_map_x,
    int from_map_z,
    const struct Editor_Loc* after,
    int to_map_x,
    int to_map_z,
    int copy,
    struct Editor_Cmd* out,
    int* count,
    int max)
{
    struct Editor_Square* target = Editor_DocFindSquare(doc, to_map_x, to_map_z);

    if( !target || !target->loaded )
        return 0;
    if( copy || from_map_x != to_map_x || from_map_z != to_map_z )
    {
        if( *count + (copy ? 1 : 2) > max )
            return 0;
        if( !copy )
        {
            Editor_CmdInitSquare(&out[*count], EDITOR_CMD_LOC, from_map_x, from_map_z);
            out[*count].has_before = 1;
            out[*count].u.loc.before = *before;
            (*count)++;
        }
        Editor_CmdInitSquare(&out[*count], EDITOR_CMD_LOC, to_map_x, to_map_z);
        out[*count].has_after = 1;
        out[*count].u.loc.after = *after;
        (*count)++;
        return 1;
    }
    if( memcmp(before, after, sizeof(*before)) == 0 )
        return 1;
    if( *count >= max )
        return 0;
    Editor_CmdInitSquare(&out[*count], EDITOR_CMD_LOC, from_map_x, from_map_z);
    out[*count].has_before = 1;
    out[*count].has_after = 1;
    out[*count].u.loc.before = *before;
    out[*count].u.loc.after = *after;
    (*count)++;
    return 1;
}

/** The loc of the target square holding the tile+shape a transformed loc
 *  lands on when that loc is not in the selection (and is not the loc
 *  itself), or -1. A selected one moves out of the way first (the order in
 *  ops_transform), so it does not count. */
static int
ops_dest_occupant(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    const struct Editor_Loc* after,
    int to_map_x,
    int to_map_z,
    int from_map_x,
    int from_map_z,
    int from_index,
    int copy)
{
    struct Editor_Square* target = Editor_DocFindSquare(doc, to_map_x, to_map_z);
    int taken;

    if( !target || !target->loaded )
        return -1;
    taken = Editor_SquareLocFind(target, after->level, after->x, after->z, after->shape);
    if( taken < 0 )
        return -1;
    if( !copy && to_map_x == from_map_x && to_map_z == from_map_z && taken == from_index )
        return -1;
    if( copy )
        return taken;
    for( int i = 0; i < n; i++ )
        if( refs[i].map_x == to_map_x && refs[i].map_z == to_map_z
            && Editor_LocRefResolve(doc, &refs[i]) == taken )
            return -1;
    return taken;
}

/** Where an unselected loc pushed off (dest) by a selection translated by
 *  (dx, dz) goes: back along the run of selected locs of that level and shape
 *  that land on each other's tiles, to the one tile the run leaves empty. A
 *  single wall piece slid along its wall line swaps with its neighbour; a run
 *  of three slid one tile pushes the neighbour to the run's tail. */
static void
ops_vacated(
    const struct Editor_LocRef* refs,
    int n,
    int dx,
    int dz,
    int origin_x,
    int origin_z,
    int level,
    int shape,
    int* out_x,
    int* out_z)
{
    int x = origin_x;
    int z = origin_z;

    for( int step = 0; step < n; step++ )
    {
        int moved = 0;
        for( int i = 0; i < n && !moved; i++ )
        {
            int wx;
            int wz;
            Editor_LocRefWorld(&refs[i], &wx, &wz);
            if( refs[i].level == level && refs[i].shape == shape && wx + dx == x && wz + dz == z )
            {
                x = wx;
                z = wz;
                moved = 1;
            }
        }
        if( !moved )
            break;
    }
    *out_x = x;
    *out_z = z;
}

struct OpsOrder
{
    int index;
    long key;
};

static int
ops_order_cmp(
    const void* a,
    const void* b)
{
    const struct OpsOrder* oa = a;
    const struct OpsOrder* ob = b;
    if( oa->key != ob->key )
        return oa->key > ob->key ? -1 : 1;
    return oa->index - ob->index;
}

/**
 * The shared body of every selection op. Moves apply the loc furthest along
 * the direction of travel first, so a loc moving onto a tile another selected
 * loc is leaving finds that tile already vacated -- apply_loc removes by
 * (tile, shape), and the other order would remove the wrong one.
 */
static int
ops_transform(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int dx,
    int dz,
    int quarters,
    int reshape,
    int copy,
    const int* shape_to,
    const int* rotation_to,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    struct OpsOrder order[EDITOR_LOC_SELECTION_MAX];
    struct
    {
        struct Editor_Loc loc;
        int map_x;
        int map_z;
    } swaps[EDITOR_LOC_SELECTION_MAX];
    int swap_count = 0;
    int count = 0;

    assert(doc);
    assert(refs || n == 0);
    assert(out);
    assert(result);
    assert(n >= 0);
    assert(n <= EDITOR_LOC_SELECTION_MAX);

    memset(result, 0, sizeof(*result));
    for( int i = 0; i < n; i++ )
    {
        int wx;
        int wz;
        Editor_LocRefWorld(&refs[i], &wx, &wz);
        order[i].index = i;
        order[i].key = (long)wx * dx + (long)wz * dz;
    }
    qsort(order, (size_t)n, sizeof(order[0]), ops_order_cmp);

    /* Results in the INPUT order: the selection stays the order it was made. */
    result->ref_count = n;
    for( int i = 0; i < n; i++ )
        result->refs[i] = refs[i];

    for( int k = 0; k < n; k++ )
    {
        int const i = order[k].index;
        const struct Editor_LocRef* ref = &refs[i];
        struct Editor_Square* square = Editor_DocFindSquare(doc, ref->map_x, ref->map_z);
        int const index = Editor_LocRefResolve(doc, ref);
        struct Editor_Loc before;
        struct Editor_Loc after;
        int wx;
        int wz;
        int to_map_x;
        int to_map_z;
        int to_x;
        int to_z;

        if( index < 0 )
            continue;
        before = square->locs[index];
        after = before;
        Editor_LocRefWorld(ref, &wx, &wz);
        if( !ops_split(wx + dx, wz + dz, &to_map_x, &to_map_z, &to_x, &to_z) )
            continue;
        after.x = to_x;
        after.z = to_z;
        after.rotation = (before.rotation + quarters) & 3;
        if( reshape )
            after.shape = Editor_LocShapeCycle(before.shape);
        if( shape_to && shape_to[i] >= 0 )
            after.shape = shape_to[i];
        if( rotation_to && rotation_to[i] >= 0 )
            after.rotation = rotation_to[i] & 3;
        /*
         * A tile holds one loc per shape, so landing on one that is not part
         * of the selection needs a decision:
         *
         *   - a MOVE swaps: the loc standing there goes to the tile the move
         *     leaves empty. Sliding a wall piece (a door, a window) along its
         *     wall line meets a wall on every neighbouring tile -- that is
         *     the common case, not the odd one -- and a swap keeps the line
         *     whole: no gap behind, no second wall on a tile, and the next
         *     nudge slides on. Refusing left the piece stuck with no word
         *     why; replacing would eat the line one wall per key press.
         *   - a copy or a reshape onto a taken shape has nowhere to put the
         *     loc standing there, so this loc stays and the result says so.
         *
         * The occupant leaves (a delete, before this loc lands) and comes
         * back at the vacated tile after every selected loc has moved -- the
         * tile is empty only then -- in the same stroke, so one undo puts
         * all of it back.
         */
        {
            int const occupant =
                ops_dest_occupant(doc, refs, n, &after, to_map_x, to_map_z, ref->map_x, ref->map_z, index, copy);
            if( occupant >= 0 )
            {
                struct Editor_Square* target = Editor_DocFindSquare(doc, to_map_x, to_map_z);
                struct Editor_Loc const held = target->locs[occupant];
                int vx;
                int vz;
                int v_map_x;
                int v_map_z;
                int v_x;
                int v_z;

                if( copy || (dx == 0 && dz == 0) || swap_count >= EDITOR_LOC_SELECTION_MAX
                    || count + 3 > max )
                {
                    if( result->blocked == 0 )
                        Editor_LocRefAt(&result->blocked_at, to_map_x * EDITOR_SQUARE_X + to_x,
                            to_map_z * EDITOR_SQUARE_Z + to_z, after.level, after.shape, held.loc_id,
                            held.rotation);
                    result->blocked++;
                    continue;
                }
                ops_vacated(refs, n, dx, dz, wx, wz, before.level, before.shape, &vx, &vz);
                if( !ops_split(vx, vz, &v_map_x, &v_map_z, &v_x, &v_z) )
                    continue;
                Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, to_map_x, to_map_z);
                out[count].has_before = 1;
                out[count].u.loc.before = held;
                count++;
                swaps[swap_count].loc = held;
                swaps[swap_count].loc.x = v_x;
                swaps[swap_count].loc.z = v_z;
                swaps[swap_count].map_x = v_map_x;
                swaps[swap_count].map_z = v_map_z;
                swap_count++;
                if( result->swapped == 0 )
                    result->swapped_loc_id = held.loc_id;
                result->swapped++;
            }
        }

        if( !ops_loc_emit(
                doc, &before, ref->map_x, ref->map_z, &after, to_map_x, to_map_z, copy, out,
                &count, max) )
            continue;

        ops_loc_of_ref(ref, &after, &result->refs[i]);
        result->refs[i].map_x = to_map_x;
        result->refs[i].map_z = to_map_z;
        result->refs[i].x = to_x;
        result->refs[i].z = to_z;
    }
    /* The swapped locs land last: their tiles are empty now. */
    for( int k = 0; k < swap_count && count < max; k++ )
    {
        Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, swaps[k].map_x, swaps[k].map_z);
        out[count].has_after = 1;
        out[count].u.loc.after = swaps[k].loc;
        count++;
    }
    result->command_count = count;
    return count;
}

int
Editor_LocOpsPlace(
    struct Editor_Doc* doc,
    int loc_id,
    int shape,
    int rotation,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    struct Editor_Square* square;
    int occupant;
    int x;
    int z;

    assert(doc);
    assert(out);
    assert(result);
    assert(loc_id >= 0);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);

    memset(result, 0, sizeof(*result));
    square = ops_square_at(doc, world_x, world_z, &x, &z);
    if( !square || max < 1 )
        return 0;
    occupant = Editor_SquareLocFind(square, level, x, z, shape);
    Editor_CmdInitSquare(&out[0], EDITOR_CMD_LOC, square->map_x, square->map_z);
    /* A tile holds one loc per shape (the document refuses a second), so a
     * place onto a taken shape REPLACES it -- what the ghost previewed by
     * hiding the occupant -- and its undo puts the occupant back. */
    if( occupant >= 0 )
    {
        out[0].has_before = 1;
        out[0].u.loc.before = square->locs[occupant];
    }
    out[0].has_after = 1;
    out[0].u.loc.after.loc_id = loc_id;
    out[0].u.loc.after.shape = shape;
    out[0].u.loc.after.rotation = rotation & 3;
    out[0].u.loc.after.level = level;
    out[0].u.loc.after.x = x;
    out[0].u.loc.after.z = z;
    Editor_LocRefAt(&result->refs[0], world_x, world_z, level, shape, loc_id, rotation & 3);
    result->ref_count = 1;
    result->command_count = 1;
    return 1;
}

int
Editor_LocOpsNudge(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int dx,
    int dz,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    return ops_transform(doc, refs, n, dx, dz, 0, 0, 0, NULL, NULL, out, max, result);
}

int
Editor_LocOpsMoveTo(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int world_x,
    int world_z,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    int wx;
    int wz;

    assert(refs || n == 0);
    if( n == 0 )
    {
        assert(result);
        memset(result, 0, sizeof(*result));
        return 0;
    }
    Editor_LocRefWorld(&refs[0], &wx, &wz);
    return ops_transform(doc, refs, n, world_x - wx, world_z - wz, 0, 0, 0, NULL, NULL, out, max, result);
}

int
Editor_LocOpsRotate(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int quarters,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    return ops_transform(doc, refs, n, 0, 0, quarters & 3, 0, 0, NULL, NULL, out, max, result);
}

int
Editor_LocOpsReshape(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    return ops_transform(doc, refs, n, 0, 0, 0, 1, 0, NULL, NULL, out, max, result);
}

int
Editor_LocOpsDuplicate(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int dx,
    int dz,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    assert(dx != 0 || dz != 0);
    return ops_transform(doc, refs, n, dx, dz, 0, 0, 1, NULL, NULL, out, max, result);
}

int
Editor_LocOpsDelete(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    int count = 0;

    assert(doc);
    assert(refs || n == 0);
    assert(out);
    assert(result);

    memset(result, 0, sizeof(*result));
    for( int i = 0; i < n && count < max; i++ )
    {
        struct Editor_Square* square = Editor_DocFindSquare(doc, refs[i].map_x, refs[i].map_z);
        int const index = Editor_LocRefResolve(doc, &refs[i]);
        if( index < 0 )
            continue;
        Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, refs[i].map_x, refs[i].map_z);
        out[count].has_before = 1;
        out[count].u.loc.before = square->locs[index];
        count++;
    }
    result->command_count = count;
    return count;
}

/* ---- spawns ------------------------------------------------------------------- */

void
Editor_SpawnDocKeyAt(
    char out[EDITOR_DOC_KEY_MAX],
    int world_x,
    int world_z)
{
    assert(out);
    assert(world_x >= 0);
    assert(world_z >= 0);
    Editor_SquareKey(out, world_x / EDITOR_SQUARE_X, world_z / EDITOR_SQUARE_Z);
}

void
Editor_SpawnGeneratedKey(
    char out[EDITOR_DOC_KEY_MAX],
    int map_x,
    int map_z)
{
    assert(out);
    snprintf(out, EDITOR_DOC_KEY_MAX, "rs2:areas/world/configs/m%d_%d.spawn", map_x, map_z);
}

void
Editor_SpawnCommand(
    struct Editor_Cmd* out,
    const struct Editor_SpawnRow* row,
    int adding)
{
    char key[EDITOR_DOC_KEY_MAX];

    assert(out);
    assert(row);
    assert(row->name[0]);

    Editor_SpawnDocKeyAt(key, row->abs_x, row->abs_z);
    Editor_CmdInit(
        out, EDITOR_DOC_SPAWN, key, row->suppress ? EDITOR_CMD_SPAWN_SUPPRESS : EDITOR_CMD_SPAWN);
    out->has_before = adding ? 0 : 1;
    out->has_after = adding ? 1 : 0;
    out->u.spawn.is_obj = row->is_obj ? 1 : 0;
    snprintf(out->u.spawn.name, sizeof(out->u.spawn.name), "%s", row->name);
    out->u.spawn.abs_x = row->abs_x;
    out->u.spawn.abs_z = row->abs_z;
    out->u.spawn.level = row->level;
    out->u.spawn.count = row->count < 1 ? 1 : row->count;
}

int
Editor_SpawnIsSuppressed(
    const struct Editor_SpawnFile* edited,
    const struct Editor_SpawnRow* row)
{
    struct Editor_SpawnRow probe;

    assert(row);
    if( !edited )
        return 0;
    probe = *row;
    probe.suppress = 1;
    return Editor_SpawnFind(edited, &probe) >= 0;
}

int
Editor_SpawnDeleteCommand(
    const struct Editor_SpawnFile* edited,
    const struct Editor_SpawnRow* row,
    struct Editor_Cmd* out)
{
    struct Editor_SpawnRow target;

    assert(row);
    assert(out);
    assert(!row->suppress);

    target = *row;
    if( edited && Editor_SpawnFind(edited, &target) >= 0 )
    {
        Editor_SpawnCommand(out, &target, 0);
        return 1;
    }
    if( Editor_SpawnIsSuppressed(edited, row) )
        return 0;
    target.suppress = 1;
    Editor_SpawnCommand(out, &target, 1);
    return 1;
}

int
Editor_SpawnOpsDuplicate(
    const struct Editor_SpawnRow* rows,
    int n,
    int dx,
    int dz,
    int dlevel,
    struct Editor_Cmd* out,
    int max,
    struct Editor_SpawnRow* out_rows)
{
    int count = 0;

    assert(rows || n == 0);
    assert(n >= 0);
    assert(out || max == 0);

    for( int i = 0; i < n && count < max; i++ )
    {
        struct Editor_SpawnRow copy = rows[i];

        copy.suppress = 0;
        copy.abs_x += dx;
        copy.abs_z += dz;
        copy.level += dlevel;
        if( copy.abs_x < 0 || copy.abs_z < 0 || copy.level < 0 || copy.level >= EDITOR_SQUARE_LEVELS )
            continue;
        Editor_SpawnCommand(&out[count], &copy, 1);
        if( out_rows )
            out_rows[count] = copy;
        count++;
    }
    return count;
}

/* ---- loc shapes: which a loc has, and fitting a wall to its line --------------- */

static const char* const k_loc_shape_names[EDITOR_LOC_SHAPE_COUNT] = {
    "wall",          "wall corner post",   "wall L",          "wall corner",        "decor inside",
    "decor outside", "decor diag outside", "decor diag inside", "decor diag double", "wall diagonal",
    "centrepiece",   "centrepiece diag",   "roof",            "roof outer",         "roof inner",
    "roof hard inner", "roof hard outer",  "roof flat",       "roof overhang",      "overhang outer",
    "overhang inner", "overhang hard outer", "ground decor",
};

/* The order a loc's shapes are offered and cycled in: what is placed most. */
static const int k_loc_shape_order[EDITOR_LOC_SHAPE_COUNT] = {
    10, 11, 22, 0, 1, 2, 3, 9, 4, 5, 6, 7, 8, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
};

const char*
Editor_LocShapeName(int shape)
{
    assert(shape >= 0);
    assert(shape < EDITOR_LOC_SHAPE_COUNT);
    return k_loc_shape_names[shape];
}

int
Editor_LocShapeModelShape(int shape)
{
    assert(shape >= 0);
    assert(shape < EDITOR_LOC_SHAPE_COUNT);
    /* world_scenery.u.c: wall decor 4..8 all draw shape 4's models (offset,
     * turned, doubled), the diagonal centrepiece draws shape 10's turned. */
    if( shape >= 4 && shape <= 8 )
        return 4;
    if( shape == 11 )
        return 10;
    return shape;
}

uint32_t
Editor_LocShapeMask(const struct ToriRS_Location* cfg)
{
    uint32_t have = 0;
    uint32_t mask = 0;

    assert(cfg);
    if( cfg->shapes_and_model_count <= 0 || !cfg->models || !cfg->lengths )
        return 0;
    if( !cfg->shapes )
    {
        /* No shape list: the models are a centrepiece's and nothing else
         * (scenery_load_model: "centrepiece only"). */
        if( cfg->lengths[0] > 0 )
            have = 1u << 10;
    }
    else
        for( int i = 0; i < cfg->shapes_and_model_count; i++ )
            if( cfg->shapes[i] >= 0 && cfg->shapes[i] < EDITOR_LOC_SHAPE_COUNT && cfg->lengths[i] > 0 )
                have |= 1u << cfg->shapes[i];
    for( int s = 0; s < EDITOR_LOC_SHAPE_COUNT; s++ )
        if( have & (1u << Editor_LocShapeModelShape(s)) )
            mask |= 1u << s;
    return mask;
}

int
Editor_LocShapeFirst(uint32_t mask)
{
    for( int i = 0; i < EDITOR_LOC_SHAPE_COUNT; i++ )
        if( mask & (1u << k_loc_shape_order[i]) )
            return k_loc_shape_order[i];
    return -1;
}

int
Editor_LocShapeList(
    uint32_t mask,
    int out[EDITOR_LOC_SHAPE_COUNT])
{
    int count = 0;

    assert(out);
    for( int i = 0; i < EDITOR_LOC_SHAPE_COUNT; i++ )
        if( mask & (1u << k_loc_shape_order[i]) )
            out[count++] = k_loc_shape_order[i];
    return count;
}

int
Editor_LocShapeNext(
    uint32_t mask,
    int shape)
{
    int at = -1;

    if( mask == 0 )
        return shape;
    if( shape < 0 || shape >= EDITOR_LOC_SHAPE_COUNT || !(mask & (1u << shape)) )
        return Editor_LocShapeFirst(mask);
    for( int i = 0; i < EDITOR_LOC_SHAPE_COUNT; i++ )
        if( k_loc_shape_order[i] == shape )
            at = i;
    for( int step = 1; step <= EDITOR_LOC_SHAPE_COUNT; step++ )
    {
        int const s = k_loc_shape_order[((at < 0 ? -1 : at) + step) % EDITOR_LOC_SHAPE_COUNT];
        if( mask & (1u << s) )
            return s;
    }
    return shape;
}

/** The edges (bit e: 0 west, 1 north, 2 east, 3 south) a wall of this shape
 *  and rotation stands on: a straight wall one, an L two. */
static unsigned
ops_wall_edges(
    int shape,
    int rotation)
{
    if( shape == 0 )
        return 1u << (rotation & 3);
    if( shape == 2 )
        return (1u << (rotation & 3)) | (1u << ((rotation + 1) & 3));
    return 0;
}

int
Editor_LocFitWall(
    struct Editor_Doc* doc,
    int loc_id,
    uint32_t mask,
    int world_x,
    int world_z,
    int level,
    int* out_shape,
    int* out_rotation)
{
    int votes[4] = { 0, 0, 0, 0 };
    int diag_rotation = -1;
    int vertical = -1;
    int horizontal = -1;

    assert(doc);
    assert(out_shape);
    assert(out_rotation);

    for( int dz = -1; dz <= 1; dz++ )
        for( int dx = -1; dx <= 1; dx++ )
        {
            struct Editor_Square* square;
            int x;
            int z;
            if( dx == 0 && dz == 0 )
                continue;
            square = ops_square_at(doc, world_x + dx, world_z + dz, &x, &z);
            if( !square )
                continue;
            for( int i = 0; i < square->loc_count; i++ )
            {
                const struct Editor_Loc* loc = &square->locs[i];
                unsigned edges;
                if( loc->loc_id != loc_id || loc->level != level || loc->x != x || loc->z != z )
                    continue;
                if( dx != 0 && dz != 0 )
                {
                    /* A diagonal wall runs on: rotations 1 and 3 join the
                     * north-west and south-east neighbours, 0 and 2 the
                     * south-west and north-east (m50_50's castle: 9 rot 3 on
                     * (15,28) and (16,27)). */
                    if( loc->shape == 9 && ((dx * dz < 0) == ((loc->rotation & 1) == 1)) )
                        diag_rotation = loc->rotation & 3;
                    continue;
                }
                edges = ops_wall_edges(loc->shape, loc->rotation);
                /* A north or south neighbour continues its west / east edge
                 * into this tile; an east or west one its north / south. */
                if( dz != 0 )
                {
                    if( edges & 1u )
                        votes[0]++;
                    if( edges & 4u )
                        votes[2]++;
                }
                else
                {
                    if( edges & 2u )
                        votes[1]++;
                    if( edges & 8u )
                        votes[3]++;
                }
            }
        }
    if( votes[0] || votes[2] )
        vertical = votes[2] > votes[0] ? 2 : 0;
    if( votes[1] || votes[3] )
        horizontal = votes[3] > votes[1] ? 3 : 1;

    if( vertical >= 0 && horizontal >= 0 && (mask & (1u << 2)) )
    {
        /* An L stands on edges r and r+1. */
        *out_shape = 2;
        *out_rotation = ((vertical == 0 && horizontal == 1) ? 0
                         : (vertical == 2 && horizontal == 1) ? 1
                         : (vertical == 2 && horizontal == 3) ? 2
                                                              : 3);
        return 1;
    }
    if( (vertical >= 0 || horizontal >= 0) && (mask & 1u) )
    {
        *out_shape = 0;
        *out_rotation = vertical >= 0 ? vertical : horizontal;
        return 1;
    }
    if( diag_rotation >= 0 && (mask & (1u << 9)) )
    {
        *out_shape = 9;
        *out_rotation = diag_rotation;
        return 1;
    }
    return 0;
}

int
Editor_LocOpsReshapeTo(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    const int* shapes,
    const int* rotations,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result)
{
    assert(shapes);
    return ops_transform(doc, refs, n, 0, 0, 0, 0, 0, shapes, rotations, out, max, result);
}
