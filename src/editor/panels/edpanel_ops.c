#include "edpanel_ops.h"

#include "editor/editor_derive.h"
#include "engine/torirs_types.h"
#include "mem/torirs_mem.h"

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

/** A number in [0, 1) drawn from a seed, a world tile and a lane (which
 *  of several draws at one tile): the same every time, so a stroke that
 *  revisits a tile decides the same thing about it. */
static double
ops_hash_unit(
    uint32_t seed,
    int world_x,
    int world_z,
    int lane)
{
    uint32_t h = seed * 0x9E3779B1u;
    h ^= (uint32_t)world_x * 0x85EBCA77u;
    h ^= (uint32_t)world_z * 0xC2B2AE3Du;
    h ^= (uint32_t)lane * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (double)(h >> 8) / 16777216.0;
}

/* ---- Roughen's relief -------------------------------------------------------- */

/** Octaves at most: 32 tiles down to 2. A wave shorter than two tiles is a
 *  draw per tile again, which is the grain the relief is there to replace. */
#define OPS_ROUGHEN_OCTAVES 5
/** What the summed octaves are divided by so that a hill's top is about the
 *  step. Measured over four 256x256 reliefs at each of ten scales and three
 *  details: with this the 99th percentile of the magnitude is 0.85..0.94 of
 *  the step and under 1 tile in 100 is cut off at it (editor_placement_test
 *  holds a relief to that). */
#define OPS_ROUGHEN_PEAK 0.58

/** One octave of gradient noise at a point of a lattice: each corner carries
 *  a direction drawn from the seed, the value is their pull blended across
 *  the cell (quintic, so the slope is continuous too). 0 at every corner. */
static double
ops_gradient_noise(
    uint32_t seed,
    double x,
    double z,
    int lane)
{
    double const cell_x = floor(x);
    double const cell_z = floor(z);
    double const tx = x - cell_x;
    double const tz = z - cell_z;
    double const sx = tx * tx * tx * (tx * (tx * 6.0 - 15.0) + 10.0);
    double const sz = tz * tz * tz * (tz * (tz * 6.0 - 15.0) + 10.0);
    double pull[2][2];
    double near_row;
    double far_row;

    for( int cz = 0; cz < 2; cz++ )
        for( int cx = 0; cx < 2; cx++ )
        {
            double const angle =
                ops_hash_unit(seed, (int)cell_x + cx, (int)cell_z + cz, lane) * 2.0 * 3.14159265358979323846;
            pull[cz][cx] = cos(angle) * (tx - (double)cx) + sin(angle) * (tz - (double)cz);
        }
    near_row = pull[0][0] + sx * (pull[0][1] - pull[0][0]);
    far_row = pull[1][0] + sx * (pull[1][1] - pull[1][0]);
    return near_row + sz * (far_row - near_row);
}

double
Editor_RoughenRelief(
    uint32_t seed,
    int world_x,
    int world_z,
    int scale,
    int detail)
{
    double wavelength;
    double amplitude = 1.0;
    double sum = 0.0;
    double power = 0.0;
    double gain;
    double value;

    if( scale <= 0 )
        scale = EDITOR_ROUGHEN_SCALE_DEFAULT;
    if( scale > EDITOR_ROUGHEN_SCALE_MAX )
        scale = EDITOR_ROUGHEN_SCALE_MAX;
    if( detail < 0 )
        detail = 0;
    if( detail > 100 )
        detail = 100;
    if( scale < 2 )
        return ops_hash_unit(seed, world_x, world_z, 0) * 2.0 - 1.0;

    gain = 0.25 + 0.5 * (double)detail / 100.0;
    wavelength = (double)scale;
    for( int octave = 0; octave < OPS_ROUGHEN_OCTAVES && wavelength >= 2.0; octave++ )
    {
        /* A lattice of its own per octave, shifted by a draw: every octave
         * is zero on its corners, and corners that coincide are a grid of
         * tiles nothing moves. The tile's centre, so no tile sits on one. */
        double const shift_x = ops_hash_unit(seed, octave, 17, 6) * 64.0;
        double const shift_z = ops_hash_unit(seed, octave, 31, 7) * 64.0;

        sum += amplitude * ops_gradient_noise(seed, ((double)world_x + 0.5) / wavelength + shift_x,
                               ((double)world_z + 0.5) / wavelength + shift_z, 8 + octave);
        power += amplitude * amplitude;
        amplitude *= gain;
        wavelength *= 0.5;
    }
    /* Octaves add as independent waves do (by power), so the step stays the
     * height of a hill whatever the detail. */
    value = sum / (sqrt(power) * OPS_ROUGHEN_PEAK);
    return value < -1.0 ? -1.0 : value > 1.0 ? 1.0 : value;
}

/** A world tile as a stroke's key: never 0. */
static uint32_t
ops_roughen_key(
    int world_x,
    int world_z,
    int level)
{
    return ((((uint32_t)level & 3u) << 28) | (((uint32_t)world_z & 0x3fffu) << 14) | ((uint32_t)world_x & 0x3fffu)) + 1u;
}

/** The slot of `key` in the stroke's table, or the empty slot it would
 *  take; -1 when the table is full and does not hold it. */
static int
ops_roughen_slot(
    const struct Editor_RoughenStroke* stroke,
    uint32_t key)
{
    uint32_t slot = (key * 2654435761u) & (EDITOR_ROUGHEN_STROKE_TILES - 1);

    for( int probe = 0; probe < EDITOR_ROUGHEN_STROKE_TILES; probe++ )
    {
        if( stroke->keys[slot] == key || stroke->keys[slot] == 0 )
            return (int)slot;
        slot = (slot + 1) & (EDITOR_ROUGHEN_STROKE_TILES - 1);
    }
    return -1;
}

/** How far the stroke has pushed a tile, in units (0: not yet). */
static int
ops_roughen_done(
    const struct Editor_RoughenStroke* stroke,
    int world_x,
    int world_z,
    int level)
{
    uint32_t const key = ops_roughen_key(world_x, world_z, level);
    int const slot = ops_roughen_slot(stroke, key);

    return slot >= 0 && stroke->keys[slot] == key ? stroke->units[slot] : 0;
}

/** What one application asks of a tile, in whole units: the relief there,
 *  the step its amplitude, the weight fading it. The fraction is a draw of
 *  the stroke's seed and the tile -- one threshold per tile for the whole
 *  stroke, so the ask only grows with the weight (never ratchets on a
 *  lucky draw) and a step of one still moves ground where rounding to the
 *  nearest left every tile under half a unit alone. */
static int
ops_roughen_want(
    const struct Editor_BrushOp* op,
    int world_x,
    int world_z,
    double weight)
{
    double const relief = Editor_RoughenRelief(op->noise_seed, world_x, world_z, op->noise_scale, op->noise_detail);
    double const amount = (double)op->brush.step * weight * relief;

    return (int)floor(amount + ops_hash_unit(op->noise_seed, world_x, world_z, 5));
}

void
Editor_RoughenStrokeBegin(
    struct Editor_RoughenStroke* stroke,
    uint32_t seed)
{
    assert(stroke);
    memset(stroke, 0, sizeof(*stroke));
    stroke->seed = seed;
}

void
Editor_RoughenStrokeNote(
    struct Editor_RoughenStroke* stroke,
    const struct Editor_BrushOp* op,
    const struct Editor_Cmd* commands,
    int count)
{
    assert(stroke);
    assert(op);
    assert(op->tool == EDITOR_TOOL_NOISE);
    assert(count >= 0);
    if( count == 0 )
        return;
    assert(commands);

    for( int i = 0; i < count; i++ )
    {
        const struct Editor_Cmd* command = &commands[i];
        int map_x;
        int map_z;
        int world_x;
        int world_z;
        uint32_t key;
        int slot;

        if( command->kind != EDITOR_CMD_TILE || !Editor_SquareKeyParse(command->doc_key, &map_x, &map_z) )
            continue;
        world_x = map_x * EDITOR_SQUARE_X + command->u.tile.x;
        world_z = map_z * EDITOR_SQUARE_Z + command->u.tile.z;
        key = ops_roughen_key(world_x, world_z, command->u.tile.level);
        slot = ops_roughen_slot(stroke, key);
        if( slot < 0 )
            continue;
        if( stroke->keys[slot] == 0 )
        {
            /* Half full is where the probes start to cost: stop taking new
             * tiles rather than slow every later lookup. */
            if( stroke->count >= EDITOR_ROUGHEN_STROKE_TILES / 2 )
                continue;
            stroke->keys[slot] = key;
            stroke->count++;
        }
        stroke->units[slot] = (int16_t)ops_roughen_want(
            op, world_x, world_z, Editor_BrushWeight(&op->brush, world_x - op->world_x, world_z - op->world_z));
    }
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
            case EDITOR_TOOL_HEIGHT:
            {
                /* Derived heights grow DOWN: a positive gap is a target
                 * below the tile. */
                int here;
                double gap;
                double move;

                (void)Editor_OpsDerivedHeight(doc, world_x, world_z, op->level, &here);
                gap = (double)(op->height_target - here);
                if( fabs(gap) < OPS_UNIT / 2.0 )
                    continue;
                if( (op->height_mode == EDITOR_HEIGHT_MODE_RAISE && gap > 0)
                    || (op->height_mode == EDITOR_HEIGHT_MODE_LOWER && gap < 0) )
                    continue;
                move = gap * (double)op->brush.strength / 100.0 * weight;
                if( fabs(move) < OPS_UNIT )
                    move = gap < 0 ? -OPS_UNIT : OPS_UNIT;
                if( fabs(move) > fabs(gap) )
                    move = gap;
                after.has_height = 1;
                after.height = (uint8_t)Editor_OpsRawForHeight(
                    ops_base(square, x, z, op->level), here + (int)floor(move + 0.5));
                break;
            }
            case EDITOR_TOOL_NOISE:
            {
                /* What this application asks of the tile, less what the
                 * stroke has pushed it already: only the part of the ask
                 * that is further the same way (the weight was higher this
                 * time). On its own, the whole ask. */
                int const want = ops_roughen_want(op, world_x, world_z, weight);
                int const done =
                    op->noise_stroke ? ops_roughen_done(op->noise_stroke, world_x, world_z, op->level) : 0;
                int delta = 0;
                if( (want > 0 && want > done) || (want < 0 && want < done) )
                    delta = want - done;
                if( delta == 0 )
                    continue;
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
                assert(op->underlay_id <= EDITOR_UNDERLAY_MAX);
                /* A mix under 100 paints a seeded share of the tiles,
                 * thinning with the falloff; at 100 every tile, whatever
                 * the falloff, as the flat paint always did. */
                if( op->paint_mix > 0 && op->paint_mix < 100
                    && ops_hash_unit(op->noise_seed, world_x, world_z, 3) * 100.0 >= (double)op->paint_mix * weight )
                    continue;
                after.underlay_id = (uint16_t)op->underlay_id;
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

/* ---- the path ------------------------------------------------------------------ */

struct OpsPathExisting
{
    struct Editor_Doc* doc;
    int level;
    int overlay_id;
};

/* Editor_PathExistingFn: the tile already holds the overlay being laid. */
static int
ops_path_existing(
    void* user,
    int world_x,
    int world_z,
    int* out_shape,
    int* out_rotation)
{
    struct OpsPathExisting const* ctx = user;
    struct Editor_Square* square;
    const struct Editor_Tile* tile;
    int x;
    int z;

    square = ops_square_at(ctx->doc, world_x, world_z, &x, &z);
    if( !square )
        return 0;
    tile = &square->tiles[Editor_TileIndex(x, z, ctx->level)];
    if( !tile->has_overlay || tile->overlay_id != ctx->overlay_id || tile->shape > 11 )
        return 0;
    *out_shape = tile->shape;
    *out_rotation = tile->rotation & 3;
    return 1;
}

int
Editor_PathCommands(
    struct Editor_Doc* doc,
    int level,
    const struct Editor_PathParams* params,
    int overlay_id,
    const int* xs,
    const int* zs,
    int count,
    struct Editor_Cmd* out,
    int max,
    int* out_closed)
{
    struct OpsPathExisting ctx;
    struct Editor_PathTile* plan;
    int planned;
    int written = 0;
    int closed = 0;

    assert(doc);
    assert(params);
    assert(level >= 0);
    assert(level < 4);
    assert(overlay_id >= 0);
    assert(overlay_id <= 0xffff);
    assert(out);
    assert(max > 0);

    ctx.doc = doc;
    ctx.level = level;
    ctx.overlay_id = overlay_id;
    plan = ToriMem_Alloc(ToriMem_Editor(), sizeof(*plan) * (size_t)max);
    assert(plan);
    planned = Editor_PathPlan(params, xs, zs, count, ops_path_existing, &ctx, plan, max);
    for( int i = 0; i < planned; i++ )
    {
        struct Editor_Square* square;
        struct Editor_Tile after;
        int x;
        int z;

        square = ops_square_at(doc, plan[i].world_x, plan[i].world_z, &x, &z);
        if( !square )
        {
            closed++;
            continue;
        }
        after = square->tiles[Editor_TileIndex(x, z, level)];
        after.has_overlay = 1;
        after.overlay_id = (uint16_t)overlay_id;
        after.shape = (uint8_t)plan[i].shape;
        after.rotation = (uint8_t)plan[i].rotation;
        ops_emit_tile(square, x, z, level, &after, out, &written, max);
    }
    ToriMem_Free(ToriMem_Editor(), plan);
    if( out_closed )
        *out_closed = closed;
    return written;
}

int
Editor_PathHeightCommands(
    struct Editor_Doc* doc,
    int level,
    const struct Editor_PathParams* params,
    enum Editor_PathHeightMode mode,
    int units,
    const int* xs,
    const int* zs,
    int count,
    struct Editor_Cmd* out,
    int have,
    int max,
    int* out_closed)
{
    int const room = EDITOR_PATH_SPAN_MAX * EDITOR_PATH_SPAN_MAX;
    int* cx;
    int* cz;
    int corners;
    int written = have;
    int closed = 0;

    assert(doc);
    assert(params);
    assert(level >= 0);
    assert(level < 4);
    assert(mode > EDITOR_PATH_HEIGHT_OFF);
    assert(mode < EDITOR_PATH_HEIGHT_MODE_COUNT);
    assert(units >= 0);
    assert(out);
    assert(have >= 0);
    assert(have <= max);

    cx = ToriMem_Alloc(ToriMem_Editor(), sizeof(*cx) * (size_t)room);
    cz = ToriMem_Alloc(ToriMem_Editor(), sizeof(*cz) * (size_t)room);
    assert(cx);
    assert(cz);
    corners = Editor_PathCorners(params, xs, zs, count, cx, cz, room);
    for( int i = 0; i < corners; i++ )
    {
        struct Editor_Square* square;
        char key[EDITOR_DOC_KEY_MAX];
        int here;
        int target;
        int raw;
        int x;
        int z;
        int merged = 0;

        square = ops_square_at(doc, cx[i], cz[i], &x, &z);
        if( !square )
        {
            closed++;
            continue;
        }
        (void)Editor_OpsDerivedHeight(doc, cx[i], cz[i], level, &here);
        /* Derived heights grow DOWN: lowering adds. */
        if( mode == EDITOR_PATH_HEIGHT_SET )
            target = -units * OPS_UNIT;
        else if( mode == EDITOR_PATH_HEIGHT_LOWER )
            target = here + units * OPS_UNIT;
        else
            target = here - units * OPS_UNIT;
        if( abs(target - here) < OPS_UNIT / 2 )
            continue;
        raw = Editor_OpsRawForHeight(ops_base(square, x, z, level), target);
        /* The path's own command for the tile, when it painted it. */
        Editor_SquareKey(key, square->map_x, square->map_z);
        for( int k = 0; k < written && !merged; k++ )
        {
            struct Editor_Cmd* command = &out[k];
            if( command->kind != EDITOR_CMD_TILE || command->u.tile.level != level || command->u.tile.x != x
                || command->u.tile.z != z || strcmp(command->doc_key, key) != 0 )
                continue;
            command->u.tile.after.has_height = 1;
            command->u.tile.after.height = (uint8_t)raw;
            merged = 1;
        }
        if( !merged )
        {
            struct Editor_Tile after = square->tiles[Editor_TileIndex(x, z, level)];
            after.has_height = 1;
            after.height = (uint8_t)raw;
            ops_emit_tile(square, x, z, level, &after, out, &written, max);
        }
    }
    ToriMem_Free(ToriMem_Editor(), cx);
    ToriMem_Free(ToriMem_Editor(), cz);
    if( out_closed )
        *out_closed += closed;
    return written;
}

/* ---- scatter ------------------------------------------------------------------- */

/** A loc of the set stands on world tile (x, z) on `level`: its index in the
 *  square's list, or -1. */
static int
ops_scatter_set_loc_at(
    struct Editor_Doc* doc,
    const struct Editor_ScatterSet* set,
    int world_x,
    int world_z,
    int level,
    struct Editor_Square** out_square)
{
    struct Editor_Square* square;
    int x;
    int z;

    *out_square = NULL;
    square = ops_square_at(doc, world_x, world_z, &x, &z);
    if( !square )
        return -1;
    *out_square = square;
    for( int i = 0; i < square->loc_count; i++ )
    {
        const struct Editor_Loc* loc = &square->locs[i];
        if( loc->level != level || loc->x != x || loc->z != z )
            continue;
        for( int k = 0; k < set->count; k++ )
            if( loc->loc_id == set->loc_id[k] )
                return i;
    }
    return -1;
}

/** The tiles one application may touch: the disc, and footprints spilling
 *  past it to the north and east. */
#define OPS_SCATTER_SPILL 16
#define OPS_SCATTER_GRID (2 * EDITOR_BRUSH_RADIUS_MAX + 1 + OPS_SCATTER_SPILL)

/** What this application has already planned, so two pieces of one call
 *  never share a tile (they are all judged against the document before any
 *  applies). Indexed from the disc's south-west corner. */
struct OpsScatterTaken
{
    int origin_x;
    int origin_z;
    unsigned char tile[OPS_SCATTER_GRID][OPS_SCATTER_GRID];
};

/** The footprint of a loc of the set placed with (world_x, world_z) as its
 *  south-west tile is free: every tile in an open square, not planned by
 *  this call, and holding no loc in the piece's layer (a centrepiece on any
 *  tile of it, any shape of the same layer on the tile itself). Marks the
 *  footprint taken when it is. */
static int
ops_scatter_free(
    struct Editor_Doc* doc,
    const struct Editor_ScatterSet* set,
    int k,
    int rotation,
    int world_x,
    int world_z,
    int level,
    struct OpsScatterTaken* taken)
{
    int const layer = Editor_LocShapeLayer(set->shape[k]);
    int w;
    int h;

    Editor_LocFootprint(set->shape[k], rotation, set->size_x[k], set->size_z[k], &w, &h);
    for( int dz = 0; dz < h; dz++ )
        for( int dx = 0; dx < w; dx++ )
        {
            struct Editor_Square* square;
            int const gx = world_x + dx - taken->origin_x;
            int const gz = world_z + dz - taken->origin_z;
            int x;
            int z;
            if( gx < 0 || gz < 0 || gx >= OPS_SCATTER_GRID || gz >= OPS_SCATTER_GRID || taken->tile[gz][gx] )
                return 0;
            square = ops_square_at(doc, world_x + dx, world_z + dz, &x, &z);
            if( !square )
                return 0;
            for( int i = 0; i < square->loc_count; i++ )
            {
                /* A loc is recorded on its south-west tile; a centrepiece
                 * covers its footprint from there -- the set's sizes for its
                 * own ids, one tile for any other (its record is not here). */
                const struct Editor_Loc* loc = &square->locs[i];
                int lw = 1;
                int lh = 1;
                if( loc->level != level )
                    continue;
                if( loc->shape == 10 || loc->shape == 11 )
                    for( int s = 0; s < set->count; s++ )
                        if( set->loc_id[s] == loc->loc_id )
                            Editor_LocFootprint(loc->shape, loc->rotation, set->size_x[s], set->size_z[s], &lw, &lh);
                if( x < loc->x || x >= loc->x + lw || z < loc->z || z >= loc->z + lh )
                    continue;
                if( loc->shape == 10 || loc->shape == 11 || Editor_LocShapeLayer(loc->shape) == layer )
                    return 0;
            }
        }
    for( int dz = 0; dz < h; dz++ )
        for( int dx = 0; dx < w; dx++ )
            taken->tile[world_z + dz - taken->origin_z][world_x + dx - taken->origin_x] = 1;
    return 1;
}

int
Editor_ScatterCommands(
    struct Editor_Doc* doc,
    const struct Editor_ScatterOp* op,
    struct Editor_Cmd* out,
    int max)
{
    int const radius = op ? op->brush.radius : 0;
    struct OpsScatterTaken* taken;
    int count = 0;

    assert(doc);
    assert(op);
    assert(out);
    assert(op->level >= 0);
    assert(op->level < EDITOR_SQUARE_LEVELS);
    assert(radius >= 0);
    assert(radius <= EDITOR_BRUSH_RADIUS_MAX);
    assert(op->density >= 1);
    assert(op->density <= 100);
    assert(op->set.count >= 0);
    assert(op->set.count <= EDITOR_SCATTER_SET_MAX);
    if( op->set.count == 0 )
        return 0;
    taken = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*taken));
    assert(taken);
    taken->origin_x = op->world_x - radius;
    taken->origin_z = op->world_z - radius;

    for( int dz = -radius; dz <= radius; dz++ )
        for( int dx = -radius; dx <= radius; dx++ )
        {
            double const weight = Editor_BrushWeight(&op->brush, dx, dz);
            int const world_x = op->world_x + dx;
            int const world_z = op->world_z + dz;
            struct Editor_Square* square;
            int at;
            int map_x = 0;
            int map_z = 0;
            int x;
            int z;

            if( weight <= 0.0 || count >= max )
                continue;
            if( op->erase )
            {
                at = ops_scatter_set_loc_at(doc, &op->set, world_x, world_z, op->level, &square);
                if( at < 0 )
                    continue;
                (void)ops_split(world_x, world_z, &map_x, &map_z, &x, &z);
                Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, map_x, map_z);
                out[count].has_before = 1;
                out[count].u.loc.before = square->locs[at];
                count++;
                continue;
            }
            if( ops_hash_unit(op->seed, world_x, world_z, 0) * 100.0 >= (double)op->density * weight )
                continue;
            {
                int const k = (int)(ops_hash_unit(op->seed, world_x, world_z, 1) * op->set.count) % op->set.count;
                int const rotation = op->rotate ? (int)(ops_hash_unit(op->seed, world_x, world_z, 2) * 4.0) & 3 : 0;
                struct Editor_Loc after;
                if( !ops_scatter_free(doc, &op->set, k, rotation, world_x, world_z, op->level, taken) )
                    continue;
                (void)ops_split(world_x, world_z, &map_x, &map_z, &after.x, &after.z);
                after.loc_id = op->set.loc_id[k];
                after.shape = op->set.shape[k];
                after.rotation = rotation;
                after.level = op->level;
                Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, map_x, map_z);
                out[count].has_after = 1;
                out[count].u.loc.after = after;
                count++;
            }
        }
    ToriMem_Free(ToriMem_Editor(), taken);
    return count;
}

/* ---- plateau ------------------------------------------------------------------- */

int
Editor_PlateauCommands(
    struct Editor_Doc* doc,
    int level,
    int height,
    int skirt,
    int x0,
    int z0,
    int x1,
    int z1,
    Editor_PlateauInsideFn inside,
    void* user,
    struct Editor_Cmd* out,
    int max)
{
    int count = 0;

    assert(doc);
    assert(inside);
    assert(out);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);
    assert(skirt >= 0);
    assert(skirt <= EDITOR_PLATEAU_SKIRT_MAX);
    assert(x0 <= x1);
    assert(z0 <= z1);

    for( int world_z = z0 - skirt; world_z <= z1 + skirt; world_z++ )
        for( int world_x = x0 - skirt; world_x <= x1 + skirt; world_x++ )
        {
            struct Editor_Square* square;
            struct Editor_Tile after;
            double share = 1.0;
            double target;
            int own = 0;
            int x;
            int z;

            if( world_x < 0 || world_z < 0 || count >= max )
                continue;
            if( !inside(user, world_x, world_z) )
            {
                /* The nearest tile of the region, by the larger axis
                 * distance: 1..skirt is the skirt, further is nothing. */
                int best = skirt + 1;
                for( int dz = -skirt; dz <= skirt && best > 1; dz++ )
                    for( int dx = -skirt; dx <= skirt; dx++ )
                    {
                        int const d = (dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz) ? (dx < 0 ? -dx : dx) : (dz < 0 ? -dz : dz);
                        if( d == 0 || d >= best || world_x + dx < x0 || world_x + dx > x1 || world_z + dz < z0
                            || world_z + dz > z1 )
                            continue;
                        if( inside(user, world_x + dx, world_z + dz) )
                            best = d;
                    }
                if( best > skirt )
                    continue;
                {
                    double const t = (double)(skirt + 1 - best) / (double)(skirt + 1);
                    share = t * t * (3.0 - 2.0 * t);
                }
            }
            square = ops_square_at(doc, world_x, world_z, &x, &z);
            if( !square )
                continue;
            (void)Editor_OpsDerivedHeight(doc, world_x, world_z, level, &own);
            target = (double)own + ((double)height - (double)own) * share;
            if( fabs(target - (double)own) < OPS_UNIT / 2.0 )
                continue;
            after = square->tiles[Editor_TileIndex(x, z, level)];
            after.has_height = 1;
            after.height = (uint8_t)Editor_OpsRawForHeight(ops_base(square, x, z, level), (int)floor(target + 0.5));
            ops_emit_tile(square, x, z, level, &after, out, &count, max);
        }
    return count;
}

struct OpsPlateauDisc
{
    int world_x;
    int world_z;
    int radius;
};

static int
ops_plateau_in_disc(
    void* user,
    int world_x,
    int world_z)
{
    struct OpsPlateauDisc const* disc = user;
    int const dx = world_x - disc->world_x;
    int const dz = world_z - disc->world_z;
    return dx * dx + dz * dz <= disc->radius * disc->radius + disc->radius;
}

int
Editor_PlateauBrushCommands(
    struct Editor_Doc* doc,
    int level,
    int height,
    int skirt,
    const struct Editor_Brush* brush,
    int world_x,
    int world_z,
    struct Editor_Cmd* out,
    int max)
{
    struct OpsPlateauDisc disc;

    assert(brush);
    assert(brush->radius >= 0);
    assert(brush->radius <= EDITOR_BRUSH_RADIUS_MAX);
    disc.world_x = world_x;
    disc.world_z = world_z;
    disc.radius = brush->radius;
    return Editor_PlateauCommands(doc, level, height, skirt, world_x - brush->radius, world_z - brush->radius,
        world_x + brush->radius, world_z + brush->radius, ops_plateau_in_disc, &disc, out, max);
}

static int
ops_plateau_in_rect(
    void* user,
    int world_x,
    int world_z)
{
    struct Editor_TileRect const* rect = user;
    return world_x >= rect->x0 && world_x <= rect->x1 && world_z >= rect->z0 && world_z <= rect->z1;
}

int
Editor_TileRectPlateau(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    int height,
    int skirt,
    struct Editor_Cmd* out,
    int max)
{
    struct Editor_TileRect r;

    assert(rect);
    if( !Editor_TileRectNormalise(rect, &r) )
        return 0;
    return Editor_PlateauCommands(
        doc, r.level, height, skirt, r.x0, r.z0, r.x1, r.z1, ops_plateau_in_rect, &r, out, max);
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
            after.underlay_id = (uint16_t)(tools->underlay_id & 0xffff);
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
Editor_SpawnScriptedKey(
    char out[EDITOR_DOC_KEY_MAX],
    int map_x,
    int map_z)
{
    assert(out);
    snprintf(out, EDITOR_DOC_KEY_MAX, "spawns:m%d_%d", map_x, map_z);
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
Editor_LocFitDecor(
    struct Editor_Doc* doc,
    uint32_t mask,
    int world_x,
    int world_z,
    int level,
    int rotation,
    int* shape)
{
    struct Editor_Square* square;
    int x;
    int z;

    assert(doc);
    assert(shape);
    if( *shape != 4 || !(mask & (1u << 5)) )
        return 0;
    square = ops_square_at(doc, world_x, world_z, &x, &z);
    if( !square )
        return 0;
    for( int i = 0; i < square->loc_count; i++ )
    {
        const struct Editor_Loc* loc = &square->locs[i];
        if( loc->level == level && loc->x == x && loc->z == z
            && (ops_wall_edges(loc->shape, loc->rotation) & (1u << (rotation & 3))) )
        {
            *shape = 5;
            return 1;
        }
    }
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
        *out_rotation = Editor_LocWallCornerRotation(vertical, horizontal);
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

/* ---- runs ----------------------------------------------------------------------- */

int
Editor_LocShapeIsWall(int shape)
{
    return shape == 0 || shape == 1 || shape == 2 || shape == 3 || shape == 9;
}

int
Editor_LocShapeLayer(int shape)
{
    assert(shape >= 0);
    assert(shape < EDITOR_LOC_SHAPE_COUNT);
    /* Client-TS LOC_SHAPE_TO_LAYER: the diagonal wall (9) is filed with the
     * centrepieces, as the game's scene keeps it. */
    if( shape <= 3 )
        return 0;
    if( shape <= 8 )
        return 1;
    if( shape == 22 )
        return 3;
    return 2;
}

int
Editor_LocWallCornerRotation(
    int edge_a,
    int edge_b)
{
    assert(edge_a >= 0);
    assert(edge_a < 4);
    assert(edge_b >= 0);
    assert(edge_b < 4);
    assert(((edge_a ^ edge_b) & 1) == 1);
    return ((edge_a + 1) & 3) == edge_b ? edge_a : edge_b;
}

void
Editor_LocRunBegin(
    struct Editor_LocRunPath* path,
    int world_x,
    int world_z)
{
    assert(path);
    assert(world_x >= 0);
    assert(world_z >= 0);
    path->count = 1;
    path->x[0] = world_x;
    path->z[0] = world_z;
}

/** Step onto (x, z): back to it where the run already holds it, else on. 0
 *  when the run is full. */
static int
run_step(
    struct Editor_LocRunPath* path,
    int x,
    int z)
{
    for( int k = 0; k < path->count; k++ )
        if( path->x[k] == x && path->z[k] == z )
        {
            path->count = k + 1;
            return 1;
        }
    if( path->count >= EDITOR_LOC_RUN_MAX )
        return 0;
    path->x[path->count] = x;
    path->z[path->count] = z;
    path->count++;
    return 1;
}

/** Orthogonal steps from the run's last tile to (x, z), the longer axis
 *  first, so a diagonal neighbour is x then z. */
static void
run_walk(
    struct Editor_LocRunPath* path,
    int x,
    int z)
{
    int cx = path->x[path->count - 1];
    int cz = path->z[path->count - 1];

    while( cx != x || cz != z )
    {
        int const dx = x - cx;
        int const dz = z - cz;
        if( (dx < 0 ? -dx : dx) >= (dz < 0 ? -dz : dz) )
            cx += dx > 0 ? 1 : -1;
        else
            cz += dz > 0 ? 1 : -1;
        if( !run_step(path, cx, cz) )
            return;
        cx = path->x[path->count - 1];
        cz = path->z[path->count - 1];
    }
}

int
Editor_LocRunTo(
    struct Editor_LocRunPath* path,
    int world_x,
    int world_z,
    int constrain)
{
    int old_count;
    int old_x;
    int old_z;

    assert(path);
    assert(path->count > 0);
    if( world_x < 0 || world_z < 0 )
        return 0;
    old_count = path->count;
    old_x = path->x[old_count - 1];
    old_z = path->z[old_count - 1];
    if( constrain == 2 )
    {
        /* The staircase along the diagonal from the first tile, as far as
         * the pointer reaches along it: a step along x then one along z per
         * diagonal tile, rebuilt from the first tile each time. */
        int const dx = world_x - path->x[0];
        int const dz = world_z - path->z[0];
        int const n = (dx < 0 ? -dx : dx) < (dz < 0 ? -dz : dz) ? (dx < 0 ? -dx : dx) : (dz < 0 ? -dz : dz);
        int const sx = dx < 0 ? -1 : 1;
        int const sz = dz < 0 ? -1 : 1;
        path->count = 1;
        for( int i = 1; i <= n && path->count + 2 <= EDITOR_LOC_RUN_MAX; i++ )
        {
            path->x[path->count] = path->x[0] + i * sx;
            path->z[path->count] = path->z[0] + (i - 1) * sz;
            path->count++;
            path->x[path->count] = path->x[0] + i * sx;
            path->z[path->count] = path->z[0] + i * sz;
            path->count++;
        }
        return path->count != old_count || path->x[path->count - 1] != old_x || path->z[path->count - 1] != old_z;
    }
    if( constrain )
    {
        int const dx = world_x - path->x[0];
        int const dz = world_z - path->z[0];
        /* The straight line from the first tile: rebuilt, so letting go of
         * Shift and pressing it again never leaves a kink behind. */
        if( (dx < 0 ? -dx : dx) >= (dz < 0 ? -dz : dz) )
            world_z = path->z[0];
        else
            world_x = path->x[0];
        path->count = 1;
    }
    run_walk(path, world_x, world_z);
    return path->count != old_count || path->x[path->count - 1] != old_x || path->z[path->count - 1] != old_z;
}

/** The edge of a tile facing the next tile of the run (0 west, 1 north,
 *  2 east, 3 south): the direction of travel. */
static int
run_heading(
    const struct Editor_LocRunPath* path,
    int from)
{
    int const dx = path->x[from + 1] - path->x[from];
    int const dz = path->z[from + 1] - path->z[from];

    assert((dx == 0) != (dz == 0));
    if( dx > 0 )
        return 2;
    if( dx < 0 )
        return 0;
    return dz > 0 ? 1 : 3;
}

/** The edges a pose stands on, as a hint for which side of the run the wall
 *  goes: a straight wall or a diagonal its rotation's edge, an L or a corner
 *  piece the two edges of its corner. */
static unsigned
run_pose_edges(
    int shape,
    int rotation)
{
    if( shape == 1 || shape == 2 || shape == 3 )
        return (1u << (rotation & 3)) | (1u << ((rotation + 1) & 3));
    return 1u << (rotation & 3);
}

/** What stands on the piece's key now: the wall layer for a wall run, the
 *  tile and shape otherwise. Fills state and holder. */
static void
run_judge(
    struct Editor_Doc* doc,
    const struct Editor_LocRunPlan* plan,
    struct Editor_LocRunPiece* piece)
{
    struct Editor_Square* square;
    int x;
    int z;
    int own = -1;
    int other = -1;

    piece->has_holder = 0;
    /* No piece goes here: nothing to refuse either. */
    if( piece->state == EDITOR_LOC_RUN_NONE )
        return;
    square = ops_square_at(doc, piece->world_x, piece->world_z, &x, &z);
    if( !square )
    {
        piece->state = EDITOR_LOC_RUN_CLOSED;
        return;
    }
    for( int i = 0; i < square->loc_count; i++ )
    {
        const struct Editor_Loc* loc = &square->locs[i];
        if( loc->level != plan->level || loc->x != x || loc->z != z )
            continue;
        /* A wall run's pieces are all of the wall layer (0..3): the world
         * keeps one of those a tile, whatever their shapes. */
        if( plan->wall ? Editor_LocShapeLayer(loc->shape) != 0 : loc->shape != piece->shape )
            continue;
        if( loc->loc_id == plan->loc_id )
        {
            if( own < 0 )
                own = i;
        }
        else if( other < 0 )
            other = i;
    }
    if( other >= 0 )
    {
        piece->state = EDITOR_LOC_RUN_TAKEN;
        piece->has_holder = 1;
        piece->holder = square->locs[other];
        return;
    }
    piece->state = EDITOR_LOC_RUN_PLACE;
    if( own < 0 )
        return;
    if( plan->fit && plan->wall && piece->shape == 0 )
    {
        /* Fit at a tile this loc already walls: a straight wall on the
         * perpendicular edge makes the L of the two (the run's end meets
         * it, a crossing keeps both); an L already carrying the run's edge
         * stays. A wall on the same or the opposite edge is replaced as
         * before (no piece holds two opposite edges). */
        const struct Editor_Loc* there = &square->locs[own];
        int const edge = piece->rotation & 3;
        if( there->shape == 0 && ((there->rotation - edge) & 1) && (plan->mask & (1u << 2)) )
        {
            piece->shape = 2;
            piece->rotation = Editor_LocWallCornerRotation(there->rotation & 3, edge);
        }
        else if( there->shape == 2 && (run_pose_edges(2, there->rotation) & (1u << edge)) )
        {
            piece->shape = 2;
            piece->rotation = there->rotation & 3;
        }
    }
    if( square->locs[own].shape == piece->shape && (square->locs[own].rotation & 3) == piece->rotation )
    {
        piece->state = EDITOR_LOC_RUN_KEEP;
        return;
    }
    piece->has_holder = 1;
    piece->holder = square->locs[own];
}

/** The path is the staircase a diagonal drag makes: every step turns, the
 *  x steps all one way and the z steps all one way, starting along x. */
static int
run_is_staircase(const struct Editor_LocRunPath* path)
{
    int sx = 0;
    int sz = 0;

    if( path->count < 3 || (path->count & 1) == 0 )
        return 0;
    for( int i = 0; i + 1 < path->count; i++ )
    {
        int const dx = path->x[i + 1] - path->x[i];
        int const dz = path->z[i + 1] - path->z[i];
        if( (i & 1) == 0 )
        {
            if( dz != 0 || (sx && dx != sx) )
                return 0;
            sx = dx;
        }
        else
        {
            if( dx != 0 || (sz && dz != sz) )
                return 0;
            sz = dz;
        }
    }
    return 1;
}

/** The wall piece for run tile `i`: straight at the ends and along a
 *  segment, the corner piece at a turn. `left` 1 puts the wall on the left of
 *  the direction of travel. */
static void
run_wall_piece(
    const struct Editor_LocRunPath* path,
    int i,
    int left,
    uint32_t mask,
    struct Editor_LocRunPiece* piece)
{
    int const n = path->count;
    int const in = i > 0 ? run_heading(path, i - 1) : run_heading(path, 0);
    int const out = i < n - 1 ? run_heading(path, i) : in;
    int const edge_in = left ? (in + 3) & 3 : (in + 1) & 3;
    int const edge_out = left ? (out + 3) & 3 : (out + 1) & 3;

    piece->state = EDITOR_LOC_RUN_PLACE;
    piece->shape = 0;
    piece->rotation = edge_in;
    if( i == 0 )
    {
        piece->rotation = edge_out;
        return;
    }
    if( i == n - 1 || in == out )
        return;
    /*
     * A turn. The wall coming in stands on edge_in, the one going out on
     * edge_out. When edge_in is the edge the run leaves through, the turn is
     * toward the wall: the two lines meet at a point, this tile's corner, and
     * it takes the corner post (3). Otherwise the turn is away from it and
     * this tile holds both edges: the L (2). Either way the rotation is the
     * corner of those two edges, as Fit has it.
     */
    if( edge_in == out )
    {
        if( mask & (1u << 3) )
        {
            piece->shape = 3;
            piece->rotation = Editor_LocWallCornerRotation(edge_in, edge_out);
        }
        else
            piece->state = EDITOR_LOC_RUN_NONE;
        return;
    }
    if( mask & (1u << 2) )
    {
        piece->shape = 2;
        piece->rotation = Editor_LocWallCornerRotation(edge_in, edge_out);
    }
}

void
Editor_LocRunPlanMake(
    struct Editor_Doc* doc,
    const struct Editor_LocRunPath* path,
    int loc_id,
    uint32_t mask,
    int shape,
    int rotation,
    int level,
    int fit,
    struct Editor_LocRunPlan* plan)
{
    int left = 1;
    int diagonal_rotation = 0;

    assert(doc);
    assert(path);
    assert(plan);
    assert(path->count > 0);
    assert(path->count <= EDITOR_LOC_RUN_MAX);
    assert(loc_id >= 0);
    assert(shape >= 0);
    assert(shape < EDITOR_LOC_SHAPE_COUNT);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);

    plan->loc_id = loc_id;
    plan->level = level;
    plan->count = path->count;
    plan->wall = Editor_LocShapeIsWall(shape) && (mask & 1u) && path->count > 1;
    plan->diagonal = shape == 9 && (mask & (1u << 9)) && run_is_staircase(path);
    plan->fit = fit ? 1 : 0;
    plan->mask = mask;
    plan->place_count = 0;
    plan->refused_count = 0;
    plan->first_refused = -1;
    if( plan->diagonal )
    {
        /* The diagonal's turn: north-west / south-east (an x step and a z
         * step of opposite signs) is rotation 1, the other 0 -- as Fit
         * reads a diagonal neighbour. The diagonal stands in the ground
         * layer, so the run is no wall run (one wall a tile is the wall
         * layer's rule, not the diagonal's). */
        int const dx = path->x[1] - path->x[0];
        int const dz = path->z[2] - path->z[1];
        diagonal_rotation = dx * dz < 0 ? 1 : 0;
        plan->wall = 0;
    }
    else if( plan->wall )
    {
        /* The side the pose stands on, read against the first segment. */
        int const heading = run_heading(path, 0);
        unsigned const edges = run_pose_edges(shape, rotation);
        left = (edges & (1u << ((heading + 3) & 3))) || !(edges & (1u << ((heading + 1) & 3)));
    }
    for( int i = 0; i < path->count; i++ )
    {
        struct Editor_LocRunPiece* piece = &plan->pieces[i];
        piece->world_x = path->x[i];
        piece->world_z = path->z[i];
        if( plan->diagonal )
        {
            piece->state = (i & 1) ? EDITOR_LOC_RUN_NONE : EDITOR_LOC_RUN_PLACE;
            piece->shape = 9;
            piece->rotation = diagonal_rotation;
        }
        else if( plan->wall )
            run_wall_piece(path, i, left, mask, piece);
        else
        {
            piece->state = EDITOR_LOC_RUN_PLACE;
            piece->shape = shape;
            piece->rotation = rotation & 3;
        }
        run_judge(doc, plan, piece);
        if( piece->state == EDITOR_LOC_RUN_PLACE )
            plan->place_count++;
        else if( piece->state == EDITOR_LOC_RUN_CLOSED || piece->state == EDITOR_LOC_RUN_TAKEN )
        {
            if( plan->first_refused < 0 )
                plan->first_refused = i;
            plan->refused_count++;
        }
    }
}

int
Editor_LocRunCommands(
    const struct Editor_LocRunPlan* plan,
    struct Editor_Cmd* out,
    int max)
{
    int count = 0;

    assert(plan);
    assert(out);
    for( int i = 0; i < plan->count; i++ )
    {
        const struct Editor_LocRunPiece* piece = &plan->pieces[i];
        struct Editor_Loc after;
        int map_x;
        int map_z;

        if( piece->state != EDITOR_LOC_RUN_PLACE )
            continue;
        if( count + 2 > max )
            break;
        /* Outside the world: no square holds it, so no command can. */
        if( !ops_split(piece->world_x, piece->world_z, &map_x, &map_z, &after.x, &after.z) )
            continue;
        after.loc_id = plan->loc_id;
        after.shape = piece->shape;
        after.rotation = piece->rotation & 3;
        after.level = plan->level;
        if( piece->has_holder && piece->holder.shape != piece->shape )
        {
            /* The loc's own piece in another shape: off its key first. */
            Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, map_x, map_z);
            out[count].has_before = 1;
            out[count].u.loc.before = piece->holder;
            count++;
        }
        Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, map_x, map_z);
        if( piece->has_holder && piece->holder.shape == piece->shape )
        {
            out[count].has_before = 1;
            out[count].u.loc.before = piece->holder;
        }
        out[count].has_after = 1;
        out[count].u.loc.after = after;
        count++;
    }
    return count;
}

int
Editor_LocRunRefusal(
    const struct Editor_LocRunPlan* plan,
    char* out,
    size_t size)
{
    const struct Editor_LocRunPiece* piece;
    int used;

    assert(plan);
    assert(out);
    assert(size > 0);
    out[0] = '\0';
    if( plan->first_refused < 0 )
        return 0;
    piece = &plan->pieces[plan->first_refused];
    if( piece->state == EDITOR_LOC_RUN_CLOSED )
        used = snprintf(out, size, "Place refused: tile %d,%d L%d is in m%d_%d, which is not open", piece->world_x,
            piece->world_z, plan->level, piece->world_x / EDITOR_SQUARE_X, piece->world_z / EDITOR_SQUARE_Z);
    else if( piece->holder.shape == piece->shape )
        used = snprintf(out, size, "Place refused: tile %d,%d L%d holds a %s of that shape (loc %d); press E to swap",
            piece->world_x, piece->world_z, plan->level, Editor_LocShapeName(piece->holder.shape),
            piece->holder.loc_id);
    else
        used = snprintf(out, size, "Place refused: tile %d,%d L%d holds a %s of another loc (loc %d); press E to swap",
            piece->world_x, piece->world_z, plan->level, Editor_LocShapeName(piece->holder.shape),
            piece->holder.loc_id);
    if( plan->refused_count > 1 && used > 0 && (size_t)used < size )
        snprintf(out + used, size - (size_t)used, ", and %d more tile%s of the run", plan->refused_count - 1,
            plan->refused_count == 2 ? "" : "s");
    return 1;
}

/* ---- selecting by area and by id ------------------------------------------------- */

void
Editor_LocFootprint(
    int shape,
    int rotation,
    int size_x,
    int size_z,
    int* out_w,
    int* out_h)
{
    assert(out_w);
    assert(out_h);
    *out_w = 1;
    *out_h = 1;
    if( shape != 10 && shape != 11 )
        return;
    if( size_x < 1 )
        size_x = 1;
    if( size_z < 1 )
        size_z = 1;
    *out_w = (rotation & 1) ? size_z : size_x;
    *out_h = (rotation & 1) ? size_x : size_z;
}

int
Editor_LocsTouchingRect(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    Editor_LocSizeFn size,
    void* size_user,
    struct Editor_LocRef* out,
    int max)
{
    int found = 0;

    assert(doc);
    assert(rect);
    assert(out || max == 0);
    assert(rect->x0 <= rect->x1);
    assert(rect->z0 <= rect->z1);

    for( int s = 0; s < doc->square_count; s++ )
    {
        struct Editor_Square* square = &doc->squares[s];
        int const base_x = square->map_x * EDITOR_SQUARE_X;
        int const base_z = square->map_z * EDITOR_SQUARE_Z;

        if( !square->loaded )
            continue;
        for( int i = 0; i < square->loc_count; i++ )
        {
            const struct Editor_Loc* loc = &square->locs[i];
            int const wx = base_x + loc->x;
            int const wz = base_z + loc->z;
            int size_x = 1;
            int size_z = 1;
            int w;
            int h;

            if( loc->level != rect->level )
                continue;
            if( size && (loc->shape == 10 || loc->shape == 11) && !size(size_user, loc->loc_id, &size_x, &size_z) )
            {
                size_x = 1;
                size_z = 1;
            }
            Editor_LocFootprint(loc->shape, loc->rotation, size_x, size_z, &w, &h);
            if( wx > rect->x1 || wz > rect->z1 || wx + w - 1 < rect->x0 || wz + h - 1 < rect->z0 )
                continue;
            if( found < max )
                Editor_LocRefAt(&out[found], wx, wz, loc->level, loc->shape, loc->loc_id, loc->rotation & 3);
            found++;
        }
    }
    return found;
}

int
Editor_LocsOfId(
    struct Editor_Doc* doc,
    int loc_id,
    int level,
    struct Editor_LocRef* out,
    int max)
{
    int found = 0;

    assert(doc);
    assert(out || max == 0);
    assert(loc_id >= 0);

    for( int s = 0; s < doc->square_count; s++ )
    {
        struct Editor_Square* square = &doc->squares[s];
        if( !square->loaded )
            continue;
        for( int i = 0; i < square->loc_count; i++ )
        {
            const struct Editor_Loc* loc = &square->locs[i];
            if( loc->loc_id != loc_id || loc->level != level )
                continue;
            if( found < max )
                Editor_LocRefAt(&out[found], square->map_x * EDITOR_SQUARE_X + loc->x,
                    square->map_z * EDITOR_SQUARE_Z + loc->z, loc->level, loc->shape, loc->loc_id, loc->rotation & 3);
            found++;
        }
    }
    return found;
}

enum Editor_LocSelectMode
Editor_LocSelectModeOf(
    int shift,
    int ctrl,
    int alt)
{
    if( alt )
        return EDITOR_LOC_SELECT_SUBTRACT;
    if( ctrl )
        return EDITOR_LOC_SELECT_TOGGLE;
    if( shift )
        return EDITOR_LOC_SELECT_ADD;
    return EDITOR_LOC_SELECT_REPLACE;
}

/** Where the selection holds the loc with ref's key, or -1. */
static int
select_find(
    const struct Editor_LocRef* selection,
    int count,
    const struct Editor_LocRef* ref)
{
    for( int i = 0; i < count; i++ )
    {
        const struct Editor_LocRef* s = &selection[i];
        if( s->map_x == ref->map_x && s->map_z == ref->map_z && s->level == ref->level && s->x == ref->x
            && s->z == ref->z && s->shape == ref->shape )
            return i;
    }
    return -1;
}

int
Editor_LocSelectionMerge(
    struct Editor_LocRef* selection,
    int count,
    int max,
    const struct Editor_LocRef* refs,
    int n,
    enum Editor_LocSelectMode mode,
    int* out_dropped)
{
    int old = count;

    assert(selection);
    assert(refs || n == 0);
    assert(out_dropped);
    assert(count >= 0);
    assert(count <= max);

    *out_dropped = 0;
    if( mode == EDITOR_LOC_SELECT_REPLACE )
        count = 0;
    for( int i = 0; i < n; i++ )
    {
        int const at = select_find(selection, count, &refs[i]);
        if( mode == EDITOR_LOC_SELECT_SUBTRACT || (mode == EDITOR_LOC_SELECT_TOGGLE && at >= 0 && at < old) )
        {
            /* A toggle takes off what was selected before this set and
             * adds the rest; what it added stays. */
            if( at >= 0 )
            {
                memmove(&selection[at], &selection[at + 1], sizeof(selection[0]) * (size_t)(count - at - 1));
                count--;
                old--;
            }
            continue;
        }
        if( at >= 0 )
            continue;
        if( count >= max )
        {
            (*out_dropped)++;
            continue;
        }
        selection[count++] = refs[i];
    }
    return count;
}
