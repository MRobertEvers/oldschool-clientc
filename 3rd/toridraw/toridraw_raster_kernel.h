#ifndef TORIDRAW_RASTER_KERNEL_H
#define TORIDRAW_RASTER_KERNEL_H

#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * THE KNOB INVENTORY.
 *
 * Every environment variable that changes which kernel runs, and WHEN it is
 * read. The rule the three-stage table exists to enforce is that a knob
 * becomes a CHOICE at a point the caller can see -- a getter, or
 * ToriDraw_KernelTake -- and is never re-asked per model or per face behind
 * their back. This list is the audit of that rule; a new knob read from inside
 * a stage is a bug against it.
 *
 *   ToriDraw_Init
 *     TORIDRAW_RASTER_SCANLINE    branching <-> scanline, SD and HD.
 *                                 ToriDraw_RasterSetScanline overrides.
 *
 *   At the getter (once, where a renderer takes its table)
 *     TORIDRAW_FACE_SORT          bucket <-> bitonic_radix, into the table's
 *                                 stage-2 slot.
 *                                 ToriDraw_FaceSortSetBitonicRadix overrides.
 *     TORIDRAW_RASTER_BATCH       whether the painter table's raster is the
 *                                 branching kernel with its whole-model run
 *                                 door or the per-face twin. The sort no
 *                                 longer asks: `presort` follows from the
 *                                 door alone.
 *
 *   Per model, into the raster context (hoisted, not asked per face)
 *     TORIDRAW_SKIP_TEXTURED      the textured-path bisect knob, read once
 *                                 into ctx->skip_textured and shared by both
 *                                 walks -- a face one drops must not be drawn
 *                                 by the other.
 *
 *   Still per model, and honestly so
 *     TORIDRAW_TILE_SORT          which two-triangle terrain-tile kernel; the
 *                                 tile shape is a per-model property.
 *     TORIDRAW_SORT_BITONIC_MAX   the bitonic/radix crossover, compared
 *                                 against this model's face count.
 *
 *   Compile time, not runtime
 *     TORIDRAW_FLIP_WINDING       a one-shot handedness check for an import,
 *                                 not an A/B arm. See graphics/winding.h.
 *     TORIDRAW_PIXEL_FORMAT (graphics/pixel_format.h), the ISA ladders,
 *                                 the tri/span assembly lanes.
 *
 *   Measurement only, and deliberately still runtime
 *     TORIDRAW_FRAME_AB[_SWAP,_KERNELS,_BATCH], TORIRS_ABL_*,
 *     TORIDRAW_ABLATE*, TORIDRAW_BATCH_STATS, the census builds.
 *
 *   Changes nothing about which kernel runs
 *     TORIDRAW_KERNEL_LOG=0       silences the four-line configuration report
 *                                 ToriDraw_KernelTake prints. Listed here
 *                                 because it is read at the same door and a
 *                                 reader auditing that door should find it,
 *                                 not because it selects anything.
 */

enum ToriDraw_RasterKernelFlags
{
    /* Traverse model face order and do not provision a depth buffer. */
    TORIDRAW_RASTER_KERNEL_FLAG_NONE = 0,
    /* Produce and consume ToriDraw's back-to-front face order. */
    TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING = 1u << 0,
    /* Provision, rebase, and reset a model-local depth buffer. */
    TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER = 1u << 1,
};

/* The four terminal algorithms in the stock/SD face rasterizer. */
enum ToriDraw_RasterFaceClassSD
{
    TORIDRAW_RASTER_FACE_SD_GOURAUD = 0,
    TORIDRAW_RASTER_FACE_SD_FLAT = 1,
    TORIDRAW_RASTER_FACE_SD_TEXTURED = 2,
    TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT = 3,
    TORIDRAW_RASTER_FACE_SD_CLASS_COUNT = 4,
};

/*
 * HD has two solid algorithms and four texture projection algorithms. Texture
 * shading, face alpha, texel gating, modulation, and depth testing are inputs
 * to these algorithms; they are deliberately not additional vtable axes.
 */
enum ToriDraw_RasterFaceClassHD
{
    TORIDRAW_RASTER_FACE_HD_GOURAUD = 0,
    TORIDRAW_RASTER_FACE_HD_FLAT = 1,
    TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE = 2,
    TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER = 3,
    TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE = 4,
    TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE = 5,
    TORIDRAW_RASTER_FACE_HD_CLASS_COUNT = 6,
};

enum ToriDraw_RasterTextureGate
{
    TORIDRAW_RASTER_TEXTURE_OPAQUE = 0,
    TORIDRAW_RASTER_TEXTURE_COLOR_KEY = 1,
    TORIDRAW_RASTER_TEXTURE_TEXEL_ALPHA = 2,
};

struct ToriDraw_RasterVertexFrame
{
    int p;
    int m;
    int n;
};

/*
 * Prepared texture state for one textured face. ToriDraw has resolved and
 * bounds-checked the texture and mapping before invoking a textured callback;
 * solid callbacks must not inspect their face's texture member.
 */
struct ToriDraw_RasterTextureSD
{
    int texture_id;
    const int* texels;
    int width;
    int height;
    enum ToriDraw_RasterTextureGate gate;
    /* Non-zero render types select the stock affine texture family. */
    unsigned int render_type;
    bool frame_fallback;
    struct ToriDraw_RasterVertexFrame frame;
};

struct ToriDraw_RasterTextureHD
{
    int texture_id;
    const int* texels;
    int width;
    int height;
    enum ToriDraw_RasterTextureGate gate;
    bool clamp_s;
    bool clamp_t;

    /* Original raw render type. face_class selects the active mapping member. */
    unsigned int render_type;
    bool frame_fallback;
    union
    {
        struct ToriDraw_RasterVertexFrame vertex_frame;
        const struct ToriDraw_TexMapping* hd_mapping;
    } mapping;

    bool modulate;
    int tint_r;
    int tint_g;
    int tint_b;
    int texture_neutral;
};

/*
 * Pass-stable input shared by every callback for one model. The structure is
 * read-only, but pixel_buffer and zbuffer name writable render targets. The
 * framebuffer pointer is rebased to clip_origin_x/clip_origin_y; width and
 * height describe that rebased region, and projection_center_* use its local
 * coordinates.
 *
 * All pointed-to storage is borrowed and valid only for the active render
 * call. `internal` is reserved for ToriDraw's built-in implementations and is
 * not an extension point for application kernels.
 */
struct ToriDraw_RasterTarget
{
    toripixel_t* pixel_buffer;
    torizdepth_t* zbuffer;
    int width;
    int height;
    int stride;
    int clip_origin_x;
    int clip_origin_y;
    int projection_center_x;
    int projection_center_y;

    int near_plane_z;
    int camera_cot16;
    int model_mid_z;
    bool parallel_projection;
    bool affine_textures;
    bool depth_test;
    bool near_clip_available;

    int vertex_count;
    const int* screen_vertices_x;
    const int* screen_vertices_y;
    const int* screen_vertices_z;
    const int* orthographic_vertices_x;
    const int* orthographic_vertices_y;
    const int* orthographic_vertices_z;

    const vertexint_t* posed_vertices_x;
    const vertexint_t* posed_vertices_y;
    const vertexint_t* posed_vertices_z;
    const vertexint_t* bind_vertices_x;
    const vertexint_t* bind_vertices_y;
    const vertexint_t* bind_vertices_z;

    void* internal;
};

/*
 * Normalized input for one drawable face. The same stack object may be reused
 * for the next face, so neither it nor any descriptor pointer may be retained.
 * Flat classes receive shade[0] repeated three times; opacity is effective
 * source coverage in the range 0..255.
 */
struct ToriDraw_RasterFaceSD
{
    enum ToriDraw_RasterFaceClassSD face_class;
    int face_index;
    int vertex[3];
    int shade[3];
    int opacity;
    bool near_clipped;
    struct ToriDraw_RasterTextureSD texture;
};

struct ToriDraw_RasterFaceHD
{
    enum ToriDraw_RasterFaceClassHD face_class;
    int face_index;
    int vertex[3];
    int shade[3];
    int opacity;
    bool near_clipped;
    struct ToriDraw_RasterTextureHD texture;
};

typedef void (*ToriDraw_RasterKernelSDFaceFn)(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face);

typedef void (*ToriDraw_RasterKernelHDFaceFn)(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face);

/*
 * STAGE 3. A raster kernel draws a MODEL, not a face.
 *
 * This is the whole of the raster stage: one call per model, and how that
 * model becomes pixels -- which traversal, which batching, which arm per face
 * type -- is the kernel's own business. Face classification is an
 * implementation detail of a walk, not a term in the interface.
 *
 * Two walks ship. ToriDraw_RasterWalkPerFace is the stock one: it normalizes
 * each face (resolving and bounds-checking the texture, the HIDDEN sentinel,
 * face alpha, near-clip eligibility, index range) and dispatches it to one of
 * the four callbacks in the vtable below. toridraw_raster_walk_batched stages
 * runs of presorted faces into the assembly kernels and falls through to the
 * per-face walk for anything it cannot take.
 *
 * A kernel that only wants to supply the four leaf callbacks names
 * ToriDraw_RasterWalkPerFace here and gets the normalizing walk for free; that
 * is the narrow extension point, and the reason the vtable survives the
 * inversion. A kernel that wants to own the loop supplies its own.
 *
 * The context is ToriDraw's own render state and is deliberately opaque: it is
 * reserved for the built-in walks, and an application kernel reaches the model
 * through the callbacks rather than through this pointer.
 */
struct ToriDrawModelRasterContext;

typedef void (*ToriDraw_RasterKernelSDModelFn)(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx);



/**
 * The stock stage-3 walk: normalize each face and dispatch it to the vtable.
 *
 * Name this as a kernel's draw_model to supply only the four leaf callbacks
 * and inherit the whole normalizing walk -- texture resolution and bounds
 * checking, the HIDDEN sentinel, face alpha, near-clip eligibility, index
 * range, then class selection. That is the narrow extension point, and the
 * reason the face vtable survives stage 3 being a whole-model call.
 *
 * Naming it is also a declaration: a kernel whose draw_model is this one has
 * no traversal of its own, which is how the library knows not to ask the face
 * sort for a presorted stash it would never read.
 */
void
ToriDraw_RasterWalkPerFace(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx);

/* Every slot is required. A kernel with a NULL callback is incomplete. */
/*
 * The face callbacks the stock walk dispatches through.
 *
 * Only ToriDraw_RasterWalkPerFace reads this. A kernel whose draw_model is its
 * own walk may leave it NULL; a kernel that names ToriDraw_RasterWalkPerFace
 * must fill every slot.
 */
struct ToriDraw_RasterKernelSDVTable
{
    ToriDraw_RasterKernelSDFaceFn draw[TORIDRAW_RASTER_FACE_SD_CLASS_COUNT];
};

struct ToriDraw_RasterKernelHDVTable
{
    ToriDraw_RasterKernelHDFaceFn draw[TORIDRAW_RASTER_FACE_HD_CLASS_COUNT];
};

/*
 * The two stages in front of the face loop, each its own kernel.
 *
 * A model draw is three stages -- project the vertices, cull and sort the
 * faces, raster the faces -- and a raster kernel names all three, so a scene
 * can be timed with any one of them swapped and the other two held. The
 * stock objects are process-lifetime and immutable, like the raster kernels.
 *
 * PROJECTION: model space to screen space into the scene's scratch
 * (screen_vertices_*, orthographic_vertices_*, near_clipped, the projected
 * centre). Returns a TORIDRAW_CULL_* verdict; anything but VISIBLE means the
 * later stages do not run.
 *
 * FACE CULL + SORT: the winding cull and the back-to-front order, into
 * scene->tmp_face_order, returning the count. `presort` asks it to leave
 * the y-sorted screen coordinates behind for the batched raster walk
 * (scene->sm_face_x4 / y4, recorded in sm_face_xy_valid); a kernel that
 * cannot must say so there rather than leave a stale stash. The stage entries
 * derive that argument from the raster -- it is never a caller's to pass.
 *
 * The stock face-sort kernels:
 *
 *   bucket         the depth-bucket sort -- one scalar winding test per
 *                  face, faces scattered into per-depth lists, walked from
 *                  far to near.
 *   bitonic+radix  a four-wide SIMD winding cull compacted into (depth, face)
 *                  keys, then a bitonic network (<= 256 keys) or a two-pass
 *                  radix, and the order read straight off the sorted keys.
 *                  Same order as `bucket`, face for face
 *                  (toridraw_face_sort_bitonic_radix_test.c). It reports
 *                  itself as `radix` on a build with no vector lane, where
 *                  the network does not exist -- see
 *                  kernels/facesort.bitonic_radix.u.c.
 *
 * Neither slot may be NULL. The prebaked kernels and tables are handed out
 * with both already filled -- the projection is fixed, the sort is whichever
 * TORIDRAW_FACE_SORT / ToriDraw_FaceSortSetBitonicRadix named when the getter
 * ran (see facesort.bitonic_radix.small.dispatch.u.c) -- so the choice is made
 * once, where the caller takes the object, and never re-made per model behind
 * their back.
 */
typedef int (*ToriDraw_ProjectionKernelFn)(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera);


/*
 * What a face cull+sort kernel hands on, and what it needs to do it.
 *
 * The scratch API used to answer both by comparing the kernel pointer against
 * ToriDraw_FaceCullSortKernelGetBitonicRadix(), which meant a caller's own
 * sort kernel could never be reasoned about at all. Declaring it makes the
 * question one the kernel answers about itself.
 *
 * PROVIDES_PRESORTED_XY is a capability, not a promise. Both stock sorts can
 * leave the y-ordered stash behind -- the small-scene bucket sort in
 * bucket_sort_by_average_depth_small, the bitonic+radix sort in its own block
 * -- but
 * only when the raster it runs under has a door for it, the batched walk is
 * armed, AND the scene is small enough to have the buffers. A full-mode scene explicitly zeroes
 * sm_face_xy_valid, and that flag stays the per-model truth; this bit only
 * says the kernel is capable, so a table naming a presorting raster and a sort
 * that cannot presort is refusable at selection time.
 */
enum ToriDraw_FaceSortTrait
{
    /** Writes tmp_face_order. Every sort does; a kernel that does not is not
     *  a sort. Declared anyway so a NULL-slot kernel reads as incomplete. */
    TORIDRAW_FACESORT_PROVIDES_FACE_ORDER = 1u << 0,
    /** Can fill sm_face_x4 / y4 when asked and when the scene allows. */
    TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY = 1u << 1,
    /** Sorts composite keys and needs sm_sort_keys / sm_sort_tmp. */
    TORIDRAW_FACESORT_NEEDS_BITONIC_RADIX_KEYS = 1u << 2,
    /** Differs from the stock bucket sort only on a small scene; on a full
     *  one it falls through to the same dense bucket walk. */
    TORIDRAW_FACESORT_NEEDS_SMALL_SCENE = 1u << 3,
};

typedef int (*ToriDraw_FaceCullSortKernelFn)(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort);

/*
 * The projection stage's vtable, the peer of ToriDraw_RasterKernelSDVTable.
 *
 * Before any vertex is touched, ToriDraw_Project resolves two properties -- is
 * the camera parallel, and can this model reach behind the near plane -- and
 * those two answers select one specialized vertex kernel. That cross used to
 * be an if/else ladder in the middle of a 386-line function, which meant
 * "which projection is this frame running" was not something a caller could
 * hold, name, or swap.
 *
 * The two answers are not peers, so they are not two indices into one flat
 * table. The first names a FAMILY; the second is a question only that family
 * knows how to ask, because "can a vertex reach the near plane, and which
 * plane is that" has a different answer under a projection that divides by z
 * than under one that does not. So a family owns its own near-clip rule and
 * the two vertex kernels that rule selects between, and the shared shell
 * neither states nor spells either rule -- it reads the camera once to pick a
 * family and asks that family everything else.
 *
 * One indirect call per MODEL for the rule and one for the vertices, both
 * behind the cull that stops most models before they reach either. The raster
 * vtable already accepts one per FACE.
 */

/*
 * Project one model's vertices into the scene's scratch.
 *
 * The angles arrive already normalized and `model_mid_z` is the model's
 * camera-space centre depth; the caller derived both once. A slot writes
 * screen_vertices_*, writes orthographic_vertices_* when the model is
 * textured, and sets projection_bound_vertices when it bounded the block in
 * registers rather than leaving it to be swept.
 */
typedef void (*ToriDraw_ProjectionVerticesFn)(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z);

/*
 * One family's near-clip rule: the plane everything downstream clips against,
 * and whether any vertex of this model can reach it.
 *
 * `*out_near_plane_z` is what the raster, the HD path and the prepared kernels
 * read back out of the scene, so a family may raise the camera's plane but
 * must always publish one. `*out_may_clip` selects this family's `project`
 * slot and must be conservative in the "may clip" direction: a no-clip kernel
 * that meets a vertex behind the plane is a wrong projection, not a slow one.
 *
 * `bounds` is NULL when the model carries no bound; a family that cannot rule
 * out a near vertex without one must answer true.
 *
 * `scene` is read-only here and is passed so a rule can take the camera's
 * derived constants (the projection cot16) off the scene's prepared block
 * when that block was published for `camera`, instead of re-deriving them per
 * model.
 */
typedef void (*ToriDraw_ProjectionNearClipFn)(
    const struct ToriDraw_Scene* scene,
    const struct ToriDraw_Camera* camera,
    const struct ToriDraw_BoundsCylinder* bounds,
    const struct ProjectedVertex* center,
    int* out_near_plane_z,
    bool* out_may_clip);

/* Both slots and the rule are required. A NULL member is an incomplete family. */
struct ToriDraw_ProjectionFamily
{
    const char* name;
    ToriDraw_ProjectionNearClipFn near_clip;
    /* Indexed by the `may_clip` that `near_clip` returned. */
    ToriDraw_ProjectionVerticesFn project[2];
};

/* Both families are required. A kernel with a NULL family is incomplete. */
struct ToriDraw_ProjectionKernelVTable
{
    const struct ToriDraw_ProjectionFamily* perspective;
    const struct ToriDraw_ProjectionFamily* parallel;
};

struct ToriDraw_ProjectionKernel
{
    const char* name;
    ToriDraw_ProjectionKernelFn project;
    void* user_data;
    /* The two projection families this stage dispatches through. */
    const struct ToriDraw_ProjectionKernelVTable* vtable;
};

struct ToriDraw_FaceCullSortKernel
{
    const char* name;
    ToriDraw_FaceCullSortKernelFn sort;
    void* user_data;
    /* ToriDraw_FaceSortTrait bits. */
    uint32_t provides;
    uint32_t needs;
};

/*
 * A render call borrows the selected object, its complete vtable, and
 * user_data. They must remain alive and immutable until that call returns.
 * Flags describe pass-wide requirements and are read before the face loop.
 *
 * Rendering one scene recursively or concurrently is outside the contract:
 * the scene owns one startup-allocated projection/sort scratch set.
 */
struct ToriDraw_RasterKernelSD
{
    /* What this kernel is called, in the vocabulary its FILE is named in:
     * `sd.smooth_branching.u.c` is "smooth_branching". The stage is not part
     * of it -- which slot of a table holds the kernel already says SD or HD --
     * and neither is the walk, which ToriDraw_KernelLogConfiguration reads off
     * draw_model rather than trusting a name to stay true when the slot
     * changes. Required on every kernel the library hands out. */
    const char* name;
    /* This IS stage 3, and it is required. A kernel that only supplies the
     * four leaf callbacks NAMES ToriDraw_RasterWalkPerFace here; that naming
     * is also what tells the library the kernel has no traversal of its own.
     * The one NULL is the GPU kernel, which has no stage 3 at all and is
     * refused by every raster entry. */
    ToriDraw_RasterKernelSDModelFn draw_model;
    /* Read only by ToriDraw_RasterWalkPerFace. */
    const struct ToriDraw_RasterKernelSDVTable* vtable;
    void* user_data;
    uint32_t flags;

    /*
     * The depth-tested twin of this kernel: what it becomes when it is handed
     * a TORIDRAW_MODEL_FLAG_ZBUFFER model.
     *
     * The depth test is per pixel and lives in the face callbacks, so
     * honouring that flag means swapping the whole kernel, not setting a bit
     * on this one. The swap happens at the stage-3 entries, and this slot is
     * how a kernel states its own replacement rather than being recognised
     * there by address -- an address comparison can only ever know the
     * library's own kernels, and only until someone adds another.
     *
     * It is also where an attribute the depth family does not carry TODAY is
     * kept: smooth gouraud shares the plain depth vtable, because that family
     * has no separate smooth callback yet. The smooth painters name the smooth
     * twin anyway, so when the callback lands the wiring is already right and
     * no substitution site has to learn what smooth means.
     *
     * NULL on a kernel that already sets NEEDS_ZBUFFER -- it is its own twin
     * -- and on one with no depth-tested form, which the stage-3 entries
     * resolve to the stock depth painter. A named twin MUST set NEEDS_ZBUFFER
     * and MUST NOT name a twin of its own.
     *
     * Not a const pointer only because the substitution sites hand it back as
     * the kernel to render with; nothing writes through it.
     */
    struct ToriDraw_RasterKernelSD* zbuffered_variant;
};

struct ToriDraw_RasterKernelHD
{
    /* As the SD kernel's, and in the same vocabulary: `hd.zbuffered.u.c` is
     * "zbuffered". The two families share the names their variants share
     * because the slot, not the string, says which pipeline drew. */
    const char* name;
    const struct ToriDraw_RasterKernelHDVTable* vtable;
    void* user_data;
    uint32_t flags;
};

/* Process-lifetime, immutable built-in kernels. */
const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetDefault(void);

/* The prepared-camera kernels, with the portable ladder behind them. The
 * default, and what every caller got before the two were separable. */
const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetPrepared(void);

/* The portable SIMD/scalar ladder only. Same pixels; the A/B baseline, and
 * the honest choice for a caller that never publishes a prepared camera. */
const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetPortable(void);

/*
 * The stock software kernel a renderer holds: branching or scanline as
 * TORIDRAW_RASTER_SCANLINE / ToriDraw_RasterSetScanline decided, smooth or
 * not, with the default projection and face sort. Resolved at the call, so
 * take it once at renderer init, after ToriDraw_Init.
 */
const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetStock(bool smooth);

/*
 * The raster kernel a GPU table names: none. Its vtable is NULL -- the faces
 * go to a vertex buffer, never to a software span -- so every raster entry
 * refuses it. A GPU renderer holds ToriDraw_KernelGetGpu(), the table.
 */
const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetGpu(void);

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetBucket(void);

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetBitonicRadix(void);

/* Whichever of the two TORIDRAW_FACE_SORT / ToriDraw_FaceSortSetBitonicRadix
 * name. */
const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetDefault(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetBranching(void);

/* The same kernel with no whole-model door: draws face by face even when the
 * sort left a presorted stash. The batched walk's A/B baseline. */
const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetBranchingPerFace(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetScanline(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothBranching(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothScanline(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetZBuffered(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothZBuffered(void);

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetBranching(void);

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetScanline(void);

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetZBuffered(void);


/* ---- The kernel table ------------------------------------------------ */

/**
 * One object naming all three render stages.
 *
 * Drawing a model is project, then cull+sort, then raster, and each stage has
 * its own selectable subkernel. A renderer holds ONE of these and passes it to
 * each stage, so which projection, which face sort and which raster it runs is
 * a decision made once at init rather than three environment reads and a
 * couple of pointer comparisons buried in the library.
 *
 * Every slot but the raster is required, and none of them is defaulted: a
 * table that answers NULL to "which projection" or "which sort" is not a table
 * with a default, it is a table with a stage missing, and the entries assert
 * rather than quietly running the stock one. The prebaked tables are published
 * with both filled; a caller building its own names them.
 *
 * NOT every triple is coherent. The stages hand each other work through the
 * scene's scratch -- the sort's face order, the projection's camera-space
 * vertices, the y-ordered stash the batched raster walk reads -- and a table
 * is valid only when each consumer's requirement is met by a producer above
 * it. ToriDraw_KernelValidate answers that, once, before the first frame.
 *
 * A NULL raster is the one slot that carries a meaning rather than a hole:
 * stages 1 and 2 only. That is the GPU table, whose faces go to a vertex
 * buffer and never to a software span, and every raster entry refuses it.
 */
struct ToriDraw_Kernel
{
    const char* name;
    const struct ToriDraw_ProjectionKernel* projection;
    const struct ToriDraw_FaceCullSortKernel* face_sort;
    const struct ToriDraw_RasterKernelSD* raster;
    /*
     * Stage 3 for the HD pipeline, if this table has one.
     *
     * A table names ONE raster, and which of these two slots holds it says
     * which pipeline the table is for: the SD painter's four face classes, or
     * HD's six. Exactly one is non-NULL in every prebaked table, and the entry
     * points assert the one they need -- an SD entry handed an HD table finds
     * a NULL where its raster should be, and says so, rather than drawing six
     * classes' worth of faces through four callbacks.
     *
     * The GPU table is the one with neither, which is not a third pipeline but
     * the absence of stage 3 altogether.
     *
     * HD never asks the sort to presort. That is not a policy written here, it
     * follows: an HD raster kernel has no whole-model door to name -- no
     * draw_model slot exists on it at all -- so there is nothing that could
     * read the y-ordered stash, and kernel_table_wants_presort answers false
     * without being told about HD.
     */
    const struct ToriDraw_RasterKernelHD* raster_hd;
};

/**
 * How well a table fits a scene.
 *
 * Three outcomes and not two, because the interesting failure is not an error.
 * Asking for the presorted fast path and not getting it is ALREADY safe: the
 * sort records what it actually did in sm_face_xy_valid, the raster reads that
 * flag rather than the request, and the frame draws the same pixels down the
 * slower path. That behaviour should stay. What was missing is any way for a
 * caller who chose the presorting table BECAUSE it is faster to find out at
 * init that they are not getting it, instead of in a profile.
 */
enum ToriDraw_KernelFit
{
    /** Every stage gets what it needs. */
    TORIDRAW_KERNEL_FIT_OK = 0,
    /** Draws correctly, but some stage falls back to a slower path. */
    TORIDRAW_KERNEL_FIT_DEGRADED = 1,
    /** Would draw wrong, or trip an assert. Do not render with this. */
    TORIDRAW_KERNEL_FIT_INCOMPATIBLE = 2,
};

/**
 * Check a table against the scene it will draw into.
 *
 * `why` receives a static string naming the first problem found, or "ok". It
 * is never allocated and never NULL on return.
 *
 * Assumes ToriDraw_KernelEnsureScratch will be (or has been) called: a scene
 * that merely has not allocated a buffer yet is not a mismatch, whereas a
 * scene whose TIER cannot carry that buffer at all is.
 */
enum ToriDraw_KernelFit
ToriDraw_KernelValidate(
    const struct ToriDraw_Kernel* kernel,
    const struct ToriDraw_Scene* scene,
    const char** why);

/** The scratch groups this table's three stages read and write. */
uint32_t
ToriDraw_KernelScratchNeeds(
    const struct ToriDraw_Scene* scene,
    const struct ToriDraw_Kernel* kernel);

/**
 * Allocate whatever the table needs and the scene does not have.
 *
 * Call once, after ToriDraw_SceneNew and before the first frame. Does NOT
 * provision the z-buffer, which is sized from the viewport rather than the
 * scene tier -- use ToriDraw_SceneZBufferResize for that.
 */
bool
ToriDraw_KernelEnsureScratch(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_Kernel* kernel);

/** Branching or scanline as TORIDRAW_RASTER_SCANLINE / ToriDraw_RasterSetScanline
 *  decided: the table a renderer takes when it has no opinion beyond "the usual
 *  one". Resolved at the call, so take it once at init, after ToriDraw_Init. */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetStock(void);

/**
 * Take a table for a scene: validate, report, provision. The one door.
 *
 * Wraps a getter -- `ToriDraw_KernelTake(scene, ToriDraw_KernelGetSoftwarePainter())`
 * -- and returns the same table, so a renderer's one line of kernel setup is
 * also the line that checks it. Aborts on INCOMPATIBLE after printing which
 * condition failed; prints and continues on DEGRADED, because those pixels are
 * correct and only the speed is not what the table's name implies.
 *
 * Does not provision the z-buffer: that is sized from the viewport, not the
 * scene tier. See ToriDraw_SceneZBufferResize.
 */
const struct ToriDraw_Kernel*
ToriDraw_KernelTake(struct ToriDraw_Scene* scene, const struct ToriDraw_Kernel* table);

/**
 * Print what a table actually resolved to -- four lines on stderr, one per
 * stage:
 *
 *     toridraw: table      : software-painter
 *     toridraw: projection : prepared
 *     toridraw: face_sort  : bitonic+radix
 *     toridraw: raster SD  : branching (whole-model door)
 *
 * Every one of those is a runtime choice by the time a renderer holds the
 * table -- the scanline knob picked the table, the batch knob picked its
 * raster, the face-sort knob picked stage 2, and the ISA ladders decided what
 * is behind all three. Reading the selection code answers what SHOULD happen
 * on a target; this answers what did, on the box the frame is being drawn on,
 * which is the question a "why is this build slow" report actually needs.
 *
 * ToriDraw_KernelTake calls it the FIRST time it sees a given configuration
 * and not again, so a renderer gets this once for free and the DEGRADED line
 * that may follow reads as a note on the report above it. That test is there
 * because a take is not an initialization: ToriRS_Soft3D_Init resets the
 * renderer once per frame, so taking a table is a per-frame act and printing
 * at every one of them is sixty reports a second saying nothing new. A
 * configuration is the five pointers printed below, so a stage swapped behind
 * an unchanged table still reports.
 *
 * TORIDRAW_KERNEL_LOG=0 silences that. Calling this directly always prints,
 * seen before or not -- it is the caller's own report, and a caller that
 * asked for it is not asking to be told they already know.
 */
void
ToriDraw_KernelLogConfiguration(const struct ToriDraw_Kernel* table);

/*
 * The prebaked tables. Process-lifetime and immutable, like the subkernels
 * they name.
 */

/** Branching raster with its whole-model door, the bitonic+radix sort,
 *  prepared projection. The world painter, and the only table that reaches
 *  the batched walk -- and only on a small scene, in a build with the run
 *  assembly. */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwarePainter(void);

/** The scanline rasteriser. Its own vtable, so it can never take a presorted
 *  run; asking the sort to stash for it would be pure cost. */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareScanline(void);

/** Depth-resolved models. Stage 2 does not run at all: faces draw in model
 *  order and the buffer decides. */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareZBuffered(void);

/** D3D9 / GL / WebGL: stages 1 and 2 only, the order goes to a vertex upload.
 *  Never wants the stash -- nothing downstream walks a software span. */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetGpu(void);

/** Offline icon and sprite bakes: software raster, but per-face, because the
 *  baker reads tmp_face_order and nothing else. */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetSpriteBaker(void);

/** The HD painter: six face classes, branching or scanline as
 *  TORIDRAW_RASTER_SCANLINE decided. Sorts; never presorts, because no HD
 *  raster has a whole-model door. Resolved at the call, like the SD stock
 *  table -- take it once at init, after ToriDraw_Init. */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetHDPainter(void);

/** HD, depth-resolved. Stage 2 does not run: the buffer decides, and faces
 *  draw in model order. */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetHDZBuffered(void);

#endif
