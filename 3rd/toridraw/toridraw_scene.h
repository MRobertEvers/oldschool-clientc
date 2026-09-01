#ifndef TORIDRAW_SCENE_H
#define TORIDRAW_SCENE_H

#include "toridraw_types.h"
#include "toridraw_element_id.h"
#include <assert.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIDRAW_SCENE_FULL          0u
#define TORIDRAW_SCENE_SMALL         (1u << 0)
#define TORIDRAW_SCENE_LAZY_TEXTURES (1u << 1)
/**
 * Allocate 16,384 face-sort depth levels instead of the reference-era 1,500.
 *
 * This is independent of the vertex/face scratch tier below.  Large imported
 * models can fit HIGH_8K's projection and face-order arrays while still having
 * a bounding-sphere diameter greater than 1,500; without this flag their
 * otherwise valid faces fall outside the depth table and are omitted.  In a
 * full scene the dense sorter adds about 14.56 MiB versus the reference depth.
 */
#define TORIDRAW_SCENE_DEPTH_16K     (1u << 2)

/**
 * Carry a z-buffer scratch, so models tagged TORIDRAW_MODEL_FLAG_ZBUFFER
 * resolve their faces per pixel instead of by face order alone.
 *
 * One buffer for the scene, screen sized, reused by every model that opts in —
 * each resets it before drawing, which is what keeps one model's depths from
 * reaching the next. It cannot be allocated here because the viewport is not
 * known until a model is drawn, so this flag is permission plus intent: the
 * first raster of a model that opts in sizes the buffer to that viewport (and
 * regrows it if a later viewport is larger). Callers that would rather pay the
 * allocation up front, or want to know it succeeded before a frame is on the
 * line, call ToriDraw_SceneZBufferResize instead; the flag is not required for
 * that, and a scene that has a buffer honours the model flag either way.
 *
 * Cost is `stride * rows * sizeof(torizdepth_t)` — 2 bytes per pixel where the
 * toolchain has a real 16-bit float, 4 otherwise (graphics/zdepth.h).
 */
#define TORIDRAW_SCENE_MODEL_ZBUFFER (1u << 3)

/**
 * Capacity tier for a scene's reusable model-render scratch buffers.
 *
 * The tier is deliberately separate from TORIDRAW_SCENE_SMALL: that flag
 * selects the compact CSR face sorter, whereas this enum selects the model's
 * vertex/face capacity.  Depth capacity is independently selected with
 * TORIDRAW_SCENE_DEPTH_16K.  HIGH_8K preserves the post-QBD vertex/face limits.
 */
enum ToriDraw_ScratchBufferSize
{
    TORIDRAW_SCRATCH_BUFFER_LOW_2K,
    TORIDRAW_SCRATCH_BUFFER_MED_4K,
    TORIDRAW_SCRATCH_BUFFER_HIGH_8K,
    TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K,
    TORIDRAW_SCRATCH_BUFFER_SIZE_COUNT,
};

bool
ToriDraw_SceneGraphInit(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneGraphShutdown(struct ToriDraw_Scene* scene);

/** Allocate a scene with an explicit reusable model-render scratch tier. */
struct ToriDraw_Scene*
ToriDraw_SceneNew(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size);

void
ToriDraw_SceneFree(struct ToriDraw_Scene* scene);

/** Version of model/sprite/font registry state observable by UITree. */
uint64_t
ToriDraw_SceneUIAssetRevision(struct ToriDraw_Scene const* scene);

size_t
ToriDraw_SceneSize(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size);

void
ToriDraw_ScenePrintSize(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size);

struct ToriDraw_RasterKernelSD;

struct ToriDraw_TextureState*
ToriDraw_SceneTexState(struct ToriDraw_Scene* scene);

/**
 * The texture map, or NULL when this scene has no texture state.
 *
 * Unlike ToriDraw_SceneTexState this never BUILDS one, which is what the
 * raster context needs: it takes the map for every model, textured or not, so
 * building on demand there puts a 16 KB calloc behind an untextured icon and
 * makes an arena scene -- which has no allocator -- unable to draw one at all.
 *
 * Handing NULL onward is safe: the two raster sites that resolve a texture
 * test the map alongside the id, and a NULL map takes the same road as an id
 * the map does not hold -- the face is skipped and the miss is tallied. That
 * is a state this engine already supports and reports, not a caller error: a
 * lazy-textures scene meets its first textured model before its first texture
 * whenever the cache load is asynchronous, which is always.
 */
struct ToriDraw_TextureMap*
ToriDraw_SceneTextureMapOrNull(struct ToriDraw_Scene* scene);

/* ---- Kernel scratch ------------------------------------------------- */

/**
 * The scratch groups a kernel's three stages read and write.
 *
 * A scene allocates by tier and by the SMALL flag, which is a decision made at
 * ToriDraw_SceneNew, before anyone has chosen a kernel. These bits let the two
 * be reconciled afterwards: ask a kernel what it needs, ask the scene what it
 * has, and allocate the difference.
 *
 * They also make one long-standing trap visible. The bitonic+radix sort's key
 * arrays and the batched walk's y-ordered stash are small-tier scratch, so on
 * a full scene that face-sort kernel silently runs the same bucket sort the
 * bucket kernel runs, and a presorted raster never sees a presorted face. That
 * is safe -- sm_face_xy_valid records what the sort actually did, and the walk
 * reads the flag rather than the request -- but until now a caller had no way
 * to find out except by profiling. ToriDraw_SceneHasScratch answers it.
 */
enum ToriDraw_SceneScratch
{
    /** Projected vertex arrays. Every kernel, always. */
    TORIDRAW_SCENE_SCRATCH_VERTICES = 1u << 0,
    /** tmp_face_order: the back-to-front order stage 3 walks. */
    TORIDRAW_SCENE_SCRATCH_FACE_ORDER = 1u << 1,
    /** The full scene's dense depth_levels x depth_stride bucket table. */
    TORIDRAW_SCENE_SCRATCH_BUCKET_SORT = 1u << 2,
    /** The small scene's CSR sorter arrays, sized off max_faces. */
    TORIDRAW_SCENE_SCRATCH_CSR_SORT = 1u << 3,
    /** sm_sort_keys / sm_sort_tmp: the bitonic+radix sort's composite keys. */
    TORIDRAW_SCENE_SCRATCH_BITONIC_RADIX_KEYS = 1u << 4,
    /** sm_face_x4 / y4: the y-ordered stash the batched raster walk reads. */
    TORIDRAW_SCENE_SCRATCH_PRESORT_XY = 1u << 5,
};

/**
 * What this kernel will read and write, given this scene.
 *
 * Depends on both: the bitonic+radix face sort needs BITONIC_RADIX_KEYS only
 * where the scene runs the CSR sorter, and the presort stash is only ever
 * asked for by the stock branching raster, which is the batched walk's only
 * door.
 *
 * `kernel` may be NULL, meaning the stock defaults.
 */
uint32_t
ToriDraw_SceneKernelScratchNeeds(
    const struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernelSD* kernel);

/** Which groups are currently allocated. */
uint32_t
ToriDraw_SceneScratchResident(const struct ToriDraw_Scene* scene);

/** Whether every group in `needs` is resident. */
bool
ToriDraw_SceneHasScratch(const struct ToriDraw_Scene* scene, uint32_t needs);

/**
 * Allocate every group in `needs` that is not already resident.
 *
 * Idempotent, and never frees: a scene that has been prepared for two kernels
 * keeps the union of what they need, so switching between them costs nothing.
 * Returns false only when a group cannot be satisfied for this scene at all.
 */
bool
ToriDraw_SceneEnsureScratch(struct ToriDraw_Scene* scene, uint32_t needs);

/**
 * The pair above, applied: prepare `scene` for `kernel`.
 *
 * Call once, after ToriDraw_SceneNew and before the first frame, with the
 * kernel the renderer intends to hold. Does NOT provision the z-buffer, which
 * is sized from the viewport rather than the scene tier -- use
 * ToriDraw_SceneZBufferResize for that.
 */
bool
ToriDraw_SceneEnsureKernelScratch(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernelSD* kernel);

/* The scene's z-buffer scratch (TORIDRAW_SCENE_MODEL_ZBUFFER). */

/**
 * Ensure the z-buffer covers `stride` x `rows`, allocating or growing it.
 * Idempotent, and never shrinks — a scene that has drawn into a large viewport
 * keeps the capacity for the next one. Returns false only on allocation
 * failure, which leaves any existing buffer intact and usable.
 *
 * `stride` must be the viewport's pixel stride, not its width: the z-buffer is
 * indexed with the same offsets as the frame buffer.
 */
bool
ToriDraw_SceneZBufferResize(
    struct ToriDraw_Scene* scene,
    int stride,
    int rows);

/** Release the z-buffer. Models that opt in then draw as if they had not, so
 *  this is a way to turn the feature off without touching the models. */
void
ToriDraw_SceneZBufferFree(struct ToriDraw_Scene* scene);

/** Whether a z-buffer large enough for `stride` x `rows` is resident. */
bool
ToriDraw_SceneHasZBuffer(
    const struct ToriDraw_Scene* scene,
    int stride,
    int rows);

/* Asset registry: models */

void
ToriDraw_SceneModelAdd(
    struct ToriDraw_Scene* scene,
    int model_id,
    struct ToriDraw_ModelHandle model);

struct ToriDraw_ModelHandle
ToriDraw_SceneModelGet(
    struct ToriDraw_Scene* scene,
    int model_id);

bool
ToriDraw_SceneModelHas(
    struct ToriDraw_Scene* scene,
    int model_id);

struct ToriDraw_ModelHandle
ToriDraw_SceneModelRemove(
    struct ToriDraw_Scene* scene,
    int model_id);

void
ToriDraw_SceneModelsClearAll(struct ToriDraw_Scene* scene);

/* Asset registry: animations */

void
ToriDraw_SceneAnimationAdd(
    struct ToriDraw_Scene* scene,
    int anim_id,
    struct ToriDraw_Animation* animation);

struct ToriDraw_Animation*
ToriDraw_SceneAnimationGet(
    struct ToriDraw_Scene* scene,
    int anim_id);

bool
ToriDraw_SceneAnimationHas(
    struct ToriDraw_Scene* scene,
    int anim_id);

/* Asset registry: textures */

void
ToriDraw_SceneSetTexture(
    struct ToriDraw_Scene* scene,
    int id,
    struct ToriDraw_Texture* texture);

/* Asset registry: sprites */

void
ToriDraw_SceneSpriteAdd(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_Sprite** sprites,
    int count);

/** Drop a sprite entry: frees the sprites and emits the unload event. Adding
 *  over an id already does this; this is for evicting without a replacement. */
void
ToriDraw_SceneSpriteRemove(
    struct ToriDraw_Scene* scene,
    int element_id);

struct ToriDraw_Sprite**
ToriDraw_SceneSpriteGet(
    struct ToriDraw_Scene* scene,
    int element_id,
    int* out_count);

bool
ToriDraw_SceneSpriteHas(
    struct ToriDraw_Scene* scene,
    int element_id);

/* Asset registry: fonts */

void
ToriDraw_SceneFontAdd(
    struct ToriDraw_Scene* scene,
    int font_id,
    struct ToriDraw_Font* font);

struct ToriDraw_Font*
ToriDraw_SceneFontGet(
    struct ToriDraw_Scene* scene,
    int font_id);

bool
ToriDraw_SceneFontHas(
    struct ToriDraw_Scene* scene,
    int font_id);

void
ToriDraw_SceneCacheFontSet(
    struct ToriDraw_Scene* scene,
    int cache_font_id,
    struct ToriDraw_Font* font);

struct ToriDraw_Font*
ToriDraw_SceneCacheFontGet(
    struct ToriDraw_Scene* scene,
    int cache_font_id);

/* Asset registry: sounds
 *
 * Decoded audio clips, held exactly like sprites and fonts. Adding emits a
 * TORIDRAW_EVENT_SOUND_LOAD; replacing or removing emits an UNLOAD first, so a
 * backend holding a copy is always told before the original goes away. The
 * scene owns the clip and frees it on shutdown.
 */

/** Wrap owned PCM in a sound. Takes ownership of `samples` (and frees it if the
 *  sound cannot be built, so the caller has one ownership rule). */
struct ToriDraw_Sound*
ToriDraw_SoundNew(
    int16_t* samples,
    int sample_count,
    int sample_rate,
    int loop_start,
    int loop_end,
    bool ping_pong,
    int queue_delay);

void
ToriDraw_SoundFree(struct ToriDraw_Sound* sound);

void
ToriDraw_SceneSoundAdd(
    struct ToriDraw_Scene* scene,
    int sound_id,
    struct ToriDraw_Sound* sound);

struct ToriDraw_Sound*
ToriDraw_SceneSoundGet(
    struct ToriDraw_Scene* scene,
    int sound_id);

bool
ToriDraw_SceneSoundHas(
    struct ToriDraw_Scene* scene,
    int sound_id);

/** Drop a clip: emits the unload event, then frees it. */
void
ToriDraw_SceneSoundRemove(
    struct ToriDraw_Scene* scene,
    int sound_id);

/** Re-emit a load event for every resident clip — for a backend that lost its
 *  table (a device re-open, a page reload). */
void
ToriDraw_SceneSoundsReemitLoads(struct ToriDraw_Scene* scene);

/* Scene elements */

/* Element clear groups. STATIC is the world-builder's terrain/scenery set —
 * torn down and rebuilt wholesale on every map rebuild (batches belong to
 * this pool). DYNAMIC holds app-spawned entity elements (players, npcs,
 * ground objs, projectiles) whose element ids must stay stable across a
 * rebuild. ToriDraw_SceneClear still frees both. */
#define TORIDRAW_SCENE_POOL_STATIC 0
#define TORIDRAW_SCENE_POOL_DYNAMIC 1

/*
 * One scene, many world views. Element ids are scene-global and the painter
 * stores bare ids, so a client that draws several worlds at once (the OSRS
 * sailing views: the mainland plus up to 15 boat decks — src/world/worldview.h)
 * keeps them all in ONE scene and tells them apart by pool: every view owns a
 * STATIC/DYNAMIC pair, so a boat's rebuild frees its own deck and sweeps its
 * own entities and the mainland's elements never move.
 *
 * View 0's pair IS the historic {STATIC, DYNAMIC}, so a single-world client is
 * byte-for-byte what it was. Only view 0's STATIC clear touches the retained
 * batch arena, which is why only view 0's geometry may be batched — see
 * ToriDraw_SceneClearPool.
 */
#define TORIDRAW_SCENE_POOL_VIEW_STRIDE 2
#define TORIDRAW_SCENE_POOL_STATIC_VIEW(view_id)                                                   \
    ((view_id)*TORIDRAW_SCENE_POOL_VIEW_STRIDE + TORIDRAW_SCENE_POOL_STATIC)
#define TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(view_id)                                                  \
    ((view_id)*TORIDRAW_SCENE_POOL_VIEW_STRIDE + TORIDRAW_SCENE_POOL_DYNAMIC)
/** Views a pool pair can be minted for: the tag is one byte per element. */
#define TORIDRAW_SCENE_POOL_VIEW_MAX (256 / TORIDRAW_SCENE_POOL_VIEW_STRIDE)

void
ToriDraw_SceneClear(struct ToriDraw_Scene* scene);

/** Free only the elements tagged with `pool`. Freed ids return to the shared
 *  free list; live elements in other pools are untouched. Clearing the
 *  STATIC pool (view 0's static half) also resets batch bookkeeping and drops
 *  the retained batch arena — batches are that pool's alone; every other pool
 *  is unloaded element by element instead. */
void
ToriDraw_SceneClearPool(
    struct ToriDraw_Scene* scene,
    int pool);

int
ToriDraw_SceneElementAdd(struct ToriDraw_Scene* scene);

/** ToriDraw_SceneElementAdd tags TORIDRAW_SCENE_POOL_STATIC; this variant
 *  picks the pool explicitly. */
int
ToriDraw_SceneElementAddPool(
    struct ToriDraw_Scene* scene,
    int pool);

/**
 * Retag a live element into another pool. The element keeps its id, its model,
 * its position and every other field — only which clear/sweep owns it changes.
 *
 * This is what an entity crossing a view boundary needs (the OSRS sailing case:
 * a player walking onto a boat's deck, SAILING_PLAN C5). Free-and-reallocate
 * cannot serve there: the element id is stored on the entity record and on the
 * painter's scenery chains, and the model would have to be rebuilt for what is
 * a bookkeeping move.
 */
void
ToriDraw_SceneElementSetPool(
    struct ToriDraw_Scene* scene,
    int element_id,
    int pool);

/** The pool tag of a live element, or -1 when `element_id` names no element. */
int
ToriDraw_SceneElementPool(
    struct ToriDraw_Scene* scene,
    int element_id);

int
ToriDraw_SceneElementRemove(
    struct ToriDraw_Scene* scene,
    int element_id);

struct ToriDraw_SceneElement*
ToriDraw_SceneElementGet(
    struct ToriDraw_Scene* scene,
    int element_id);

/*
 * Warm the caches for an element a caller is ABOUT to get, in two stages.
 *
 * An element is reached through the pool's node table and then the node's
 * data pointer: two dependent loads, and in painter order both are cold --
 * consecutive draws are neighbours in depth, not in memory. A frame that
 * walks two thousand of them spends its iterator waiting on exactly those
 * loads. The stages let a loop pipeline them: prefetch the NODE for the
 * command two ahead, and the DATA for the one ahead (whose node the previous
 * iteration fetched). Neither stalls, neither reads past what the list
 * holds, and an id that is not live is simply ignored.
 */
void
ToriDraw_SceneElementPrefetchNode(
    const struct ToriDraw_Scene* scene,
    int element_id);
void
ToriDraw_SceneElementPrefetchData(
    const struct ToriDraw_Scene* scene,
    int element_id);
/* The same, warming both cache lines of the element: the emit reads the model
 * handle near the front and pick_aabb at the very end. */
void
ToriDraw_SceneElementPrefetchDataBothLines(
    const struct ToriDraw_Scene* scene,
    int element_id);
/*
 * Warm the lines the cull and the projection read off the element's MODEL:
 * the leading struct (counts, vertex and face array pointers) and the bounds
 * cylinder. Reads the element data, so the element's own line must already
 * be warm -- issue this a step later in the pipeline than PrefetchData.
 */
void
ToriDraw_SceneElementPrefetchModel(
    const struct ToriDraw_Scene* scene,
    int element_id);
/*
 * One step later again: the first line of each vertex axis and face index
 * array, which the projection and the face sort read next. Reads the model
 * struct, so PrefetchModel must have gone out a step earlier.
 */
void
ToriDraw_SceneElementPrefetchArrays(
    const struct ToriDraw_Scene* scene,
    int element_id);

bool
ToriDraw_SceneElementIsLive(
    struct ToriDraw_Scene* scene,
    int element_id);

/**
 * Occlusion sample height above ground for a scene element — reference
 * Model.minY / bottomY (= max(-vertexY)). Returns 0 when the element is dead,
 * the handle is not a full model, or bounds are missing.
 */
static inline int
ToriDraw_SceneElementOcclusionHeight(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* el;
    struct ToriDraw_Model* model;
    int h;

    assert(scene);
    if( element_id < 0 )
        return 0;
    if( !ToriDraw_SceneElementIsLive(scene, element_id) )
        return 0;
    el = ToriDraw_SceneElementGet(scene, element_id);
    if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) )
        return 0;
    model = el->model.u.model.model;
    if( !model || !model->has_bounds_cylinder )
        return 0;
    h = -model->bounds_cylinder.min_y;
    return h > 0 ? h : 0;
}

int
ToriDraw_SceneElementSlotCount(struct ToriDraw_Scene* scene);

/** Ids of elements carrying a non-external animation seq, so the per-cycle tick
 *  visits only those instead of scanning the (mostly static) element pool.
 *  Rebuilt lazily; the returned pointer is owned by the scene and is invalidated
 *  by the next call. Entries are a hint — callers must still re-check liveness
 *  and anim_seq_id. Returns NULL with *out_count 0 if the list cannot be built. */
int const*
ToriDraw_SceneAnimatedElements(
    struct ToriDraw_Scene* scene,
    int* out_count);

/** Force the next ToriDraw_SceneAnimatedElements call to rebuild. Needed when a
 *  caller mutates anim_seq_id or anim_external on an element directly. */
void
ToriDraw_SceneAnimListInvalidate(struct ToriDraw_Scene* scene);

/** Mark this element's sequence as looping — at the end of the frame list the
 *  frame wraps to 0 instead of terminating the sequence. Projectiles and map
 *  spotanims want this; DynamicObject locs do not. Survives a later
 *  SetAnimationSeq/SetAnimation bind, so it can be set at spawn while the
 *  sequence is still loading. */
void
ToriDraw_SceneElementSetAnimLoop(
    struct ToriDraw_Scene* scene,
    int element_id,
    bool loop);

/**
 * This element's model, made private first if other elements share it.
 *
 * The one door through which a placed model may be written. A loc that stands
 * in a scene hundreds of times holds ONE model (see
 * ToriDraw_Model::shared_owner), so editing it through
 * ToriDraw_SceneElementGet would edit every placement; ask here instead and
 * the element gets a copy of its own, which it then owns outright.
 *
 * Returns NULL when the element is dead or does not carry a full model -- both
 * are ordinary states for a caller that is chasing an element it did not
 * create. The returned pointer belongs to the element, not the caller.
 */
struct ToriDraw_Model*
ToriDraw_SceneElementModelForWrite(
    struct ToriDraw_Scene* scene,
    int element_id);

/**
 * The scene's shared-model store, created on the first ask.
 *
 * One per scene, and the only place a shared model may live: the models in it
 * are held by this scene's elements, so its lifetime is the scene's. Built
 * lazily because a scene that draws no world geometry -- a widget model view,
 * an icon raster -- never has two placements of anything and would otherwise
 * pay for a table it never reads. See toridraw_shared_model.h.
 */
struct ToriDraw_SharedModelStore*
ToriDraw_SceneSharedModels(struct ToriDraw_Scene* scene);

/** The scene's store of lendable face buffers, built on the first ask. Separate
 *  from the whole-model store because they hold different types with different
 *  lifetimes; see ToriDraw_SharedFacesStoreBorrow. */
struct ToriDraw_SharedFacesStore*
ToriDraw_SceneSharedFaces(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneElementSetModel(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model);

void
ToriDraw_SceneElementSetAnimation(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary);

void
ToriDraw_SceneElementSetAnimationSeq(
    struct ToriDraw_Scene* scene,
    int element_id,
    int seq_id);

/* Secondary (walk) track for the walkmerge blend. seq_id <= 0 clears the
 * track (and the bound secondary animation). Bind the animation itself with
 * ToriDraw_SceneElementSetAnimation(..., primary=false). */
void
ToriDraw_SceneElementSetSecondaryAnimationSeq(
    struct ToriDraw_Scene* scene,
    int element_id,
    int seq_id);

/* Entity-driven frame state for both tracks (world sim stepping). */
void
ToriDraw_SceneElementSetAnimFrames(
    struct ToriDraw_Scene* scene,
    int element_id,
    int primary_frame,
    int secondary_frame);

/*
 * Forget the pose the element's model holds, so the next
 * ToriDraw_SceneElementApplyAnimation re-poses even for a frame it has
 * already produced. The scene's own mutators call this; it is public for the
 * caller that edits the element's model or animation fields directly.
 */
void
ToriDraw_SceneElementPoseInvalidate(
    struct ToriDraw_Scene* scene,
    int element_id);

/*
 * Pose the element's model for `frame` of its primary (or secondary) track.
 * A request identical to the pose the model already holds is skipped
 * (TORIDRAW_ANIM_SKIP_SAME=0 re-poses every time) -- see the posed_* fields
 * on ToriDraw_SceneElement for what "identical" means.
 */
void
ToriDraw_SceneElementApplyAnimation(
    struct ToriDraw_Scene* scene,
    int element_id,
    bool primary,
    int frame);

void
ToriDraw_SceneElementSetPosition(
    struct ToriDraw_Scene* scene,
    int element_id,
    int x,
    int y,
    int z,
    int yaw);

void
ToriDraw_SceneElementSetPositionPitchYaw(
    struct ToriDraw_Scene* scene,
    int element_id,
    int x,
    int y,
    int z,
    int pitch,
    int yaw);

/* Batch building */

void
ToriDraw_SceneBatchBegin(struct ToriDraw_Scene* scene);

struct ToriDraw_SceneBatchElementHandle
ToriDraw_SceneBatchAddElement(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneBatchEnd(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneBatchClear(
    struct ToriDraw_Scene* scene,
    int batch_id);

void
ToriDraw_SceneBatchElementAddPose(
    struct ToriDraw_Scene* scene,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle baked);

/* Events and frame lifecycle */

void
ToriDraw_SceneSpritesReemitLoads(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneFontsReemitLoads(struct ToriDraw_Scene* scene);

struct ToriDraw_EventQueue*
ToriDraw_SceneEvents(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneFrameEnd(struct ToriDraw_Scene* scene);

#endif
