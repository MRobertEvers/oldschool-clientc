#ifndef SRC_REPLAY_REPLAY_TIMELINE_H
#define SRC_REPLAY_REPLAY_TIMELINE_H

/*
 * Where a replay is in time, and what happened where: the tick table and the
 * marker lanes of an RSProx recording (docs: the replay scrubber design,
 * "Structure 1" and "Structure 2").
 *
 * A pure function of an indexed recording (net/rsprox_recording.h) and the
 * revision table that framed it, built in one pass when the recording opens.
 * It needs no client, so it is tested without one.
 *
 * THE TICK. SERVER_TICK_END is the only point where the stream is between two
 * states, so a position is a tick: tick t is "every server packet of ticks
 * < t has been executed and ticks[t].first_frame is the next one". Ticks run
 * 0..tick_count; tick_count itself is the end (only what follows the last
 * SERVER_TICK_END, if anything, lies beyond it).
 *
 * THREE CLOCKS, ONE STORED. A tick's recording time is ticks[t].t_ms (ms since
 * the login); the wall clock is timestamp_ms + t_ms. Ticks are 600 ms apart
 * except across a stall (REPLAY_MAX_GAP_MS), which the replay skips and the
 * table marks.
 */

#include <stddef.h>
#include <stdint.h>

struct RsproxRecording;
struct GameProtoRevTable;

/* The longest recorded silence played as silence. A server writes every
 * 600 ms tick; a longer gap is the recorder's side stalling (a frozen client,
 * a suspended machine), and the recorder's own session survived it. Played in
 * full, a gap past NET_LINK_TIMEOUT_MS reads to the client as a dead server:
 * RSProx 4407 is silent for 18.8 s after tick 747, so the replay dropped and
 * restarted from the login every time it got there. The scrubber skips what
 * is left of a longer gap; the timeline marks it. */
#define REPLAY_MAX_GAP_MS 5000

/* Density levels per lane: level k counts marks per 2^k ticks. 2^15 ticks is
 * five and a half hours, so the top level of the longest recording is a
 * handful of cells. */
#define REPLAY_LANE_LEVELS 16

/* ReplayTick.flags */
enum
{
    /** The gap before one of this tick's packets is a stall: the replay skips
     *  it, and the bar draws a break. */
    REPLAY_TICK_STALL_BEFORE = 1 << 0,
    /** The session moved to a new connection in this tick (RSProx RECONNECT). */
    REPLAY_TICK_RECONNECT = 1 << 1,
    /** IF_OPENTOP in this tick. */
    REPLAY_TICK_TOP_OPENED = 1 << 2,
    /** World-entity packets in this tick (WORLDENTITY_INFO, REBUILD_WORLDENTITY,
     *  SET_ACTIVE_WORLD): a sailing stretch no ledger can fold yet. */
    REPLAY_TICK_WORLD_ENTITY = 1 << 3,
    /** A REBUILD_NORMAL / REBUILD_REGION in this tick. */
    REPLAY_TICK_REBUILD = 1 << 4,
};

/* No REBUILD is in force yet (ReplayTick.scene). */
#define REPLAY_SCENE_NONE 0xFFFF

struct ReplayTick
{
    /** frames[first_frame] is the tick's first server packet. */
    uint32_t first_frame;
    /** Recording time of that packet, ms since the login. */
    uint32_t t_ms;
    /** inputs[first_input] is the first client packet sent at or after it. */
    uint32_t first_input;
    /** Which REBUILD is in force (the last one at or before this tick): an
     *  index into scenes[], or REPLAY_SCENE_NONE. */
    uint16_t scene;
    uint16_t flags;
};

/** One REBUILD_NORMAL / REBUILD_REGION. */
struct ReplayScene
{
    uint32_t frame;
    uint32_t tick;
    /** The scene's base tile: rev->scene_base, or the (zone - 6) * 8 rule the
     *  table states for a NULL one; -1, -1 when the packet did not parse. */
    int base_x;
    int base_z;
};

enum ReplayLaneId
{
    /** One mark per REBUILD; arg = index into scenes[]. */
    REPLAY_LANE_SCENE = 0,
    /** MIDI_SONG / MIDI_SONG_WITHSECONDARY; arg = the track id. */
    REPLAY_LANE_MUSIC,
    /** RECONNECT, LOGOUT*, stalls; kind = enum ReplaySessionKind, arg = the
     *  silence a stall skipped (ms). */
    REPLAY_LANE_SESSION,
    /** IF_OPENTOP and modal IF_OPENSUB; kind = enum ReplayInterfaceKind, arg =
     *  the interface id. */
    REPLAY_LANE_INTERFACE,
    /** MESSAGE_GAME; arg = the text's offset (ReplayTimeline_ChatText). */
    REPLAY_LANE_CHAT,
    /** No marks: its density counts server packets (the heat strip). */
    REPLAY_LANE_ACTIVITY,
    REPLAY_LANE_COUNT,
};

enum ReplaySessionKind
{
    REPLAY_SESSION_RECONNECT = 0,
    REPLAY_SESSION_LOGOUT,
    REPLAY_SESSION_STALL,
};

enum ReplayInterfaceKind
{
    REPLAY_INTERFACE_TOP = 0,
    REPLAY_INTERFACE_MODAL,
};

/* The longest chat line a mark keeps, in bytes. */
#define REPLAY_CHAT_TEXT_MAX 96

struct ReplayMark
{
    uint32_t tick;
    uint32_t kind;
    uint32_t arg;
};

struct ReplayLane
{
    /** Sorted by tick; next and previous are a binary search. */
    struct ReplayMark* marks;
    int count;
    int cap;
    /** density[k][i]: marks (activity: packets) in ticks [i << k, (i + 1) << k).
     *  density_cells[k] cells, enough to hold tick tick_count. */
    uint32_t* density[REPLAY_LANE_LEVELS];
    int density_cells[REPLAY_LANE_LEVELS];
};

struct ReplayTimeline
{
    int tick_count;
    uint64_t duration_ms;
    /** The recording header's timestamp (unix ms): the wall clock at t_ms 0. */
    int64_t timestamp_ms;
    /** tick_count rows. */
    struct ReplayTick* ticks;
    /** Server packets per tick, tick_count of them (RSProx's pseudo-packets
     *  included: they are frames the replay walks). */
    uint32_t* activity;
    struct ReplayScene* scenes;
    int scene_count;
    struct ReplayLane lanes[REPLAY_LANE_COUNT];
    /** MESSAGE_GAME texts, each NUL-terminated, cut to REPLAY_CHAT_TEXT_MAX. */
    char* chat_text;
    size_t chat_len;
    size_t chat_cap;
};

/**
 * The tick table and lanes of `rec` (indexed: RsproxRecording_Index), whose
 * packets `rev` frames and decodes (rev->parse_wire). Allocated in
 * ToriMem_Net(). The revision's player-info state is saved and restored
 * around the pass: a login REBUILD's decode seeds it.
 */
struct ReplayTimeline*
ReplayTimeline_Build(
    struct RsproxRecording const* rec,
    struct GameProtoRevTable const* rev);

/** Accepts NULL. */
void
ReplayTimeline_Free(struct ReplayTimeline* tl);

/** The tick in progress at recording time `t_ms`: the last tick whose first
 *  packet is at or before it (0 before the first), tick_count past the end. */
int
ReplayTimeline_TickAtMs(
    struct ReplayTimeline const* tl,
    uint32_t t_ms);

/** Recording time of tick `tick` (0..tick_count): its first packet's, and
 *  duration_ms for tick_count. */
uint32_t
ReplayTimeline_MsAtTick(
    struct ReplayTimeline const* tl,
    int tick);

/** The tick of the first mark in `lane` after `tick`, or -1. */
int
ReplayTimeline_NextMark(
    struct ReplayTimeline const* tl,
    int lane,
    int tick);

/** The tick of the last mark in `lane` before `tick`, or -1. */
int
ReplayTimeline_PrevMark(
    struct ReplayTimeline const* tl,
    int lane,
    int tick);

/** A chat mark's text. */
char const*
ReplayTimeline_ChatText(
    struct ReplayTimeline const* tl,
    struct ReplayMark const* mark);

/**
 * The page's timeline() object (the scrubber contract): ticks, duration_ms,
 * timestamp_ms, tick_ms (tick_count + 1 entries), activity and the five mark
 * lanes. malloc'd, because the page releases it with _free.
 */
char*
ReplayTimeline_Json(struct ReplayTimeline const* tl);

#endif
