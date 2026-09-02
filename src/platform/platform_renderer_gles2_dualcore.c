/*
 * The dual-core GLES2 lane. See the header for the shape of a frame.
 */
#include "platform/platform_renderer_gles2_dualcore.h"

#include "log/torirs_log.h"
#include "painters/painters.h"
#include "platform/platform_renderer_gles2_core.h"
#include "platform/platform_renderer_gles2_dualcore_stage.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"

#include "toridraw.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The debug line's cadence, in frames: the swap report's (300) so the two
 * can be read side by side in logcat. */
#define GLES2_DUALCORE_DEBUG_PERIOD 300u

/* Spins on the ready counter before the wait starts yielding the core. On
 * two cores the worker is on the other one, so spinning costs it nothing;
 * yielding is for the case where it is not (a hot-plugged core, a pin the
 * kernel overrode). */
#define GLES2_DUALCORE_SPINS_BEFORE_YIELD 64u

struct ToriRS_GLES2DualCore
{
    struct ToriRS_GLES2* renderer;

    /* The scratch view of the renderer's scene, and which scene it views. */
    struct ToriDraw_Scene* view;
    const struct ToriDraw_Scene* view_of;

    struct GLES2DualCoreStageArena arena;
    struct GLES2DualCoreStageContext context;
    struct GLES2ModelStageSource source;
    /* The worker's own iterator over the frame's buffers (a copy of the
     * draw's frame, begun world-only). */
    struct ToriRS_Frame worker_frame;

    /* --- the thread ------------------------------------------------------- */
    pthread_t thread;
    bool thread_started;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_cond_t done;
    /* Guarded by `lock`: the frame the draw handed over (bumped to hand one
     * over), the frame the worker last finished, and the stop request. */
    uint32_t handed_serial;
    uint32_t finished_serial;
    bool quit;

    /* --- per-frame consumer state (draw thread only) ---------------------- */
    /* The stage source is installed for this frame. */
    bool armed;
    /* The worker was woken for this frame (at the first BEGIN_3D). */
    bool kicked;
    /* DRAW_MODEL commands taken so far this frame: the next result index. */
    uint32_t take_index;

    /* --- configuration ---------------------------------------------------- */
    bool enabled;
    uint32_t warmup_frames;
    bool pin;
    bool debug;

    /* --- statistics --------------------------------------------------------- */
    uint64_t frames;
    uint64_t frames_dual;
    uint64_t models_taken;
    uint64_t models_inline;
    /* Models the draw claimed and staged itself because the worker had not
     * reached them (the balance working), counted on the draw thread ... */
    uint64_t models_claimed_by_draw;
    /* ... and the placeholders the worker published for them, on the worker. */
    uint64_t models_stage_draw_seen;
    uint64_t stalls;
    uint64_t stall_spins;
    uint64_t desyncs;
    uint64_t worker_ns;
    uint64_t join_ns;
    uint64_t worker_ns_window;
    uint64_t join_ns_window;
};

static uint64_t
dualcore_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static long
dualcore_env_long(char const* name, long fallback)
{
    char const* value = getenv(name);
    if( !value || !value[0] )
        return fallback;
    return atol(value);
}

/* ---- the worker ------------------------------------------------------------ */

static void
dualcore_worker_pass(struct ToriRS_GLES2DualCore* lane)
{
    struct ToriRS_RenderCommand command;
    bool stopped = false;

    while( !stopped && ToriRS_FrameNextCommand(&lane->worker_frame, &command) )
    {
        switch( command.kind )
        {
        case TORIRSRC_BEGIN_3D:
            GLES2DualCoreStage_BeginPass(&lane->context, &command.u.begin_3d);
            break;
        case TORIRSRC_DRAW_MODEL:
            switch( GLES2DualCoreStageArena_ClaimNextForProducer(&lane->arena) )
            {
            case GLES2_DUALCORE_CLAIMED:
                if( !GLES2DualCoreStage_ComputeModel(
                        &lane->context, &lane->arena, &command.u.model) )
                    stopped = true;
                break;
            case GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW:
                /* The draw got to this one first and is computing it on the
                 * scene's own bench; keep the count paired and move on. */
                GLES2DualCoreStageArena_PublishTakenByDraw(&lane->arena);
                lane->models_stage_draw_seen++;
                break;
            case GLES2_DUALCORE_CLAIM_EXHAUSTED:
                stopped = true;
                break;
            }
            break;
        case TORIRSRC_END_3D:
            GLES2DualCoreStage_EndPass(&lane->context);
            break;
        default:
            /* A world-only frame yields nothing else; if it ever does, it is
             * not the stage's to act on. */
            break;
        }
    }
    GLES2DualCoreStage_EndPass(&lane->context);
    /* The worker's iterator is a copy; the scene's frame is ended once, by
     * the draw (ToriRS_FrameBeginWorldOnly's contract). */
    atomic_store_explicit(
        &lane->arena.finished,
        stopped ? GLES2_DUALCORE_STAGE_EXHAUSTED : GLES2_DUALCORE_STAGE_DONE,
        memory_order_release);
}

static void
dualcore_worker_pin(struct ToriRS_GLES2DualCore* lane)
{
#if defined(__linux__)
    cpu_set_t set;
    if( !lane->pin )
        return;
    CPU_ZERO(&set);
    CPU_SET(1, &set);
    /* Best effort: a kernel that has the second CPU offline right now
     * refuses, and the worker then runs wherever the scheduler puts it,
     * which is the unpinned behaviour. */
    if( sched_setaffinity(0, sizeof(set), &set) != 0 && lane->debug )
        TORIRS_ERR("gles2-dualcore: could not pin the worker to cpu1\n");
#else
    (void)lane;
#endif
}

static void*
dualcore_worker_main(void* argument)
{
    struct ToriRS_GLES2DualCore* lane = (struct ToriRS_GLES2DualCore*)argument;
    uint32_t serial;

#if defined(__linux__)
    pthread_setname_np(pthread_self(), "gles2-stage");
#endif
    dualcore_worker_pin(lane);
    for( ;; )
    {
        uint64_t began;
        pthread_mutex_lock(&lane->lock);
        while( lane->handed_serial == lane->finished_serial && !lane->quit )
            pthread_cond_wait(&lane->wake, &lane->lock);
        if( lane->quit )
        {
            pthread_mutex_unlock(&lane->lock);
            break;
        }
        serial = lane->handed_serial;
        pthread_mutex_unlock(&lane->lock);

        began = dualcore_now_ns();
        dualcore_worker_pass(lane);
        lane->worker_ns_window += dualcore_now_ns() - began;

        pthread_mutex_lock(&lane->lock);
        lane->finished_serial = serial;
        pthread_cond_broadcast(&lane->done);
        pthread_mutex_unlock(&lane->lock);
    }
    return NULL;
}

/* ---- the stage source (draw thread) ------------------------------------------ */

static void
dualcore_source_begin_3d(void* user, const struct ToriRS_RenderCommand_Begin3D* command)
{
    struct ToriRS_GLES2DualCore* lane = (struct ToriRS_GLES2DualCore*)user;
    (void)command;
    assert(lane);
    assert(lane->armed);
    if( lane->kicked )
        return;
    lane->kicked = true;
    pthread_mutex_lock(&lane->lock);
    lane->handed_serial++;
    pthread_cond_signal(&lane->wake);
    pthread_mutex_unlock(&lane->lock);
}

static bool
dualcore_source_take(
    void* user,
    const struct ToriRS_RenderCommand_Model* command,
    struct GLES2ModelStage* out)
{
    struct ToriRS_GLES2DualCore* lane = (struct ToriRS_GLES2DualCore*)user;
    struct GLES2DualCoreStageArena* arena;
    const struct GLES2DualCoreStageResult* result;
    uint32_t index;
    uint32_t ready;
    uint32_t spins = 0u;

    assert(lane);
    assert(command);
    assert(out);
    (void)command;
    arena = &lane->arena;
    index = lane->take_index++;
    if( !lane->kicked )
    {
        /* A model command before any BEGIN_3D: not a frame shape the emitter
         * produces; compute it here rather than wait for a worker that was
         * never woken. */
        lane->desyncs++;
        lane->models_inline++;
        return false;
    }

    for( ;; )
    {
        ready = atomic_load_explicit(&arena->ready, memory_order_acquire);
        if( index < ready )
            break;
        /* Not published. If nobody has started it, it is ours: computing it
         * here on the scene's own bench beats waiting for a worker that is
         * behind, and it is what keeps the two halves of the frame level.
         * (One attempt: a claim that fails means the worker is on it, and
         * its result is moments away.) */
        if( spins == 0u && index < arena->result_capacity &&
            GLES2DualCoreStageArena_ClaimForConsumer(arena, index) )
        {
            lane->models_claimed_by_draw++;
            return false;
        }
        if( atomic_load_explicit(&arena->finished, memory_order_acquire) !=
            GLES2_DUALCORE_STAGE_RUNNING )
        {
            /* The producer is done; everything it published is visible now. */
            ready = atomic_load_explicit(&arena->ready, memory_order_acquire);
            if( index < ready )
                break;
            /* Exhausted: the tail of the frame is the draw's to compute. A
             * finished producer with fewer results than asks is a count
             * mismatch between the two iterations -- counted, and still
             * drawn correctly, since the inline stage is complete. */
            if( atomic_load_explicit(&arena->finished, memory_order_relaxed) ==
                GLES2_DUALCORE_STAGE_DONE )
                lane->desyncs++;
            lane->models_inline++;
            return false;
        }
        if( spins == 0u )
            lane->stalls++;
        spins++;
        if( spins > GLES2_DUALCORE_SPINS_BEFORE_YIELD )
            sched_yield();
    }
    lane->stall_spins += spins;
    lane->models_taken++;

    result = &arena->results[index];
    /* A placeholder is only ever published for a model the draw claimed, and
     * a claimed model returned above before reaching here. */
    assert(!result->taken_by_draw);
    out->cull = result->cull;
    out->pick_hit = result->pick_hit != 0u;
    out->sorted = result->sorted != 0u;
    out->sorted_face_count = result->sorted_face_count;
    out->face_order = result->sorted && result->sorted_face_count > 0
                          ? arena->orders + result->order_offset
                          : NULL;
    out->projected_depth = result->projected_depth;
    return true;
}

/* ---- the frame (draw thread) ----------------------------------------------------- */

static void
dualcore_join_worker(struct ToriRS_GLES2DualCore* lane)
{
    uint64_t const began = dualcore_now_ns();
    pthread_mutex_lock(&lane->lock);
    while( lane->finished_serial != lane->handed_serial )
        pthread_cond_wait(&lane->done, &lane->lock);
    pthread_mutex_unlock(&lane->lock);
    lane->join_ns_window += dualcore_now_ns() - began;
}

static bool
dualcore_arm(struct ToriRS_GLES2DualCore* lane, struct ToriRS_Frame* frame)
{
    struct ToriRS_GLES2* renderer = lane->renderer;

    if( !lane->enabled || lane->frames < lane->warmup_frames )
        return false;
    if( !renderer->scene || !frame->world || !frame->painters )
        return false;

    /* The view snapshots the scene's non-scratch fields; it is taken (or
     * re-taken) every frame, after the app's last mutation and before the
     * worker's first read. A scene swap re-creates it. */
    if( lane->view && lane->view_of != renderer->scene )
    {
        ToriDraw_SceneScratchViewFree(lane->view);
        lane->view = NULL;
    }
    if( !lane->view )
    {
        lane->view = ToriDraw_SceneScratchViewNew(renderer->scene);
        lane->view_of = renderer->scene;
    }
    else
        ToriDraw_SceneScratchViewSync(lane->view, renderer->scene);

    /* One result per DRAW_MODEL, and the walk emits at most one model
     * command per painter command. Running out is not an error (the draw
     * takes over), only a slower frame. */
    GLES2DualCoreStageArena_BeginFrame(&lane->arena, (uint32_t)frame->painters->command_count + 64u);

    memset(&lane->context, 0, sizeof(lane->context));
    lane->context.scene = lane->view;
    lane->context.kernel = renderer->kernel;
    lane->context.pick_enabled = renderer->pick_enabled;
    lane->context.pick_mouse_x = renderer->pick_mouse_x;
    lane->context.pick_mouse_y = renderer->pick_mouse_y;
    lane->context.zbuffer = renderer->zbuffer != NULL;

    lane->worker_frame = *frame;
    lane->worker_frame.scene = lane->view;
    ToriRS_FrameBeginWorldOnly(&lane->worker_frame);

    lane->take_index = 0u;
    lane->kicked = false;
    lane->armed = true;
    renderer->model_stage_source = &lane->source;
    return true;
}

static void
dualcore_debug_line(struct ToriRS_GLES2DualCore* lane)
{
    if( !lane->debug || lane->frames == 0u || (lane->frames % GLES2_DUALCORE_DEBUG_PERIOD) != 0u )
        return;
    TORIRS_ERR(
        "gles2-dualcore: frames %llu dual %llu taken %llu claimed-by-draw %llu (worker saw %llu) "
        "inline %llu stalls %llu (spins %llu) desyncs %llu exhausted-frames %u reprojected %u "
        "worker %.2f ms/frame join %.3f ms/frame\n",
        (unsigned long long)lane->frames,
        (unsigned long long)lane->frames_dual,
        (unsigned long long)lane->models_taken,
        (unsigned long long)lane->models_claimed_by_draw,
        (unsigned long long)lane->models_stage_draw_seen,
        (unsigned long long)lane->models_inline,
        (unsigned long long)lane->stalls,
        (unsigned long long)lane->stall_spins,
        (unsigned long long)lane->desyncs,
        lane->arena.exhausted_frames,
        lane->renderer->stage_reprojected_models,
        (double)lane->worker_ns_window / 1.0e6 / (double)GLES2_DUALCORE_DEBUG_PERIOD,
        (double)lane->join_ns_window / 1.0e6 / (double)GLES2_DUALCORE_DEBUG_PERIOD);
    lane->worker_ns += lane->worker_ns_window;
    lane->join_ns += lane->join_ns_window;
    lane->worker_ns_window = 0u;
    lane->join_ns_window = 0u;
}

void
ToriRS_GLES2DualCore_RenderFrame(struct ToriRS_GLES2DualCore* lane, struct ToriRS_Frame* frame)
{
    struct ToriRS_GLES2* renderer;

    assert(lane);
    assert(frame);
    renderer = lane->renderer;
    lane->armed = false;
    lane->kicked = false;

    if( !gles2_render_frame_begin(renderer) )
        return;
    ToriRS_FrameBegin(frame);
    if( dualcore_arm(lane, frame) )
        lane->frames_dual++;
    gles2_render_frame_commands(renderer, frame);
    if( lane->armed )
    {
        /* Before the frame ends: ToriRS_FrameEnd frees the scene's pending
         * poses, and the worker must be past every model by then. */
        if( lane->kicked )
            dualcore_join_worker(lane);
        renderer->model_stage_source = NULL;
        lane->armed = false;
    }
    ToriRS_FrameEnd(frame);
    gles2_render_frame_end(renderer);
    lane->frames++;
    dualcore_debug_line(lane);
}

/* ---- lifetime -------------------------------------------------------------------- */

struct ToriRS_GLES2DualCore*
ToriRS_GLES2DualCore_New(struct ToriRS_GLES2* renderer)
{
    struct ToriRS_GLES2DualCore* lane;
    long warmup;

    assert(renderer);
    lane = (struct ToriRS_GLES2DualCore*)calloc(1, sizeof(*lane));
    assert(lane);
    lane->renderer = renderer;
    GLES2DualCoreStageArena_Init(&lane->arena);
    lane->source.user = lane;
    lane->source.take = dualcore_source_take;
    lane->source.begin_3d = dualcore_source_begin_3d;

    lane->enabled = dualcore_env_long("TORIRS_GLES2_DUALCORE", 1) != 0;
    warmup = dualcore_env_long("TORIRS_GLES2_DUALCORE_WARMUP", 1);
    lane->warmup_frames = warmup > 0 ? (uint32_t)warmup : 0u;
    lane->pin = dualcore_env_long("TORIRS_GLES2_DUALCORE_PIN", 0) != 0;
    lane->debug = dualcore_env_long("TORIRS_GLES2_DUALCORE_DEBUG", 0) != 0;

    pthread_mutex_init(&lane->lock, NULL);
    pthread_cond_init(&lane->wake, NULL);
    pthread_cond_init(&lane->done, NULL);
    if( lane->enabled )
    {
        if( pthread_create(&lane->thread, NULL, dualcore_worker_main, lane) != 0 )
        {
            TORIRS_ERR("gles2-dualcore: could not start the worker; running single-threaded\n");
            lane->enabled = false;
        }
        else
            lane->thread_started = true;
    }
    if( lane->debug )
        TORIRS_ERR(
            "gles2-dualcore: %s (warmup %u frames, pin %d)\n",
            lane->enabled ? "worker started" : "disabled, single-threaded",
            lane->warmup_frames,
            lane->pin ? 1 : 0);
    return lane;
}

void
ToriRS_GLES2DualCore_Free(struct ToriRS_GLES2DualCore* lane)
{
    if( !lane )
        return;
    if( lane->thread_started )
    {
        pthread_mutex_lock(&lane->lock);
        lane->quit = true;
        pthread_cond_broadcast(&lane->wake);
        pthread_mutex_unlock(&lane->lock);
        pthread_join(lane->thread, NULL);
    }
    if( lane->renderer && lane->renderer->model_stage_source == &lane->source )
        lane->renderer->model_stage_source = NULL;
    pthread_cond_destroy(&lane->done);
    pthread_cond_destroy(&lane->wake);
    pthread_mutex_destroy(&lane->lock);
    ToriDraw_SceneScratchViewFree(lane->view);
    GLES2DualCoreStageArena_Free(&lane->arena);
    free(lane);
}
