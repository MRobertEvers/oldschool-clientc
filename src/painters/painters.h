#ifndef PAINTERS_H
#define PAINTERS_H

#include "graphics/projection.h"

#include <stddef.h>
#include <stdint.h>
/**
 * Coordinate naming convention:
 * c := chunk -> relative to the map chunk, 0-63
 * s := scene -> relative to the scene, however big the scene is
 * w := world -> relative to the world, 0-16k. This is the world coordinate system.
 * m := model -> model coordinates for rendering.
 */

/**
 * Tells the renderer to defer drawing locs until
 * the underlay is drawn for tiles in the direction
 * of the span.
 */
enum SpanFlag
{
    SPAN_FLAG_WEST = 1 << 0,
    SPAN_FLAG_NORTH = 1 << 1,
    SPAN_FLAG_EAST = 1 << 2,
    SPAN_FLAG_SOUTH = 1 << 3,
};

enum PaintersTileFlags
{
    /**
     * Bridge underpass slot after LinkBelow push-down: holds the former level-0 tile at grid
     * level 3, drawn via bridge_tile before the surface tile. Skipped in normal level passes.
     */
    PAINTERS_TILE_FLAG_BRIDGE = 1 << 0,
};

/**
 * Linked-list node in the painter's scenery pool (one node per tile footprint slot).
 * Tile grid position is implicit in the PaintersTile pointer; element_idx indexes
 * painter->elements.
 */
struct SceneryNode
{
    int16_t element_idx;
    uint8_t span;
    int32_t next; /* pool index, or -1 */
};

struct PaintersTile
{
    uint16_t sx;
    uint16_t sz;

    int16_t wall_a;
    int16_t wall_b;
    int16_t wall_decor_a;
    int16_t wall_decor_b;

    int16_t ground_decor;

    int16_t ground_object_bottom;
    int16_t ground_object_middle;
    int16_t ground_object_top;

    int32_t bridge_tile;
    int32_t scenery_head; /* pool index, or -1 */

    // Contains directions for which tiles are waiting for us to draw.
    // This is determined by locs that are larger than 1x1.
    // E.g. If a table is 3x1, then the spans for each tile will be:
    // (Assuming the table is going west-east direction)
    // WEST SIDE
    //     SPAN_FLAG_EAST,
    //     SPAN_FLAG_EAST | SPAN_FLAG_WEST,
    //     SPAN_FLAG_WEST
    // EAST SIDE
    //
    // For a 2x2 table, the spans will be:
    // WEST SIDE  <->  EAST SIDE
    //     [SPAN_FLAG_EAST | SPAN_FLAG_SOUTH,    SPAN_FLAG_WEST | SPAN_FLAG_SOUTH]
    //     [SPAN_FLAG_EAST | SPAN_FLAG_NORTH,    SPAN_FLAG_WEST | SPAN_FLAG_NORTH]
    //
    //
    // As the underlays are drawn diagonally inwards from the corner, once each of the
    // underlays is drawn, the loc on top is drawn.
    // The spans are used to determine which tiles are waiting for us to draw.

    /* Combined span flags for all scenery on this tile (see enum SpanFlag). */
    uint8_t spans;

    /*
     * Which cache levels' terrain meshes this tile's ground pass emits, as a
     * bitmask over levels 0..3. Normally just its own (1 << mesh_level);
     * painter_tile_copyto carries the source's bit through the bridge shuffle
     * so a shifted tile keeps emitting the mesh it came with.
     *
     * Zero is legal and means "emit nothing" — the world builder clears the
     * bits of levels that decoded no terrain mesh, so a content-less level
     * costs no command (the reference never queues such tiles at all).
     *
     * VIS_BELOW does NOT edit this set. The flag lowers visible_gte_level (the
     * reference's renderLevel, class112.method4161) and nothing else — the
     * mesh stays on its own level and pops in its own traversal slot, after
     * the tile below fully retires. An earlier revision relocated the flagged
     * mesh into the lower level's set, which drew it before the lower tile's
     * walls — the reverse of the reference order.
     */
    uint8_t terrain_levels;

    /*
     * packed_meta layout (uint16_t):
     *   bits 0-2:   visible_gte_level, 0-7
     *   bits 3-5:   paintgrid_level, 0-7
     *   bits 6-8:   mesh_level, 0-7
     *   bits 9-15:  PaintersTileFlags
     *
     * visible_gte_level:
     *   The tile is drawn when the UI's current view floor is >= this value. Literally the bit
     *   tested in tile_excluded_by_bridge_or_draw_mask: (draw_mask & (1 << visible_gte_level)).
     *   The "visible at or above" semantics come from the UI building cumulative draw_mask bits
     *   (levels 0..current_floor). VisBelow (FLOFLAG_VIS_BELOW) can lower this below
     *   paintgrid_level so a tile physically at paintgrid_level=2 is revealed when viewing level 1.
     *   Set at init to the allocation level; overwritten after world build by
     *   painter_tile_set_draw_level / RSCacheDat2A_MapFloorVisBelowDrawLevel. Not changed by
     *   painter_tile_copyto.
     *
     * paintgrid_level:
     *   The tile's level (0-3) within the painter's tile grid at (sx, sz). Used for array
     *   indexing (painter_coord_idx, step_idx_up/down) and vertical neighbour stepping during the
     *   paint wavefront. Reset to the destination level by painter_tile_copyto during bridge
     *   push-down; on non-bridge columns equals the allocation slot and never changes after build.
     *
     * mesh_level:
     *   Which original cache level's terrain mesh this tile renders — passed to push_command_terrain
     *   and used by the renderer (world_tile_entity_at(x, z, mesh_level)). Provenance of geometry,
     *   not current grid position: painter_tile_copyto deliberately does not reset it so shifted
     *   tiles keep the source cache index. On non-bridge columns mesh_level equals paintgrid_level.
     */
    uint16_t packed_meta;
};

#define PAINTERS_TILE_META_VISIBLE_GTE_LEVEL_MASK 0x7u
#define PAINTERS_TILE_META_PAINTGRID_LEVEL_SHIFT 3
#define PAINTERS_TILE_META_PAINTGRID_LEVEL_MASK (0x7u << PAINTERS_TILE_META_PAINTGRID_LEVEL_SHIFT)
#define PAINTERS_TILE_META_MESH_LEVEL_SHIFT 6
#define PAINTERS_TILE_META_MESH_LEVEL_MASK (0x7u << PAINTERS_TILE_META_MESH_LEVEL_SHIFT)
#define PAINTERS_TILE_META_FLAGS_SHIFT 9

static inline uint8_t
painters_tile_get_visible_gte_level(const struct PaintersTile* t)
{
    return (uint8_t)(t->packed_meta & PAINTERS_TILE_META_VISIBLE_GTE_LEVEL_MASK);
}

static inline void
painters_tile_set_visible_gte_level(
    struct PaintersTile* t,
    uint8_t v)
{
    t->packed_meta = (uint16_t)((t->packed_meta & ~PAINTERS_TILE_META_VISIBLE_GTE_LEVEL_MASK) |
                                (v & 7u));
}

static inline uint8_t
painters_tile_get_paintgrid_level(const struct PaintersTile* t)
{
    return (uint8_t)((t->packed_meta & PAINTERS_TILE_META_PAINTGRID_LEVEL_MASK) >>
                     PAINTERS_TILE_META_PAINTGRID_LEVEL_SHIFT);
}

static inline void
painters_tile_set_paintgrid_level(
    struct PaintersTile* t,
    uint8_t v)
{
    t->packed_meta = (uint16_t)((t->packed_meta & ~PAINTERS_TILE_META_PAINTGRID_LEVEL_MASK) |
                                ((uint16_t)(v & 7u) << PAINTERS_TILE_META_PAINTGRID_LEVEL_SHIFT));
}

static inline uint8_t
painters_tile_get_mesh_level(const struct PaintersTile* t)
{
    return (uint8_t)((t->packed_meta & PAINTERS_TILE_META_MESH_LEVEL_MASK) >>
                     PAINTERS_TILE_META_MESH_LEVEL_SHIFT);
}

static inline void
painters_tile_set_mesh_level(
    struct PaintersTile* t,
    uint8_t v)
{
    t->packed_meta = (uint16_t)((t->packed_meta & ~PAINTERS_TILE_META_MESH_LEVEL_MASK) |
                                ((uint16_t)(v & 7u) << PAINTERS_TILE_META_MESH_LEVEL_SHIFT));
}

static inline uint16_t
painters_tile_get_flags(const struct PaintersTile* t)
{
    return (uint16_t)(t->packed_meta >> PAINTERS_TILE_META_FLAGS_SHIFT);
}

static inline void
painters_tile_set_flags(
    struct PaintersTile* t,
    uint16_t f)
{
    t->packed_meta = (uint16_t)((t->packed_meta & ((1u << PAINTERS_TILE_META_FLAGS_SHIFT) - 1u)) |
                                (f << PAINTERS_TILE_META_FLAGS_SHIFT));
}

static inline void
painters_tile_or_flags(
    struct PaintersTile* t,
    uint16_t f)
{
    painters_tile_set_flags(t, (uint16_t)(painters_tile_get_flags(t) | f));
}

enum PaintersElementKind
{
    PNTRELEM_INVALID = 0,
    PNTRELEM_GROUND,
    PNTRELEM_SCENERY,
    PNTRELEM_WALL_A,
    PNTRELEM_WALL_B,
    PNTRELEM_GROUND_DECOR,
    PNTRELEM_WALL_DECOR,
    PNTRELEM_GROUND_OBJECT,
};

/** Widest loc footprint the painter tracks, per axis. Bounded by the uint8_t
 *  fields on NormalScenery; the largest real content is the rs2012 QBD arena
 *  floor at 20 tiles. */
#define PAINTER_SCENERY_MAX_SIZE 255

/** Flags on NormalScenery — draw-order hints for stacked locs / raised ground items. */
enum PaintersSceneryFlags
{
    /**
     * Ground item lifted onto a raiseobject loc (Client-TS GroundObject.height != 0).
     * Skipped in the scenery pass; emitted at tile completion after all locs.
     */
    PNTR_SCENERY_RAISED = 1 << 0,
    /**
     * Multi-tile loc that can host stacked scenery on its footprint. Undrawn
     * STACK_BASE elements block contained smaller locs (non-reference rule —
     * see docs/painter_bucket_vs_world3d.md "Loc stacking").
     */
    PNTR_SCENERY_STACK_BASE = 1 << 1,
    /**
     * Pseudo-loc standing in for a nested world view (a boat). `entity` is the
     * *view id*, not a scene element: the painter never emits a model command
     * for it. When the drain reaches it, it emits PNTR_CMD_BEGIN_WORLD, descends
     * into that view's own painter, and emits PNTR_CMD_END_WORLD on the way out.
     * @see painter_add_world_entity, painter_set_world_entity_view.
     */
    PNTR_SCENERY_WORLDENTITY = 1 << 2,
};

struct NormalScenery
{
    uint16_t entity;
    /*
     * Tile footprint, 1..PAINTER_SCENERY_MAX_SIZE.
     *
     * These were 4-bit fields, which silently clamped anything wider than 15.
     * A clamped footprint is not a cosmetic loss: compute_normal_scenery_spans
     * derives each covered tile's span flags from the clamped maximum, so the
     * loc stops waiting on the ground of the tiles past the cut and emits
     * early. The ground beyond then paints over it - a strip of terrain lying
     * on top of a floor. The rs2012 QBD arena floor (12x18 and 20x7 pieces) is
     * exactly that case; see docs/qbd_toridraw_streaks_debug.md.
     *
     * Full bytes land in what was padding, so the struct is the same size.
     */
    uint8_t size_x;
    uint8_t size_z;
    /** Reference Model.minY / bottomY — height above ground for spriteOccluded. */
    uint16_t model_height;
    /** Bitfield of PaintersSceneryFlags. */
    uint8_t flags;
};

struct GroundObject
{
    uint16_t entity;
};

enum WallSide
{
    WALL_SIDE_WEST = 1 << 0,        // 1
    WALL_SIDE_NORTH = 1 << 1,       // 2
    WALL_SIDE_EAST = 1 << 2,        // 4
    WALL_SIDE_SOUTH = 1 << 3,       // 8
    WALL_CORNER_NORTHWEST = 1 << 4, // 16
    WALL_CORNER_NORTHEAST = 1 << 5, // 32
    WALL_CORNER_SOUTHEAST = 1 << 6, // 64
    WALL_CORNER_SOUTHWEST = 1 << 7, // 128
};

struct Wall
{
    uint16_t entity;

    uint8_t side;
};

struct GroundDecor
{
    int entity;
    /** Slot occupant this element displaced, or -1. Only a *dynamic* add
     *  (painter_add_ground_decor_dynamic) ever displaces anything: a baked
     *  static decor keeps the tile until painter_reset_to_static hands it
     *  back, which is what this field is for. Static adds leave it -1 and it
     *  is never read for them. */
    int prev_slot;
};

enum ThroughWallFlags
{
    THROUGHWALL = 0x01,
    // THROUGHWALL = 0x100,
    // In OS1 and later, this is removed; only present in 2004scape.
    // THROUGHWALL_OUTSIDE = 0x200,
};

struct WallDecor
{
    uint16_t entity;

    // For throughwall, this specifies which side is the "outside".
    // enum WallSide side;
    struct
    {
        uint8_t _bf_side : 8;
        // In the 2004scape 0x100 is used to indicate an interior decor,
        // and 0x200 to indicate an exterior decor. 0x300 means draw both with same model.
        // In OS1 and deobs, this is removed and 0x100 represents both for throughwall always.
        // Both models are drawn before and after locs.
        // In this case, "side" represents the side of the wall that model_a is facing.
        // model_b faces the opposite side.
        uint8_t _bf_through_wall_flags : 8;
    };

    /** Reference Model.minY / bottomY — height above ground for spriteOccluded. */
    uint16_t model_height;
};

struct PaintersElement
{
    enum PaintersElementKind kind;

    // These are stored on the element because sometimes tiles refer to elements that are not on the
    // same tile.
    uint16_t sx;
    uint16_t sz;
    /*
     * source_level: cache level this element was placed at (painter_add_* slevel argument).
     * Fixed at add time; not updated when bridge push-down moves the element into another tile's
     * scenery_head via clone_scenery_chain. Can diverge from the host tile's paintgrid_level after
     * a bridge shift — painters must use paintgrid_level for footprint readiness, not source_level.
     */
    uint8_t source_level;

    union
    {
        struct NormalScenery _scenery;
        struct Wall _wall;
        struct GroundDecor _ground_decor;
        struct WallDecor _wall_decor;
        struct GroundObject _ground_object;
    };
};

enum PaintersCommandKind
{
    PNTR_CMD_INVALID = 0,
    PNTR_CMD_ELEMENT,
    PNTR_CMD_TERRAIN,
    /** Occluded terrain: hit-tested for picking but not rasterized. */
    PNTR_CMD_TERRAIN_PICK_ONLY,
    /** Descend into nested world view `_bf_entity` (0..PAINTER_MAX_WORLD_VIEWS-1). */
    PNTR_CMD_BEGIN_WORLD,
    /** Leave the nested world view `_bf_entity`; pairs with PNTR_CMD_BEGIN_WORLD. */
    PNTR_CMD_END_WORLD,
};

// Want to pack into 64 bits.
// 16 bits: Need 16 bits for world element idx.
// 18 bits: x,y 9 bits each = 18 bits (512x512)
// 3  bits: level (0-7)
// 4  bits: command (0-15) : scenery or terrain

// Either element or terrain.
// Entity:
// - 4  bits: kind = 1, CMD = Entity
// - 16 bits: world entity idx.
// Terrain:
// - 4  bits: kind = 2, CMD = Terrain
// - 16 bits: terrain x,y,z. (9 bits each)
struct PaintersElementCommand
{
    union
    {
        uint32_t _packed;

        struct
        {
            uint32_t _bf_kind : 4;
        };

        struct
        {
            uint32_t _bf_kind : 4;
            uint32_t _bf_entity : 16;
        } _entity;

        struct
        {
            uint32_t _bf_kind : 4;
            uint32_t _bf_terrain_x : 9;
            uint32_t _bf_terrain_z : 9;
            uint32_t _bf_terrain_y : 4;
        } _terrain;
    };
};

/** Registry bound of nested world views, matching WORLDVIEW_MAX and the
 *  reference class61 table of 16. Also the descent depth cap. */
#define PAINTER_MAX_WORLD_VIEWS 16

/**
 * One nested world view as the parent painter sees it: which painter to descend
 * into, and where the camera sits once transformed into that view's tile space.
 * Rebuilt every frame alongside the pseudo-locs.
 */
struct PainterWorldEntityView
{
    struct Painter* painter;
    int camera_sx;
    int camera_sz;
    int camera_slevel;
    int active;
};

struct Painter;

struct PaintersProjectedVertex
{
    int x;
    int y;
    int z;
    int clipped;
};

typedef void (*PaintersProjectFn)(
    struct PaintersProjectedVertex* out,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int camera_cot16,
    int near_clip,
    int screen_width,
    int screen_height,
    void* user);

/** Angle in units of (2π/2048); returns fixed-point sin (typically Q16). */
typedef int (*PaintersSinFn)(int angle_r2pi2048, void* user);

/* Bake grid parameters (must match between bake tool and runtime).
 * Pitch covers the orbit / free-cam range the app actually produces (scene-reset
 * uses 450; shake clamps 128..383; orbit can go higher). Y sweep and the pitch-
 * height offset must reach typical orbit eye heights (~1600), not the old 600
 * stub that left every sample behind the near plane. */
#ifndef PCULL_PITCH_MIN
#define PCULL_PITCH_MIN 128
#endif
#ifndef PCULL_PITCH_MAX
#define PCULL_PITCH_MAX 512
#endif
#ifndef PCULL_PITCH_STEP
#define PCULL_PITCH_STEP 32
#endif
#ifndef PCULL_YAW_STEP
#define PCULL_YAW_STEP 64
#endif
#ifndef PCULL_FRUSTUM_Y_START
#define PCULL_FRUSTUM_Y_START (-500)
#endif
#ifndef PCULL_FRUSTUM_Y_END
#define PCULL_FRUSTUM_Y_END 1500
#endif
#ifndef PCULL_Y_STEP
#define PCULL_Y_STEP 128
#endif
#ifndef PCULL_Y_GRANULARITY
#define PCULL_Y_GRANULARITY 1
#endif
/** Camera-height term in pcull_pitch_height (scene units). */
#ifndef PCULL_PITCH_HEIGHT_OFFSET
#define PCULL_PITCH_HEIGHT_OFFSET 1600
#endif

struct PaintersCullMap
{
    uint8_t* visibility;
    int radius;
    int pitch_levels;
    int yaw_levels;
    int grid_side;
    int all_visible;
};

/** Max |dz| (eye-relative tiles) the per-frame span table can cover. Must fit
 * radius 25 plus the largest orbit-eye offset (~40 at 300% zoom). */
#ifndef PAINTERS_CULLSPAN_MAX_DZ
#define PAINTERS_CULLSPAN_MAX_DZ 96
#endif
#define PAINTERS_CULLSPAN_ROWS (2 * PAINTERS_CULLSPAN_MAX_DZ + 1)

struct PaintersCullSpanParams
{
    int pitch;
    int yaw;
    /** Down-positive eye-to-ground offset in scene units (matches bake `ph`). */
    int eye_height;
    int y_lo;
    int y_hi;
    int near_clip;
    int far_clip;
    int screen_width;
    int screen_height;
    /** Mirror of ToriDraw_Camera's projection knobs, and they must be the SAME
     *  values the frame is drawn with — see the note at the focal computation
     *  in painters_cullspan.u.c. proj_mode selects; see graphics/projection.h. */
    int proj_mode;
    int proj_scale;
    int fov_rpi2048;
    int dz_min;
    int dz_max;
};

struct PaintersCullSpan
{
    int16_t row_min[PAINTERS_CULLSPAN_ROWS];
    int16_t row_max[PAINTERS_CULLSPAN_ROWS];
    int dz_min;
    int dz_max;
    int empty;
};

void
painters_cullspan_build(
    struct PaintersCullSpan* span,
    const struct PaintersCullSpanParams* params);

/** Build cullmap at runtime (CPU bake).
 * project and sin_fn are required; both receive the same user pointer.
 *
 * camera_cot16 is the resolved projection multiplier the bake assumes (see
 * toridraw_proj_cot16). It used to be a bare 512 buried in the frustum test,
 * which silently baked a scale-512 frustum no matter what the camera projected
 * with; pass what the frame will actually use. The bake is conservative
 * (padding + dilation), so it tolerates being slightly wide but not narrow. */
struct PaintersCullMap*
painters_cullmap_build(
    int radius,
    int near_clip_z,
    int screen_width,
    int screen_height,
    int camera_cot16,
    PaintersProjectFn project,
    void* user,
    PaintersSinFn sin_fn);

struct PaintersCullMap*
painters_cullmap_new_nocull(void);

/** Load from packed visibility bytes (e.g. file read by caller). Caller frees with
 * painters_cullmap_free. */
struct PaintersCullMap*
painters_cullmap_from_blob(
    const uint8_t* data,
    size_t nbytes,
    int radius);

void
painters_cullmap_free(struct PaintersCullMap* cm);

/** Visible-bit count in the (pitch, yaw) slice (0 when empty / nocull-null). */
int
painters_cullmap_slice_visible_count(
    const struct PaintersCullMap* cm,
    int pitch,
    int yaw);

void
painter_set_cullmap(
    struct Painter* painter,
    struct PaintersCullMap* cm);

/** Install a per-frame analytic span cull (clears cullspan_active when span is
 * NULL or empty). Takes precedence over the baked cullmap bit test. */
void
painter_set_cullspan(
    struct Painter* painter,
    const struct PaintersCullSpan* span);

/** Returns the painter's live cullspan when active, else NULL. */
const struct PaintersCullSpan*
painter_get_cullspan(const struct Painter* painter);

struct SceneOccluders;

/** Install (or clear, with NULL) the planar occluder set for this painter.
 * Takes ownership: painter_free / a subsequent set frees the previous set. */
void
painter_set_occluders(
    struct Painter* painter,
    struct SceneOccluders* occ);

struct SceneOccluders*
painter_get_occluders(struct Painter* painter);

void
painter_set_camera_angles(
    struct Painter* painter,
    int pitch,
    int yaw);

/** Centre the draw box on (sx, sz). Distance / wall / seed logic stays
 * eye-relative (the camera tile passed to painter_paint_*). Pass sx=sz=-1 to
 * clear and fall back to the eye tile (fuzz / tests default). */
void
painter_set_draw_center(
    struct Painter* painter,
    int sx,
    int sz);

/**
 * Scene.drawDistance — paint-box / occluder footprint radius in tiles.
 * Clamped to [25, 90] (deob Scene.setDrawDistance). Default 25.
 */
void
painter_set_draw_distance(
    struct Painter* painter,
    int draw_distance);

int
painter_get_draw_distance(const struct Painter* painter);

/** Bitmask of levels to draw (bits 0-3 for levels 0-3). Default 0xF = all levels. */
void
painter_set_level_mask(
    struct Painter* painter,
    uint8_t mask);

/** Draw only levels lo through hi inclusive (0-based). */
void
painter_set_level_range(
    struct Painter* painter,
    int lo,
    int hi);

/** Bitmask: which scratch contexts painter_new allocates up front (see painters_bucket / world3d /
 * distancemetric). */
enum PainterNewContextFlags
{
    PAINTER_NEW_CTX_BUCKET = 1u << 0,
    PAINTER_NEW_CTX_WORLD3D = 1u << 1,
    PAINTER_NEW_CTX_DISTMETRIC = 1u << 2,
};

struct Painter*
painter_new(
    int width, //
    int height,
    int levels,
    uint32_t init_contexts);

void
painter_free(
    struct Painter* painter //
);

int
painter_max_levels(struct Painter* painter);

struct PaintersTile*
painter_tile_at(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel);

struct PaintersElement*
painter_element_at(
    struct Painter* painter, //
    int element);

void
painter_tile_set_bridge(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int bridge_tile_sx,
    int bridge_tile_sz,
    int bridge_tile_slevel);

/** Replace the set of cache-level terrain meshes this tile's ground pass emits.
 *  See PaintersTile::terrain_levels. `levels` is a bitmask over 0..3; 0 means
 *  the tile emits no terrain at all. */
void
painter_tile_set_terrain_levels(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    unsigned levels);

/** The current terrain-mesh set for a tile (see painter_tile_set_terrain_levels). */
unsigned
painter_tile_get_terrain_levels(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel);

void
painter_tile_set_draw_level(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int draw_level);

void
painter_tile_set_grid_level(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int grid_level);

void
painter_tile_copyto(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int dest_sx,
    int dest_sz,
    int dest_slevel);

int
painter_add_normal_scenery(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity,
    int size_x,
    int size_y,
    int model_height);

/** Like painter_add_normal_scenery, with PaintersSceneryFlags (e.g. RAISED, STACK_BASE). */
int
painter_add_normal_scenery_ex(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity,
    int size_x,
    int size_y,
    int model_height,
    uint8_t flags);

void
painter_mark_static_count(struct Painter* painter);

/** Toggle suppression of the single-slot wall / wall_decor / ground_decor
 *  registrations. Set around a runtime loc spawn (WorldBuilder_ApplyLocChange)
 *  so reusing the build path doesn't assert on / clobber the baked static tile
 *  slots; the spawned loc draws via the per-frame scenery pass instead. */
void
painter_set_suppress_slot_registration(struct Painter* painter, int suppress);

/** 0 when TORIRS_NO_GROUND_DECOR is set: every painter variant skips its
 *  ground-decor emit. A bisection knob for "is that geometry decor or floor?",
 *  which a screenshot cannot answer — shape-22 locs (floor plates, paths, the
 *  Inferno's lava floor planes) read exactly like terrain. Read once. */
int
painter_ground_decor_enabled(void);

void
painter_reset_to_static(struct Painter* painter);

#define WALL_A 0
#define WALL_B 1

/** Free the exclusive wall_a/wall_b tile slot(s) whose painter element renders
 *  `entity` (a scene element id). Used when a runtime loc change removes a wall
 *  loc: the baked static painter element stays in the list but must stop
 *  claiming the tile slot, both so the dead wall stops being referenced and so
 *  a replacement wall (re-registered per frame) can claim the slot without
 *  tripping the add-time assert. */
void
painter_release_wall(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity);

/** The normal-scenery counterpart of painter_release_wall: unlink every baked
 *  static scenery element on `(sx, sz, slevel)`'s chain that renders `entity`
 *  from its whole footprint, so a runtime loc change stops the dead loc being
 *  drawn.
 *
 *  Not cosmetic bookkeeping. Scene element ids are recycled: when a `loc_del` /
 *  `loc_change` frees the old loc's scene element and the replacement is
 *  allocated the same id, the abandoned static painter element starts rendering
 *  the NEW model — so the loc draws twice, once at the stale element's place in
 *  the back-to-front order and once at its own, and everything emitted between
 *  the two is overpainted by the second. `painter_reset_to_static` cannot help:
 *  the stale element is below `static_element_count` and that is exactly the
 *  range it preserves. */
void
painter_release_scenery(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity);

int
painter_add_wall(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity,
    int wall_ab,
    int side);

int
painter_add_wall_decor(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int wall_ab,
    int side,
    int through_wall_flags,
    int model_height);

int
painter_add_ground_decor(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity);

/** Ground decor for a loc spawned at RUNTIME (a zone LOC_ADD_CHANGE), which
 *  world_cycle re-registers every frame after painter_reset_to_static has
 *  truncated the previous frame's copy.
 *
 *  Two differences from painter_add_ground_decor, both from that lifetime: it
 *  claims an occupied tile slot instead of asserting on one (a spawn can land
 *  on a tile that baked its own floor decor), and it records what it displaced
 *  so the reset can put it back.
 *
 *  Registering these as ordinary scenery -- which is what the per-frame pass
 *  did before this existed -- is not just a category error. Ground decor is
 *  emitted in a tile's BASE step, which is ahead of every scenery element whose
 *  footprint covers that tile; scenery is emitted after the base step and is
 *  ordered against the other scenery on the tile. So a puddle spawned under a
 *  big NPC drew *over* the NPC whenever its own tile sorted nearer than the
 *  NPC's anchor (Xarpus' acid). As decor it is underneath by construction. */
int
painter_add_ground_decor_dynamic(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity);

#define GROUND_OBJECT_BOTTOM 0
#define GROUND_OBJECT_MIDDLE 1
#define GROUND_OBJECT_TOP 2

/**
 * Register a ground-object slot on a tile (bottom / middle / top).
 *
 * Production obj stacks go through painter_add_normal_scenery_ex with
 * PNTR_SCENERY_RAISED when lifted onto a raiseobject loc — see
 * docs/painter_bucket_vs_world3d.md "Loc stacking". This API remains for the
 * painter fuzzer and for flat (height==0) piles; middle/top are unused.
 */
int
painter_add_ground_object(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity,
    int bottom_middle_top);

struct PaintersBuffer
{
    struct PaintersElementCommand* commands;
    int command_count;
    int command_capacity;
};

struct PaintersBuffer*
painter_buffer_new();

int
painter_paint(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

int
painter_paint3(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

int
painter_paint4(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

/**
 * Like painter_paint4 (distance-bucket queue + insertion-sort scenery), but uses a full
 * tile_paints memset on native and targeted bbox clear on Emscripten/WASM.
 */
int
painter_paint4_1(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

/**
 * Bind view `view_id`'s painter and its view-space camera onto `painter`, so
 * the bucket drain can descend into it when it reaches that view's pseudo-loc.
 * Cleared wholesale by painter_clear_world_entity_views; both are per-frame.
 */
void
painter_set_world_entity_view(
    struct Painter* painter,
    int view_id,
    struct Painter* view_painter,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

void
painter_clear_world_entity_views(struct Painter* painter);

/**
 * Register the per-frame pseudo-loc that carries nested view `view_id` in this
 * painter's draw order. Transient exactly like every other dynamic: the next
 * painter_reset_to_static drops it.
 *
 * `sx`/`sz` are the MIN corner of the footprint and `size_x`/`size_z` its tile
 * extent — not the entity's centre. The footprint is the axis-aligned bound of
 * the hull ROTATED by its current heading, so it changes shape as the boat
 * turns and the caller must recompute it every frame (@see Wev_FootprintTiles).
 * A fixed 1x1 under-covers: the drain wakes only the tiles inside this
 * rectangle and sorts neighbouring locs against its extent, so a long hull
 * pinned to one tile lets scenery beside it sort in front.
 */
int
painter_add_world_entity(
    struct Painter* painter,
    int level,
    int sx,
    int sz,
    int view_id,
    int model_height,
    int size_x,
    int size_z);

int
painter_paint_bucket(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

/** Collect the visible world set for a depth-buffered renderer. This retains
 * tile/level/frustum/bridge/occluder decisions but deliberately omits painter
 * wavefront and distance ordering. */
int
painter_collect_visible_depth(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

int
painter_paint_world3d(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

/**
 * Draw-order telemetry (TORIRS_WEDGELOG=<path>). Records the eye and viewport the
 * caller is about to paint with so the log header can be compared against the
 * instrumented official client's `#path` line. No-op unless the env var is set;
 * never reads or writes painter/render state.
 */
void
painter_wedgelog_set_eye(
    int eye_x,
    int eye_y,
    int eye_z,
    int viewport_w,
    int viewport_h);

#endif
