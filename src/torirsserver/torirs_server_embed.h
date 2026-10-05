#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_EMBED_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_EMBED_H

/*
 * The server hosted inside another process, with no socket anywhere.
 *
 * The client half of this already worked: `struct ToriRS_Network` never touches
 * a descriptor — bytes arrive through ToriRS_Network_HandleCmd(NET_RECV) and
 * leave through ToriRS_Network_PopOut, and the platform layer is what bridges
 * that to a real socket. So an in-process game is just those two byte streams
 * crossed over with the server's, which is exactly what this exposes:
 *
 *      client PopOut  --->  ToriRSServer_EmbedWrite   (client -> server)
 *      client NET_RECV <---  ToriRSServer_EmbedRead   (server -> client)
 *
 * with ToriRSServer_EmbedPump in between to let the server act on what it was
 * given. See src/platform/net_transport_embed.c for that bridge, and
 * src/torirsserver/test/embed_test.c for the whole loop driven by hand.
 *
 * What made this possible is not this file. It is that the login handshake
 * stopped blocking (torirs_server_session.c): client and server share one thread
 * here, so a server that waits for bytes is a server that waits forever.
 *
 * Threading: none. Every call must come from the same thread, and the server
 * only advances inside ToriRSServer_EmbedPump — there is no background tick.
 *
 * One exception to "no socket": a PARTY (below). The leader's embed may also
 * accept other client processes over a loopback link, and then a pump that
 * runs a tick first waits, inside the pump, for every member to reach the
 * same boundary. Nothing of it exists unless the host attaches a listener.
 */

#include <stdint.h>

enum
{
    /**
     * Connections one embedded world can hold.
     *
     * A *client* limit, not a world limit: several clients in one embed is the
     * point of the array, and `ToriRSServer_EmbedStart` refusing a second embed is
     * about the process-wide cache tables, not about players. Kept at or below
     * TORIRSSERVER_PLAYER_MAX so a connection that handshakes always gets a slot.
     */
    TORIRSSERVER_EMBED_CLIENT_MAX = 4,
};

struct ToriRSServerEmbed;
struct ToriRSServer;
struct ToriRSServerPlayer;

/**
 * Bring up the static data and open one embedded world, with client 0 already
 * connected.
 *
 * `rev_name` is the connecting client's protocol ("osrs230", "osrs239"). The
 * embed and its client are two ends of one in-process queue pair, so unlike the
 * socket server there is no deployment where they may legitimately differ — a
 * mismatch reads the login block's RSA size out of position and dies as "rsa
 * decrypt failed". Pass the client's revision and the wire follows it; NULL
 * falls back to TORIRSSERVER_REV, then to the default (hosts with no client
 * revision of their own, e.g. embed_test).
 *
 * Returns NULL when the loaders failed hard enough that there is nothing to
 * serve. A missing content tree is *not* that — the server runs without one.
 * Loading is the same sequence the socket server uses (torirs_server_boot.c), so an
 * embedded world and a socket world differ in no respect but their transport.
 */
struct ToriRSServerEmbed*
ToriRSServer_EmbedStart(char const* rev_name);

/**
 * Open another connection to the same world. Returns its client id, or -1.
 *
 * This is the whole of what "two players in one process" needs from the host
 * side: a second byte-queue pair and a second session. Everything after it —
 * the handshake, the pool slot, the login burst — is the same code the first
 * client ran, which is what makes the test a test of the *server* rather than
 * of a second code path.
 */
int
ToriRSServer_EmbedConnect(struct ToriRSServerEmbed* embed);

/**
 * Close one client, as a dropped socket closes one.
 *
 * The same three calls the socket server makes when `ToriRSServer_SessionAlive`
 * goes false (torirs_server_main.c `serve`'s tail): release the pool slot, free the
 * session, free the byte queues. It is deliberately *not* the whole of that
 * tail — `ToriRSServer_BankShutdown`, `ToriRSServer_ScriptsFree` and
 * `ToriRSServer_WorldReset` there are the world going away with the last
 * connection, which is exactly the single-connection assumption an embed does
 * not share.
 *
 * Added because there was no in-process way to log a player *out*, so nothing
 * that happens on a logout — the follower broadcast, the logout notification,
 * the slot release — had a test it could be asserted in. Returns 1 if a client
 * was closed.
 */
int
ToriRSServer_EmbedDisconnect(
    struct ToriRSServerEmbed* embed,
    int client_id);

void
ToriRSServer_EmbedStop(struct ToriRSServerEmbed* embed);

/**
 * Client -> server. Returns `len`, or -1 once the session is gone.
 *
 * The bytes are the same ones that would have gone down a socket: the login
 * hello, the RSA block, then ISAAC-scrambled packets. Nothing is special-cased
 * for being in-process, which is what keeps this path honest — an embedded
 * session exercises the real protocol, not a shortcut around it.
 */
int
ToriRSServer_EmbedWrite(
    struct ToriRSServerEmbed* embed,
    int client_id,
    const uint8_t* data,
    int len);

/** Server -> client. Returns the byte count, 0 when nothing is pending, or -1
 *  once the session is gone and drained. */
int
ToriRSServer_EmbedRead(
    struct ToriRSServerEmbed* embed,
    int client_id,
    uint8_t* dst,
    int max);

/** Bytes waiting to be read. */
int
ToriRSServer_EmbedPending(
    const struct ToriRSServerEmbed* embed,
    int client_id);

/**
 * Let the server act.
 *
 * Decodes whatever ToriRSServer_EmbedWrite left for *every* client, runs the login
 * burst for any whose handshake just completed, and advances one 600 ms game
 * tick when `run_tick` is set. The tick is the world's and runs once however
 * many clients are attached. A host with a real frame clock passes 1 every
 * 600 ms and 0 otherwise; a test passes 1 every call and runs the world as fast
 * as it likes.
 *
 * Returns 0 once every session is dead.
 */
int
ToriRSServer_EmbedPump(
    struct ToriRSServerEmbed* embed,
    int run_tick);

/** The world, for a host that wants to assert on game state directly rather
 *  than through the wire. NULL before the first handshake completes. */
struct ToriRSServer*
ToriRSServer_EmbedWorld(struct ToriRSServerEmbed* embed);

/** 1 once this client's login handshake finished. */
int
ToriRSServer_EmbedOnline(
    const struct ToriRSServerEmbed* embed,
    int client_id);

/** 1 while this client's session is alive. EmbedPump's own answer is "any
 *  client alive", which a party keeps true after the leader's session died. */
int
ToriRSServer_EmbedClientAlive(
    const struct ToriRSServerEmbed* embed,
    int client_id);

/** The pool slot this client logged in to, or NULL before it did. */
struct ToriRSServerPlayer*
ToriRSServer_EmbedPlayer(
    struct ToriRSServerEmbed* embed,
    int client_id);

/*
 * ── The party link: more than one client PROCESS in one embedded world ──
 *
 * A raid party is three driven clients, and every server-side driver verb
 * (t.cheat, the tick log, the server tick, server varps) reads the world in
 * the process that hosts it (src/plugin/torirs_plugin_drive.c). So the world
 * stays where it is -- embedded in the LEADER's client -- and the other
 * clients reach it over a loopback socket as embed clients 1..N-1. They keep
 * `transport=embed` in their manifest (so no JS5, the same cache, the same
 * manifest run.py already writes) and only an environment knob says "dial the
 * leader instead of starting a world" (src/platform/net_transport_embed.c).
 *
 * The link is NOT the game socket protocol. It is a framed stream, each frame
 * a 1-byte type and a 4-byte big-endian length, carrying three things:
 *
 *   'D' DATA   the game's own bytes, either direction, untouched
 *   'R' READY  member -> host: "my clock reached its next 600 ms boundary,
 *              and every byte I sent before this frame belongs to that tick"
 *              (payload: the member's FRAME COUNT since its last TICK, u32
 *              big-endian -- see "Lockstep, pinned" below)
 *   'T' TICK   host -> member: "the boundary has run" (payload: srv->tick,
 *              then the world digest after that tick, two u32 big-endian;
 *              tick -1 and digest 0 before the world is built); every DATA
 *              frame before it is that tick's output
 *   'S' SEAT   member -> host, the link's first frame (payload: three u32
 *              big-endian: the link PROTOCOL version, the seat it takes, and
 *              its TORIRS_LOGIC_CYCLES_PER_FRAME). Seat n is client id n-1,
 *              the leader is seat 1; 0 = the first free one. The client id is
 *              the login order and so the pid, so a seat makes "who is pid 1"
 *              a fact of the run's command line instead of a race between
 *              processes.
 *
 * which is what makes the three clients tick in LOCK STEP: the host runs a
 * boundary only once every member has sent READY for it, and a member does
 * not advance past its boundary until the TICK arrives. A member's input is
 * fed to the world at the boundary, in client-id order, before the tick --
 * never mid-frame -- so where a member's packet lands is decided by its own
 * (virtual) clock and not by when the kernel delivered it.
 *
 * Native POSIX hosts only. A Windows or web build that is asked for a party
 * says so and aborts.
 *
 * ── Lockstep, pinned (raid seam21, party_lockstep_frames) ──
 *
 * F frames per tick, READY carries the count, TICK carries the digest.
 *
 *   - A client frame pays k logic cycles (TORIRS_LOGIC_CYCLES_PER_FRAME, 1 by
 *     default, headless only) and advances the link's clock k x 20 ms, so a
 *     600 ms tick is F = TORIRSSERVER_EMBED_PARTY_FRAMES_PER_TICK / k clock
 *     frames on the leader and on every member. A frame the virtual clock
 *     HOLDS (a screenshot in flight, async IO; content_test.c) advances
 *     neither clock and is not counted.
 *   - READY carries how many clock frames the member ran since its last TICK.
 *     The leader requires exactly F from a member's second READY on (the
 *     first follows the join, which ticks at once), and otherwise prints
 *     `party: seat n ran k frames, expected F` and aborts the run: a member
 *     that drifts is a loud failure, never a silent one-tick shift.
 *   - TICK carries the tick and ToriRSServer_EmbedWorldDigest() after it. With
 *     TORIRS_EMBED_PARTY_TRACE=1 the leader and every member print the same
 *     `net: party: boundary k -> tick t digest d` line per boundary, so the
 *     three traces of a run (and of two runs) compare line for line.
 *   - SEAT carries TORIRSSERVER_EMBED_PARTY_PROTOCOL and the member's k; a
 *     mismatch with the leader's aborts with a message.
 *   - Driver state that crosses processes outside the link (t.party.barrier's
 *     files) is stamped with ToriRSServer_EmbedLockstepTick() when written and
 *     honoured only from the NEXT boundary on (torirs_plugin_drive.c): a file
 *     written between two boundaries is then seen at the same tick by every
 *     raider, never "whenever the other process got there".
 */
enum
{
    TORIRSSERVER_EMBED_LINK_DATA = 'D',
    TORIRSSERVER_EMBED_LINK_READY = 'R',
    TORIRSSERVER_EMBED_LINK_TICK = 'T',
    TORIRSSERVER_EMBED_LINK_SEAT = 'S',
    /** type byte + 4-byte big-endian length */
    TORIRSSERVER_EMBED_LINK_HEADER = 5,
    /** The link's protocol version, carried in SEAT. 2: SEAT(version, seat,
     *  k), READY(frames), TICK(tick, digest). Bump it with any change to a
     *  frame's payload. */
    TORIRSSERVER_EMBED_PARTY_PROTOCOL = 2,
    /** Frames per 600 ms tick at one 20 ms logic cycle per frame; divided by
     *  TORIRS_LOGIC_CYCLES_PER_FRAME it is the F every client runs. */
    TORIRSSERVER_EMBED_PARTY_FRAMES_PER_TICK = 30,
    TORIRSSERVER_EMBED_LINK_SEAT_LEN = 12,
    TORIRSSERVER_EMBED_LINK_READY_LEN = 4,
    TORIRSSERVER_EMBED_LINK_TICK_LEN = 8,
};

/** "No lockstep tick yet": the process is not in a party, or no boundary
 *  has run since it joined. */
#define TORIRSSERVER_EMBED_LOCKSTEP_NONE (-2147483647 - 1)

/** TORIRS_LOGIC_CYCLES_PER_FRAME, read once: 1 when unset. Aborts with a
 *  message unless it divides TORIRSSERVER_EMBED_PARTY_FRAMES_PER_TICK (1, 2,
 *  3, 5, 6, 10, 15, 30). The frame loop's own reader (app_frame.c) applies the
 *  same rule; this one is the link's. */
int
ToriRSServer_EmbedLogicCyclesPerFrame(void);

/** F: the clock frames a client runs per tick at `cycles_per_frame`. */
int
ToriRSServer_EmbedPartyFramesPerTick(int cycles_per_frame);

/*
 * The world digest a TICK carries: FNV-1a over the tick, then every active
 * player in pid order (pid, x, z, level, hitpoints), then the number of
 * active npcs. One function, so the leader that sends it and anything that
 * logs or compares it agree on what it covers. An unbuilt world is 0; `srv` is
 * asserted.
 */
uint32_t
ToriRSServer_EmbedWorldDigest(const struct ToriRSServer* srv);

/** SEAT, READY and TICK payloads: encoders write exactly *_LEN bytes; the
 *  decoders return 0 when `len` is not the frame's length (a protocol break
 *  the caller reports), 1 otherwise. */
void
ToriRSServer_EmbedLinkSeatEncode(
    uint8_t* out,
    int seat,
    int cycles_per_frame);
int
ToriRSServer_EmbedLinkSeatDecode(
    const uint8_t* payload,
    int len,
    int* out_version,
    int* out_seat,
    int* out_cycles_per_frame);
void
ToriRSServer_EmbedLinkReadyEncode(
    uint8_t* out,
    int frames);
int
ToriRSServer_EmbedLinkReadyDecode(
    const uint8_t* payload,
    int len,
    int* out_frames);
void
ToriRSServer_EmbedLinkTickEncode(
    uint8_t* out,
    int tick,
    uint32_t digest);
int
ToriRSServer_EmbedLinkTickDecode(
    const uint8_t* payload,
    int len,
    int* out_tick,
    uint32_t* out_digest);

/**
 * The leader's judgement of one READY: 1 when `frames` is `expected`, or when
 * this is the seat's first READY since it joined (`first`); otherwise prints
 * `party: seat n ran k frames, expected F` and returns 0. The leader aborts
 * on 0 (ToriRSServer_EmbedPump); a test calls it directly.
 */
int
ToriRSServer_EmbedPartyReadyCheck(
    int seat,
    int frames,
    int expected,
    int first);

/**
 * The lockstep tick of THIS process: on the leader, the tick its last party
 * boundary ran; on a member, the tick of the last TICK it received.
 * TORIRSSERVER_EMBED_LOCKSTEP_NONE outside a party. The member's transport
 * records it with ToriRSServer_EmbedLockstepNote.
 */
int
ToriRSServer_EmbedLockstepTick(void);
void
ToriRSServer_EmbedLockstepNote(int tick);

/**
 * The frame audit (seam21 item 5). A party client's transport calls
 * ...AuditPoll once per poll; the frame loop calls ...AuditFrame once per
 * App_RunOnce and ...AuditCycles with the logic cycles that frame paid.
 * While a party link is up (ToriRSServer_EmbedPartyAuditArm), a poll that is
 * not followed by exactly one frame before the next poll, or a logic step
 * that pays other than TORIRS_LOGIC_CYCLES_PER_FRAME cycles, aborts with a
 * message. Outside a party these only count.
 */
void
ToriRSServer_EmbedPartyAuditArm(int armed);
int
ToriRSServer_EmbedPartyAuditArmed(void);
void
ToriRSServer_EmbedPartyAuditPoll(void);
void
ToriRSServer_EmbedPartyAuditFrame(void);
void
ToriRSServer_EmbedPartyAuditCycles(int cycles);

/** Bytes read off a link and not yet taken as frames. */
struct ToriRSServerEmbedLinkReader
{
    uint8_t* data;
    int cap;
    int head;
    int tail;
};

/**
 * Leader: bind and listen on 127.0.0.1:`port`. Done at transport creation,
 * before the loaders, for the reason torirs_server_main.c binds early: a
 * member dialling while the leader boots waits in the backlog instead of
 * being refused. Returns the descriptor; a bind failure prints why and
 * returns -1 (the port is somebody else's -- a run-time state, not a bug).
 */
int
ToriRSServer_EmbedPartyListen(int port);

/**
 * Leader: hand the listener to a started embed. From the next pump on, the
 * embed accepts members and runs every boundary under the barrier.
 *
 * `party_size` counts the leader: 3 means "do not run any boundary until two
 * members have joined", which puts every member's login in lock step from
 * the world's first tick (0 or 1: no assembly wait, members join whenever
 * they dial). `wait_ms` bounds every wait for a member; a member that
 * overruns it is named on stderr and dropped (a logout), so the leader's run
 * goes on and fails on whatever needed that member, instead of hanging.
 * `trace` (TORIRS_EMBED_PARTY_TRACE) prints the leader's own
 * `net: party: boundary k -> tick t digest d` line at every boundary.
 */
void
ToriRSServer_EmbedPartyAttach(
    struct ToriRSServerEmbed* embed,
    int listener,
    int party_size,
    int wait_ms,
    int trace);

/** Members ever accepted, and members open now (client ids 1..). */
int
ToriRSServer_EmbedPartyJoined(const struct ToriRSServerEmbed* embed);
int
ToriRSServer_EmbedPartyOpen(const struct ToriRSServerEmbed* embed);

/** Member: connect to the leader on 127.0.0.1:`port`, retrying while the
 *  leader is not yet listening, for at most `wait_ms`. Returns the
 *  descriptor or -1. */
int
ToriRSServer_EmbedPartyDial(
    int port,
    int wait_ms);

/** Write one whole frame (blocking). Returns 1, or 0 once the peer is gone. */
int
ToriRSServer_EmbedLinkSend(
    int fd,
    int type,
    const uint8_t* data,
    int len);

/** Read what the socket has, waiting at most `timeout_ms` (0: do not wait)
 *  for the first byte. Returns 1 if bytes arrived, 0 if none did, -1 once
 *  the peer closed. */
int
ToriRSServer_EmbedLinkFill(
    int fd,
    struct ToriRSServerEmbedLinkReader* reader,
    int timeout_ms);

/** Take one whole frame, if one is buffered. The payload points into the
 *  reader and is valid until the next Fill. Returns 1 if a frame was taken. */
int
ToriRSServer_EmbedLinkNext(
    struct ToriRSServerEmbedLinkReader* reader,
    int* out_type,
    const uint8_t** out_payload,
    int* out_len);

void
ToriRSServer_EmbedLinkReaderFree(struct ToriRSServerEmbedLinkReader* reader);

/** Milliseconds on a monotonic clock (the party waits are wall-clock). */
long
ToriRSServer_EmbedNowMs(void);

#endif
