#ifndef SRC_PLATFORM_PLATFORM_RENDERER_GLES2_DUALCORE_STAGE_H
#define SRC_PLATFORM_PLATFORM_RENDERER_GLES2_DUALCORE_STAGE_H

/*
 * The dual-core lane's model stage: the CPU half of gles2_draw_model, with
 * no GL in it.
 *
 * For one DRAW_MODEL command the GLES2 renderer poses the model, culls it,
 * projects it, hit-tests it against the armed mouse point and sorts its
 * faces, and only THEN touches the GPU. Every one of those steps is a pure
 * function of (command, scene, pass camera) that writes nothing but the
 * scene's per-model scratch -- so given a second scratch
 * (ToriDraw_SceneScratchViewNew) it can run on the second core, ahead of the
 * draw, and hand its results across.
 *
 * This unit is that function and the arena its results land in. It is
 * GL-free on purpose: the host builds it for the parity test
 * (platform/test/gles2_dualcore_stage_test.c), which runs the stage on a
 * scratch view next to the same stage on the scene itself and demands the
 * same answers. The thread, the hand-over and the renderer glue live in
 * platform_renderer_gles2_dualcore.c.
 *
 * Producer / consumer contract, one frame at a time:
 *
 *   - the producer appends one GLES2DualCoreStageResult per DRAW_MODEL
 *     command, in the order the frame emits them, and publishes each by
 *     bumping `ready` (release);
 *   - the consumer (the draw, on the other core) reads result i once
 *     `ready` > i (acquire); face orders are appended to `orders` before
 *     the result naming them is published, so they are visible with it;
 *   - when the producer stops early -- storage ran out -- it sets
 *     `finished` to GLES2_DUALCORE_STAGE_EXHAUSTED after its last publish;
 *     the consumer then computes the rest of the frame itself. When it
 *     finishes normally it sets GLES2_DUALCORE_STAGE_DONE. Nothing in the
 *     arena is reused, freed or resized while a frame is in flight;
 *   - BALANCE: a model is CLAIMED before it is computed, by whichever thread
 *     reaches it first -- and the producer HANDS OFF a slot to the consumer
 *     whenever the consumer is right behind it (its published `consumer_index`
 *     is within GLES2_DUALCORE_STAGE_LEAD of the producer's slot). Measured
 *     without the hand-off: the two ran in lockstep, the producer one slot
 *     ahead, and the consumer spent a quarter of its frame waiting on the
 *     slot the producer had just started. With it the producer keeps a
 *     lead and the consumer stages every slot it is handed instead. The producer claims model j as it comes to it; the
 *     consumer, finding result i not yet published, claims i for itself and
 *     computes it on the scene's own bench rather than wait. A producer that
 *     loses a claim publishes a placeholder (`taken_by_draw`) so the count
 *     stays paired. Neither thread ever waits on the other for a model
 *     nobody has started, which is what keeps two unequal halves of a frame
 *     from serialising on the slower one.
 */

#include "render/torirs_render.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Scene;
struct ToriDraw_Kernel;

struct GLES2DualCoreStageResult
{
    /** TORIDRAW_CULL_VISIBLE or the rejection. Nothing else below is
     *  meaningful for a rejected model. */
    int32_t cull;
    /** Faces in the order (painter: all of them; depth path: the count the
     *  sort returned, for a model with blended faces). */
    int32_t sorted_face_count;
    /** scene->projected_vertex.z after the projection. */
    int32_t projected_depth;
    /** Where in GLES2DualCoreStageArena::orders the order starts. */
    uint32_t order_offset;
    /** The pick test's answer for the armed mouse point. */
    uint8_t pick_hit;
    /** Whether the model was sorted at all (the depth path leaves a model
     *  with no blended face unsorted). */
    uint8_t sorted;
    /** The consumer claimed this model and computed it itself; the rest of
     *  the result is a placeholder the consumer never reads. */
    uint8_t taken_by_draw;
};

enum GLES2DualCoreStageClaim
{
    GLES2_DUALCORE_CLAIM_FREE = 0,
    GLES2_DUALCORE_CLAIM_PRODUCER = 1,
    GLES2_DUALCORE_CLAIM_CONSUMER = 2,
};

enum GLES2DualCoreStageClaimResult
{
    /** The producer owns the model: compute it. */
    GLES2_DUALCORE_CLAIMED,
    /** The consumer got there first: publish a placeholder and move on. */
    GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW,
    /** No result slot left: the producer stops for the frame. */
    GLES2_DUALCORE_CLAIM_EXHAUSTED,
};

enum GLES2DualCoreStageFinished
{
    GLES2_DUALCORE_STAGE_RUNNING = 0,
    GLES2_DUALCORE_STAGE_DONE = 1,
    GLES2_DUALCORE_STAGE_EXHAUSTED = 2,
};

struct GLES2DualCoreStageArena
{
    struct GLES2DualCoreStageResult* results;
    uint32_t result_capacity;
    /** The producer's private count; `ready` is the published one. */
    uint32_t result_count;
    /** One claim word per result slot, GLES2DualCoreStageClaim; zeroed
     *  between frames. */
    atomic_uint* claims;

    int32_t* orders;
    uint32_t order_capacity;
    uint32_t order_count;

    /* The two words the consumer polls, each on its own cache line: the
     * producer's private counters above must not share a line with what
     * the other core is spinning on. */
    _Alignas(64) atomic_uint ready;
    _Alignas(64) atomic_uint finished;
    /** The consumer's next take index, published (relaxed) at every take, so
     *  the producer can tell how close behind it is. Its own line too. */
    _Alignas(64) atomic_uint consumer_index;
    /** The hand-off distance (see GLES2_DUALCORE_STAGE_LEAD_DEFAULT); set
     *  between frames by whoever owns the arena. */
    uint32_t lead;

    /** Set by the producer when it stopped for want of storage; read by
     *  BeginFrame to grow before the next frame. */
    bool exhausted;
    /** How many frames ended exhausted. */
    uint32_t exhausted_frames;
};

void
GLES2DualCoreStageArena_Init(struct GLES2DualCoreStageArena* arena);

void
GLES2DualCoreStageArena_Free(struct GLES2DualCoreStageArena* arena);

/**
 * Make the arena ready for a frame of at most `model_commands` DRAW_MODEL
 * commands. Between frames only -- it may reallocate. Grows the order store
 * when the previous frame ran out of it.
 */
void
GLES2DualCoreStageArena_BeginFrame(
    struct GLES2DualCoreStageArena* arena,
    uint32_t model_commands);

/* The default for GLES2DualCoreStageArena::lead: how far ahead of the
 * consumer the producer must be to keep a slot for itself. At or under it,
 * it hands the slot to the consumer instead. Measured on the phone: with 1
 * the consumer (whose gather is faster than the producer's stage) still
 * caught the producer mid-model on 13% of slots and waited ~1.5 ms a frame. */
#define GLES2_DUALCORE_STAGE_LEAD_DEFAULT 2u

/**
 * Producer: claim the next model (the one result_count names) for itself,
 * or hand it to the consumer when the consumer is within
 * GLES2_DUALCORE_STAGE_LEAD slots. Not part of ComputeModel so the producer
 * can stop, skip or compute on the answer.
 */
enum GLES2DualCoreStageClaimResult
GLES2DualCoreStageArena_ClaimNextForProducer(struct GLES2DualCoreStageArena* arena);

/** Producer: the consumer claimed this model; publish the placeholder that
 *  keeps result i paired with command i. */
void
GLES2DualCoreStageArena_PublishTakenByDraw(struct GLES2DualCoreStageArena* arena);

/**
 * Consumer: claim result `index` for itself. True means the consumer
 * computes that model and the producer will publish a placeholder for it;
 * false means the producer already owns it and its result will be
 * published. `index` must be below the result capacity.
 */
bool
GLES2DualCoreStageArena_ClaimForConsumer(
    struct GLES2DualCoreStageArena* arena,
    uint32_t index);

/**
 * The inputs that hold for a whole 3D pass. `camera` lives HERE rather than
 * being pointed at: the prepared projection block is keyed on the camera's
 * address, so the address handed to ToriDraw_ScenePrepareProjectionCamera
 * must be the one every projection of the pass is called with.
 */
struct GLES2DualCoreStageContext
{
    /** The scratch view the stage projects and sorts on. */
    struct ToriDraw_Scene* scene;
    const struct ToriDraw_Kernel* kernel;
    struct ToriDraw_ViewPort view_port;
    struct ToriDraw_Camera camera;
    bool pick_enabled;
    int pick_mouse_x;
    int pick_mouse_y;
    /** The depth-buffered world pass: sort only a model with blended faces. */
    bool zbuffer;
    bool in_pass;
};

/** Take the pass camera and viewport from a BEGIN_3D command and publish the
 *  prepared camera block on the view. */
void
GLES2DualCoreStage_BeginPass(
    struct GLES2DualCoreStageContext* context,
    const struct ToriRS_RenderCommand_Begin3D* command);

/** Unpublish the prepared camera block. */
void
GLES2DualCoreStage_EndPass(struct GLES2DualCoreStageContext* context);

/*
 * Crash breadcrumbs: the stage writes where it is -- which command, which
 * step -- so a fault on the worker can be read back from a signal handler
 * (platform_renderer_gles2_dualcore.c). Plain volatile ints; the reader is a
 * handler on the same thread. Always on: the stores are a few words per
 * model. NOTE (2026-09-02): a worker fault on OSRS239 with plugins off, which
 * every build before these stores reproduced within a minute, has not
 * reproduced since they went in. That is the signature of a timing-dependent
 * race, not of a fix; the root cause is still open (see the memory note
 * gles2-dualcore-lane). Do not remove these on the grounds that nothing
 * reads them.
 */
enum GLES2DualCoreStageStep
{
    GLES2_DUALCORE_STEP_IDLE = 0,
    GLES2_DUALCORE_STEP_POSE,
    GLES2_DUALCORE_STEP_PROJECT,
    GLES2_DUALCORE_STEP_PICK,
    GLES2_DUALCORE_STEP_SORT,
    GLES2_DUALCORE_STEP_COPY,
    GLES2_DUALCORE_STEP_PUBLISH,
};
struct GLES2DualCoreStageCrumb
{
    volatile int step;
    volatile int element_id;
    volatile int model_kind;
    volatile const void* model;
    volatile int anim_frame;
    volatile int slot;
};
extern struct GLES2DualCoreStageCrumb g_gles2_dualcore_stage_crumb;

/**
 * Pose, cull, project, pick-test and sort one model and append its result.
 *
 * Returns false when the arena had no room -- NOTHING was appended, and the
 * producer must stop for the frame (the consumer takes over at exactly this
 * command). The stage's steps and their order are gles2_draw_model's own;
 * a change to one must be made in both.
 */
bool
GLES2DualCoreStage_ComputeModel(
    struct GLES2DualCoreStageContext* context,
    struct GLES2DualCoreStageArena* arena,
    const struct ToriRS_RenderCommand_Model* command);

/**
 * Whether the depth path will sort this model: does it have a face that is
 * drawn AND translucent? The same test as gles2_world_face_pass's alpha
 * branch, made without the renderer (the texture half of that
 * classification can only make a face opaque-or-cutout, never blended, so
 * it is not needed here). The depth path's emit tolerates a disagreement --
 * it projects and sorts on the draw thread and counts it -- but the two are
 * meant to agree, and a change to one must be made in the other.
 */
bool
GLES2DualCoreStage_ModelHasBlendedFaces(struct ToriDraw_ModelHandle handle);

#endif
