#ifndef TORIDRAW_SCENE_H
#define TORIDRAW_SCENE_H

#include "toridraw_types.h"

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

size_t
ToriDraw_SceneSize(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size);

void
ToriDraw_ScenePrintSize(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size);

struct ToriDraw_TextureState*
ToriDraw_SceneTexState(struct ToriDraw_Scene* scene);

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

void
ToriDraw_SceneClear(struct ToriDraw_Scene* scene);

/** Free only the elements tagged with `pool`. Freed ids return to the shared
 *  free list; live elements in other pools are untouched. Clearing the
 *  STATIC pool also resets batch bookkeeping (batches are static-only). */
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

int
ToriDraw_SceneElementRemove(
    struct ToriDraw_Scene* scene,
    int element_id);

struct ToriDraw_SceneElement*
ToriDraw_SceneElementGet(
    struct ToriDraw_Scene* scene,
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

    if( !scene || element_id < 0 )
        return 0;
    if( !ToriDraw_SceneElementIsLive(scene, element_id) )
        return 0;
    el = ToriDraw_SceneElementGet(scene, element_id);
    if( !el || el->model.kind != TORIDRAWMK_MODEL )
        return 0;
    model = el->model.u.model.model;
    if( !model || !model->bounds_cylinder )
        return 0;
    h = -model->bounds_cylinder->min_y;
    return h > 0 ? h : 0;
}

/**
 * World-space XZ extent of a scene element's drawn geometry, in fine units.
 *
 * The element's own world position plus the model's axis-aligned XZ bounds —
 * i.e. where the polygons actually land, which is not the same rectangle as
 * the loc footprint the painter orders it by. A model carrying a draw-time yaw
 * (`world_position.yaw`, animated locs) is not axis-aligned in model space, so
 * the rotation-invariant `radius` is used on both axes instead: an
 * over-estimate, never an under-estimate.
 *
 * Returns false (and touches nothing) when the element is dead, is not a full
 * model, or has no bounds.
 */
static inline bool
ToriDraw_SceneElementDrawExtentXZ(
    struct ToriDraw_Scene* scene,
    int element_id,
    int* out_min_x,
    int* out_max_x,
    int* out_min_z,
    int* out_max_z)
{
    struct ToriDraw_SceneElement* el;
    struct ToriDraw_Model* model;
    struct ToriDraw_BoundsCylinder* bc;

    if( !scene || element_id < 0 )
        return false;
    if( !ToriDraw_SceneElementIsLive(scene, element_id) )
        return false;
    el = ToriDraw_SceneElementGet(scene, element_id);
    if( !el || el->model.kind != TORIDRAWMK_MODEL )
        return false;
    model = el->model.u.model.model;
    if( !model || !model->bounds_cylinder || model->vertex_count <= 0 )
        return false;

    bc = model->bounds_cylinder;
    if( el->world_position.yaw != 0 )
    {
        *out_min_x = el->world_position.x - bc->radius;
        *out_max_x = el->world_position.x + bc->radius;
        *out_min_z = el->world_position.z - bc->radius;
        *out_max_z = el->world_position.z + bc->radius;
        return true;
    }
    *out_min_x = el->world_position.x + bc->min_x;
    *out_max_x = el->world_position.x + bc->max_x;
    *out_min_z = el->world_position.z + bc->min_z;
    *out_max_z = el->world_position.z + bc->max_z;
    return true;
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
