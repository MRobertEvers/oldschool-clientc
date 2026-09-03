/*
 * The dual-core GLES2 lane. See the header for the shape of a frame.
 */
#include "platform/platform_renderer_gles2_dualcore.h"

#include "log/torirs_log.h"
#include "painters/painters.h"
#include "platform/platform_renderer_gles2.h"
#include "platform/platform_renderer_gles2_core.h"
#include "platform/platform_renderer_gles2_dualcore_stage.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"

#include "toridraw.h"

#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <ucontext.h>
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
 * kernel overrode). A spin is now one relaxed load (~3 cycles), so this is
 * ~100 us of spinning; at 4096 the draw reached sched_yield inside every
 * ordinary stall and spent 0.8 ms/frame in the kernel doing it (kr7). */
#define GLES2_DUALCORE_SPINS_BEFORE_YIELD 65536u

/* The worker's stack: 16 MB, the size class of a main thread rather than of
 * a helper. See the note at the create. */
#define GLES2_DUALCORE_WORKER_STACK_BYTES (16u * 1024u * 1024u)

/* The worker's poll of the feed: it is waiting on the draw's translation of
 * the next command, which is microseconds away unless the draw is inside a
 * long GL call; yield after this many empty polls (~100 us) so a same-core
 * worker does not starve the draw it is waiting on. */
#define GLES2_DUALCORE_FEED_SPINS_BEFORE_YIELD 65536u

/* How far ahead of dispatch the draw translates inside a world pass, in
 * commands; 0 is the whole pass, which is the default. The lead that
 * matters is in TIME, not slots: a tile whose stage the worker already did
 * costs the draw ~3 us, so 16 slots of tiles is ~50 us of lead, less than
 * one big model's stage (200+ us), and the draw caught the worker mid-model
 * 93 times a frame at 16 and 11 times at 128 (kr7, kr9) -- every cluster of
 * large models. The whole pass costs the draw nothing it did not already pay
 * (the same translation, done before the dispatch instead of interleaved
 * with it) and hands the worker the frame in one piece. Bounded values are
 * the A/B arm (TORIRS_GLES2_DUALCORE_LOOKAHEAD). */
#define GLES2_DUALCORE_LOOKAHEAD_DEFAULT 0u

/* The debug line's model classes: below this many faces a model is "small". */
#define GLES2_DUALCORE_SMALL_FACES 64

struct ToriRS_GLES2DualCore
{
    struct ToriRS_GLES2* renderer;

    /* The scratch view of the renderer's scene, and which scene it views. */
    struct ToriDraw_Scene* view;
    const struct ToriDraw_Scene* view_of;

    struct GLES2DualCoreStageArena arena;
    struct GLES2DualCoreStageContext context;
    struct GLES2ModelStageSource source;

    /* --- the dispatch cursor (draw thread only) --------------------------- */
    /* Inside a world pass the draw translates into the arena's feed and
     * dispatches from it: entries below `dispatched` have been; entries from
     * there to the feed's count are translated and waiting. Outside a pass
     * (and after an overflow) commands go through `local`, one at a time. */
    uint32_t dispatched;
    struct ToriRS_RenderCommand local;
    /* The bus has nothing more this frame. */
    bool bus_done;
    /* Between a dispatched BEGIN_3D and the translation of its END_3D: the
     * only stretch the draw runs ahead in. */
    bool in_pass;
    /* The feed overflowed this frame; nothing more is published. */
    bool feed_full;

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
    uint32_t lead;
    uint32_t lookahead;
    bool pin;
    bool debug;

    /* --- statistics --------------------------------------------------------- */
    uint64_t frames;
    uint64_t frames_dual;
    /* Frames that carried a world pass and ran it INLINE: the warm-up is
     * counted in these, not in frames. A title screen draws no model, so
     * counting it would start the worker on the first frame that ever
     * projects -- exactly when the kernels' lazily resolved statics are
     * being written by the draw thread. */
    uint64_t world_frames_inline;
    uint64_t models_taken;
    uint64_t models_inline;
    /* Models the draw claimed and staged itself because the worker had not
     * reached them (the balance working), counted on the draw thread ... */
    uint64_t models_claimed_by_draw;
    /* ... and the placeholders the worker published for them, on the worker. */
    uint64_t models_stage_draw_seen;
    uint64_t stalls;
    uint64_t stall_spins;
    /* The stalls split by what the draw was waiting on (debug line only):
     * a ground tile, a model under GLES2_DUALCORE_SMALL_FACES faces, or a
     * larger one -- which says whether the worker loses its lead on the
     * tile runs or on the big models. */
    uint64_t stalls_by_class[3];
    uint64_t stall_spins_by_class[3];
    /* The worker's lead over the draw in slots, sampled at every take
     * (debug line only): <=0, 1..7, 8..63, 64..511, 512+. */
    uint64_t lead_hist[5];
    uint64_t desyncs;
    /* Empty polls of the feed on the worker: the worker waiting on the
     * draw's translation. */
    uint64_t feed_waits;
    uint64_t feed_wait_spins;
    uint64_t worker_ns;
    uint64_t join_ns;
    uint64_t worker_ns_window;
    uint64_t join_ns_window;
};

/* ---- crash breadcrumbs (TORIRS_GLES2_DUALCORE_DEBUG) --------------------------
 *
 * A fault on the worker with a corrupted program counter leaves the system's
 * unwinder nothing to work with. This handler prints what the stage was
 * doing (the crumb), the fault registers, and a frame-pointer walk from the
 * faulting context -- the chain is usually intact even when the pc is not,
 * since it is the last VALID function's frame the bad jump left behind. The
 * addresses are printed as module offsets for the symbolizer. Then the
 * default action is restored and the fault re-raised, so the tombstone is
 * still written. Installed whenever the worker starts.
 */
static pthread_t g_dualcore_worker_thread;
static int g_dualcore_worker_thread_set;
static struct sigaction g_dualcore_previous_segv;

static void
dualcore_print_module_offset(char const* label, uintptr_t address)
{
    Dl_info info;
    if( address && dladdr((void*)address, &info) && info.dli_fname )
        fprintf(stderr, "gles2-dualcore: %s %#lx = %s+%#lx\n", label, (unsigned long)address,
            info.dli_fname, (unsigned long)(address - (uintptr_t)info.dli_fbase));
    else
        fprintf(stderr, "gles2-dualcore: %s %#lx = ?\n", label, (unsigned long)address);
}

static void
dualcore_segv_handler(int signal_number, siginfo_t* info, void* context)
{
    if( g_dualcore_worker_thread_set && pthread_equal(pthread_self(), g_dualcore_worker_thread) )
    {
        struct GLES2DualCoreStageCrumb const* crumb = &g_gles2_dualcore_stage_crumb;
        fprintf(stderr,
            "gles2-dualcore: WORKER FAULT signal %d addr %p; crumb step %d element %d kind %d "
            "model %p anim_frame %d slot %d\n",
            signal_number, info ? info->si_addr : NULL, crumb->step, crumb->element_id,
            crumb->model_kind, (void*)crumb->model, crumb->anim_frame, crumb->slot);
#if defined(__arm__)
        if( context )
        {
            ucontext_t* uc = (ucontext_t*)context;
            uintptr_t pc = uc->uc_mcontext.arm_pc;
            uintptr_t lr = uc->uc_mcontext.arm_lr;
            uintptr_t fp = uc->uc_mcontext.arm_fp;
            uintptr_t sp = uc->uc_mcontext.arm_sp;
            int depth;
            dualcore_print_module_offset("pc", pc);
            dualcore_print_module_offset("lr", lr);
            fprintf(stderr, "gles2-dualcore: sp %#lx fp %#lx\n", (unsigned long)sp, (unsigned long)fp);
            /* clang's ARM frame: [fp] = caller's fp, [fp+4] = return address.
             * Walk while the chain stays on this stack and moves upward. */
            for( depth = 0; depth < 24 && fp > sp && fp < sp + (64u * 1024u * 1024u) && (fp & 3u) == 0u;
                 depth++ )
            {
                uintptr_t next_fp = ((uintptr_t*)fp)[0];
                uintptr_t return_address = ((uintptr_t*)fp)[1];
                char label[16];
                snprintf(label, sizeof(label), "frame %d", depth);
                dualcore_print_module_offset(label, return_address);
                if( next_fp <= fp )
                    break;
                fp = next_fp;
            }
        }
#endif
        fflush(stderr);
    }
    /* Back to the default action for the re-raise: the tombstone. */
    sigaction(signal_number, &g_dualcore_previous_segv, NULL);
    raise(signal_number);
}

static void
dualcore_install_segv_handler(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = dualcore_segv_handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, &g_dualcore_previous_segv);
}

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
    struct GLES2DualCoreStageArena* arena = &lane->arena;
    const struct ToriRS_RenderCommand* entry;
    uint32_t index = 0u;
    uint32_t spins = 0u;
    bool stopped = false;

    while( !stopped )
    {
        switch( GLES2DualCoreStageArena_FeedTake(arena, index, &entry) )
        {
        case GLES2_DUALCORE_FEED_PENDING:
            /* The draw has not translated this far yet. */
            if( spins == 0u )
                lane->feed_waits++;
            spins++;
            if( spins > GLES2_DUALCORE_FEED_SPINS_BEFORE_YIELD )
                sched_yield();
            continue;
        case GLES2_DUALCORE_FEED_ENDED:
            goto done;
        case GLES2_DUALCORE_FEED_OVERFLOW:
            stopped = true;
            goto done;
        case GLES2_DUALCORE_FEED_READY:
            break;
        }
        lane->feed_wait_spins += spins;
        spins = 0u;
        index++;

        switch( entry->kind )
        {
        case TORIRSRC_BEGIN_3D:
            GLES2DualCoreStage_BeginPass(&lane->context, &entry->u.begin_3d);
            break;
        case TORIRSRC_DRAW_MODEL:
            switch( GLES2DualCoreStageArena_ClaimNextForProducer(arena) )
            {
            case GLES2_DUALCORE_CLAIMED:
                if( !GLES2DualCoreStage_ComputeModel(&lane->context, arena, &entry->u.model) )
                    stopped = true;
                break;
            case GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW:
                /* The draw got to this one first and is computing it on the
                 * scene's own bench; keep the count paired and move on. */
                GLES2DualCoreStageArena_PublishTakenByDraw(arena);
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
            /* Not the stage's: the draw dispatches it, the worker steps
             * over it. The bus does not emit these inside a pass today. */
            break;
        }
    }
done:
    GLES2DualCoreStage_EndPass(&lane->context);
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
    g_dualcore_worker_thread = pthread_self();
    g_dualcore_worker_thread_set = 1;
    /* Always, not only under the debug env: the one worker fault seen so far
     * (OSRS239, plugins off, 2026-09-02) left a program counter inside a data
     * table and no usable frames in the tombstone, and stopped reproducing
     * the moment the stage's breadcrumb stores went in. If it comes back, this
     * is what turns it into a stack. */
    dualcore_install_segv_handler();
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
    assert(lane);
    assert(command);
    assert(lane->armed);
    /* The pass opens on the worker too, and the draw may translate ahead
     * from here to the pass's END_3D. The BEGIN_3D itself came through
     * `local` (nothing runs ahead outside a pass), so it is copied in. A
     * feed already closed (the previous END_3D judged itself the frame's
     * last) takes nothing more: this pass is the draw's alone. */
    if( atomic_load_explicit(&lane->arena.feed_state, memory_order_relaxed) !=
        GLES2_DUALCORE_FEED_OPEN )
        lane->feed_full = true;
    if( !lane->feed_full )
    {
        struct ToriRS_RenderCommand begin;
        memset(&begin, 0, sizeof(begin));
        begin.kind = TORIRSRC_BEGIN_3D;
        begin.u.begin_3d = *command;
        if( !GLES2DualCoreStageArena_FeedPush(&lane->arena, &begin) )
            lane->feed_full = true;
        else
        {
            /* Dispatched already (this hook runs inside its dispatch). */
            assert(lane->dispatched + 1u == lane->arena.feed_count);
            lane->dispatched++;
        }
    }
    lane->in_pass = true;
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
    arena = &lane->arena;
    index = lane->take_index++;
    /* Where the draw is, for the worker's hand-off decision. */
    atomic_store_explicit(&arena->consumer_index, index, memory_order_relaxed);
    if( lane->debug )
    {
        uint32_t const published = atomic_load_explicit(&arena->ready, memory_order_relaxed);
        uint32_t const lead = published > index ? published - index : 0u;
        lane->lead_hist[lead == 0u ? 0 : lead < 8u ? 1 : lead < 64u ? 2 : lead < 512u ? 3 : 4]++;
    }
    if( !lane->kicked )
    {
        /* A model command before any BEGIN_3D: not a frame shape the emitter
         * produces; compute it here rather than wait for a worker that was
         * never woken. */
        lane->desyncs++;
        lane->models_inline++;
        return false;
    }

    /* A slot the worker handed to this thread, or one it claimed earlier,
     * is staged here without looking at `ready` at all. */
    if( index < arena->result_capacity &&
        atomic_load_explicit(&arena->claims[index], memory_order_acquire) ==
            GLES2_DUALCORE_CLAIM_CONSUMER )
    {
        lane->models_claimed_by_draw++;
        return false;
    }
    /*
     * The poll is RELAXED loads, with one acquire fence once the word has
     * moved. An acquire load on ARMv7 is a load and a `dmb ish`; the
     * previous shape (two acquire loads per iteration) was two barriers per
     * poll, ~100-160 cycles on Krait, for up to 4096 polls per stall -- and
     * measured at 8.7% of the draw thread's samples (kr4, 2026-09-03).
     * Nothing is read behind the words until the fence, so the ordering is
     * the same; only the spinning is cheaper.
     */
    for( ;; )
    {
        ready = atomic_load_explicit(&arena->ready, memory_order_relaxed);
        if( index < ready )
        {
            atomic_thread_fence(memory_order_acquire);
            break;
        }
        /* Not published. If nobody has started it, it is ours: computing it
         * here on the scene's own bench beats waiting for a worker that is
         * behind, and it is what keeps the two halves of the frame level.
         * One attempt: a claim that fails means the worker is on it and its
         * result is moments away -- or that it handed the slot over in the
         * same instant, which the re-read below catches. */
        if( spins == 0u && index < arena->result_capacity )
        {
            if( GLES2DualCoreStageArena_ClaimForConsumer(arena, index) ||
                atomic_load_explicit(&arena->claims[index], memory_order_acquire) ==
                    GLES2_DUALCORE_CLAIM_CONSUMER )
            {
                lane->models_claimed_by_draw++;
                return false;
            }
        }
        if( atomic_load_explicit(&arena->finished, memory_order_relaxed) !=
            GLES2_DUALCORE_STAGE_RUNNING )
        {
            /* The producer is done; everything it published is visible now. */
            atomic_thread_fence(memory_order_acquire);
            ready = atomic_load_explicit(&arena->ready, memory_order_relaxed);
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
    if( spins != 0u && lane->debug )
    {
        int class = 0;
        if( ToriDraw_ModelKindIsFull(command->model.kind) )
            class = ToriDraw_ModelRead(command->model)->face_count < GLES2_DUALCORE_SMALL_FACES ? 1 : 2;
        lane->stalls_by_class[class]++;
        lane->stall_spins_by_class[class] += spins;
    }
    lane->models_taken++;

    result = &arena->results[index];
    /* The worker can hand the slot over BETWEEN the claim-word read above and
     * the ready check: then what was published is its placeholder, and the
     * model is this thread's to stage. Not a fault, a race the protocol
     * allows; the draw just takes the other branch. */
    if( result->taken_by_draw )
    {
        lane->models_claimed_by_draw++;
        return false;
    }
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

    if( !lane->enabled || !renderer->scene || !frame->world || !frame->painters )
        return false;
    if( lane->world_frames_inline < lane->warmup_frames )
    {
        /* This world frame runs on the draw thread alone and settles every
         * first-use static the stage will read from the worker. */
        if( frame->painters->command_count > 0 )
            lane->world_frames_inline++;
        return false;
    }

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
    lane->arena.lead = lane->lead;

    memset(&lane->context, 0, sizeof(lane->context));
    lane->context.scene = lane->view;
    lane->context.kernel = renderer->kernel;
    lane->context.pick_enabled = renderer->pick_enabled;
    lane->context.pick_mouse_x = renderer->pick_mouse_x;
    lane->context.pick_mouse_y = renderer->pick_mouse_y;
    lane->context.zbuffer = renderer->zbuffer != NULL;

    lane->dispatched = 0u;
    lane->bus_done = false;
    lane->in_pass = false;
    lane->feed_full = false;

    lane->take_index = 0u;
    lane->kicked = false;
    lane->armed = true;
    renderer->model_stage_source = &lane->source;
    return true;
}

/* ---- the dispatch loop (draw thread) -------------------------------------------- */

/*
 * Translate the bus into the feed, up to the lookahead (or the end of the
 * pass, when it is 0 = whole pass), publishing each command as it lands.
 * Only inside a pass: every command there is a world command, with no scene
 * event queued behind it that its staging could depend on. Stops at the
 * pass's END_3D, at the end of the bus, or when the feed is full.
 */
static void
dualcore_translate_ahead(struct ToriRS_GLES2DualCore* lane, struct ToriRS_Frame* frame)
{
    struct GLES2DualCoreStageArena* arena = &lane->arena;

    while( lane->in_pass && !lane->bus_done && !lane->feed_full &&
           (lane->lookahead == 0u || arena->feed_count - lane->dispatched < lane->lookahead) )
    {
        struct ToriRS_RenderCommand* slot = GLES2DualCoreStageArena_FeedReserve(arena);
        if( !slot )
        {
            lane->feed_full = true;
            break;
        }
        if( !ToriRS_FrameNextCommand(frame, slot) )
        {
            lane->bus_done = true;
            break;
        }
        GLES2DualCoreStageArena_FeedPublish(arena);
        if( slot->kind == TORIRSRC_END_3D )
        {
            lane->in_pass = false;
            /* The last pass of the frame: close the feed HERE, not at the
             * frame's end. The worker asks for the next entry the moment it
             * has the END_3D, and an open feed keeps it polling -- through
             * the whole interface phase, 2-3 ms, yielding into the kernel
             * for most of it (1.7 ms/frame of system time on the worker,
             * kr10). Closed, it finishes and sleeps on the condvar. */
            if( !ToriRS_FrameHasWorldPassAhead(frame) )
                GLES2DualCoreStageArena_FeedClose(arena);
        }
    }
}

/*
 * The next command to dispatch, or NULL at the end of the frame: what is
 * waiting in the feed first, else one command straight off the bus. The
 * pointer is live until the next call.
 */
static const struct ToriRS_RenderCommand*
dualcore_next_command(struct ToriRS_GLES2DualCore* lane, struct ToriRS_Frame* frame)
{
    struct GLES2DualCoreStageArena* arena = &lane->arena;

    dualcore_translate_ahead(lane, frame);
    if( lane->dispatched < arena->feed_count )
        return &arena->feed[lane->dispatched++];
    if( lane->bus_done )
        return NULL;
    if( !ToriRS_FrameNextCommand(frame, &lane->local) )
    {
        lane->bus_done = true;
        return NULL;
    }
    return &lane->local;
}

/* The element id of the DRAW_MODEL `ahead` entries past the dispatch cursor,
 * or -1: the input the renderer's prefetch pipeline wants. */
static int
dualcore_ahead_element_id(const struct ToriRS_GLES2DualCore* lane, uint32_t ahead)
{
    const struct ToriRS_RenderCommand* command;
    if( lane->dispatched + ahead >= lane->arena.feed_count )
        return -1;
    command = &lane->arena.feed[lane->dispatched + ahead];
    return command->kind == TORIRSRC_DRAW_MODEL ? command->u.model.element_id : -1;
}

static void
dualcore_render_frame_commands(struct ToriRS_GLES2DualCore* lane, struct ToriRS_Frame* frame)
{
    struct ToriRS_GLES2* renderer = lane->renderer;
    const struct ToriRS_RenderCommand* command;

    while( (command = dualcore_next_command(lane, frame)) != NULL )
    {
        /* The cursor has moved past `command`: entry 0 is the one after it. */
        gles2_prefetch_ahead_ids(
            renderer,
            dualcore_ahead_element_id(lane, 0u),
            dualcore_ahead_element_id(lane, 1u),
            dualcore_ahead_element_id(lane, 2u));
        ToriRS_GLES2_Execute(renderer, command);
    }
    GLES2DualCoreStageArena_FeedClose(&lane->arena);
}

static void
dualcore_debug_line(struct ToriRS_GLES2DualCore* lane)
{
    if( !lane->debug || lane->frames == 0u || (lane->frames % GLES2_DUALCORE_DEBUG_PERIOD) != 0u )
        return;
    TORIRS_ERR(
        "gles2-dualcore: frames %llu dual %llu taken %llu claimed-by-draw %llu (worker saw %llu) "
        "inline %llu stalls %llu (spins %llu) [tile %llu/%llu small %llu/%llu large %llu/%llu] "
        "lead-hist [0 %llu, 1-7 %llu, 8-63 %llu, 64-511 %llu, 512+ %llu] "
        "desyncs %llu exhausted-frames %u "
        "feed-waits %llu (spins %llu) feed-overflow-frames %u reprojected %u "
        "worker %.2f ms/frame join %.3f ms/frame\n",
        (unsigned long long)lane->frames,
        (unsigned long long)lane->frames_dual,
        (unsigned long long)lane->models_taken,
        (unsigned long long)lane->models_claimed_by_draw,
        (unsigned long long)lane->models_stage_draw_seen,
        (unsigned long long)lane->models_inline,
        (unsigned long long)lane->stalls,
        (unsigned long long)lane->stall_spins,
        (unsigned long long)lane->stalls_by_class[0],
        (unsigned long long)lane->stall_spins_by_class[0],
        (unsigned long long)lane->stalls_by_class[1],
        (unsigned long long)lane->stall_spins_by_class[1],
        (unsigned long long)lane->stalls_by_class[2],
        (unsigned long long)lane->stall_spins_by_class[2],
        (unsigned long long)lane->lead_hist[0],
        (unsigned long long)lane->lead_hist[1],
        (unsigned long long)lane->lead_hist[2],
        (unsigned long long)lane->lead_hist[3],
        (unsigned long long)lane->lead_hist[4],
        (unsigned long long)lane->desyncs,
        lane->arena.exhausted_frames,
        (unsigned long long)lane->feed_waits,
        (unsigned long long)lane->feed_wait_spins,
        lane->arena.feed_overflow_frames,
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
    {
        lane->frames_dual++;
        dualcore_render_frame_commands(lane, frame);
    }
    else
        gles2_render_frame_commands(renderer, frame);
    if( lane->armed )
    {
        /* Before the frame ends: ToriRS_FrameEnd frees the scene's pending
         * poses, and the worker must be past every model by then. */
        if( lane->kicked )
            dualcore_join_worker(lane);
        else
            /* Armed, but the frame never opened a pass: the worker did not
             * run, so it is this thread that closes the arena's frame. */
            atomic_store_explicit(
                &lane->arena.finished, GLES2_DUALCORE_STAGE_DONE, memory_order_relaxed);
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
    {
        long lead = dualcore_env_long(
            "TORIRS_GLES2_DUALCORE_LEAD", (long)GLES2_DUALCORE_STAGE_LEAD_DEFAULT);
        lane->lead = lead >= 0 ? (uint32_t)lead : GLES2_DUALCORE_STAGE_LEAD_DEFAULT;
    }
    {
        long lookahead = dualcore_env_long(
            "TORIRS_GLES2_DUALCORE_LOOKAHEAD", (long)GLES2_DUALCORE_LOOKAHEAD_DEFAULT);
        lane->lookahead = lookahead > 0 ? (uint32_t)lookahead : 0u;
    }
    lane->debug = dualcore_env_long("TORIRS_GLES2_DUALCORE_DEBUG", 0) != 0;

    pthread_mutex_init(&lane->lock, NULL);
    pthread_cond_init(&lane->wake, NULL);
    pthread_cond_init(&lane->done, NULL);
    if( lane->enabled )
    {
        /* The stage runs the same kernels the frame thread runs -- the
         * skeletal pose, the projection lanes, the sort -- and some of them
         * keep large frames on the stack. The frame thread's stack is the
         * platform's; a pthread's default is far smaller on 32-bit Android,
         * and an overflow past the guard page corrupts whatever is mapped
         * below it instead of faulting. Give the worker a frame-thread-sized
         * stack. */
        pthread_attr_t attributes;
        pthread_attr_init(&attributes);
        pthread_attr_setstacksize(&attributes, GLES2_DUALCORE_WORKER_STACK_BYTES);
        if( pthread_create(&lane->thread, &attributes, dualcore_worker_main, lane) != 0 )
        {
            TORIRS_ERR("gles2-dualcore: could not start the worker; running single-threaded\n");
            lane->enabled = false;
        }
        else
            lane->thread_started = true;
        pthread_attr_destroy(&attributes);
    }
    if( lane->debug )
        TORIRS_ERR(
            "gles2-dualcore: %s (warmup %u frames, lead %u, lookahead %u, pin %d)\n",
            lane->enabled ? "worker started" : "disabled, single-threaded",
            lane->warmup_frames,
            lane->lead,
            lane->lookahead,
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
