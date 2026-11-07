#ifndef SRC_EDITOR_PANELS_EDPANEL_OPS_H
#define SRC_EDITOR_PANELS_EDPANEL_OPS_H

/**
 * The edits the tools make, as COMMANDS -- built against a document, never
 * applied here. App-free and world-free: a brush stroke, a rectangle paste, a
 * loc nudge and a spawn suppression are each a list of Editor_Cmd the caller
 * submits inside one stroke (Editor_StrokeBegin / Editor_Apply /
 * Editor_StrokeEnd), so the authority records one undo step and every mirror
 * applies the same echo. tools-panels' tests drive these straight against an
 * Editor_Doc.
 *
 * Coordinates are ABSOLUTE world tiles (map square * 64 + local), the one
 * address every connection agrees on. A tile whose square is not open in the
 * document is skipped: its edit could not be saved.
 *
 * Heights. A tile's `h` token is a raw step of 8 world units above the level
 * below (Editor_TileDeriveColumn): raw 0 is absent (Perlin noise on level 0,
 * the level below minus 240 above it), raw 1 is height 0, raw r >= 2 is r
 * units. So the representable heights are 0, 16, 24, 32, ... units of 8
 * above the base -- 8 cannot be spelled -- and these ops work in that ladder.
 * Derived heights are the renderer's (negative is up).
 */

#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_path.h"
#include "editor/editor_spawn.h"
#include "editor/editor_ui.h"

/* ---- heights ---------------------------------------------------------------- */

/** The derived (rendered) height of world tile (x, z) on `level`, from the
 *  document alone. 0 when its square is not open. */
int
Editor_OpsDerivedHeight(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int level,
    int* out_height);

/** The raw `h` byte that puts a tile at `height` (derived, negative up) on a
 *  base of `base` (0 on level 0, the derived height below above that):
 *  the nearest representable step, a tie going up. 1..255. */
int
Editor_OpsRawForHeight(
    int base,
    int height);

/* ---- brushes ------------------------------------------------------------------ */

/** One brush application at one centre tile. */
struct Editor_BrushOp
{
    /** RAISE, LOWER, FLATTEN, SMOOTH, PAINT_UNDERLAY, PAINT_OVERLAY,
     *  PAINT_FLAGS, NOISE. */
    enum Editor_Tool tool;
    int world_x;
    int world_z;
    int level;
    struct Editor_Brush brush;
    /** FLATTEN: the derived height every tile in the brush goes to. */
    int flatten_height;
    /** NOISE: the seed. Each tile's offset is a hash of the seed and its
     *  world tile, -step..+step scaled by the falloff: one seed is one
     *  relief, so a plan can be remade (the preview) and a tile revisited
     *  in a stroke takes the same offset again, as Raise would; the caller
     *  moves the seed on per stroke so the next drag is another relief. */
    uint32_t noise_seed;
    int underlay_id;
    /** PAINT_UNDERLAY: the share of the brush's tiles painted, 1..100 %
     *  (0 reads as 100: a zeroed op paints every tile), each tile's draw a
     *  hash of noise_seed and the tile scaled by the falloff -- the dither
     *  the client's blend smooths into a gradient. */
    int paint_mix;
    int overlay_id;
    int overlay_none;
    int overlay_shape;
    int overlay_rotation;
    int flags_value;
};

/** The brush's weight at offset (dx, dz) from its centre: 0 outside the disc,
 *  1 at the centre, per the falloff in between. */
double
Editor_BrushWeight(
    const struct Editor_Brush* brush,
    int dx,
    int dz);

/**
 * The TILE commands one application makes: one per tile it changes, none for
 * a tile it would leave as it is. Every target is computed from the document
 * BEFORE any of them applies, so smoothing reads one consistent surface.
 * Returns the count (at most `max`, and at most EDITOR_BRUSH_TILES_MAX).
 */
int
Editor_BrushCommands(
    struct Editor_Doc* doc,
    const struct Editor_BrushOp* op,
    struct Editor_Cmd* out,
    int max);

/* ---- the path ------------------------------------------------------------------ */

/**
 * The TILE commands a path makes (editor/editor_path.h): overlay `overlay_id`
 * (as the .jm2 holds it) on every tile Editor_PathPlan covers at `level`,
 * with the shape and rotation it chose. A tile already carrying that overlay
 * is joined, not replaced; a tile whose square is not open is left out, and
 * counted in *out_closed when that is given. Returns the count (at most
 * `max`); none for tiles it would leave as they are.
 */
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
    int* out_closed);

/* ---- scatter: locs by the brushful --------------------------------------------- */

/** Kinds one scatter set holds. */
#define EDITOR_SCATTER_SET_MAX 32

/** The locs Scatter drops: ids with their record sizes (one tile until the
 *  record is resident), and the shape each is placed as (the record's first
 *  shape; a centrepiece for most). */
struct Editor_ScatterSet
{
    int count;
    int loc_id[EDITOR_SCATTER_SET_MAX];
    int shape[EDITOR_SCATTER_SET_MAX];
    int size_x[EDITOR_SCATTER_SET_MAX];
    int size_z[EDITOR_SCATTER_SET_MAX];
};

/** One Scatter application at one centre tile. */
struct Editor_ScatterOp
{
    int world_x;
    int world_z;
    int level;
    struct Editor_Brush brush;
    /** The share of tiles that take a loc, 1..100, scaled by the brush's
     *  weight at the tile. */
    int density;
    /** 1: each loc turned at random (0..3); 0: rotation 0. */
    int rotate;
    uint32_t seed;
    /** 1: take the set's locs off the brush's tiles instead. */
    int erase;
    struct Editor_ScatterSet set;
};

/**
 * The LOC commands one Scatter application makes. Every tile under the brush
 * draws a number from (seed, tile): below density x weight the tile takes a
 * loc of the set (which one, and its turn, drawn the same way), placed at
 * its shape with the tile as the footprint's south-west corner -- unless any
 * tile of that footprint is in a closed square or already holds a loc in
 * that layer (a centrepiece on any of its tiles; so a drag never stacks and
 * revisiting a tile adds nothing). `erase`: every loc of the set standing on
 * a brush tile (at any shape) is removed. `out` needs one slot per tile.
 * Returns the count.
 */
int
Editor_ScatterCommands(
    struct Editor_Doc* doc,
    const struct Editor_ScatterOp* op,
    struct Editor_Cmd* out,
    int max);

/* ---- plateau: a flat top with a smoothed skirt ---------------------------------- */

/**
 * The TILE commands that flatten a region to `height` (derived, negative
 * up) on `level` and ease a skirt of `skirt` tiles around it to the ground
 * as it is: a tile `d` tiles outside the region (1..skirt, by the larger
 * axis distance) goes to its own height plus (height - own) x s, s the
 * smoothstep of (skirt + 1 - d) / (skirt + 1), so the slope leaves the top
 * and meets the ground tangentially -- a mound where the ground is lower, a
 * hole where it is higher. `inside` says whether a world tile is in the
 * region; the skirt is the tiles within `skirt` of one that is. The region
 * must lie in `x0..x1`, `z0..z1` (the brush's disc, the rectangle). A tile
 * within half a step of where it goes is left alone. Returns the count (at
 * most `max`).
 */
typedef int (*Editor_PlateauInsideFn)(
    void* user,
    int world_x,
    int world_z);

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
    int max);

/** The Plateau brush at one centre: the disc of `brush->radius` (its
 *  falloff ignored: the top is flat) flattened to `height` with `skirt`. */
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
    int max);

/** The rectangle (normalised, clamped) flattened to `height` with `skirt`. */
int
Editor_TileRectPlateau(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    int height,
    int skirt,
    struct Editor_Cmd* out,
    int max);

/* ---- the rectangle ------------------------------------------------------------ */

/** Tiles a rectangle op may cover: 64 x 64, one square's worth. */
#define EDITOR_RECT_TILES_MAX (64 * 64)

/** A copied rectangle of authored tiles. */
struct Editor_TileClip
{
    int w;
    int h;
    /** w * h, row-major from the SW corner; `present` 0 where the square was
     *  not open (the paste leaves those tiles alone). */
    struct Editor_Tile tiles[EDITOR_RECT_TILES_MAX];
    unsigned char present[EDITOR_RECT_TILES_MAX];
    /** The SW tile's height in units above its base, so a paste shifts the
     *  authored heights by the difference to where it lands. */
    int anchor_units;
    int valid;
};

/** Normalised (x0 <= x1, z0 <= z1) and clamped to EDITOR_RECT_TILES_MAX.
 *  0 when the rectangle is invalid or too large. */
int
Editor_TileRectNormalise(
    const struct Editor_TileRect* in,
    struct Editor_TileRect* out);

/** Copy the rectangle's authored tiles. Returns the tiles copied. */
int
Editor_TileClipCopy(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    struct Editor_TileClip* clip);

/** Paste `clip` with its SW corner at (world_x, world_z) on `level`: authored
 *  heights move by the anchor difference, absent heights stay absent. */
int
Editor_TileClipPaste(
    struct Editor_Doc* doc,
    const struct Editor_TileClip* clip,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max);

/** Paint the rectangle with the palette's picks (`tools`): underlay, and the
 *  overlay (or its removal). */
int
Editor_TileRectFill(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    const struct EditorUiTools* tools,
    struct Editor_Cmd* out,
    int max);

/** Every tile of the rectangle back to absent: no tokens at all. */
int
Editor_TileRectClear(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    struct Editor_Cmd* out,
    int max);

/* ---- locs ----------------------------------------------------------------------- */

/** The loc a ref names in the document: its index in the square's list, or
 *  -1 (square not open, nothing there, or -- when ref->loc_id >= 0 -- a
 *  different loc there now). */
int
Editor_LocRefResolve(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* ref);

/** A ref for world tile (x, z): fills map and local coordinates. */
void
Editor_LocRefAt(
    struct Editor_LocRef* ref,
    int world_x,
    int world_z,
    int level,
    int shape,
    int loc_id,
    int rotation);

/** The ref's absolute tile. */
void
Editor_LocRefWorld(
    const struct Editor_LocRef* ref,
    int* out_world_x,
    int* out_world_z);

/** The next shape in the loc's own family (walls 0,1,2,3,9; wall decor
 *  4..8; ground 10,11; roofs 12..21; ground decor 22 stays). */
int
Editor_LocShapeCycle(int shape);

/** What a selection op did: the commands, and where each selected loc is now. */
struct Editor_LocOpResult
{
    int command_count;
    /** The selection after the op, in the input's order (a delete leaves it
     *  empty; a duplicate selects the copies). */
    struct Editor_LocRef refs[EDITOR_LOC_SELECTION_MAX];
    int ref_count;
    /** Locs the op pushed off a tile+shape a moved loc landed on: each went
     *  to the tile the move left empty (a swap). The first one's id. */
    int swapped;
    int swapped_loc_id;
    /** Selected locs left where they were because their landing tile+shape
     *  is held and a copy / reshape has nowhere to put the holder; the first
     *  one's landing tile (its loc_id / rotation are the holder's). */
    int blocked;
    struct Editor_LocRef blocked_at;
};

/** Place one loc. `out` needs 1 slot. Onto a tile+shape already placed the
 *  command is a replace (before = the occupant), so its undo restores it. */
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
    struct Editor_LocOpResult* result);

/** Move every selected loc by (dx, dz) tiles. A loc that changes square is a
 *  delete from one and an add to the other, in the same stroke. A loc landing
 *  on a tile+shape held by an unselected loc SWAPS with it: the holder goes to
 *  the tile the move leaves empty (a wall piece slides along its wall line).
 *  The reshape and duplicate below leave such a loc where it is and count it
 *  in result->blocked. `out` needs 4 * n slots. */
int
Editor_LocOpsNudge(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int dx,
    int dz,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Move the selection so its first loc lands on (world_x, world_z). */
int
Editor_LocOpsMoveTo(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int world_x,
    int world_z,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Turn every selected loc by `quarters` (1 = 90 degrees). */
int
Editor_LocOpsRotate(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int quarters,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/* ---- loc shapes ----------------------------------------------------------------
 *
 * Which of the 23 placement shapes a loc can be placed in is what its config
 * has a model for: a shape-list row per model shape (none: a centrepiece
 * only), wall decor 5..8 drawing shape 4's models and the diagonal
 * centrepiece 11 shape 10's (world_scenery.u.c). Every way of choosing a
 * shape -- the Place pose, the shape strip, Reshape, a command the panels
 * build -- is limited to that mask.
 */
#define EDITOR_LOC_SHAPE_COUNT 23

struct ToriRS_Location;

/** "wall", "wall L", "centrepiece", ... for shape 0..22. */
const char*
Editor_LocShapeName(int shape);

/** The shape whose models a placement of `shape` draws (4 for 5..8, 10 for
 *  11). */
int
Editor_LocShapeModelShape(int shape);

/** Bit s set: the loc can be placed as shape s. 0 when it has no models. */
uint32_t
Editor_LocShapeMask(const struct ToriRS_Location* cfg);

/** The mask's shapes in the order they are offered (centrepiece, ground
 *  decor, walls, decor, roofs); returns the count. */
int
Editor_LocShapeList(
    uint32_t mask,
    int out[EDITOR_LOC_SHAPE_COUNT]);

/** The first shape the mask offers, or -1 for an empty mask. */
int
Editor_LocShapeFirst(uint32_t mask);

/** The mask's next shape after `shape` in that order, wrapping (a shape not
 *  in the mask goes to the first); `shape` itself for an empty mask. */
int
Editor_LocShapeNext(
    uint32_t mask,
    int shape);

/**
 * Fit to neighbours: the wall shape and rotation that joins loc `loc_id` at
 * world tile (x, z) on `level` to the walls OF THE SAME LOC on the tiles
 * around it -- a straight wall (0) on the edge its north/south or east/west
 * neighbours run along, an L (2) where a vertical and a horizontal run meet,
 * a diagonal (9) continuing a diagonal neighbour. Only shapes in `mask`.
 * Returns 0 (outputs untouched) when no neighbour says anything.
 */
int
Editor_LocFitWall(
    struct Editor_Doc* doc,
    int loc_id,
    uint32_t mask,
    int world_x,
    int world_z,
    int level,
    int* out_shape,
    int* out_rotation);

/** Give every selected loc the shape `shapes[i]` (and, when `rotations` is
 *  not NULL and rotations[i] >= 0, that rotation); -1 keeps it. A loc whose
 *  new tile+shape is held is left as it is (result->blocked). */
int
Editor_LocOpsReshapeTo(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    const int* shapes,
    const int* rotations,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Cycle every selected loc's shape within its family. (The panels cycle
 *  within the loc's own shapes instead: Editor_LocOpsReshapeTo.) */
int
Editor_LocOpsReshape(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Copy every selected loc offset by (dx, dz); the copies become the
 *  selection. */
int
Editor_LocOpsDuplicate(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int dx,
    int dz,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Delete every selected loc. */
int
Editor_LocOpsDelete(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/* ---- runs: the Place tool's drag (design #placing, "Drag along tiles") -------
 *
 * A drag with the Place tool is a RUN: the tiles the pointer crossed, in
 * order, each one orthogonal step from the one before (a diagonal step is two,
 * along x and then along z). Going back over the run takes the tiles after
 * the one returned to off it, so a run never crosses itself and a wrong turn
 * is unwound by retracing it.
 *
 * With a wall pick the run is a WALL: one piece per tile, on the side of the
 * path the pose names (the edge of the first segment the pose stands on; the
 * left side when it stands on neither), the two ends a straight piece (0),
 * and every turn a corner at the rotation Fit computes for the two edges that
 * meet there (Editor_LocWallCornerRotation): an L (2) on the outside of the
 * turn, a corner post (3) on the inside, where the two lines only touch at a
 * point. Any other pick (a centrepiece, ground decor, a roof) puts its pose
 * on every tile of the run.
 *
 * The plan is the preview the overlay draws and what the release commits:
 * nothing is in the document before the release turns it into commands (one
 * stroke, one undo step). A tile the plan cannot fill is REFUSED: its square
 * is not open, or another loc holds its key -- for a wall the tile's wall
 * layer (shapes 0..3: the world keeps one wall a tile), for anything else the
 * tile and shape. A piece of the same loc already there is re-posed
 * (the straight end of an earlier run becomes this run's corner).
 */
#define EDITOR_LOC_RUN_MAX 256

struct Editor_LocRunPath
{
    int count;
    int x[EDITOR_LOC_RUN_MAX];
    int z[EDITOR_LOC_RUN_MAX];
};

/** A run of one tile, (world_x, world_z). */
void
Editor_LocRunBegin(
    struct Editor_LocRunPath* path,
    int world_x,
    int world_z);

/**
 * Carry the run to (world_x, world_z) in orthogonal steps from its last tile
 * (the longer axis first), retracing where a step returns to a tile already
 * on it. `constrain` (Shift held): the run is instead the straight line from
 * its first tile along the axis the pointer has moved further on, to the
 * pointer's projection on that axis. `constrain` 2 (Shift with a diagonal
 * pose): the staircase from the first tile along the diagonal the pointer
 * is nearest, one tile along x then one along z per diagonal step, as far
 * as the pointer's projection on it -- what a diagonal wall run is laid on.
 * A run of EDITOR_LOC_RUN_MAX tiles stops growing. Returns 1 when the run
 * changed.
 */
int
Editor_LocRunTo(
    struct Editor_LocRunPath* path,
    int world_x,
    int world_z,
    int constrain);

enum Editor_LocRunState
{
    /** Goes into the document: a new loc, or this loc re-posed (has_holder). */
    EDITOR_LOC_RUN_PLACE = 0,
    /** Already there as planned: nothing to do. */
    EDITOR_LOC_RUN_KEEP,
    /** No piece: an inside corner of a loc with no corner post (shape 3). */
    EDITOR_LOC_RUN_NONE,
    /** Refused: the tile's square is not open. */
    EDITOR_LOC_RUN_CLOSED,
    /** Refused: another loc holds the key (`holder`). */
    EDITOR_LOC_RUN_TAKEN,
};

struct Editor_LocRunPiece
{
    int world_x;
    int world_z;
    int shape;
    int rotation;
    enum Editor_LocRunState state;
    /** PLACE over this loc's own piece (re-posed), or TAKEN: the loc there. */
    int has_holder;
    struct Editor_Loc holder;
};

struct Editor_LocRunPlan
{
    int loc_id;
    int level;
    /** 1: a wall run (corners and ends); 0: the pose on every tile. */
    int wall;
    /** 1: a diagonal wall run (shape 9 on every second tile). */
    int diagonal;
    /** The Fit toggle and the loc's shapes, as the plan was made. */
    int fit;
    uint32_t mask;
    int count;
    struct Editor_LocRunPiece pieces[EDITOR_LOC_RUN_MAX];
    int place_count;
    int refused_count;
    /** The first refused piece, -1 when none is. */
    int first_refused;
};

/** A Place drag in progress, one per window (the panel holds it): the path,
 *  the plan the overlay draws, and whether its refusal was said (once a run:
 *  R8, without a sentence a frame). */
struct Editor_PlaceRun
{
    int active;
    int level;
    int said;
    struct Editor_LocRunPath path;
    struct Editor_LocRunPlan plan;
};

/** A wall pose: the straight wall, its corners and the diagonal (0, 1, 2, 3,
 *  9; Fit's family). The diagonal stands in the ground layer, the rest in the
 *  wall layer (Editor_LocShapeLayer). */
int
Editor_LocShapeIsWall(int shape);

/** The world's layer for a placement shape (0 wall, 1 wall decor, 2 ground,
 *  3 ground decor), as world.h's World_LocShapeToLayer has it. */
int
Editor_LocShapeLayer(int shape);

/** The rotation of the corner piece (an L, 2, or a corner post, 3) standing
 *  where edges `edge_a` and `edge_b` meet (0 west, 1 north, 2 east, 3 south;
 *  perpendicular): the r with {r, r + 1} = {a, b}. Fit's L uses the same
 *  rule. */
int
Editor_LocWallCornerRotation(
    int edge_a,
    int edge_b);

/**
 * The plan for placing loc `loc_id` (its shapes `mask`) along `path` on
 * `level`, with the pose (`shape`, `rotation`) the ghost shows at the run's
 * first tile. A wall run when `shape` is a wall shape and the loc has the
 * straight wall (0); otherwise the pose on every tile.
 *
 * `fit` (the Fit toggle): a straight piece of the run landing on a tile
 * where this loc already stands as a straight wall on a PERPENDICULAR edge
 * becomes the L (2) of the two edges, so a run's end meets a wall already
 * there (and a crossing keeps both); one landing on its own L that already
 * carries the run's edge is kept as it is. Without `fit` the piece replaces
 * what is there, as before.
 *
 * A DIAGONAL run: the pose is the diagonal wall (9) and the path is a
 * staircase (each tile one step along x then one along z, or the reverse,
 * the same way throughout -- what Editor_LocRunTo builds with `constrain`
 * 2). Then every second tile, the ones on the diagonal, takes shape 9 turned
 * to that diagonal (rotation 1 for north-west / south-east, 0 for
 * south-west / north-east, as Editor_LocFitWall reads them) and the tiles
 * between take nothing (EDITOR_LOC_RUN_NONE). A diagonal pose on any other
 * path is the wall run it always was.
 */
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
    struct Editor_LocRunPlan* plan);

/** The plan's PLACE pieces as LOC commands: an add, a replace (same key), or
 *  a remove and an add (the loc's own piece re-shaped). `out` needs 2 a
 *  piece. Returns the count. */
int
Editor_LocRunCommands(
    const struct Editor_LocRunPlan* plan,
    struct Editor_Cmd* out,
    int max);

/** The status sentence for the plan's first refused piece ("Place refused:
 *  tile 3222,3218 L1 holds a wall of that shape (loc 1902); press E to
 *  swap"). 0 (and `out` empty) when nothing is refused. */
int
Editor_LocRunRefusal(
    const struct Editor_LocRunPlan* plan,
    char* out,
    size_t size);

/* ---- selecting by area and by id (design #placing, "Select tool") ----------- */

/** The tiles a placed loc covers from its SW tile: a centrepiece (10, 11) its
 *  size, x by z, turned with it (rotation 1 or 3 swaps them); every other
 *  shape one tile. */
void
Editor_LocFootprint(
    int shape,
    int rotation,
    int size_x,
    int size_z,
    int* out_w,
    int* out_h);

/** A loc's size from its record into out_size_x / out_size_z; 0 when the
 *  record is not resident (the loc then counts as one tile). */
typedef int (*Editor_LocSizeFn)(
    void* user,
    int loc_id,
    int* out_size_x,
    int* out_size_z);

/**
 * Every loc on `rect->level` of the open squares whose footprint touches the
 * rectangle (normalised: x0 <= x1, z0 <= z1), in the squares' and their
 * lists' order. Writes the first `max` into `out`; returns how many touch it
 * (more than `max` when they did not all fit). `size` may be NULL: every loc
 * then one tile.
 */
int
Editor_LocsTouchingRect(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    Editor_LocSizeFn size,
    void* size_user,
    struct Editor_LocRef* out,
    int max);

/** Every loc `loc_id` on `level` in the open squares ("select same"): the
 *  first `max` into `out`; returns how many there are. */
int
Editor_LocsOfId(
    struct Editor_Doc* doc,
    int loc_id,
    int level,
    struct Editor_LocRef* out,
    int max);

/** How a set of locs joins the selection: the marquee's and the click's
 *  modifiers (none replaces, Shift adds, Ctrl toggles, Alt subtracts). */
enum Editor_LocSelectMode
{
    EDITOR_LOC_SELECT_REPLACE = 0,
    EDITOR_LOC_SELECT_ADD,
    EDITOR_LOC_SELECT_TOGGLE,
    EDITOR_LOC_SELECT_SUBTRACT,
};

/** The modifiers held, as the mode they mean (Alt over Ctrl over Shift). */
enum Editor_LocSelectMode
Editor_LocSelectModeOf(
    int shift,
    int ctrl,
    int alt);

/**
 * Join `refs` (n of them) to `selection` (count, room for max) by `mode`; a
 * loc is the same loc by its tile, level and shape (its key). Returns the new
 * count; a loc the selection had no room for is counted in *out_dropped.
 */
int
Editor_LocSelectionMerge(
    struct Editor_LocRef* selection,
    int count,
    int max,
    const struct Editor_LocRef* refs,
    int n,
    enum Editor_LocSelectMode mode,
    int* out_dropped);

/* ---- spawns ------------------------------------------------------------------- */

/** The SPAWN document key of the square holding world tile (x, z). */
void
Editor_SpawnDocKeyAt(
    char out[EDITOR_DOC_KEY_MAX],
    int world_x,
    int world_z);

/** The TEXT key of a square's GENERATED spawn file
 *  (server/scripts/areas/world/configs/m<x>_<z>.spawn), read only. */
void
Editor_SpawnGeneratedKey(
    char out[EDITOR_DOC_KEY_MAX],
    int map_x,
    int map_z);

/** A spawn row as a SPAWN (or SPAWN_SUPPRESS) command: `adding` 1 adds the
 *  row, 0 removes it. */
void
Editor_SpawnCommand(
    struct Editor_Cmd* out,
    const struct Editor_SpawnRow* row,
    int adding);

/**
 * Delete a spawn the panel lists. `edited` is the square's edited `.spawn`
 * (NULL when it has none yet). A row the edited file holds is removed from
 * it; a row only the GENERATED file holds gets a suppression row instead
 * (design #placement), because a write into the generated file would be
 * undone by its next regeneration. Returns 1 and fills `out`.
 */
int
Editor_SpawnDeleteCommand(
    const struct Editor_SpawnFile* edited,
    const struct Editor_SpawnRow* row,
    struct Editor_Cmd* out);

/**
 * Duplicate spawn rows: each of `rows` copied by (dx, dz, dlevel) as a plain
 * SPAWN add (a copy of a generated row is an EDITED row: the generated file
 * is never written), into the edited document of the square the copy lands
 * in — which may differ from the original's. `out_rows` (n of them, may be
 * NULL) gets each copy, for the selection. Returns the commands written;
 * a copy that would leave the world (negative tile, level outside 0..3) is
 * skipped.
 */
int
Editor_SpawnOpsDuplicate(
    const struct Editor_SpawnRow* rows,
    int n,
    int dx,
    int dz,
    int dlevel,
    struct Editor_Cmd* out,
    int max,
    struct Editor_SpawnRow* out_rows);

/** Whether `edited` suppresses the generated `row`. */
int
Editor_SpawnIsSuppressed(
    const struct Editor_SpawnFile* edited,
    const struct Editor_SpawnRow* row);

#endif
