#include "editor_building.h"

#include <assert.h>
#include <string.h>

/* The loc shapes a building is made of (rscache's placement shapes). */
#define SHAPE_WALL 0
#define SHAPE_WALL_POST 1
#define SHAPE_WALL_L 2
#define SHAPE_WALL_CORNER 3
#define SHAPE_CENTREPIECE 10
#define SHAPE_ROOF_SLOPE 12
#define SHAPE_ROOF_CONVEX 16
#define SHAPE_ROOF_FLAT 17
#define SHAPE_ROOFEDGE 18
#define SHAPE_ROOFEDGE_DIAGONAL 19
#define SHAPE_ROOFEDGE_CORNER 21

/* Sides and corners of a footprint, in the order the rotations are told. */
enum
{
    SIDE_WEST = 0,
    SIDE_NORTH,
    SIDE_EAST,
    SIDE_SOUTH,
};

enum
{
    CORNER_SW = 0,
    CORNER_NW,
    CORNER_NE,
    CORNER_SE,
};

static const struct Editor_BuildingRoofStyle k_roofs[] = {
    { "Thatch", 1926, 1792 },
    { "Wooden shingles", 1933, 1793 },
    { "Red slate", 1924, 1790 },
    { "Grey slate", 1925, 1791 },
    { "Varrock slate", 15552, 15552 },
};

static const struct Editor_BuildingWallStyle k_walls[] = {
    { "Timber and plaster", 1602, 1540, -1, 1823, 0, 0 },
    { "Brick", 1902, 1535, -1, 1821, 0, 1 },
    { "Old stone", 1631, 1535, -1, 1838, 0, 1 },
    { "Castle stone", 1620, 1517, -1, 1938, 0, 3 },
    { "Desert plaster", 1415, 1533, -1, 1848, 0, 3 },
    { "Wooden planks", 52375, 58116, -1, 1838, 0, 1 },
    /* fencegate_l / fencegate_r; fai_varrock_gate_l / _r. */
    { "Wooden fence", 980, 1558, 1560, -1, 1, 0 },
    { "Picket fence", 23849, 15514, 15516, -1, 1, 0 },
    { "Wooden railing", 994, -1, -1, -1, 1, 0 },
};

static const struct Editor_BuildingFloorStyle k_floors[] = {
    { "Wooden planks", 5 },
    { "Brown boards", 22 },
    { "Dark boards", 14 },
    { "Grey flagstones", 10 },
};

int
Editor_BuildingWallStyleCount(void)
{
    return (int)(sizeof(k_walls) / sizeof(k_walls[0]));
}

const struct Editor_BuildingWallStyle*
Editor_BuildingWallStyleAt(int index)
{
    assert(index >= 0);
    assert(index < Editor_BuildingWallStyleCount());
    return &k_walls[index];
}

int
Editor_BuildingWallStyleOf(int loc_id)
{
    if( loc_id < 0 )
        return -1;
    for( int i = 0; i < Editor_BuildingWallStyleCount(); i++ )
        if( k_walls[i].wall_id == loc_id || k_walls[i].door_id == loc_id || k_walls[i].door2_id == loc_id )
            return i;
    return -1;
}

int
Editor_BuildingRoofStyleCount(void)
{
    return (int)(sizeof(k_roofs) / sizeof(k_roofs[0]));
}

const struct Editor_BuildingRoofStyle*
Editor_BuildingRoofStyleAt(int index)
{
    assert(index >= 0);
    assert(index < Editor_BuildingRoofStyleCount());
    return &k_roofs[index];
}

int
Editor_BuildingFloorStyleCount(void)
{
    return (int)(sizeof(k_floors) / sizeof(k_floors[0]));
}

const struct Editor_BuildingFloorStyle*
Editor_BuildingFloorStyleAt(int index)
{
    assert(index >= 0);
    assert(index < Editor_BuildingFloorStyleCount());
    return &k_floors[index];
}

int
Editor_BuildingSideRotation(int side)
{
    assert(side >= 0);
    assert(side <= 3);
    /* A side's pieces stand on the edge facing indoors: the opposite edge. */
    return (side + 2) & 3;
}

int
Editor_BuildingCornerRotation(int corner)
{
    assert(corner >= 0);
    assert(corner <= 3);
    /* The corner piece's rotation r names the tile corner between edges r
     * and r + 1 (edpanel_ops.h Editor_LocWallCornerRotation); the south-west
     * tile's inner corner is its north-east one, between north (1) and east
     * (2), and each corner on round the footprint is a quarter on. */
    return (corner + 1) & 3;
}

void
Editor_BuildingGateBeside(
    int x,
    int z,
    int rotation,
    int* out_x,
    int* out_z)
{
    /* As the content pairs them: `fencegate_l 0 3` with its `fencegate_r` a
     * tile west, `0 0` a tile north, `0 2` a tile south, `0 1` a tile east. */
    static const int k_dx[4] = { 0, 1, 0, -1 };
    static const int k_dz[4] = { 1, 0, -1, 0 };

    assert(out_x);
    assert(out_z);
    *out_x = x + k_dx[rotation & 3];
    *out_z = z + k_dz[rotation & 3];
}

void
Editor_BuildingAcross(
    int x,
    int z,
    int rotation,
    int* out_x,
    int* out_z)
{
    static const int k_dx[4] = { -1, 0, 1, 0 };
    static const int k_dz[4] = { 0, 1, 0, -1 };

    assert(out_x);
    assert(out_z);
    *out_x = x + k_dx[rotation & 3];
    *out_z = z + k_dz[rotation & 3];
}

static struct Editor_BuildingPiece*
plan_add(
    struct Editor_BuildingPlan* plan,
    enum Editor_BuildingPieceKind kind,
    enum Editor_BuildingPart part,
    int x,
    int z,
    int level)
{
    struct Editor_BuildingPiece* piece;

    assert(plan->count < EDITOR_BUILDING_PIECES_MAX);
    piece = &plan->pieces[plan->count++];
    memset(piece, 0, sizeof(*piece));
    piece->kind = kind;
    piece->part = part;
    piece->x = x;
    piece->z = z;
    piece->level = level;
    piece->overlay = -1;
    plan->parts[part]++;
    return piece;
}

static void
plan_loc(
    struct Editor_BuildingPlan* plan,
    enum Editor_BuildingPart part,
    int x,
    int z,
    int level,
    int loc_id,
    int shape,
    int rotation)
{
    struct Editor_BuildingPiece* piece = plan_add(plan, EDITOR_BUILDING_LOC, part, x, z, level);
    piece->loc_id = loc_id;
    piece->shape = shape;
    piece->rotation = rotation & 3;
}

/**
 * One ring of a footprint (x0..x1, z0..z1, at least 2 x 2) as loc pieces:
 * `straight` on the sides at their side's rotation, `corner` on the four
 * corners (a shape below 0 leaves the corners out). A ring one tile thick
 * (a single row or column) has no corners to turn: the caller lays it.
 */
static void
plan_ring(
    struct Editor_BuildingPlan* plan,
    enum Editor_BuildingPart side_part,
    enum Editor_BuildingPart corner_part,
    int x0,
    int z0,
    int x1,
    int z1,
    int level,
    int loc_id,
    int straight,
    int corner)
{
    assert(x1 > x0);
    assert(z1 > z0);

    for( int x = x0 + 1; x < x1; x++ )
    {
        plan_loc(plan, side_part, x, z0, level, loc_id, straight, Editor_BuildingSideRotation(SIDE_SOUTH));
        plan_loc(plan, side_part, x, z1, level, loc_id, straight, Editor_BuildingSideRotation(SIDE_NORTH));
    }
    for( int z = z0 + 1; z < z1; z++ )
    {
        plan_loc(plan, side_part, x0, z, level, loc_id, straight, Editor_BuildingSideRotation(SIDE_WEST));
        plan_loc(plan, side_part, x1, z, level, loc_id, straight, Editor_BuildingSideRotation(SIDE_EAST));
    }
    if( corner < 0 )
        return;
    plan_loc(plan, corner_part, x0, z0, level, loc_id, corner, Editor_BuildingCornerRotation(CORNER_SW));
    plan_loc(plan, corner_part, x0, z1, level, loc_id, corner, Editor_BuildingCornerRotation(CORNER_NW));
    plan_loc(plan, corner_part, x1, z1, level, loc_id, corner, Editor_BuildingCornerRotation(CORNER_NE));
    plan_loc(plan, corner_part, x1, z0, level, loc_id, corner, Editor_BuildingCornerRotation(CORNER_SE));
}

static int
mask_has(
    uint32_t mask,
    int shape)
{
    return (mask >> shape) & 1u;
}

/**
 * A fence round a rectangle (at least 2 x 2): straight pieces on the outer
 * edge of each side's tiles, an L on each corner tile turned to its two
 * outer edges (rotation r holds edges r and r + 1: south-west 3, north-west
 * 0, north-east 1, south-east 2).
 */
static void
plan_fence(
    struct Editor_BuildingPlan* plan,
    int level,
    int loc_id)
{
    int const x0 = plan->x0;
    int const z0 = plan->z0;
    int const x1 = plan->x1;
    int const z1 = plan->z1;

    for( int x = x0 + 1; x < x1; x++ )
    {
        plan_loc(plan, EDITOR_BUILDING_PART_WALL, x, z0, level, loc_id, SHAPE_WALL, SIDE_SOUTH);
        plan_loc(plan, EDITOR_BUILDING_PART_WALL, x, z1, level, loc_id, SHAPE_WALL, SIDE_NORTH);
    }
    for( int z = z0 + 1; z < z1; z++ )
    {
        plan_loc(plan, EDITOR_BUILDING_PART_WALL, x0, z, level, loc_id, SHAPE_WALL, SIDE_WEST);
        plan_loc(plan, EDITOR_BUILDING_PART_WALL, x1, z, level, loc_id, SHAPE_WALL, SIDE_EAST);
    }
    plan_loc(plan, EDITOR_BUILDING_PART_CORNER, x0, z0, level, loc_id, SHAPE_WALL_L, SIDE_SOUTH);
    plan_loc(plan, EDITOR_BUILDING_PART_CORNER, x0, z1, level, loc_id, SHAPE_WALL_L, SIDE_WEST);
    plan_loc(plan, EDITOR_BUILDING_PART_CORNER, x1, z1, level, loc_id, SHAPE_WALL_L, SIDE_NORTH);
    plan_loc(plan, EDITOR_BUILDING_PART_CORNER, x1, z0, level, loc_id, SHAPE_WALL_L, SIDE_EAST);
}

static void
plan_roof(
    const struct Editor_BuildingSpec* spec,
    struct Editor_BuildingPlan* plan,
    int level)
{
    int const x0 = plan->x0;
    int const z0 = plan->z0;
    int const x1 = plan->x1;
    int const z1 = plan->z1;
    int const edge_corner = mask_has(spec->edge_mask, SHAPE_ROOFEDGE_CORNER)     ? SHAPE_ROOFEDGE_CORNER
                            : mask_has(spec->edge_mask, SHAPE_ROOFEDGE_DIAGONAL) ? SHAPE_ROOFEDGE_DIAGONAL
                                                                                 : SHAPE_ROOFEDGE;
    int const slope = mask_has(spec->roof_mask, SHAPE_ROOF_SLOPE) ? SHAPE_ROOF_SLOPE : SHAPE_ROOF_FLAT;
    int const convex = mask_has(spec->roof_mask, SHAPE_ROOF_CONVEX) ? SHAPE_ROOF_CONVEX : slope;
    int const flat = mask_has(spec->roof_mask, SHAPE_ROOF_FLAT) ? SHAPE_ROOF_FLAT : SHAPE_ROOF_SLOPE;

    /* The eaves: the edge loc over the walls. */
    plan_ring(plan, EDITOR_BUILDING_PART_ROOF_EDGE, EDITOR_BUILDING_PART_ROOF_EDGE, x0, z0, x1, z1, level,
        spec->edge_id, SHAPE_ROOFEDGE, edge_corner);
    /* The slopes: the ring inside the eaves, where it is a ring. Over a room
     * one tile across, the two slopes would be the same tile: it takes the
     * flat top below instead. */
    if( x1 - x0 >= 3 && z1 - z0 >= 3 )
        plan_ring(plan, EDITOR_BUILDING_PART_ROOF, EDITOR_BUILDING_PART_ROOF, x0 + 1, z0 + 1, x1 - 1, z1 - 1, level,
            spec->roof_id, slope, convex);
    for( int z = z0 + 1; z < z1; z++ )
        for( int x = x0 + 1; x < x1; x++ )
        {
            int const ring = x1 - x0 >= 3 && z1 - z0 >= 3
                             && (x == x0 + 1 || x == x1 - 1 || z == z0 + 1 || z == z1 - 1);
            if( !ring )
                plan_loc(plan, EDITOR_BUILDING_PART_ROOF, x, z, level, spec->roof_id, flat, 0);
        }
}

int
Editor_BuildingPlanMake(
    const struct Editor_BuildingSpec* spec,
    struct Editor_BuildingPlan* plan)
{
    int want_walls;
    int want_roof;
    int want_floor;
    int wall_corner;
    int fence;
    int inset;
    int least;
    int w;
    int h;

    assert(spec);
    assert(plan);
    assert(spec->level >= 0);
    assert(spec->storeys >= 1);
    assert(spec->storeys <= EDITOR_BUILDING_STOREYS_MAX);

    plan->refusal = EDITOR_BUILDING_OK;
    plan->count = 0;
    memset(plan->parts, 0, sizeof(plan->parts));
    plan->x0 = spec->x0 < spec->x1 ? spec->x0 : spec->x1;
    plan->x1 = spec->x0 < spec->x1 ? spec->x1 : spec->x0;
    plan->z0 = spec->z0 < spec->z1 ? spec->z0 : spec->z1;
    plan->z1 = spec->z0 < spec->z1 ? spec->z1 : spec->z0;
    w = plan->x1 - plan->x0 + 1;
    h = plan->z1 - plan->z0 + 1;
    want_walls = spec->wall_id >= 0;
    want_roof = spec->roof_id >= 0 && spec->edge_id >= 0;
    want_floor = spec->floor_overlay >= 0 || spec->roof_flag;
    /* A fence with no L of its own cannot turn a corner on one tile: it is
     * laid as walls are. */
    fence = spec->outward && want_walls && mask_has(spec->wall_mask, SHAPE_WALL_L);
    inset = fence ? 0 : 1;
    least = fence ? 2 : EDITOR_BUILDING_SIDE_MIN;

    if( w < least || h < least )
        plan->refusal = EDITOR_BUILDING_TOO_SMALL;
    else if( w > EDITOR_BUILDING_SIDE_MAX || h > EDITOR_BUILDING_SIDE_MAX )
        plan->refusal = EDITOR_BUILDING_TOO_LARGE;
    else if( !want_walls && !want_roof && !want_floor )
        plan->refusal = EDITOR_BUILDING_EMPTY;
    /* The top storey's walls are on level + storeys - 1; a roof is one more. */
    else if( spec->level + spec->storeys - 1 + (want_roof ? 1 : 0) > 3 )
        plan->refusal = EDITOR_BUILDING_NO_LEVEL;
    else if( want_walls && !mask_has(spec->wall_mask, SHAPE_WALL) )
        plan->refusal = EDITOR_BUILDING_NOT_A_WALL;
    else if( want_roof
             && (!mask_has(spec->edge_mask, SHAPE_ROOFEDGE)
                 || (!mask_has(spec->roof_mask, SHAPE_ROOF_SLOPE) && !mask_has(spec->roof_mask, SHAPE_ROOF_FLAT))) )
        plan->refusal = EDITOR_BUILDING_NOT_A_ROOF;
    if( plan->refusal != EDITOR_BUILDING_OK )
        return 0;

    wall_corner = mask_has(spec->wall_mask, SHAPE_WALL_CORNER) ? SHAPE_WALL_CORNER
                  : mask_has(spec->wall_mask, SHAPE_WALL_POST) ? SHAPE_WALL_POST
                                                               : -1;
    for( int storey = 0; storey < spec->storeys; storey++ )
    {
        int const level = spec->level + storey;

        if( want_floor )
            for( int z = plan->z0 + inset; z <= plan->z1 - inset; z++ )
                for( int x = plan->x0 + inset; x <= plan->x1 - inset; x++ )
                {
                    struct Editor_BuildingPiece* piece =
                        plan_add(plan, EDITOR_BUILDING_FLOOR, EDITOR_BUILDING_PART_FLOOR, x, z, level);
                    piece->overlay = spec->floor_overlay;
                    piece->flags = spec->roof_flag ? EDITOR_BUILDING_FLAG_REMOVE_ROOF : 0;
                }
        if( want_walls && fence )
            plan_fence(plan, level, spec->wall_id);
        else if( want_walls )
            plan_ring(plan, EDITOR_BUILDING_PART_WALL, EDITOR_BUILDING_PART_CORNER, plan->x0, plan->z0, plan->x1,
                plan->z1, level, spec->wall_id, SHAPE_WALL, wall_corner);
        /* The ladder up from this storey stands in the room's north-east
         * corner, its top on the same tile of the storey above; the next
         * flight starts a tile west of it (in a room wide enough to have
         * one), so no tile holds a foot and a top. */
        if( storey + 1 < spec->storeys && spec->ladder_up_id >= 0 && spec->ladder_down_id >= 0 && w >= 3 && h >= 3 )
        {
            int const room_w = w - 2;
            int const x = plan->x1 - 1 - (room_w > 1 ? storey % room_w : 0);
            int const z = plan->z1 - 1;
            plan_loc(plan, EDITOR_BUILDING_PART_LADDER, x, z, level, spec->ladder_up_id, SHAPE_CENTREPIECE, 0);
            plan_loc(plan, EDITOR_BUILDING_PART_LADDER, x, z, level + 1, spec->ladder_down_id, SHAPE_CENTREPIECE, 0);
        }
    }
    /* A roof is eaves round something: it needs the house's least size. */
    if( want_roof && w >= EDITOR_BUILDING_SIDE_MIN && h >= EDITOR_BUILDING_SIDE_MIN )
        plan_roof(spec, plan, spec->level + spec->storeys);
    return plan->count;
}

int
Editor_BuildingWallLine(
    int wall_id,
    int x,
    int z,
    int level,
    int count,
    int along_x,
    struct Editor_BuildingPiece* out)
{
    assert(out);
    assert(wall_id >= 0);
    assert(count >= 0);

    for( int i = 0; i < count; i++ )
    {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].kind = EDITOR_BUILDING_LOC;
        out[i].part = EDITOR_BUILDING_PART_WALL;
        out[i].x = along_x ? x + i : x;
        out[i].z = along_x ? z : z + i;
        out[i].level = level;
        out[i].loc_id = wall_id;
        out[i].shape = SHAPE_WALL;
        /* The south edge (3) for a run along x, the west edge (0) along z. */
        out[i].rotation = along_x ? 3 : 0;
        out[i].overlay = -1;
    }
    return count;
}
