/*
 * The replay's playhead (replay_scrub.h): what the replay transport stages
 * next, on what clock, and how a seek gets there.
 *
 * Normal playback is exactly what net_transport_replay.c's play() did before
 * it moved here: packets keep the recording's relative timing on the replay
 * clock, a stall past REPLAY_MAX_GAP_MS is skipped, the recorded chatbox
 * answers go where they were sent, and after a REBUILD the next tick waits for
 * MAP_BUILD_COMPLETE with the clock held.
 */
#include "replay/replay_scrub.h"

#include "mem/torirs_mem.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rsprox_recording.h"
#include "replay/replay_checkpoint_store.h"
#include "replay/replay_classify.h"
#include "replay/replay_ledger.h"
#include "replay/replay_world.h"
#include "net/rev/packets/pkt_player_appearance.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/torirs_log.h"

#define REPLAY_CYCLE_MS 20
/* Below NET_LINK_TIMEOUT_MS (15 s): a client that never sends
 * MAP_BUILD_COMPLETE is played on, not dropped for silence. */
#define REPLAY_MAP_WAIT_MS 10000
/*
 * The stalled-seek watchdog (seek_watchdog). A seek whose cursor, executed tick
 * count and plan step have all stood still is stuck when it has stood still
 * for REPLAY_SEEK_STALL_MS of time the client spent RUNNING -- frames going,
 * nothing it waits on that bounds itself -- and says so and starts again from
 * the login, in full simulation. Time spent waiting on a bounded thing (the
 * map-build barrier, REPLAY_MAP_WAIT_MS; a restart's login, the link watch's;
 * a client whose packet pipeline is held on a load it awaits, a scene build
 * or a cache fetch, each with its own budget) is counted apart against
 * REPLAY_SEEK_WAIT_STALL_MS: should one of those bounds fail to hold, the seek
 * still ends loudly. A gap between two polls counts for at most
 * REPLAY_SEEK_STALL_POLL_MS: a client not running frames (a hidden tab, a
 * synchronous load) is not a seek making no progress.
 */
#define REPLAY_SEEK_STALL_MS 20000
#define REPLAY_SEEK_WAIT_STALL_MS 180000
#define REPLAY_SEEK_STALL_POLL_MS 250
/* The answers to a chatbox prompt. The recorder's client closed the prompt as
 * it sent one (rs_gameproto_exec.c, exec_control_replay_dialog), and no server
 * packet says so, so the replay passes each one on where it was sent, as a
 * REPLAY_DIALOG control frame (net/torirs_control.h). */
#define REPLAY_DIALOG_PROTS 5
static int const k_dialog_names[REPLAY_DIALOG_PROTS] = {
    PKTOUT_NAME_RESUME_P_OBJDIALOG,     PKTOUT_NAME_RESUME_P_COUNTDIALOG,
    PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG, PKTOUT_NAME_RESUME_P_NAMEDIALOG,
    PKTOUT_NAME_RESUME_P_STRINGDIALOG,
};

struct ReplayScrub
{
    struct RsproxRecording const* rec;
    struct GameProtoRevTable const* rev;
    struct ReplayTimeline* tl;
    /** k_dialog_names' wire opcodes in this revision (-1: not in it). */
    int dialog_wire[REPLAY_DIALOG_PROTS];

    /* The cursor: the next frame and client packet not yet handed out. */
    int cursor;
    int next_input;
    int last_tick;
    uint64_t clock_ms;

    /* The map-build barrier: after the REBUILD of tick wait_tick, nothing of a
     * later tick goes until MAP_BUILD_COMPLETE. */
    int wait_map;
    int wait_tick;
    int map_built;
    uint64_t wait_since_ms;
    uint64_t wall_ms;

    /* The client's executed SERVER_TICK_ENDs, ever, and at the session's
     * start. */
    unsigned client_serial;
    unsigned serial_base;
    int session_live;
    /** The session's connection ended at a RECONNECT or a hop and no new
     *  one has taken it up yet (ReplayScrub_ConnectionLost). */
    int reconnecting;

    int hold;
    int hold_tick;
    int hold_reported;

    int seeking;
    struct ReplaySeekPlan plan;
    /** A RESTART has been handed out and the new session has not begun. */
    int restarting;
    uint64_t seek_wall_start;

    /* The fast plan. */
    enum ReplaySeekMode mode;
    int chase_cycles;
    /** Built on the first fast seek (ReplayLedger_Build): the keyframes and
     *  the chase's tiles. */
    struct ReplayLedger* ledger;
    int ledger_failed;
    /** The chase: the tick whose COORD went last (-1: none this session). */
    int chase_tick;
    /** The local player's name, for the chase's first tile ("" once sent). */
    char chase_name[32];
    /** The ledger's entity fold kept level with the chase's cursor, for the
     *  public chat in the PLAYER_INFO it withholds; and the lines owed. */
    struct ReplayEntityState* chase_state;
    struct ReplayChatLine chat[16];
    int chat_count;
    int chat_at;
    uint64_t chase_wall_start;
    uint64_t chase_wall_end;
    /** The seek in flight's times, and the last landed one's. */
    struct ReplaySeekTimes times;
    struct ReplaySeekTimes landed;
    int chase_ticks;
    /** The keyframe's REBUILD is what the map-build barrier waits for. */
    int world_wait;
    /** The REBUILD frame of the scene the client has (its last REBUILD
     *  handed out, or the keyframe's): -1 when none this session. */
    int client_scene_frame;
    /** Session checkpoints taken on the way (replay_checkpoint_store.h). */
    struct ReplayCheckpointStore* checkpoints;
    /** The stall watchdog: the cursor, executed ticks and plan step last seen
     *  to move; the running and the waiting time since (ms); the wall clock at
     *  the last look (0: none yet). */
    int stall_cursor;
    int stall_reached;
    int stall_step;
    uint64_t stall_run_ms;
    uint64_t stall_wait_ms;
    uint64_t stall_last_wall;
    /** The client's packet pipeline is held on a load it awaits
     *  (ReplayScrub_ClientBusy). */
    int client_busy;
};

/* --- helpers ------------------------------------------------------------- */

/* The first tick none of whose packets has been handed out. */
static int
staged_frontier(struct ReplayScrub const* scrub)
{
    return scrub->cursor > 0 ? scrub->rec->frames[scrub->cursor - 1].tick + 1 : 0;
}

static enum ReplaySeekStepKind
seek_step(struct ReplayScrub const* scrub)
{
    assert(scrub->seeking);
    assert(scrub->plan.at < scrub->plan.count);
    return scrub->plan.steps[scrub->plan.at];
}

/* Packets of this tick or later are not staged: a seek's target, or a hold's
 * boundary; INT_MAX when nothing stops the cursor. */
static int
stop_tick(struct ReplayScrub const* scrub)
{
    if( scrub->seeking )
    {
        enum ReplaySeekStepKind const step = seek_step(scrub);

        if( step == REPLAY_SEEK_FULLSIM || step == REPLAY_SEEK_PREROLL )
            return scrub->plan.target;
        if( step == REPLAY_SEEK_CHASE )
            return scrub->plan.world_tick;
        if( step == REPLAY_SEEK_RESTORE )
            return scrub->plan.drain_tick;
        return -1;
    }
    if( scrub->hold )
        return scrub->hold_tick;
    return INT_MAX;
}

/* A step that plays packets up to the target on the replay clock. */
static int
step_plays(enum ReplaySeekStepKind step)
{
    return step == REPLAY_SEEK_FULLSIM || step == REPLAY_SEEK_PREROLL;
}

static int
at_stop(struct ReplayScrub const* scrub)
{
    int const stop = stop_tick(scrub);

    if( stop == INT_MAX )
        return 0;
    return scrub->cursor >= scrub->rec->frame_count || scrub->rec->frames[scrub->cursor].tick >= stop;
}

/* A stopped clock reads no later than the packet it stopped in front of, so
 * the packets after it keep their spacing when it runs again. */
static void
clamp_clock(struct ReplayScrub* scrub)
{
    if( scrub->cursor < scrub->rec->frame_count &&
        scrub->clock_ms > scrub->rec->frames[scrub->cursor].t_ms )
        scrub->clock_ms = scrub->rec->frames[scrub->cursor].t_ms;
}

static void
try_land(struct ReplayScrub* scrub)
{
    int reached;
    uint64_t took_ms;

    if( !scrub->seeking || !step_plays(seek_step(scrub)) || !scrub->session_live )
        return;
    reached = ReplayScrub_Reached(scrub);
    if( reached < scrub->plan.target )
        return;
    if( reached > scrub->plan.target )
        TORIRS_ERR("replay: the client has executed %d ticks, past the seek's target %d\n", reached,
                   scrub->plan.target);
    scrub->plan.at++;
    assert(seek_step(scrub) == REPLAY_SEEK_PRESENT);
    scrub->seeking = 0;
    scrub->world_wait = 0;
    if( scrub->hold )
    {
        scrub->hold_tick = scrub->plan.target;
        scrub->hold_reported = 1;
    }
    clamp_clock(scrub);
    took_ms = scrub->wall_ms > scrub->seek_wall_start ? scrub->wall_ms - scrub->seek_wall_start : 0;
    scrub->times.target = scrub->plan.target;
    scrub->times.from = scrub->plan.from;
    scrub->times.fast = scrub->plan.world_tick >= 0;
    scrub->times.chase_ticks = scrub->chase_ticks;
    scrub->times.landed_ms = scrub->wall_ms;
    scrub->times.serial = scrub->landed.serial + 1;
    scrub->landed = scrub->times;
    if( scrub->plan.world_tick >= 0 )
    {
        uint64_t const chase_ms = scrub->chase_wall_end > scrub->chase_wall_start
                                      ? scrub->chase_wall_end - scrub->chase_wall_start
                                      : 0;

        TORIRS_REPORT("replay: seek landed at tick %d (fast from %d: chase %d ticks in %.2f s, %.0f "
                      "ticks/s at %d cycles a tick; keyframe at %d; pre-roll %d ticks; %.2f s in "
                      "all); %s\n",
                      scrub->plan.target, scrub->plan.from, scrub->chase_ticks,
                      (double)chase_ms / 1000.0,
                      chase_ms ? (double)scrub->chase_ticks * 1000.0 / (double)chase_ms : 0.0,
                      scrub->chase_cycles, scrub->plan.world_tick,
                      scrub->plan.target - scrub->plan.world_tick, (double)took_ms / 1000.0,
                      scrub->hold ? "held" : "running");
        return;
    }
    TORIRS_REPORT("replay: seek landed at tick %d (%d ticks simulated from %d in %.2f s, %.0f ticks/s); %s\n",
                  scrub->plan.target, scrub->plan.target - scrub->plan.from, scrub->plan.from,
                  (double)took_ms / 1000.0,
                  took_ms ? (double)(scrub->plan.target - scrub->plan.from) * 1000.0 / (double)took_ms : 0.0,
                  scrub->hold ? "held" : "running");
}

/* --- the fast plan --------------------------------------------------------- */

/* The ledger (about 230 ms for a 5.5-hour recording natively, 260 ms in
 * wasm): built at the session's login under the fast plan
 * (ReplayScrub_SessionBegin), else the first time a fast seek asks. 0 when
 * this recording cannot have one. */
static int
ledger_ready(struct ReplayScrub* scrub)
{
    struct GameProtoRevTable const* rev = scrub->rev;

    if( scrub->ledger )
        return 1;
    if( scrub->ledger_failed )
        return 0;
    if( !rev->parse_wire || !rev->player_info_state_size || !rev->player_info_state_save ||
        !rev->player_info_state_load || !rev->player_info_set_local || !rev->npc_info_read )
    {
        scrub->ledger_failed = 1;
        TORIRS_REPORT("replay: revision %s has no world ledger; seeks simulate every tick\n",
                      rev->name);
        return 0;
    }
    ReplayClassify_Check();
    scrub->ledger = ReplayLedger_Build(scrub->rec, rev);
    assert(scrub->ledger);
    {
        struct ReplayLedgerStats st;

        ReplayLedger_Stats(scrub->ledger, &st);
        TORIRS_REPORT("replay: world ledger built: %d scenes, %d zone ops, %d keys, %.1f MB in "
                      "%.0f ms\n",
                      st.scenes, st.zone_ops, st.keys, (double)st.total_bytes / (1024.0 * 1024.0),
                      st.build_ms);
    }
    return 1;
}

/* The local player's name in front of `tick`, from its appearance in the
 * ledger's keyframe: the chase withholds the PLAYER_INFO that states it. */
static void
chase_name(
    struct ReplayScrub* scrub,
    int tick)
{
    struct ReplayWorldKeyframe kf;
    struct GameProtoRevTable const* rev = scrub->rev;

    scrub->chase_name[0] = '\0';
    memset(&kf, 0, sizeof(kf));
    if( !ReplayLedger_WorldAt(scrub->ledger, tick, &kf) )
    {
        ReplayWorldKeyframe_Free(&kf);
        return;
    }
    for( int i = 0; i < kf.player_count; i++ )
    {
        struct ReplayWorldPlayer const* p = &kf.players[i];

        if( p->pid != kf.local_index )
            continue;
        for( int k = 0; k < p->op_count; k++ )
        {
            RsprotInfoOp const* op = &kf.ops[p->op_first + k];
            static struct PktPlayerAppearance decoded;
            int ok;

            if( op->kind != RSPROT_OP_APPEARANCE || !op->appearance.appearance )
                continue;
            memset(&decoded, 0, sizeof(decoded));
            ok = rev->appearance_decode
                     ? rev->appearance_decode(op->appearance.appearance, op->appearance.len, &decoded)
                     : PktPlayerAppearance_Decode(&decoded, op->appearance.appearance,
                                                  op->appearance.len);
            if( ok )
                snprintf(scrub->chase_name, sizeof(scrub->chase_name), "%s", decoded.name);
        }
    }
    ReplayWorldKeyframe_Free(&kf);
}

/* The chase's entity fold, level with the cursor (built when it is not: a
 * new chase, a restart's frame 0). */
static void
chase_state_sync(struct ReplayScrub* scrub)
{
    struct ReplayChatLine discard[16];
    int tick;
    int key;

    if( scrub->chase_state &&
        ReplayEntityState_NextFrame(scrub->chase_state) == (uint32_t)scrub->cursor )
        return;
    ReplayEntityState_Free(scrub->chase_state);
    scrub->chase_state = ReplayLedger_EntityNew(scrub->ledger);
    tick = scrub->rec->frames[scrub->cursor].tick;
    key = tick / REPLAY_LEDGER_KEY_TICKS;
    if( key > 0 && key < ReplayLedger_KeyCount(scrub->ledger) )
        ReplayLedger_EntityLoadKey(scrub->ledger, key, scrub->chase_state);
    ReplayLedger_EntityAdvance(scrub->ledger, scrub->chase_state, tick);
    while( ReplayEntityState_NextFrame(scrub->chase_state) < (uint32_t)scrub->cursor )
        ReplayLedger_EntityStepChat(scrub->ledger, scrub->chase_state, discard, 16);
}

/* A withheld chat line's speaker, by its appearance's name. */
static void
chat_name(
    struct ReplayScrub const* scrub,
    struct ReplayChatLine const* line,
    char* out,
    size_t cap)
{
    static struct PktPlayerAppearance decoded;
    struct GameProtoRevTable const* rev = scrub->rev;
    int ok;

    out[0] = '\0';
    if( !line->appearance )
        return;
    memset(&decoded, 0, sizeof(decoded));
    ok = rev->appearance_decode
             ? rev->appearance_decode(line->appearance, line->appearance_len, &decoded)
             : PktPlayerAppearance_Decode(&decoded, line->appearance, line->appearance_len);
    if( ok )
        snprintf(out, cap, "%s", decoded.name);
}

/* The fast plan from a checkpoint: RESTORE (CHASE) WORLD PREROLL PRESENT.
 * `drain_chasing`: the tick the client is part way through is a chase's. */
static void
plan_build_restore(
    struct ReplayScrub* scrub,
    int tick,
    int world_tick,
    struct ReplayCheckpoint const* cp,
    int cp_tick,
    uint64_t cp_clock_ms,
    int drain_chasing)
{
    assert(cp);
    assert(world_tick >= cp_tick);
    memset(&scrub->plan, 0, sizeof(scrub->plan));
    scrub->plan.target = tick;
    scrub->plan.from = cp_tick;
    scrub->plan.world_tick = world_tick;
    scrub->plan.restore_cp = cp;
    scrub->plan.restore_tick = cp_tick;
    scrub->plan.restore_clock_ms = cp_clock_ms;
    scrub->plan.drain_tick = staged_frontier(scrub);
    scrub->plan.drain_chasing = drain_chasing;
    scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_RESTORE;
    if( world_tick > cp_tick )
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_CHASE;
    scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_WORLD;
    scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_PREROLL;
    scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_PRESENT;
}

/* The plan for a seek to `tick` that starts at the login (`restart`) or at
 * the live client -- or, in the fast plan, at a session checkpoint when one
 * at or before the keyframe tick is nearer than either. */
static void
plan_build(
    struct ReplayScrub* scrub,
    int tick,
    int restart,
    int mid_chase)
{
    int const from_tick = restart ? 0 : staged_frontier(scrub);
    int const world_tick = tick - REPLAY_PREROLL_TICKS;
    char const* why = NULL;

    if( scrub->mode == REPLAY_SEEK_MODE_FAST && world_tick >= 1 && scrub->session_live && !scrub->restarting &&
        ledger_ready(scrub) && !ReplayLedger_InWorldEntitySpan(scrub->ledger, world_tick) )
    {
        int cp_tick = -1;
        uint64_t cp_clock = 0;
        struct ReplayCheckpoint const* cp =
            ReplayCheckpointStore_AtOrBefore(scrub->checkpoints, world_tick, &cp_tick, &cp_clock);

        /* Behind the playhead, any checkpoint beats the login; ahead of it,
         * only one past the live client beats chasing from where it is. */
        if( cp && (restart || cp_tick > from_tick) )
        {
            plan_build_restore(scrub, tick, world_tick, cp, cp_tick, cp_clock, mid_chase);
            return;
        }
    }

    memset(&scrub->plan, 0, sizeof(scrub->plan));
    scrub->plan.target = tick;
    scrub->plan.world_tick = -1;
    if( restart )
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_RESTART;

    if( scrub->mode != REPLAY_SEEK_MODE_FAST )
        why = NULL;
    else if( world_tick < 1 || world_tick <= from_tick )
        why = "within the pre-roll";
    else if( !ledger_ready(scrub) )
        why = "no world ledger";
    else if( ReplayLedger_InWorldEntitySpan(scrub->ledger, world_tick) )
        why = "the keyframe tick is inside a world-entity span the ledger does not fold";
    else
    {
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_CHASE;
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_WORLD;
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_PREROLL;
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_PRESENT;
        scrub->plan.world_tick = world_tick;
        /* The chase starts at the live client's frontier: a tick partly
         * handed out before the seek gets no tile of its own. */
        scrub->chase_tick = restart ? -1 : from_tick - 1;
        chase_name(scrub, from_tick > 0 ? from_tick : 1);
        scrub->chat_count = 0;
        scrub->chat_at = 0;
        scrub->chase_ticks = 0;
        scrub->chase_wall_start = 0;
        scrub->chase_wall_end = 0;
        return;
    }
    if( why && (world_tick >= 1 && world_tick > from_tick) )
        TORIRS_REPORT("replay: seek to tick %d simulates every tick: %s\n", tick, why);
    scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_FULLSIM;
    scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_PRESENT;
}

/* The clientclock a chase tick does not run: the tick's span on the replay
 * clock (from its first packet to the next tick's, each gap cut at
 * REPLAY_MAX_GAP_MS as playback cuts it) in client cycles, less the
 * chase_cycles / 30 of it the client does run. */
static int
chase_charge(
    struct ReplayScrub const* scrub,
    int tick)
{
    struct RsproxRecording const* rec = scrub->rec;
    uint32_t const first = ReplayLedger_FirstFrame(scrub->ledger, tick);
    uint32_t const next = ReplayLedger_FirstFrame(scrub->ledger, tick + 1);
    uint64_t span_ms = 0;
    uint64_t span_cycles;

    for( uint32_t f = first; f < next && f + 1 < (uint32_t)rec->frame_count; f++ )
    {
        uint64_t gap = rec->frames[f + 1].t_ms - rec->frames[f].t_ms;

        span_ms += gap < REPLAY_MAX_GAP_MS ? gap : REPLAY_MAX_GAP_MS;
    }
    span_cycles = span_ms / REPLAY_CYCLE_MS;
    return (int)(span_cycles - span_cycles * (uint64_t)scrub->chase_cycles / REPLAY_TICK_CYCLES);
}

/* The chase reached P: the keyframe, and whether the client's scene is
 * another. 0 when the ledger cannot say (the seek then restarts as a full
 * simulation, said). */
static int
world_step(
    struct ReplayScrub* scrub,
    struct ReplayStep* out)
{
    struct RsproxRecording const* rec = scrub->rec;
    struct ReplayWorldDelivery* d;
    int const tick = scrub->plan.world_tick;
    int same = 0;

    d = calloc(1, sizeof(*d));
    assert(d);
    d->tick = tick;
    if( !ReplayLedger_WorldAt(scrub->ledger, tick, &d->kf) )
    {
        ReplayWorldDelivery_Free(d);
        return 0;
    }
    if( d->kf.world_entity )
    {
        ReplayWorldDelivery_Free(d);
        return 0;
    }
    if( d->kf.scene >= 0 && scrub->client_scene_frame >= 0 )
    {
        int const have = ReplayLedger_SceneAt(scrub->ledger, (uint32_t)scrub->client_scene_frame + 1);
        struct ReplayLedgerScene const* row = have >= 0 ? ReplayLedger_Scene(scrub->ledger, have) : NULL;

        /* The same REBUILD, or another naming the same normal scene: the
         * client's same-zone rule would not rebuild for it either. */
        same = (uint32_t)scrub->client_scene_frame == d->kf.scene_row.frame ||
               (row && !row->instanced && !d->kf.scene_row.instanced &&
                row->zone_x == d->kf.scene_row.zone_x && row->zone_z == d->kf.scene_row.zone_z);
    }
    /* An instanced scene is never "the loaded one" to the world apply
     * (replay_world_apply.c scene_loaded), which rebuilds it from these bytes
     * whatever this says -- so it always gets them. */
    if( d->kf.scene >= 0 && (!same || d->kf.scene_row.instanced) )
    {
        struct RsproxRecordingFrame const* f = &rec->frames[d->kf.scene_row.frame];

        d->rebuild = 1;
        d->scene_wire = f->opcode;
        d->scene_len = (int)f->length;
        d->scene_bytes = malloc(f->length > 0 ? f->length : 1);
        assert(d->scene_bytes);
        memcpy(d->scene_bytes, rec->data + f->offset, f->length);
        /* The keyframe's rebuild is a REBUILD like any other to the barrier:
         * tick P waits for its MAP_BUILD_COMPLETE. */
        scrub->wait_map = 1;
        scrub->wait_tick = tick - 1;
        scrub->map_built = 0;
        scrub->wait_since_ms = scrub->wall_ms;
        scrub->world_wait = 1;
    }
    if( d->kf.scene >= 0 )
        scrub->client_scene_frame = (int)d->kf.scene_row.frame;
    /* The pre-roll plays on the recording's clock from P's first packet. */
    if( scrub->cursor < rec->frame_count )
        scrub->clock_ms = rec->frames[scrub->cursor].t_ms;
    scrub->chase_wall_end = scrub->wall_ms;
    scrub->times.chase_end_ms = scrub->wall_ms;
    scrub->times.world_ms = scrub->wall_ms;
    scrub->times.world_rebuilt = d->rebuild;
    if( !d->rebuild )
        scrub->times.world_built_ms = scrub->wall_ms;
    out->world = d;
    return 1;
}

/*
 * Step over the client packets sent before the next server one. True when one
 * of them answered a chatbox prompt: it is then out->dialog_name, and nothing
 * after it is staged until it is on the bus.
 */
static int
pass_inputs(
    struct ReplayScrub* scrub,
    struct ReplayStep* out)
{
    struct RsproxRecording const* rec = scrub->rec;

    while( scrub->next_input < rec->client_packet_count &&
           rec->inputs[scrub->next_input].frame <= scrub->cursor )
    {
        int const opcode = rec->inputs[scrub->next_input++].opcode;

        for( int i = 0; i < REPLAY_DIALOG_PROTS; i++ )
        {
            if( scrub->dialog_wire[i] == opcode )
            {
                out->dialog_name = k_dialog_names[i];
                return 1;
            }
        }
    }
    return 0;
}

static int
frame_is_rebuild(
    struct ReplayScrub const* scrub,
    struct RsproxRecordingFrame const* f)
{
    int const name = scrub->rev->packetin_code(f->opcode);

    return name == PKT_NAME_REBUILD_NORMAL || name == PKT_NAME_REBUILD_REGION;
}

/* --- life ---------------------------------------------------------------- */

struct ReplayScrub*
ReplayScrub_New(
    struct RsproxRecording const* rec,
    struct GameProtoRevTable const* rev)
{
    struct ReplayScrub* scrub;

    assert(rec);
    assert(rev);
    assert(rev->packetin_code);
    assert(rev->packetout_code);
    scrub = ToriMem_Calloc(ToriMem_Net(), 1, sizeof(*scrub));
    assert(scrub);
    scrub->rec = rec;
    scrub->rev = rev;
    scrub->tl = ReplayTimeline_Build(rec, rev);
    for( int i = 0; i < REPLAY_DIALOG_PROTS; i++ )
        scrub->dialog_wire[i] = rev->packetout_code(k_dialog_names[i]);
    scrub->mode = REPLAY_SEEK_MODE_FULLSIM;
    scrub->chase_cycles = REPLAY_TICK_CYCLES;
    scrub->client_scene_frame = -1;
    scrub->stall_step = -1;
    {
        char const* mb = getenv("TORIRS_REPLAY_CHECKPOINT_MB");
        long const budget_mb = mb && mb[0] ? strtol(mb, NULL, 10) : REPLAY_CKPT_STORE_DEFAULT_MB;

        scrub->checkpoints = ReplayCheckpointStore_New((size_t)(budget_mb > 0 ? budget_mb : 0) * 1024u * 1024u);
    }
    ReplayScrub_Rewind(scrub);
    return scrub;
}

void
ReplayScrub_Free(struct ReplayScrub* scrub)
{
    if( !scrub )
        return;
    ReplayEntityState_Free(scrub->chase_state);
    ReplayLedger_Free(scrub->ledger);
    ReplayCheckpointStore_Free(scrub->checkpoints);
    ReplayTimeline_Free(scrub->tl);
    ToriMem_Free(ToriMem_Net(), scrub);
}

void
ReplayScrub_SetSeekMode(
    struct ReplayScrub* scrub,
    enum ReplaySeekMode mode)
{
    assert(scrub);
    assert(mode == REPLAY_SEEK_MODE_FULLSIM || mode == REPLAY_SEEK_MODE_FAST);
    scrub->mode = mode;
}

enum ReplaySeekMode
ReplayScrub_SeekMode(struct ReplayScrub const* scrub)
{
    assert(scrub);
    return scrub->mode;
}

void
ReplayScrub_SetChaseCycles(
    struct ReplayScrub* scrub,
    int cycles)
{
    assert(scrub);
    assert(cycles >= 1);
    assert(cycles <= REPLAY_TICK_CYCLES);
    scrub->chase_cycles = cycles;
}

struct ReplayTimeline const*
ReplayScrub_Timeline(struct ReplayScrub const* scrub)
{
    assert(scrub);
    return scrub->tl;
}

/* --- the controls -------------------------------------------------------- */

void
ReplayScrub_SeekTick(
    struct ReplayScrub* scrub,
    int tick)
{
    int reachable;

    assert(scrub);
    if( tick < 0 )
        tick = 0;
    if( tick > scrub->rec->tick_count )
        tick = scrub->rec->tick_count;

    /* Ahead of everything handed out: the work so far is on the way there. */
    reachable = staged_frontier(scrub) <= tick;
    if( scrub->seeking && seek_step(scrub) == REPLAY_SEEK_FULLSIM && reachable )
    {
        scrub->plan.target = tick;
        TORIRS_REPORT("replay: seek retargeted to tick %d\n", tick);
    }
    else if( scrub->seeking &&
             (seek_step(scrub) == REPLAY_SEEK_CHASE || seek_step(scrub) == REPLAY_SEEK_WORLD) &&
             !scrub->restarting && tick - REPLAY_PREROLL_TICKS >= staged_frontier(scrub) &&
             !ReplayLedger_InWorldEntitySpan(scrub->ledger, tick - REPLAY_PREROLL_TICKS) )
    {
        /* The chase is on its way to the new keyframe tick too. */
        scrub->plan.target = tick;
        scrub->plan.world_tick = tick - REPLAY_PREROLL_TICKS;
        if( seek_step(scrub) == REPLAY_SEEK_WORLD )
            scrub->plan.at--;
        TORIRS_REPORT("replay: seek retargeted to tick %d (the chase goes on to %d)\n", tick,
                      scrub->plan.world_tick);
    }
    else
    {
        /* A chase or a keyframe in flight has the world withheld: the client
         * is no live client to start from, and the session restarts. */
        int const mid_chase = scrub->seeking && !scrub->restarting &&
                              (seek_step(scrub) == REPLAY_SEEK_CHASE ||
                               seek_step(scrub) == REPLAY_SEEK_WORLD);
        int restart = !reachable || mid_chase;
        int restore;

        plan_build(scrub, tick, restart, mid_chase);
        restore = scrub->plan.count > 0 && scrub->plan.steps[0] == REPLAY_SEEK_RESTORE;
        if( restore )
            restart = 0;
        else
            scrub->plan.from = restart ? 0 : ReplayScrub_Reached(scrub);
        scrub->seek_wall_start = scrub->wall_ms;
        scrub->world_wait = 0;
        memset(&scrub->times, 0, sizeof(scrub->times));
        scrub->times.start_ms = scrub->wall_ms;
        scrub->times.restarted = restart;
        scrub->times.restore_tick = restore ? scrub->plan.restore_tick : -1;
        if( restore )
            TORIRS_REPORT("replay: seek to tick %d from %d: restore the checkpoint at tick %d, chase, "
                          "keyframe, pre-roll\n",
                          tick, ReplayScrub_Reached(scrub), scrub->plan.restore_tick);
        else
            TORIRS_REPORT("replay: seek to tick %d from %d%s%s\n", tick, ReplayScrub_Reached(scrub),
                          !reachable ? " (behind the playhead: the session restarts from the login)"
                          : mid_chase ? " (a chase was in flight: the session restarts from the login)"
                                      : "",
                          scrub->plan.world_tick >= 0 ? "; chase, keyframe, pre-roll" : "");
    }
    /* Seeking from now, even when the client is already there: it lands on
     * the next ReplayScrub_ClientTicks, so a caller reading the status right
     * after the call sees the seek it made. */
    scrub->seeking = 1;
    scrub->hold_reported = 0;
}

void
ReplayScrub_Step(
    struct ReplayScrub* scrub,
    int ticks)
{
    int const from = ReplayScrub_Tick(scrub);

    scrub->hold = 1;
    ReplayScrub_SeekTick(scrub, from + ticks);
}

void
ReplayScrub_Hold(struct ReplayScrub* scrub)
{
    assert(scrub);
    if( scrub->hold )
        return;
    scrub->hold = 1;
    scrub->hold_reported = 0;
    /* A seek in flight holds at its target (try_land); otherwise at the end
     * of the tick being staged. */
    scrub->hold_tick = scrub->seeking ? scrub->plan.target : staged_frontier(scrub);
}

void
ReplayScrub_Run(struct ReplayScrub* scrub)
{
    assert(scrub);
    if( scrub->hold && !scrub->seeking )
        TORIRS_REPORT("replay: running from tick %d\n", scrub->hold_tick);
    scrub->hold = 0;
}

void
ReplayScrub_HoldAt(
    struct ReplayScrub* scrub,
    int tick)
{
    assert(scrub);
    assert(!scrub->seeking);
    assert(tick >= staged_frontier(scrub));
    if( tick > scrub->rec->tick_count )
        tick = scrub->rec->tick_count;
    scrub->hold = 1;
    scrub->hold_tick = tick;
    scrub->hold_reported = 0;
}

/* The cursor and the clock back to the start of `tick`, with the session
 * kept: the client has just restored a checkpoint taken there. */
static void
cursor_back(
    struct ReplayScrub* scrub,
    int tick,
    uint64_t clock_ms)
{
    struct RsproxRecording const* rec;
    int f = 0;
    int i = 0;

    assert(scrub->session_live);
    assert(tick >= 0);
    assert(tick <= staged_frontier(scrub));
    rec = scrub->rec;
    while( f < rec->frame_count && rec->frames[f].tick < tick )
        f++;
    /* The client packets sent before frame f went with the frames before it
     * (pass_inputs hands an input over before the frame it precedes). */
    while( i < rec->client_packet_count && rec->inputs[i].frame < f )
        i++;
    scrub->cursor = f;
    scrub->next_input = i;
    scrub->last_tick = f > 0 ? rec->frames[f - 1].tick : 0;
    scrub->clock_ms = clock_ms;
    scrub->wait_map = 0;
    scrub->map_built = 0;
    scrub->world_wait = 0;
    scrub->chase_tick = -1;
    /* Executed: exactly `tick` ticks of this session. */
    scrub->serial_base = scrub->client_serial - (unsigned)tick;
    (void)f;
}

/* --- session checkpoints -------------------------------------------------- */

struct ReplayCheckpointStore*
ReplayScrub_Checkpoints(struct ReplayScrub* scrub)
{
    assert(scrub);
    return scrub->checkpoints;
}

int
ReplayScrub_CheckpointWanted(
    struct ReplayScrub const* scrub,
    int* out_tick,
    uint64_t* out_clock_ms)
{
    int tick;

    assert(scrub);
    assert(out_tick);
    assert(out_clock_ms);
    if( !scrub->session_live || scrub->restarting || scrub->reconnecting || scrub->cursor == 0 )
        return 0;
    if( scrub->seeking )
    {
        enum ReplaySeekStepKind const step = seek_step(scrub);

        if( step == REPLAY_SEEK_RESTORE || step == REPLAY_SEEK_RESTART || step == REPLAY_SEEK_WORLD )
            return 0;
        /* A chase tick's COORD charges the client clock before its packets:
         * once it has gone, the boundary is no longer clean. */
        if( step == REPLAY_SEEK_CHASE && scrub->chase_tick >= staged_frontier(scrub) )
            return 0;
    }
    tick = staged_frontier(scrub);
    if( ReplayScrub_Reached(scrub) != tick )
        return 0;
    if( !ReplayCheckpointStore_Wants(scrub->checkpoints, tick) )
        return 0;
    *out_tick = tick;
    *out_clock_ms = scrub->clock_ms;
    return 1;
}

struct ReplayCheckpoint const*
ReplayScrub_RestoreWanted(struct ReplayScrub const* scrub)
{
    assert(scrub);
    if( !scrub->seeking || seek_step(scrub) != REPLAY_SEEK_RESTORE || !scrub->session_live )
        return NULL;
    if( ReplayScrub_Reached(scrub) < scrub->plan.drain_tick )
        return NULL;
    return scrub->plan.restore_cp;
}

void
ReplayScrub_Restored(
    struct ReplayScrub* scrub,
    double cost_ms)
{
    int tick;

    assert(scrub);
    assert(scrub->seeking);
    assert(seek_step(scrub) == REPLAY_SEEK_RESTORE);
    tick = scrub->plan.restore_tick;
    cursor_back(scrub, tick, scrub->plan.restore_clock_ms);
    scrub->plan.at++;
    scrub->plan.restore_cp = NULL;
    scrub->times.restored_ms = scrub->wall_ms;
    scrub->times.restore_cost_ms = cost_ms;
    /* The chase starts at the checkpoint, as one from the live client starts
     * at its frontier. */
    scrub->chase_tick = tick - 1;
    chase_name(scrub, tick > 0 ? tick : 1);
    scrub->chat_count = 0;
    scrub->chat_at = 0;
    scrub->chase_ticks = 0;
    scrub->chase_wall_start = 0;
    scrub->chase_wall_end = 0;
    TORIRS_REPORT("replay: restored the checkpoint at tick %d in %.2f ms; the cursor is back there\n", tick,
                  cost_ms);
}

void
ReplayScrub_SeekFromCheckpoint(
    struct ReplayScrub* scrub,
    int tick,
    struct ReplayCheckpoint const* cp,
    int cp_tick,
    uint64_t cp_clock_ms,
    int preroll)
{
    assert(scrub);
    assert(cp);
    assert(!scrub->seeking);
    assert(scrub->session_live);
    assert(preroll >= 0);
    assert(tick - preroll >= cp_tick);
    assert(ledger_ready(scrub));
    plan_build_restore(scrub, tick, tick - preroll, cp, cp_tick, cp_clock_ms, 0);
    scrub->seek_wall_start = scrub->wall_ms;
    scrub->world_wait = 0;
    memset(&scrub->times, 0, sizeof(scrub->times));
    scrub->times.start_ms = scrub->wall_ms;
    scrub->times.restore_tick = cp_tick;
    scrub->seeking = 1;
    scrub->hold_reported = 0;
    TORIRS_REPORT("replay: seek to tick %d from the checkpoint at tick %d (keyframe at %d)\n", tick, cp_tick,
                  tick - preroll);
}

/* --- what it says -------------------------------------------------------- */

int
ReplayScrub_Tick(struct ReplayScrub const* scrub)
{
    assert(scrub);
    if( scrub->seeking )
        return scrub->plan.target;
    if( scrub->hold )
        return scrub->hold_tick;
    return staged_frontier(scrub);
}

int
ReplayScrub_Reached(struct ReplayScrub const* scrub)
{
    assert(scrub);
    return scrub->session_live ? (int)(scrub->client_serial - scrub->serial_base) : 0;
}

int
ReplayScrub_LastTick(struct ReplayScrub const* scrub)
{
    assert(scrub);
    return scrub->last_tick;
}

int
ReplayScrub_Held(struct ReplayScrub const* scrub)
{
    assert(scrub);
    return scrub->hold;
}

int
ReplayScrub_Seeking(struct ReplayScrub const* scrub)
{
    assert(scrub);
    return scrub->seeking;
}

int
ReplayScrub_Plan(
    struct ReplayScrub const* scrub,
    struct ReplaySeekPlan* out)
{
    assert(scrub);
    assert(out);
    if( !scrub->seeking )
        return 0;
    *out = scrub->plan;
    return 1;
}

char const*
ReplayScrub_SeekPhase(struct ReplayScrub const* scrub)
{
    assert(scrub);
    if( !scrub->seeking )
        return "";
    if( scrub->restarting || seek_step(scrub) == REPLAY_SEEK_RESTART )
        return "restart";
    if( seek_step(scrub) == REPLAY_SEEK_RESTORE )
        return "restore";
    if( seek_step(scrub) == REPLAY_SEEK_CHASE )
        return "chase";
    /* The keyframe, and the scene build it waits on. */
    if( seek_step(scrub) == REPLAY_SEEK_WORLD ||
        (scrub->world_wait && scrub->wait_map && !scrub->map_built) )
        return "world";
    return "preroll";
}

void
ReplayScrub_Pos(
    struct ReplayScrub const* scrub,
    struct ReplayPos* out)
{
    int const tick = ReplayScrub_Tick(scrub);
    int const row = tick < scrub->tl->tick_count ? tick : scrub->tl->tick_count - 1;
    int const last = scrub->last_tick < scrub->tl->tick_count ? scrub->last_tick : scrub->tl->tick_count;
    uint32_t const last_ms = ReplayTimeline_MsAtTick(scrub->tl, last);

    assert(out);
    memset(out, 0, sizeof(*out));
    out->tick = tick;
    out->t_ms = scrub->clock_ms;
    out->wall_ms = scrub->tl->timestamp_ms + (int64_t)scrub->clock_ms;
    out->scene = row >= 0 && scrub->tl->ticks[row].scene != REPLAY_SCENE_NONE ? scrub->tl->ticks[row].scene : -1;
    if( !scrub->seeking && !scrub->hold && scrub->clock_ms > last_ms )
        out->cycle = (int)((scrub->clock_ms - last_ms) / REPLAY_CYCLE_MS);
}

uint64_t
ReplayScrub_ClockMs(struct ReplayScrub const* scrub)
{
    assert(scrub);
    return scrub->clock_ms;
}

int
ReplayScrub_WaitingMap(struct ReplayScrub const* scrub)
{
    assert(scrub);
    return scrub->wait_map && !scrub->map_built;
}

/* --- the transport's side ------------------------------------------------ */

void
ReplayScrub_Rewind(struct ReplayScrub* scrub)
{
    assert(scrub);
    scrub->cursor = 0;
    scrub->next_input = 0;
    scrub->last_tick = 0;
    scrub->clock_ms = 0;
    scrub->wait_map = 0;
    scrub->map_built = 0;
    scrub->session_live = 0;
    scrub->reconnecting = 0;
    scrub->world_wait = 0;
    /* client_scene_frame stays: a new connection does not unload the
     * client's scene (nothing clears App.world_active), so a seek's keyframe
     * after a restart can keep the scene it finds. */
    /* A chase in flight starts again at the login. */
    scrub->chase_tick = -1;
    if( scrub->seeking )
    {
        scrub->plan.from = 0;
        /* A new connection is the restart a pending RESTART asks for: a seek
         * made after the recording finished starts here, with the client's
         * next login. */
        if( seek_step(scrub) == REPLAY_SEEK_RESTART )
        {
            scrub->plan.at++;
            scrub->restarting = 1;
        }
    }
    else if( scrub->hold )
        scrub->hold_tick = 0;
}

void
ReplayScrub_SessionBegin(struct ReplayScrub* scrub)
{
    assert(scrub);
    scrub->serial_base = scrub->client_serial;
    scrub->session_live = 1;
    if( scrub->seeking && scrub->restarting )
        scrub->times.session_ms = scrub->wall_ms;
    scrub->restarting = 0;
    /* The ledger is built here, at the login, not at the first fast seek:
     * the loading screen hides the quarter second it costs in a browser, and
     * the first click no longer pays it. */
    if( scrub->mode == REPLAY_SEEK_MODE_FAST )
        (void)ledger_ready(scrub);
    try_land(scrub);
}

void
ReplayScrub_ConnectionLost(struct ReplayScrub* scrub)
{
    assert(scrub);
    assert(scrub->session_live);
    scrub->reconnecting = 1;
}

void
ReplayScrub_ConnectionResumed(struct ReplayScrub* scrub)
{
    assert(scrub);
    assert(scrub->session_live);
    scrub->reconnecting = 0;
    if( scrub->seeking && scrub->restarting )
        scrub->times.session_ms = scrub->wall_ms;
    try_land(scrub);
}

int
ReplayScrub_NextReconnect(struct ReplayScrub const* scrub)
{
    struct RsproxRecording const* rec;
    int f;

    assert(scrub);
    rec = scrub->rec;
    for( f = scrub->cursor; f < rec->frame_count; f++ )
    {
        if( rec->frames[f].opcode != RSPROX_RECORDING_PACKET_GROUP_END )
            break;
    }
    if( f < rec->frame_count && rec->frames[f].opcode == RSPROX_RECORDING_RECONNECT )
        return f;
    return -1;
}

void
ReplayScrub_ClientTicks(
    struct ReplayScrub* scrub,
    unsigned serial)
{
    assert(scrub);
    scrub->client_serial = serial;
    try_land(scrub);
    if( scrub->hold && !scrub->seeking && !scrub->hold_reported && scrub->session_live &&
        ReplayScrub_Reached(scrub) >= scrub->hold_tick )
    {
        scrub->hold_reported = 1;
        TORIRS_REPORT("replay: held at tick %d\n", scrub->hold_tick);
    }
}

void
ReplayScrub_MapBuilt(struct ReplayScrub* scrub)
{
    assert(scrub);
    if( scrub->wait_map )
        scrub->map_built = 1;
    if( scrub->wait_map && scrub->world_wait && !scrub->times.world_built_ms )
        scrub->times.world_built_ms = scrub->wall_ms;
}

void
ReplayScrub_SeekTimes(
    struct ReplayScrub const* scrub,
    struct ReplaySeekTimes* out)
{
    assert(scrub);
    assert(out);
    *out = scrub->landed;
}

void
ReplayScrub_Wall(
    struct ReplayScrub* scrub,
    uint64_t wall_ms)
{
    assert(scrub);
    scrub->wall_ms = wall_ms;
}

/* What the seek is waiting on that bounds itself, or NULL: the watchdog
 * counts that time apart (REPLAY_SEEK_WAIT_STALL_MS). */
static char const*
seek_waiting_on(struct ReplayScrub const* scrub)
{
    if( scrub->wait_map && !scrub->map_built )
        return "the map-build barrier";
    if( scrub->restarting || !scrub->session_live )
        return "the restart's login";
    if( scrub->reconnecting )
        return "the recorded reconnect's login";
    if( scrub->client_busy )
        return "the client's load";
    return NULL;
}

/* A seek that has stopped moving fails loudly and falls back to the login and
 * a full simulation, never an endless "seeking" (REPLAY_SEEK_STALL_MS). */
static void
seek_watchdog(struct ReplayScrub* scrub)
{
    int const reached = ReplayScrub_Reached(scrub);
    uint64_t const last = scrub->stall_last_wall;
    uint64_t dt;
    char const* waiting;
    int step;

    scrub->stall_last_wall = scrub->wall_ms;
    if( !scrub->seeking )
    {
        scrub->stall_run_ms = 0;
        scrub->stall_wait_ms = 0;
        scrub->stall_step = -1;
        return;
    }
    step = scrub->plan.at;
    if( scrub->cursor != scrub->stall_cursor || reached != scrub->stall_reached || step != scrub->stall_step )
    {
        scrub->stall_cursor = scrub->cursor;
        scrub->stall_reached = reached;
        scrub->stall_step = step;
        scrub->stall_run_ms = 0;
        scrub->stall_wait_ms = 0;
        return;
    }
    dt = last && scrub->wall_ms > last ? scrub->wall_ms - last : 0;
    if( dt > REPLAY_SEEK_STALL_POLL_MS )
        dt = REPLAY_SEEK_STALL_POLL_MS;
    waiting = seek_waiting_on(scrub);
    if( waiting )
        scrub->stall_wait_ms += dt;
    else
        scrub->stall_run_ms += dt;
    if( scrub->stall_run_ms < REPLAY_SEEK_STALL_MS && scrub->stall_wait_ms < REPLAY_SEEK_WAIT_STALL_MS )
        return;
    {
        int const target = scrub->plan.target;

        if( scrub->stall_run_ms >= REPLAY_SEEK_STALL_MS )
            TORIRS_ERR("replay: the seek to tick %d made no progress for %d s of running frames (phase %s, %d "
                       "ticks executed, cursor at frame %d); dropping %d checkpoints and restarting from the "
                       "login, simulating every tick\n",
                       target, REPLAY_SEEK_STALL_MS / 1000, ReplayScrub_SeekPhase(scrub), reached, scrub->cursor,
                       ReplayCheckpointStore_Count(scrub->checkpoints));
        else
            TORIRS_ERR("replay: the seek to tick %d waited %d s on %s, past every bound it has (phase %s, %d "
                       "ticks executed, cursor at frame %d); dropping %d checkpoints and restarting from the "
                       "login, simulating every tick\n",
                       target, REPLAY_SEEK_WAIT_STALL_MS / 1000, waiting ? waiting : "a bounded wait",
                       ReplayScrub_SeekPhase(scrub), reached, scrub->cursor,
                       ReplayCheckpointStore_Count(scrub->checkpoints));
        ReplayCheckpointStore_Clear(scrub->checkpoints);
        memset(&scrub->plan, 0, sizeof(scrub->plan));
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_RESTART;
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_FULLSIM;
        scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_PRESENT;
        scrub->plan.target = target;
        scrub->plan.world_tick = -1;
        scrub->restarting = 0;
        scrub->wait_map = 0;
        scrub->stall_run_ms = 0;
        scrub->stall_wait_ms = 0;
        scrub->stall_step = -1;
        scrub->times.restarted = 1;
        scrub->times.restore_tick = -1;
    }
}

void
ReplayScrub_ClientBusy(
    struct ReplayScrub* scrub,
    int busy)
{
    assert(scrub);
    scrub->client_busy = busy != 0;
}

void
ReplayScrub_Advance(
    struct ReplayScrub* scrub,
    uint64_t step_ms,
    uint64_t wall_ms)
{
    assert(scrub);
    scrub->wall_ms = wall_ms;
    seek_watchdog(scrub);
    if( scrub->wait_map && !scrub->map_built && wall_ms - scrub->wait_since_ms > REPLAY_MAP_WAIT_MS )
    {
        TORIRS_ERR("replay: no MAP_BUILD_COMPLETE %d s after the tick %d rebuild; playing on\n",
                   REPLAY_MAP_WAIT_MS / 1000, scrub->wait_tick);
        scrub->map_built = 1;
    }
    /* The barrier holds the clock too: the time the client spends building
     * is not recorded time, so the packets behind it keep their spacing. */
    if( scrub->wait_map && !scrub->map_built )
        return;
    /* A chase runs the recording's clock 30 / chase_cycles times as fast:
     * a tick of it is chase_cycles client cycles, and the rest of the tick's
     * clientclock is charged (ReplayScrub_Next, chase_charge). */
    if( scrub->seeking && seek_step(scrub) == REPLAY_SEEK_CHASE )
        step_ms = step_ms * REPLAY_TICK_CYCLES / (uint64_t)scrub->chase_cycles;
    /* So does a hold, or a seek, at its boundary. */
    if( at_stop(scrub) )
    {
        clamp_clock(scrub);
        return;
    }
    scrub->clock_ms += step_ms;
}

enum ReplayNext
ReplayScrub_Next(
    struct ReplayScrub* scrub,
    struct ReplayStep* out)
{
    struct RsproxRecording const* rec;

    assert(scrub);
    assert(out);
    rec = scrub->rec;
    memset(out, 0, sizeof(*out));
    out->frame = -1;
    out->dialog_name = -1;

    for( ;; )
    {
        struct RsproxRecordingFrame const* f;
        int chasing;

        if( scrub->seeking && seek_step(scrub) == REPLAY_SEEK_RESTART )
        {
            /* The transport drops the session and rewinds (ReplayScrub_Rewind);
             * the client's next login plays from frame 0. */
            scrub->plan.at++;
            scrub->restarting = 1;
            return REPLAY_NEXT_RESTART;
        }
        /* Nothing goes before the new session: a chase plays its login. */
        if( scrub->seeking && scrub->restarting )
            return REPLAY_NEXT_WAIT;
        if( scrub->seeking && seek_step(scrub) == REPLAY_SEEK_WORLD )
        {
            /* One keyframe in flight: the next waits for the client to take
             * the last (replay_world.h). */
            if( ReplayWorld_Pending() )
                return REPLAY_NEXT_WAIT;
            if( !world_step(scrub, out) )
            {
                /* What the chase skipped cannot be put back: start again,
                 * simulating every tick. */
                int const target = scrub->plan.target;

                TORIRS_REPORT("replay: the ledger has no keyframe at tick %d; the seek to %d "
                              "restarts and simulates every tick\n",
                              scrub->plan.world_tick, target);
                memset(&scrub->plan, 0, sizeof(scrub->plan));
                scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_RESTART;
                scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_FULLSIM;
                scrub->plan.steps[scrub->plan.count++] = REPLAY_SEEK_PRESENT;
                scrub->plan.target = target;
                scrub->plan.world_tick = -1;
                continue;
            }
            scrub->plan.at++;
            assert(seek_step(scrub) == REPLAY_SEEK_PREROLL);
            return REPLAY_NEXT_WORLD;
        }
        if( scrub->cursor >= rec->frame_count )
        {
            /* A seek to the end lands when the last tick has executed; a hold
             * at the end keeps the session. */
            if( scrub->seeking || scrub->hold )
                return REPLAY_NEXT_WAIT;
            return REPLAY_NEXT_END;
        }
        if( pass_inputs(scrub, out) )
            return REPLAY_NEXT_DIALOG;
        f = &rec->frames[scrub->cursor];
        if( scrub->wait_map && f->tick > scrub->wait_tick )
        {
            if( !scrub->map_built )
                return REPLAY_NEXT_WAIT;
            scrub->wait_map = 0;
        }
        /* A RESTORE finishing a chase tick keeps its world withheld. */
        chasing = scrub->seeking && (seek_step(scrub) == REPLAY_SEEK_CHASE ||
                                     (seek_step(scrub) == REPLAY_SEEK_RESTORE && scrub->plan.drain_chasing));
        if( chasing && seek_step(scrub) == REPLAY_SEEK_CHASE && f->tick >= scrub->plan.world_tick )
        {
            /* Every tick in front of P handed out: the keyframe next. */
            scrub->plan.at++;
            continue;
        }
        if( f->tick >= stop_tick(scrub) )
            return REPLAY_NEXT_WAIT;
        if( f->t_ms > scrub->clock_ms )
        {
            /* A stall in the recording: skip what is left of it. The clock
             * jumps, so everything after keeps its own spacing. */
            if( scrub->cursor > 0 &&
                scrub->clock_ms >= rec->frames[scrub->cursor - 1].t_ms + REPLAY_MAX_GAP_MS )
                scrub->clock_ms = f->t_ms;
            else
                return REPLAY_NEXT_WAIT;
        }
        /* A chase tick opens with the tile the ledger says, and its world and
         * transient packets are not handed out -- tick 0's too: the login's
         * REBUILD would build a scene the keyframe then replaces (or that the
         * client still has from before a restart), and the player table it
         * seeds is the keyframe's to load. On the recording's clock
         * throughout, run 30 / chase_cycles times as fast
         * (ReplayScrub_Advance). */
        if( chasing )
        {
            /* The chat lines of the PLAYER_INFO just withheld, where it was. */
            if( scrub->chat_at < scrub->chat_count )
            {
                struct ReplayChatLine const* line = &scrub->chat[scrub->chat_at++];

                chat_name(scrub, line, out->name, sizeof(out->name));
                out->chat_say = line->say;
                out->chat_crown = line->crown;
                out->chat_colour_effect = line->colour_effect;
                out->chat_data = line->say ? (uint8_t const*)line->text : line->data;
                out->chat_len = line->say ? (int)strlen(line->text) : line->len;
                return REPLAY_NEXT_CHAT;
            }
            if( f->tick > scrub->chase_tick )
            {
                if( scrub->chase_ticks == 0 )
                {
                    scrub->chase_wall_start = scrub->wall_ms;
                    scrub->times.chase_start_ms = scrub->wall_ms;
                }
                scrub->chase_ticks++;
                scrub->chase_tick = f->tick;
                /* Tick 0's tile is the one its login REBUILD seeds. */
                out->coord = (int)ReplayLedger_LocalTile(scrub->ledger, f->tick > 0 ? f->tick : 1);
                out->charge = chase_charge(scrub, f->tick);
                snprintf(out->name, sizeof(out->name), "%s", scrub->chase_name);
                scrub->chase_name[0] = '\0';
                return REPLAY_NEXT_COORD;
            }
            if( f->opcode != RSPROX_RECORDING_RECONNECT &&
                f->opcode != RSPROX_RECORDING_PACKET_GROUP_END )
            {
                int const name = scrub->rev->packetin_code(f->opcode);
                enum ReplayPktClass const cls =
                    ReplayClassify_Packet(name >= 0 ? (enum GameProtoPktName)name : PKT_NAME_NONE);

                if( cls == REPLAY_PKT_WORLD || cls == REPLAY_PKT_TRANSIENT )
                {
                    /* Withheld: never enciphered, so both ciphers stay in
                     * step, and nothing to drop on the client side. The fold
                     * steps over it, and the public chat of a PLAYER_INFO --
                     * which the chatbox keeps -- is owed in its place. */
                    chase_state_sync(scrub);
                    scrub->chat_count = ReplayLedger_EntityStepChat(
                        scrub->ledger, scrub->chase_state, scrub->chat, 16);
                    scrub->chat_at = 0;
                    scrub->cursor++;
                    continue;
                }
            }
            /* Handed out: the fold steps over it too. */
            chase_state_sync(scrub);
            {
                struct ReplayChatLine none[16];

                (void)ReplayLedger_EntityStepChat(scrub->ledger, scrub->chase_state, none, 16);
            }
        }
        /* RSProx's pseudo-packets are the proxy's notes, not Jagex bytes. A
         * PACKET_GROUP_END closes what PACKET_GROUP_START already sized. A
         * RECONNECT is where the recorded session's connection ended and the
         * server answered its reconnect: the transport ends this connection
         * there and answers the client's own reconnect with the frame's
         * block (REPLAY_NEXT_RESUME). */
        if( f->opcode == RSPROX_RECORDING_PACKET_GROUP_END )
        {
            scrub->cursor++;
            continue;
        }
        out->frame = scrub->cursor++;
        scrub->last_tick = f->tick;
        if( f->opcode == RSPROX_RECORDING_RECONNECT )
            return REPLAY_NEXT_RESUME;
        /* The very REBUILD_NORMAL the client built last, handed out again after
         * a checkpoint restore moved the cursor back over it: the client's
         * same-zone early-out sends no MAP_BUILD_COMPLETE for it, so there is
         * nothing to wait for. */
        if( frame_is_rebuild(scrub, f) && out->frame == scrub->client_scene_frame &&
            scrub->rev->packetin_code(f->opcode) == PKT_NAME_REBUILD_NORMAL )
            return REPLAY_NEXT_FRAME;
        if( frame_is_rebuild(scrub, f) )
        {
            static int trace = -1;

            if( trace < 0 )
                trace = getenv("TORIRS_REPLAY_TRACE_REBUILD") != NULL;
            if( trace )
                TORIRS_REPORT("replay: REBUILD handed out at tick %d (frame %d)\n", f->tick, out->frame);
            scrub->wait_map = 1;
            scrub->wait_tick = f->tick;
            scrub->map_built = 0;
            scrub->wait_since_ms = scrub->wall_ms;
            scrub->client_scene_frame = out->frame;
        }
        return REPLAY_NEXT_FRAME;
    }
}
