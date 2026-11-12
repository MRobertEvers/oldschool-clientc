#ifndef SRC_EDITOR_EDITOR_BUILDING_H
#define SRC_EDITOR_EDITOR_BUILDING_H

/**
 * BUILDINGS (the Building tool, 2026-11-11): a dragged rectangle planned as
 * the pieces a house is made of -- walls with their corner pieces, a floor,
 * the "remove roof" flag indoors, a roof with its edge, slope and corner
 * pieces, further storeys and a ladder between them. The plan is ordinary
 * locs and tile tokens: what it lays can be moved, swapped and deleted by
 * hand afterwards like anything else in the square.
 *
 * THE RULES ARE THE CONTENT'S. They were read off the houses of m50_50
 * (Lumbridge: the smithy at 21..29 x 51..58, the house at 25..34 x 35..43)
 * and checked against every square's counts:
 *
 *   - The dragged rectangle is the FOOTPRINT: what the roof covers. The walls
 *     stand on its outer ring of tiles, each on the tile's edge that faces
 *     indoors (the west column's walls on their east edge, rotation 2; north
 *     row 3; east column 0; south row 1), so the room is the footprint less
 *     one tile all round. A corner tile takes the corner piece (shape 3, the
 *     "wall corner", else shape 1) turned to the two edges that meet at its
 *     inner corner (Editor_LocWallCornerRotation: SW 1, NW 2, NE 3, SE 0).
 *   - The room's tiles carry the floor overlay and the "remove roof" flag
 *     (4): the client hides everything above a player standing on one. The
 *     ring is left as it is (it is outdoors: grass under the eaves).
 *   - The roof is one level up over the whole footprint: the ring is the
 *     roof's EDGE loc (shape 18, the square corner 21, else 19, at the
 *     corners), the ring inside it the roof loc's slope (12, the convex
 *     corner 16), everything further in its flat top (17). Each piece takes
 *     the rotation of the wall under its side.
 *   - A window is two wall decorations: the outside face on the wall's own
 *     tile (shape 5, the wall's rotation) and the inside face on the tile
 *     across the wall (shape 4, turned half round).
 *   - A door replaces the straight wall piece, same tile, same rotation. A
 *     GATE is two leaves side by side on one edge (fencegate_l beside
 *     fencegate_r, 20 times of 20): the second stands on the next tile
 *     clockwise round the enclosure, same rotation.
 *   - A FENCE is not a house: it has no eaves to stand under, so it runs
 *     along the dragged rectangle's own outer edge (west column on its west
 *     edge, rotation 0; north row 1; east 2; south 3) and turns the corners
 *     with the L piece (shape 2) on the corner tile. An 8x8 drag is an 8x8
 *     pen.
 *
 * Pure: no document, no host. A loc's shapes arrive as its mask
 * (edpanel_ops.h Editor_LocShapeMask); the document side (commands, the door
 * and window swaps) is panels/edpanel_ops_building.c.
 */

#include <stdint.h>

/** The longest side a building may have, in tiles, and the shortest (walls
 *  all round and one tile of room). */
#define EDITOR_BUILDING_SIDE_MAX 32
#define EDITOR_BUILDING_SIDE_MIN 3
/** Storeys one gesture lays: walls on up to three levels, the roof above. */
#define EDITOR_BUILDING_STOREYS_MAX 3
/** Pieces the largest plan holds: three storeys of walls, floor and ladder,
 *  and the roof. */
#define EDITOR_BUILDING_PIECES_MAX \
    (EDITOR_BUILDING_STOREYS_MAX * (4 * EDITOR_BUILDING_SIDE_MAX + EDITOR_BUILDING_SIDE_MAX * EDITOR_BUILDING_SIDE_MAX + 2) \
        + EDITOR_BUILDING_SIDE_MAX * EDITOR_BUILDING_SIDE_MAX)

/** The "remove roof" tile flag (the `f` token's bit 4). */
#define EDITOR_BUILDING_FLAG_REMOVE_ROOF 4

enum Editor_BuildingPieceKind
{
    /** One loc: id, shape, rotation at (x, z, level). */
    EDITOR_BUILDING_LOC = 0,
    /** One tile's floor: the overlay laid (when `overlay` >= 0) and the flag
     *  bits set on top of what the tile holds; its height and underlay are
     *  left as they are. */
    EDITOR_BUILDING_FLOOR,
};

/** What a loc piece is of the building, for the count in the status
 *  sentence and for the tests. */
enum Editor_BuildingPart
{
    EDITOR_BUILDING_PART_WALL = 0,
    EDITOR_BUILDING_PART_CORNER,
    EDITOR_BUILDING_PART_ROOF_EDGE,
    EDITOR_BUILDING_PART_ROOF,
    EDITOR_BUILDING_PART_LADDER,
    EDITOR_BUILDING_PART_FLOOR,
    EDITOR_BUILDING_PART_COUNT
};

struct Editor_BuildingPiece
{
    enum Editor_BuildingPieceKind kind;
    enum Editor_BuildingPart part;
    /** Absolute world tile and level. */
    int x;
    int z;
    int level;
    /* LOC */
    int loc_id;
    int shape;
    int rotation;
    /* FLOOR: the overlay as the .jm2 holds it (-1: leave the overlay), and
     * the flag bits to set. */
    int overlay;
    int flags;
};

/** What to build. Ids below 0 leave that part out. */
struct Editor_BuildingSpec
{
    /** The footprint, absolute tiles, inclusive (any corner order). */
    int x0;
    int z0;
    int x1;
    int z1;
    /** The ground floor's level; the roof lands `storeys` above it. */
    int level;
    /** 1..EDITOR_BUILDING_STOREYS_MAX. */
    int storeys;
    /** The wall loc and its shapes (Editor_LocShapeMask). */
    int wall_id;
    uint32_t wall_mask;
    /** 1: a fence -- the walls stand on the rectangle's outer edges with L
     *  corners, and the floor (when asked for) is the whole rectangle. A
     *  loc with no L (shape 2) is laid as a house's walls are. */
    int outward;
    /** The room's floor overlay, as the .jm2 holds it; -1 lays none. */
    int floor_overlay;
    /** 1: the room's tiles take the "remove roof" flag. */
    int roof_flag;
    /** The roof loc (slopes 12, corners 16, top 17) and its edge loc (18,
     *  corners 21 or 19); either below 0 lays no roof. A kit that is one loc
     *  names it twice. */
    int roof_id;
    uint32_t roof_mask;
    int edge_id;
    uint32_t edge_mask;
    /** The ladder between storeys: its foot (climb up) and its top (climb
     *  down), centrepieces. Either below 0, or one storey, lays none. */
    int ladder_up_id;
    int ladder_down_id;
};

enum Editor_BuildingRefusal
{
    EDITOR_BUILDING_OK = 0,
    /** A side under EDITOR_BUILDING_SIDE_MIN: no room inside the walls (a
     *  fence: under 2, where two of its sides would share a tile's edge). */
    EDITOR_BUILDING_TOO_SMALL,
    /** A side over EDITOR_BUILDING_SIDE_MAX. */
    EDITOR_BUILDING_TOO_LARGE,
    /** The storeys (and the roof above them) do not fit under level 3. */
    EDITOR_BUILDING_NO_LEVEL,
    /** The wall loc has no straight wall (shape 0). */
    EDITOR_BUILDING_NOT_A_WALL,
    /** The roof loc has no slope (12) or flat top (17), or the edge loc no
     *  roof edge (18). */
    EDITOR_BUILDING_NOT_A_ROOF,
    /** Nothing was asked for: no walls, no floor, no flag, no roof. */
    EDITOR_BUILDING_EMPTY,
};

struct Editor_BuildingPlan
{
    enum Editor_BuildingRefusal refusal;
    /** The footprint normalised, and the room inside it. */
    int x0;
    int z0;
    int x1;
    int z1;
    int count;
    int parts[EDITOR_BUILDING_PART_COUNT];
    struct Editor_BuildingPiece pieces[EDITOR_BUILDING_PIECES_MAX];
};

/**
 * Plan `spec` into `plan` (cleared first). Returns the pieces; 0 with
 * plan->refusal set when it cannot be built. Pieces are in the order they
 * are laid: per storey its floor, walls and ladder, then the roof.
 */
int
Editor_BuildingPlanMake(
    const struct Editor_BuildingSpec* spec,
    struct Editor_BuildingPlan* plan);

/** The rotation a wall (or a roof edge, or a roof slope) takes on side
 *  `side` of a footprint (0 west, 1 north, 2 east, 3 south): the edge of its
 *  tile that faces indoors. */
int
Editor_BuildingSideRotation(int side);

/** The rotation of the corner piece on corner `corner` of a footprint
 *  (0 south-west, 1 north-west, 2 north-east, 3 south-east). */
int
Editor_BuildingCornerRotation(int corner);

/**
 * A straight wall run of `count` tiles from (x, z) along +x (`along_x` 1) or
 * +z (0), as LOC pieces of shape 0 into `out` (room for `count`): the pieces
 * stand on the tiles' SOUTH edge for a run along x and their WEST edge for a
 * run along z, so the line a person dragged is the line the wall follows --
 * an inside wall dividing two rooms. Returns the pieces written.
 */
int
Editor_BuildingWallLine(
    int wall_id,
    int x,
    int z,
    int level,
    int count,
    int along_x,
    struct Editor_BuildingPiece* out);

/** The tile a gate's second leaf stands on, beside the first on (x, z) with
 *  `rotation`: the next one clockwise round the enclosure (north of a west
 *  edge's, east of a north edge's, south of an east edge's, west of a south
 *  edge's). */
void
Editor_BuildingGateBeside(
    int x,
    int z,
    int rotation,
    int* out_x,
    int* out_z);

/** The tile across a wall standing on (x, z) with `rotation` (the edge it is
 *  on: 0 west, 1 north, 2 east, 3 south). */
void
Editor_BuildingAcross(
    int x,
    int z,
    int rotation,
    int* out_x,
    int* out_z);

/* ---- styles ------------------------------------------------------------------
 *
 * Named kits of real, commonly used locs, so a map maker chooses "Timber and
 * plaster" and not loc 1602. Each was taken from the content by count: the
 * wall with the door and the window it is most often built with
 * (brickwall stands beside poordoor 75 times and carries `window` 168
 * times), the roof with the edge it is laid with (roof_wooden with
 * roofedge_wooden 2,004 times). The ids are the cache's, the same in every
 * OldSchool revision that has the loc.
 */
struct Editor_BuildingWallStyle
{
    const char* name;
    int wall_id;
    /** The door that replaces a straight piece; -1 none. */
    int door_id;
    /** A gate's second leaf, on the next piece along; -1 for a door of one
     *  piece. */
    int door2_id;
    /** The window decoration; -1 none (a fence). */
    int window_id;
    /** 1: a fence or a railing -- no floor, no roof and no flag by default. */
    int fence;
    /** The roof style (index) that most often tops this wall. */
    int roof_style;
};

struct Editor_BuildingRoofStyle
{
    const char* name;
    int roof_id;
    int edge_id;
};

struct Editor_BuildingFloorStyle
{
    const char* name;
    /** The overlay as the .jm2 holds it. */
    int overlay;
};

int
Editor_BuildingWallStyleCount(void);

const struct Editor_BuildingWallStyle*
Editor_BuildingWallStyleAt(int index);

/** The first Walls style `loc_id` is the wall, the door or a gate leaf of;
 *  -1 for a loc no style names. What a Door or Window click on a piece of
 *  that wall puts in, whatever the Walls dropdown now says (2026-11-11: with
 *  the pen's "Wooden fence" still chosen, a Door click on the cottage hung a
 *  fence gate in its timber wall). */
int
Editor_BuildingWallStyleOf(int loc_id);

int
Editor_BuildingRoofStyleCount(void);

const struct Editor_BuildingRoofStyle*
Editor_BuildingRoofStyleAt(int index);

int
Editor_BuildingFloorStyleCount(void);

const struct Editor_BuildingFloorStyle*
Editor_BuildingFloorStyleAt(int index);

/** The ladder the storeys are joined with: `ladder` (Climb-up) under
 *  `laddertop` (Climb-down), which the content's generic climb scripts
 *  already serve. */
#define EDITOR_BUILDING_LADDER_UP 16683
#define EDITOR_BUILDING_LADDER_DOWN 16679

#endif
