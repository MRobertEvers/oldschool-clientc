#ifndef TORIDRAW_TYPES_H
#define TORIDRAW_TYPES_H

#include "impl/projection/projection.scalar_reference.h"
#include "graphics/zdepth.h"
#include "toridraw_intrusive_list.h"
#include "toridraw_texture_mapping.h"

#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

typedef int16_t faceint_t;
typedef int16_t vertexint_t;
typedef uint16_t hsl16_t;
typedef uint8_t alphaint_t;
typedef uint16_t boneint_t;

/** Map dat2 raw per-face texture coord to renderer form (-1 = none, else PNM index). */
static inline faceint_t
ToriDraw_NormalizeFaceTextureCoord(int raw, int textured_face_count)
{
    int const idx = raw & 255;
    if( idx == 255 )
        return -1;
    if( textured_face_count <= 0 || idx >= textured_face_count )
        return -1;
    return (faceint_t)idx;
}

/** Sentinels for channel C in lit face colors; lightness=127 is never produced by lighting. */
#define TORIDRAWHSL16_HIDDEN ((hsl16_t)0xFFFF)
#define TORIDRAWHSL16_FLAT ((hsl16_t)0xFF7F)

/* TORIDRAW_SCREEN_X_NEAR_CLIPPED and its nudge live in graphics/projection.h,
 * with the kernels that write them; included above via that header. */

struct ToriDraw_BoundsCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int min_y;
    int max_y;
    int radius;

    // TODO: Name?
    // - Max extent from model origin.
    // - Distance to farthest vertex?
    int min_z_depth_any_rotation;
};

/** The box over the model's own projected vertices, dilated by the pick slop;
 *  see toridraw_projected_bound. (Kinds 0 and 1 were the cylinder boxes the
 *  fast cull and the eight-corner bound used to write; neither exists now.) */
#define TORIDRAW_AABB_KIND_VERTICES 2

struct ToriDraw_AABB
{
    int kind;
    int min_screen_x;
    int min_screen_y;
    int max_screen_x;
    int max_screen_y;
};

struct ToriDraw_Normal
{
    int16_t x;
    int16_t y;
    int16_t z;
    uint16_t face_count;
    uint8_t merged;
};

struct ToriDraw_Normals
{
    struct ToriDraw_Normal* vertex_normals;
    int vertex_normals_count;

    struct ToriDraw_Normal* face_normals;
    int face_normals_count;

    /** Allocated length of the two arrays, which a recycled block keeps while
     *  `*_count` drops to what the current model actually uses. Only
     *  `ToriDraw_NormalsNew` / `ToriDraw_NormalsFree` may touch these -- they
     *  are what lets a freed block be handed to a smaller model without
     *  reallocating, and reading `*_count` as the capacity would then walk off
     *  the end of a block that is genuinely larger than it claims. */
    int vertex_normals_cap;
    int face_normals_cap;
};

struct ToriDraw_Bones
{
    int bones_count;
    boneint_t** bones;
    boneint_t* bones_sizes;
};

/**
 * Draw this model's faces through the depth-tested kernels instead of relying
 * on the depth sort alone.
 *
 * Opt-in, and deliberately per model rather than per scene: the painter's sort
 * is what the content was authored against, and it is right for the
 * overwhelming majority of models. It is wrong for a model whose parts
 * genuinely interpenetrate — a wing through a body, a jaw through a skull —
 * because no single order over whole faces can express "these two triangles
 * each occlude the other". Those models set this and get the per-pixel answer.
 *
 * Setting it makes the model reset the scene's z-buffer before it draws, so the
 * depth test only ever resolves this model against itself and nothing changes
 * about how it layers against the rest of the scene. See graphics/zdepth.h.
 *
 * It ALSO drops the model's face render priorities at sort time
 * (toridraw_render.u.c). The two cannot both decide a pixel and the priority
 * would win: a priority pins a face into a draw band regardless of depth, which
 * is exactly the painter's-algorithm crutch this flag replaces. A model that
 * kept both would be depth-tested only within a band, i.e. would give the
 * priority's answer while paying for the depth test.
 *
 * Requires the scene to carry the buffer: TORIDRAW_SCENE_MODEL_ZBUFFER at
 * ToriDraw_SceneNew, or an explicit ToriDraw_SceneZBufferResize. With no buffer
 * the flag is inert and the model draws exactly as it did before.
 */
#define TORIDRAW_MODEL_FLAG_ZBUFFER ((uint8_t)(1u << 0))

/**
 * Drop this model's face render priorities at sort time, without depth-testing
 * it.
 *
 * This is the half of TORIDRAW_MODEL_FLAG_ZBUFFER that is about the model's
 * AUTHORING rather than about how it is resolved: a model imported from a
 * z-buffered client carries face priorities its own client never honoured, so
 * they are not a draw order anyone chose — they are leftover bytes, and obeying
 * them pins faces into bands for no reason. Dropping them leaves the model
 * sorted by depth like everything else in the scene.
 *
 * ZBUFFER implies this (see the sort in toridraw_render.u.c); this flag alone is
 * what an imported model gets while the depth-tested kernels are off.
 */
#define TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY ((uint8_t)(1u << 1))

/**
 * Rasterise this model's textured faces with the AFFINE texture kernels.
 *
 * The stock textured kernels are perspective-correct: every eight pixels they
 * re-derive u and v from the plane equation, which is a reciprocal and two
 * multiplies per block. The affine family derives u and v only at the two ends
 * of each span and steps linearly between them. On a face that is nearly
 * parallel to the screen -- a terrain tile seen from the game camera -- the two
 * agree to the texel, and the affine walk is the cheaper one.
 *
 * This is a per-MODEL policy, set by whoever builds the model and knows what
 * it is (world_decode_tile sets it on every textured terrain tile); the
 * raster reads it once per model, next to the camera's own texture_affine.
 * Every lane honours it: the per-face C kernels through the affine family
 * (tri.texture_affine.u.c), the presorted-run assembly kernels through the
 * affine lane of the staged row (tex_tri_asm.h, TORIDRAW_TEXBATCH_LANE_AFFINE).
 * It is not implied by, and does not imply, either flag above.
 */
#define TORIDRAW_MODEL_FLAG_AFFINE_TEXTURES ((uint8_t)(1u << 2))

/**
 * A model that owns every array reachable from it.
 *
 * That is not a description, it is the invariant. Geometry a placement does NOT
 * own lives in one of the two types below, each of which embeds one of these as
 * its first member -- so a `struct ToriDraw_Model*` you can write through is
 * always yours, and the only way to obtain one is a handle whose kind says
 * TORIDRAWMK_MODEL. The shared regimes yield a `const struct ToriDraw_Model*`
 * instead; see ToriDraw_ModelRead / ToriDraw_ModelWrite.
 *
 * The alternative -- one struct with a couple of nullable "actually this bit
 * belongs to someone else" back-pointers -- is what this replaced, and it made
 * every write site look identical whether or not it was legal. One of them was
 * not, and the wall it deleted took a day to find.
 */
struct ToriDraw_Model
{
    uint8_t flags;
    /**
     * Non-zero if this model is a terrain tile whose triangulation the face
     * sort may resolve at COMPILE time: `1 + rotation`, rotation in 0..3.
     *
     * A world tile is not an arbitrary mesh. Its vertex layout and its index
     * triples come from four static tables in world_decode_tile.c, keyed by a
     * shape id, and a census of a loaded map says 94% of tiles are one of the
     * three 4-vertex, 2-triangle shapes -- PLAIN, DIAGONAL and the unnamed
     * shape 0 -- which all carry the SAME triples, (1,2,3) and (0,1,3), turned
     * by the tile's rotation. That is the fast path's premise: with the triples
     * known at compile time the sort reads nothing out of face_indices_a/b/c,
     * the two faces provably share two of the four vertices so the duplicated
     * coordinate loads fold, and a two-face model is culled, keyed and ordered
     * without a loop or a sort network. 6.5 ns per input face against the
     * general path's 8.7 on the dev host. See
     * toridraw_face_sort_bitonic_radix_tile2_scalar.
     *
     * Spending the same constants on SIMD instead -- one vector load per axis,
     * a shuffle per lane -- is written as toridraw_face_sort_bitonic_radix_tile2_sse2 and is
     * SLOWER than both, 8.9 ns. Its comment has the numbers and the reason. The
     * field selects neither; TORIDRAW_TILE_SORT does, and the field only says
     * the model is eligible.
     *
     * Zero for everything else, INCLUDING the other ten tile shapes: the field
     * is the eligibility test, so nothing downstream re-derives it, and the
     * shapes that would need their own kernels simply take the general path.
     * Set only by world_decode_tile, which is the one place a tile's shape and
     * rotation are known.
     */
    uint8_t tile_sort_kernel;
    int vertex_count;
    int face_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;

    vertexint_t* original_vertices_x;
    vertexint_t* original_vertices_y;
    vertexint_t* original_vertices_z;

    /*
     * Placement the reference applies AFTER animating, not before.
     *
     * A model's keyframes are authored against the model at its own size, in
     * its own frame, about its own origin. Every getModel in the rev-239 deob
     * therefore animates first and places afterwards:
     *
     *   npc      Statics.method9204   animate -> resize
     *   spotanim Statics.method8758   animate -> resize -> rotate90 x n
     *   loc      class393.method8916  animate -> rotate90 x n -> resize -> translate
     *
     * original_vertices_* holds that authored bind pose, so the placement has
     * to be re-derived on every pose. Baking it into the bind instead leaves a
     * frame's translations and its type-0 ORIGIN pivots at full magnitude
     * against geometry that has been moved, turned or shrunk out from under
     * them. Xarpus (resizeh/resizev 64) came out floating several tiles above
     * his own arena; `whirlpool` is a loc resized to 1/128 of its height.
     *
     * Applied in ONE canonical order -- orient, resize, translate -- which is
     * the loc order verbatim. It is also exact for a spotanim despite the deob
     * resizing first there, because a spotanim's resize is (h, h, v): x and z
     * scale by the same factor, and a quarter turn only ever exchanges those
     * two axes.
     *
     * `post_transform` is the "any of these is not identity" fast path, and it
     * is also the contract: ToriDraw_ModelApplyPostTransforms must run exactly
     * ONCE per pose (or once at build for a model that is never posed), or the
     * placement compounds.
     */
    bool post_transform;
    bool post_resize; /* post_resize_* are meaningful (identity is 128, not 0) */
    int post_resize_x;
    int post_resize_z;
    int post_resize_height;
    int post_orient; /* quarter turns, 0..3 */
    int post_offset_x;
    int post_offset_y;
    int post_offset_z;

    alphaint_t* face_alphas;
    alphaint_t* original_face_alphas;
    int* face_infos;
    /** Two 4-bit priorities per byte (low nibble = even face index). Values 0–12. */
    uint8_t* face_priorities;
    /* Uniform priority for models with no per-face array; consumed by ToriDraw_ModelNewMerge so
     * merged parts keep their layering. See ToriRS_Model.model_priority. */
    uint8_t model_priority;
    hsl16_t* face_colors;


    int textured_face_count;
    faceint_t* textured_p_coordinate;
    faceint_t* textured_m_coordinate;
    faceint_t* textured_n_coordinate;
    uint8_t* texture_render_types;
    faceint_t* face_texture_coords;

    /*
     * One block holding every array below the geometry line, or NULL.
     *
     * A world tile is four vertices and two faces -- about a hundred bytes of
     * real data spread over thirteen arrays. Allocating them one at a time cost
     * thirteen mallocs and thirteen frees per tile, and a scene is eleven
     * thousand tiles, so the allocator saw a hundred and forty thousand calls
     * per rebuild to move a megabyte. Carving them out of a single block makes
     * that one call, and the free path one more.
     *
     * Set ONLY by a producer that carved every one of those arrays out of it
     * (world_decode_tile is the one), and it is all-or-nothing: with this set,
     * ToriDraw_ModelFree_arrays frees the block INSTEAD of the individual
     * arrays it covers, so a model carrying it must never have one of them
     * replaced or grown. The fields it does not cover -- normals, bones,
     * animaya, the original_* bind pose -- are freed the ordinary way whether
     * this is set or not.
     */
    void* arrays_block;

    struct ToriDraw_Normals* normals;
    struct ToriDraw_Normals* merged_normals;
    struct ToriDraw_Bones* vertex_bones;
    struct ToriDraw_Bones* face_bones;
    /*
     * Embedded, not pointed to. ToriDraw_Project reads the cylinder on every
     * model it culls, and as a separate allocation that was a dependent cache
     * line behind the model struct itself -- for a four-vertex terrain tile,
     * one of about seven the projection touched. has_bounds_cylinder is the
     * "was it ever computed" test the NULL pointer used to be;
     * ToriDraw_ModelGetBoundsCylinder still answers NULL when it is false.
     */
    struct ToriDraw_BoundsCylinder bounds_cylinder;
    bool has_bounds_cylinder;

    /* Animaya per-vertex skin (NULL if no skeletal rigging) */
    int      animaya_vertex_count;
    uint8_t* animaya_group_counts; /* [animaya_vertex_count] */
    uint8_t** animaya_groups;      /* [animaya_vertex_count][count] bone indices */
    uint8_t** animaya_scales;      /* [animaya_vertex_count][count] weights 0-255 */
};

struct ToriDraw_ModelGround
{
    int vertex_count;
    int face_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;
};

/**
 * Which struct is behind a handle -- and for the three model types, that is the
 * same question as who owns the geometry, because each regime is its own type.
 *
 *   MODEL             struct ToriDraw_Model. Owns every array. Writable.
 *   MODEL_HD          struct ToriDraw_ModelHD. Owns everything too; the HD tail
 *                     is the difference.
 *   MODEL_SHARED      struct ToriDraw_SharedModel. Owned by a store, N holders,
 *                     no private half. Read only.
 *   MODEL_LENT_FACES  struct ToriDraw_ModelLentFaces. Vertices, per-corner
 *                     colours and face_infos are the placement's; the twelve
 *                     face arrays are on loan.
 *
 * All four embed or ARE a ToriDraw_Model at offset zero, so reading is uniform
 * (ToriDraw_ModelRead). Writing is not, which is the entire point.
 */
enum ToriDraw_ModelKind
{
    TORIDRAWMK_NONE = 0,
    TORIDRAWMK_MODEL = 1,
    TORIDRAWMK_GROUND = 2,
    TORIDRAWMK_MODEL_HD = 3,
    TORIDRAWMK_MODEL_SHARED = 4,
    TORIDRAWMK_MODEL_LENT_FACES = 5,
};

/**
 * A model that carries what the HD (procedural-material) render path needs, and
 * that nothing else does.
 *
 * ## Why this is a variant and not four more fields on ToriDraw_Model
 *
 * Almost no model is HD. A cache is tens of thousands of models, of which the
 * ones with a mapped texture group are a minority even in RS727 and are absent
 * from OldSchool entirely. Fields on the base struct would cost a pointer each
 * on every model ever loaded, for nothing — and this library has already paid
 * for that shape of decision once (see docs/memory-lifetime notes: 12% of the
 * boot heap was per-field allocation overhead).
 *
 * `base` is embedded first and by value, so a `struct ToriDraw_ModelHD*` IS a
 * `struct ToriDraw_Model*` and every existing entry point keeps working through
 * ToriDraw_ModelAsFull. The HD tail is reachable only through a handle that
 * says TORIDRAWMK_MODEL_HD, which is what stops a non-HD path from reading it.
 */
struct ToriDraw_ModelHD
{
    struct ToriDraw_Model base;

    /*
     * Per-face-group mapping for render types 1-3. Length
     * `base.textured_face_count`, indexed like `base.texture_render_types`;
     * only entries whose render type is above 0 are meaningful.
     *
     * The derived form — centre, basis matrix, direction, speed, offsets —
     * rather than the raw stored scales, because the derivation needs the whole
     * face group (the centre is the midpoint of the group's bounding box, so no
     * single triangle can compute it) and because it is invariant under
     * animation: only the vertices move.
     */
    struct ToriDraw_TexMapping* texture_mappings;
};

/**
 * A model the scene shares whole: one object, N placements pointing at it.
 *
 * `base` first and by value, so the read view is a plain model and every
 * consumer keeps working -- but only through a const pointer, because this
 * model has no private half at all and a write moves every fence in the county.
 * ToriDraw_SceneElementModelForWrite is the way to geometry you may edit.
 *
 * The tail is the store's bookkeeping and nothing outside toridraw_shared_model.c
 * touches it.
 */
struct ToriDraw_SharedModel
{
    struct ToriDraw_Model base;

    struct ToriDraw_SharedModelStore* store;
    struct ToriDraw_SharedModel* next;
    int64_t key;
    /** Placements holding this model. The store is not one of them. */
    int holders;
};

/**
 * A placement that owns its vertices and borrows its faces.
 *
 * The half-shared case, and the larger population: a loc contoured to the
 * ground or lit from its neighbour needs its own vertices, per-corner colours
 * and face_infos, but the faces indexing those vertices are identical at every
 * placement of it and are most of the bytes.
 *
 * `base`'s twelve TORIDRAW_SHARED_FACE_FIELDS pointers alias `faces`; every
 * other array in it is this placement's own. That is why the read view is const
 * here too even though half of it really is writable -- C has no way to say
 * "these twelve members are not yours", so the write goes through
 * ToriDraw_ModelLentFacesPrivate, which names what it is handing over.
 */
struct ToriDraw_ModelLentFaces
{
    struct ToriDraw_Model base;
    struct ToriDraw_SharedFaces* faces;
};

struct ToriDraw_ModelHandle
{
    enum ToriDraw_ModelKind kind;
    union
    {
        struct
        {
            struct ToriDraw_Model* model;
            struct ToriDraw_ModelGround* ground;
        } model;
        /** kind == TORIDRAWMK_MODEL_SHARED */
        struct ToriDraw_SharedModel* shared;
        /** kind == TORIDRAWMK_MODEL_LENT_FACES */
        struct ToriDraw_ModelLentFaces* lent;
    } u;
};

static inline int
ToriDraw_ModelKindIsFull(enum ToriDraw_ModelKind kind)
{
    return kind == TORIDRAWMK_MODEL || kind == TORIDRAWMK_MODEL_HD ||
           kind == TORIDRAWMK_MODEL_SHARED || kind == TORIDRAWMK_MODEL_LENT_FACES;
}

/**
 * The geometry, for reading, whoever owns it.
 *
 * All four model types put a ToriDraw_Model at offset zero -- MODEL is one,
 * the other three embed one as their first member -- so this is the uniform
 * view every consumer wants and the only one most of them need. Const because
 * three of the four are not the caller's to write; the two accessors below are
 * how a caller that has earned the right says so.
 */
static inline const struct ToriDraw_Model*
ToriDraw_ModelRead(struct ToriDraw_ModelHandle hnd)
{
    assert(ToriDraw_ModelKindIsFull(hnd.kind));
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL_SHARED:
        return &hnd.u.shared->base;
    case TORIDRAWMK_MODEL_LENT_FACES:
        return &hnd.u.lent->base;
    default:
        return hnd.u.model.model;
    }
}

struct ToriDraw_Position
{
    int x;
    int y;
    int z;
    int pitch;
    int yaw;
    int roll;
};

struct ToriDraw_ViewPort
{
    int width;
    int height;
    int stride;

    int x_center;
    int y_center;

    int clip_left;
    int clip_top;
    int clip_right;
    int clip_bottom;
};

struct ToriDraw_Camera
{
    /** Which of the two knobs below drives the projection. Zero-initialising a
     *  camera selects SCALE, so a memset camera projects at the reference's
     *  default 512 rather than at whatever an unset angle would resolve to. */
    enum ToriDraw_ProjectionMode projection_mode;

    /** The reference client's viewport scale (class159.method5357 ->
     *  client.field817): the integer multiplier in screen = coord * scale / z,
     *  recomputed per layout from the world viewport height. Live when
     *  projection_mode == TORIDRAW_PROJECTION_MODE_SCALE. 0 = TORIDRAW_PROJECTION_SCALE_DEFAULT.
     *
     *  The only way to match a reference projection exactly -- most integer
     *  scales are not reachable through fov_rpi2048 at all. */
    int projection_scale;

    /** Field of view, in units of 2*pi/2048. Live when projection_mode ==
     *  TORIDRAW_PROJECTION_MODE_FOV. 0 = TORIDRAW_PROJECTION_FOV_DEFAULT. Natural for a
     *  free camera; lossy as a way to request a specific scale (see the ladder
     *  note in graphics/projection.h). */
    int fov_rpi2048;

    /**
     * Pixels per world unit, 16.16 fixed point. Live when projection_mode ==
     * TORIDRAW_PROJECTION_MODE_PARALLEL; TORIDRAW_ORTHO_ZOOM_UNIT (65536) is 1:1.
     * Deliberately not projection_scale reused: that one is a perspective numerator
     * measured against z, this is a plain screen scale, and collapsing two
     * different quantities into one field is how a camera ends up projecting
     * with a value nobody set.
     */
    int parallel_zoom16;

    /**
     * Depth at which geometry stops being drawn.
     *
     * Under perspective this is a hard requirement -- it is the divide's
     * singularity. Under parallel projection nothing is unsafe, so it becomes a
     * policy knob: geometry nearer than this is hidden, which is what stops a
     * map-editor camera sitting inside a wall from drawing the wall behind it.
     * Set it to a large negative value to disable near clipping entirely and
     * keep every model on the cheaper no-clip kernels.
     */
    int near_plane_z;

    /** Interface-model projections supply their own already-projected screen
     * triangle and need the affine texture kernel rather than the world-camera
     * perspective texture basis. */
    int texture_affine;

    int pitch;
    int yaw;
    int roll;
};

/*
 * Camera-only constants shared by every yaw projection in a command stream.
 * Each value is splatted so the Apple AArch64 projection kernel can load the
 * complete prepared state with two paired vector loads and one vector load.
 * Keep the order and 16-byte alignment in sync with projection16.aarch64.S.
 */
struct ToriDraw_ProjectionPreparedCamera
{
    _Alignas(16) int cos_yaw[4];
    int sin_yaw[4];
    int cos_pitch[4];
    int sin_pitch[4];
    int cot15[4];
};

/**
 * The same prepared camera's pitch and fov, already in the form the SSE2
 * kernel actually multiplies by.
 *
 * toridraw_projection_prepared_core wants these three as floats scaled by 1/65536,
 * 1/65536 and 1/64. They were being derived from the int block above on every
 * call -- a load, a cvtdq2ps and a mulps each -- for values that change once a
 * frame. That is nothing on a 380-vertex model and it is not nothing on a
 * four-vertex terrain tile, where the loop body runs exactly ONCE and the
 * prologue is the call.
 *
 * A SEPARATE BLOCK, not three more members on the struct above, because that
 * struct's size and field offsets are pinned by _Static_asserts in toridraw.c
 * for projection16.aarch64.S, which loads it with ldp pairs. Appending would
 * keep those offsets valid and still trip the size assert, and the Apple lane
 * cannot be built here to check. Nothing on that lane reads this one.
 *
 * The conversion is EXACT, so this is a hoist and not an approximation: both
 * scales are powers of two (2^-16 and 2^-6), so the multiply only adjusts an
 * exponent, and the int-to-float conversion rounds identically whether C or
 * cvtdq2ps does it. The bytes the kernel reads are the bytes it used to
 * compute -- which the pixel comparison then confirms rather than assumes.
 */
struct ToriDraw_ProjectionPreparedCameraFloat
{
    _Alignas(16) float cos_pitch[4];
    float sin_pitch[4];
    float cot15[4];
};

enum ToriDraw_TextureAnimation
{
    TORIDRAW_TEXANIM_DIRECTION_NONE,
    TORIDRAW_TEXANIM_DIRECTION_V_DOWN = 1,
    TORIDRAW_TEXANIM_DIRECTION_U_DOWN = 2,
    TORIDRAW_TEXANIM_DIRECTION_V_UP = 3,
    TORIDRAW_TEXANIM_DIRECTION_U_UP = 4,
};

struct ToriDraw_Texture
{
    int* texels;
    int width;
    int height;
    bool opaque;
    /* Set when `texels` belongs to somebody else and this texture only points
     * at it -- the scene still owns the struct and frees it, but leaves the
     * pixels alone. Zero means owned, so a texture built the ordinary way (all
     * of them are calloc'd or brace-initialised) frees its texels as before. */
    bool borrowed_texels;
    int animation_direction;
    int animation_speed;
};

/*
 * Texture id capacity. 256 held for the sprite-backed eras, but the RS2 procedural
 * materials number to 1163 in cache.643 — and 234 of its SD-drawable materials sit above
 * 255, so a 256-slot map silently never draws them (the raster skips faces whose texture
 * is absent). 2048 covers the era with headroom; the map is pointers, so the cost is 16KB.
 */
/* Overridable, because the map is a flat array and 2048 pointers is 16 KB --
 * affordable in a world client, most of an embedded client's whole budget. A
 * client whose texture ids are all small builds with
 * -DTORIDRAW_TEXTURE_ID_CAPACITY=64. Registering an id past the capacity is
 * dropped by ToriDraw_TextureMapSet; LOOKING one up aborts in
 * ToriDraw_TextureMapGet, which is the right way round -- a model naming a
 * texture this build cannot hold is a configuration error, and the
 * alternative is a face that silently does not draw. */
#ifndef TORIDRAW_TEXTURE_ID_CAPACITY
#define TORIDRAW_TEXTURE_ID_CAPACITY 2048
#endif


struct ToriDraw_TextureMap
{
    struct ToriDraw_Texture* textures[TORIDRAW_TEXTURE_ID_CAPACITY];
    int count;
};

struct ToriDraw_Animation;
struct ToriDraw_Map;
struct ToriDraw_Vec;
struct ToriDraw_ScenePendingPose;
struct ToriDraw_Sprite;
struct ToriDraw_Font;
struct ToriDraw_Sound;

/*
 * A decoded sound clip held by the scene.
 *
 * Sound is an *asset*, exactly like a model or a sprite: the game decodes it
 * once, hands it to the scene under an id, and the scene's load/unload events
 * carry it to whichever backend is listening. Keeping it here rather than in a
 * private cache is what lets one rule -- "the scene owns loaded assets, and
 * clearing the scene unloads them" -- cover audio too, so an area sound cannot
 * outlive the map it belongs to.
 *
 * PCM is 16-bit signed mono. `loop_start`/`loop_end` are sample offsets and are
 * the clip's own loop span, not a playback choice; whether a given play loops is
 * decided per voice.
 */
struct ToriDraw_Sound
{
    int16_t* samples; /* owned */
    int sample_count;
    int sample_rate;
    int loop_start;
    int loop_end;
    bool ping_pong;
    /** Ticks of silence trimmed off the front (the reference's Wave.trim), which
     *  the game adds back to the server's play delay. Carried with the clip
     *  because it is a property of the clip, not of a play. */
    int queue_delay;
};

#define TORIDRAW_SCENE_INVALID_BATCH_ID (-1)
#define TORIDRAW_SCENE_INVALID_ELEMENT_ID (-1)
#define TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE 65536
#define TORIDRAW_SCENE_MAX_ELEMENTS 65536

enum ToriDraw_EventKind
{
    TORIDRAW_EVENT_NONE = 0,
    TORIDRAW_EVENT_MODEL_LOAD,
    TORIDRAW_EVENT_MODEL_UNLOAD,
    TORIDRAW_EVENT_ANIM_LOAD,
    TORIDRAW_EVENT_ANIM_UNLOAD,
    TORIDRAW_EVENT_TEX_LOAD,
    TORIDRAW_EVENT_TEX_UNLOAD,
    TORIDRAW_EVENT_SPRITE_LOAD,
    TORIDRAW_EVENT_SPRITE_UNLOAD,
    TORIDRAW_EVENT_FONT_LOAD,
    TORIDRAW_EVENT_FONT_UNLOAD,
    TORIDRAW_EVENT_SOUND_LOAD,
    TORIDRAW_EVENT_SOUND_UNLOAD,
    TORIDRAW_EVENT_BATCH_BEGIN,
    TORIDRAW_EVENT_BATCH_MODEL_ADD,
    TORIDRAW_EVENT_BATCH_ANIM_ADD,
    TORIDRAW_EVENT_BATCH_END,
    TORIDRAW_EVENT_BATCH_CLEAR,
    TORIDRAW_EVENT_SCENE_RESET,
};

struct ToriDraw_Event
{
    enum ToriDraw_EventKind kind;
    int batch_id;
    int element_id;
    int pose_id;
    int anim_index;
    int texture_id;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Texture* texture;
    struct ToriDraw_Sprite** sprites;
    int sprite_count;
    struct ToriDraw_Font* font;
    struct ToriDraw_Sound* sound;
    int sound_id;
    struct ToriDraw_Position world_position;
};

struct ToriDraw_EventQueue
{
    /* Grown on demand instead of a flat TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE
     * array. The queue is drained every frame by ToriDraw_SceneFrameEnd, so
     * its real high-water is a small fraction of the cap, while the inline
     * form put 5.5 MB into every struct ToriDraw_Scene -- and the scene is
     * calloc'd, so all of it was resident from construction.
     *
     * The cap itself is unchanged: the emitters still refuse a push at
     * TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE. Only the allocation follows
     * demand, and capacity is never released -- there is nothing to release.
     * The high-water is one event per scene element (19,360 in a loaded
     * region), and it is reached again inside every window a trim could
     * measure, so a shrink policy here only ever gives back what the next
     * frame immediately takes.
     *
     * Element pointers must not outlive a push: `events` moves on grow.
     * Both back-patch sites take theirs from the push that just happened,
     * and both drains (rs_audio.c, torirs_frame.c) index. */
    struct ToriDraw_Event* events;
    int count;
    int cap;
};

struct ToriDraw_TextureState
{
    struct ToriDraw_TextureMap texture_map;
};

struct ToriDraw_SkeletalAnim;

struct ToriDraw_SceneElement
{
    int scene_id;
    /** Clear group (TORIDRAW_SCENE_POOL_*): STATIC elements are freed
     *  wholesale by ToriDraw_SceneClearPool on a map rebuild; DYNAMIC
     *  elements (entities) keep their ids across it. */
    uint8_t pool;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Animation* secondary_animation;
    struct ToriDraw_SkeletalAnim* skeletal_animation;
    struct ToriDraw_Position world_position;
    bool dynamic;
    bool pending_batch_add;
    bool is_skeletal; /* true: use skeletal_animation; false: use animation */
    int skeletal_play_frames; /* playback length from seq anim_maya_end-start; -1 = palette size */
    int anim_seq_id;
    int anim_frame;
    int anim_cycle;

    /* Secondary (walk) track for the walkmerge blend: when the primary
     * animation carries a walkmerge mask and anim2 is bound, the apply pass
     * blends primary+secondary via ToriDraw_ModelAnimateFrameMasked. */
    int anim2_seq_id; /* -1/0 = no secondary track */
    int anim2_frame;
    /** Entity-owned animation stepping: the app's world sim drives
     * anim_frame/anim2_frame; the generic per-element modulo tick skips
     * elements with this set. */
    bool anim_external;
    /** The sequence loops instead of terminating: at the end of the frame list
     * anim_frame wraps to 0 rather than consulting SeqType.frameStep. Set for
     * projectiles and map spotanims (reference ClientProj.move /
     * MapSpotAnim.update); locs are DynamicObjects and leave it clear so they
     * still drop their sequence and revert to the static model. */
    bool anim_loop;
    /** Reference Model.useAABBMouseCheck: pick against the projected bounding
     *  box instead of per-face. Set for npcs, players and ground objs (see
     *  ObjType.getWorldModel / ClientPlayer / NpcType); locs keep the exact
     *  per-face test. */
    bool pick_aabb;

    /*
     * The pose the model is currently holding, so ToriDraw_SceneElementApplyAnimation
     * can skip a request for the pose it already computed (TORIDRAW_ANIM_SKIP_SAME):
     * a sequence advances its frame every two to four cycles, and the
     * renderer asks for the pose every frame, so most requests are repeats.
     * posed_primary < 0 = the model holds no known pose. Every mutation that
     * can change what a (track, frame) pair produces -- a model mounted or
     * un-shared, a sequence bound or dropped, an in-place edit through
     * ToriDraw_SceneElementModelForWrite -- bumps model_revision and
     * clears posed_primary. The track pointers are in the tuple as well so
     * a direct write of `animation` / `skeletal_animation` is caught by
     * identity even without the bump.
     */
    int8_t posed_primary;
    int16_t posed_frame;
    int16_t posed_frame2;
    uint32_t model_revision;
    const void* posed_track;
    const void* posed_track2;
};

struct ToriDraw_SceneBatchElementHandle
{
    struct ToriDraw_Scene* scene;
    int batch_id;
    int id;
};

#define TORIDRAW_CACHE_FONT_SLOT_COUNT 4

struct ToriDraw_Scene
{
    uint32_t flags;
    int max_vertices;
    int max_faces;
    int depth_levels;
    int depth_stride;
    int priority_stride;
    /** Entries in each flexible-priority (10/11) array. The two are allocated
     *  the same size; the sorter merges 11 into 10, so the merged run must fit
     *  this many entries. Kept so the sort can assert that rather than trust
     *  it: these arrays sit next to the other scratch, and an overrun lands in
     *  a live neighbour instead of anywhere a sanitizer can see. */
    int flex_prio_capacity;

    struct ToriDraw_ModelHandle active_hnd;

    struct ProjectedVertex projected_vertex;
    struct ToriDraw_AABB aabb;

    /*
     * Whether the model ToriDraw_Project last projected could reach behind the
     * near plane, and so whether screen_vertices_x may hold
     * TORIDRAW_SCREEN_X_NEAR_CLIPPED. False for the overwhelming majority of
     * models: the camera has to be inside the model's bounding sphere for a
     * vertex to clip. Consumers that test for the sentinel (the triangle
     * dispatchers, the per-face pick) must check this first, both to skip the
     * test entirely in the common case and because the no-clip kernel does not
     * nudge a genuine -5000 out of the way. Mirrors `clipped` in the reference
     * (Client-TS Model.worldRender:1755, consumed at render2:1876).
     */
    bool near_clipped;

    /*
     * Near plane ToriDraw_Project actually used for the model it last
     * projected, which is camera->near_plane_z raised far enough that no
     * projected coordinate can leave the rasterizer's 16.16 domain. See
     * toridraw_safe_near_plane_z. Every consumer of the projection scratch —
     * the near-clip triangle builders above all — must clip against this
     * value, not the camera's, or the two disagree about where the plane is.
     */
    int projection_near_plane_z;

    struct ToriDraw_TextureState* tex_state;

    /*
     * The scene's one z-buffer scratch, screen sized. NULL unless the scene was
     * created with TORIDRAW_SCENE_MODEL_ZBUFFER (allocated lazily, on the first
     * raster of a model that opts in) or a caller sized it up front.
     *
     * Only models carrying TORIDRAW_MODEL_FLAG_ZBUFFER touch it, and each such
     * model resets the region it draws into before drawing — see
     * graphics/zdepth.h for why that reset is what bounds the effect to one
     * model.
     *
     * `zbuffer_stride` is the row stride in ELEMENTS and matches the viewport
     * stride the buffer was sized for, so a pixel at `offset` in the frame
     * buffer is at the same `offset` here. Keeping the two layouts identical is
     * what lets one offset walk both.
     */
    torizdepth_t* zbuffer;
    int zbuffer_stride;
    int zbuffer_rows;

    int* screen_vertices_x;
    int* screen_vertices_y;
    int* screen_vertices_z;
    int* orthographic_vertices_x;
    int* orthographic_vertices_y;
    int* orthographic_vertices_z;

    /*
     * Optional prepared-camera state for the projection hot path. Pointer
     * identity makes the normal render-command stream a single cheap compare;
     * callers using another camera continue through the portable kernels.
     */
    struct ToriDraw_ProjectionPreparedCamera projection_prepared_camera;
    /*
     * The screen box the AArch64 prepared kernel accumulates WHILE it
     * projects: lane-wise min x, max x, min y, max y over every full
     * four-vertex block its vector loop ran, straight from the registers the
     * screen coordinates were converted in. projection16.aarch64.S writes it
     * 128 bytes past screen_vertices_x -- immediately after the prepared
     * camera; the static asserts in toridraw.c pin both -- so the bound of a
     * large model costs four vector ops per block inside the kernel instead
     * of a second pass that reads every coordinate back.
     *
     * projection_bound_vertices is how many leading vertices the block
     * covers (a multiple of four); the caller sets it after the kernel
     * returns and ToriDraw_Project zeroes it before dispatch, so a kernel
     * that does not write the block leaves zero and
     * toridraw_projected_bound sweeps the outputs instead.
     */
    _Alignas(16) int projection_bound[4][4];
    int projection_bound_vertices;
    const struct ToriDraw_Camera* projection_prepared_camera_source;
    /* Published and cleared with the block above, and by the same function --
     * projection_prepared_camera_source guards both. Placed AFTER the source
     * pointer so the offset of projection_prepared_camera, which a static
     * assert pins relative to screen_vertices_x, does not move. */
    struct ToriDraw_ProjectionPreparedCameraFloat projection_prepared_camera_f;
    /*
     * The same camera's full cot16 -- the value the near-clip rule's safe
     * plane scales by -- so the per-model near-clip reads it here instead of
     * re-running the fov table and clamp ladder for every model in the frame
     * (toridraw_projection_near_clip_perspective). Guarded by the same
     * projection_prepared_camera_source, written by the same function. Here
     * and not in the int block above: that block's size is pinned for
     * projection16.aarch64.S. */
    int projection_prepared_cot16;

    faceint_t* tmp_depth_face_count;
    faceint_t* tmp_depth_faces;
    faceint_t* tmp_priority_face_count;
    /* Sum of face depths per priority, for the flexible-priority averages in
     * sort_face_draw_order. **int, not faceint_t**: this accumulates, where
     * every other scratch array here holds one face index. A depth is 0..1499
     * and a model can have hundreds of faces at one priority, so an int16 wraps
     * — 451 faces at priority 4 on npc 999 sum past 32767 long before the
     * average is taken. The reference holds these in an int array for the same
     * reason. */
    int* tmp_priority_depth_sum;
    faceint_t* tmp_priority_faces;
    int* tmp_flex_prio11_face_to_depth;
    int* tmp_flex_prio12_face_to_depth;

    faceint_t* sm_face_depth;

    /*
     * The six screen coordinates of each accepted face, stashed by the depth
     * sort so nothing downstream has to gather them again.
     *
     * Eight ints per face: x0,x1,x2, the near-clip flag, y0,y1,y2, spare. The
     * sort already holds all six in registers -- it needs them for the winding
     * cross product -- and used to throw them away, leaving the raster pass to
     * re-read them through face_indices_a/b/c into screen_vertices_x/y, which
     * is nine dependent loads per face to recover what was in hand a moment
     * earlier. Writing them out costs six stores into a region that is a few
     * hundred bytes for a typical model and therefore always hot.
     *
     * The flag in lane 3 is the sort's `clip_candidate`, carried for the same
     * reason: the raster pass re-derived it by reading the same three vertex_x
     * entries the sort had already tested.
     *
     * Only faces the sort ACCEPTED have meaningful entries, and only they can
     * appear in tmp_face_order, so no consumer can read a stale one.
     */
    /* Two planes, four ints per face each, so the NEON sort can write four
     * faces with two interleaving stores: sm_face_x4 = {x0, x1, x2, clip},
     * sm_face_y4 = {y0, y1, y2, perm}. Allocated max_faces + 4 records, since
     * the vector sort writes whole blocks of four. */
    int* sm_face_x4;
    int* sm_face_y4;
    /*
     * The bitonic+radix sort's composite keys, (0xFFFF - depth) << 16 | face,
     * and the radix sort's bounce buffer. Sized to the next power of two above
     * max_faces plus four lanes of slack for the unconditional pack store.
     */
    uint32_t* sm_sort_keys;
    uint32_t* sm_sort_tmp;
    /* The projected vertices of the model being sorted, interleaved as
     * {x, y, z, 0} quads: max_vertices + 4 of them, rebuilt per model by a
     * lane whose gather wants whole-register loads (the A32 NEON lane; see
     * its block4). NULL until the bitonic+radix scratch is allocated. */
    int* sm_vertex_xyz;
    /*
     * The same vertices as {x - box_min_x, y - box_min_y, z, 0} int16 quads,
     * for the A32 lane's eight-face K16 block: a model whose screen box is
     * under 32K wide and tall has every rebased coordinate and every winding
     * delta an exact int16, and the products exact int32 (see the lane).
     * (max_vertices + 8) quads.
     */
    int16_t* sm_vertex_xyz16;
    /*
     * The screen box toridraw_projected_bound swept for the model last
     * projected, RAW: min x, max x, min y, max y in the projection's own
     * space, before the viewport offset and the pick dilation that go into
     * `aabb`. What a sort lane reads to classify a model's extent.
     */
    int projected_box[4];
    /*
     * The depth range a lane could prove for the model it just culled, from
     * the z range of its vertices: every accepted face's depth is in
     * [sm_sort_depth_lo, sm_sort_depth_hi]. The dispatcher resets both to
     * "unknown" (0, INT_MAX) before the lane runs; a lane that sweeps the
     * vertices anyway (the A32 lane's interleave pass) narrows them for
     * free, and a range under 256 levels lets the radix finish in ONE pass.
     */
    int sm_sort_depth_lo;
    int sm_sort_depth_hi;
    /*
     * Whether the LAST sort actually filled sm_face_x4/y4, which is not the same
     * question as whether the build can. Three things have to hold -- the
     * kernel wanted it (its raster has a whole-model door and its sort can
     * stash -- see sd_wants_presort; no caller states this), the batched
     * kernels are armed, and this is a small-mode scene, since sm_face_x4/y4
     * are allocated nowhere else. The batched raster
     * walk requires this rather than re-deriving it, so the side that writes
     * the buffer and the side that reads it cannot disagree.
     */
    int sm_face_xy_valid;
    int* sm_depth_offset;
    int* sm_depth_cursor;
    faceint_t* sm_faces_by_depth;
    int sm_prio_count[13];
    int* sm_prio_offset;
    faceint_t* sm_prio_faces;
    int* sm_flex_prio11_face_to_depth;
    int* sm_flex_prio12_face_to_depth;

    int* tmp_face_order;
    int tmp_face_order_count;

    struct ToriDraw_EventQueue event_queue;
    struct ToriDraw_Map* models_hmap;
    struct ToriDraw_Map* animation_hmap;
    struct ToriDraw_Map* sprites_hmap;
    struct ToriDraw_Map* fonts_hmap;
    /** Monotonic version of model/sprite/font registry mutations. UITree uses
     *  this instead of map cardinality so same-id replacements are visible. */
    uint64_t ui_asset_revision;
    /** Decoded sound clips by id — see ToriDraw_SceneSoundAdd. */
    struct ToriDraw_Map* sounds_hmap;
    /** Always-resident cache fonts indexed by revconfig cache_font_id 0–3. */
    struct ToriDraw_Font* cache_fonts[TORIDRAW_CACHE_FONT_SLOT_COUNT];
    struct ToriDraw_IntrusiveList elements;

    /*
     * Models this scene's elements share between them, NULL until something
     * asks. See toridraw_shared_model.h; reach it through
     * ToriDraw_SceneSharedModels / ToriDraw_SceneSharedFaces, each built on
     * the first ask.
     *
     * It belongs to the scene because its entries are held by the scene's
     * elements and by nothing else: the store retains nothing of its own, so
     * clearing the element pool empties it and freeing the scene -- after the
     * graph shutdown that disposes those elements -- is what frees it.
     */
    struct ToriDraw_SharedModelStore* shared_models;
    struct ToriDraw_SharedFacesStore* shared_faces;

    bool batch_building;
    int current_batch_id;
    int current_batch_element_count;
    int next_batch_id;

    struct ToriDraw_ScenePendingPose* pending_poses;
    int pending_pose_count;
    int pending_pose_cap;

    /*
     * Ids of elements the per-cycle animation tick has to visit — those with a
     * seq bound that is not externally driven. The pool is dominated by static
     * scenery, so scanning every slot each cycle to find the handful of
     * animated ones was pure overhead; this list is rebuilt lazily whenever
     * anim_list_dirty is set. See ToriDraw_SceneAnimatedElements.
     *
     * Entries are only a hint: consumers still re-check liveness and
     * anim_seq_id, so a stale id is harmless. A *missing* id is what matters,
     * hence every element alloc/release/clear and every seq mutation marks the
     * list dirty.
     */
    int* anim_list;
    int anim_list_count;
    int anim_list_cap;
    bool anim_list_dirty;

};

#define TORIDRAW_CULL_VISIBLE 0
#define TORIDRAW_CULL_FAST 1
#define TORIDRAW_CULL_AABB 2
#define TORIDRAW_CULL_ERROR 3

static inline int*
ToriDraw_FaceOrder(struct ToriDraw_Scene* scene)
{
    return scene->tmp_face_order;
}

static inline int
ToriDraw_FaceOrderCount(struct ToriDraw_Scene* scene)
{
    return scene->tmp_face_order_count;
}

#endif
