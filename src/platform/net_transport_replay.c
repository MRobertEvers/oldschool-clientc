/*
 * An RSProx recording, played back as the server.
 *
 * A fourth NetTransport beside TCP, WebSocket and the embedded server. Like the
 * embed transport there is no wire under it; unlike it there is no world
 * either. What answers the client is the server's half of a real Jagex session
 * (net/rsprox_recording.h), released on the recording's own clock:
 *
 *      client PopOut   --->  the login handshake, answered here;
 *                            after it, read only for MAP_BUILD_COMPLETE
 *      NET_RECV        <---  the recorded server packets, re-enciphered
 *
 * Modelled on RSProx's own replay server (proxy/.../replay/: ReplaySession,
 * ReplayClientLoginHandler, ReplayPlaybackController), which does the same for
 * the Jagex client over a socket:
 *
 *   - The handshake is answered, not recorded: INIT_GAME_CONNECTION gets a
 *     session id, a login gets LoginResponse.Ok carrying the recording's local
 *     player index. RSProx decrypts the RSA block to learn the cipher seed;
 *     this transport is in the client's own process and reads the seed the
 *     login driver published (net->prev_seed) instead, so no private key is
 *     involved.
 *   - A session's connection ends where the recording's did, and the client
 *     takes it up again the way the official client does (THE SESSION'S
 *     CONNECTIONS, below).
 *   - Every server packet is re-enciphered for this session's ISAAC (seed +
 *     50, pSmart1Or2 opcodes) and framed exactly as the recording framed it.
 *     The payloads are the recording's bytes, untouched.
 *   - After a REBUILD_NORMAL / REBUILD_REGION, the rest of that tick still
 *     goes, and the first packet of a later tick waits until the client says
 *     MAP_BUILD_COMPLETE (ReplayPlaybackController's map-build barrier). A
 *     real server does not wait, but the recorder's client was loading while
 *     the server kept writing, so not waiting replays the loading screen's
 *     backlog as a burst. The wait is bounded below the link watch's 15 s
 *     silence rule (net_link_watch.h), so a client that never answers is
 *     played on rather than dropped.
 *
 * THE CLOCK. Packets keep the recording's relative timing. Locked, the replay
 * clock is the wall clock (one recorded second a second). Unlocked, it is the
 * client's logic cycles x 20 ms -- the unlocked clock's rule (App_SpeedUnlock)
 * that 30 client cycles are one 600 ms tick -- so a client running as fast as
 * the machine allows plays the recording as fast as it can while every
 * animation, timer and clientscript clock still sees the gaps the recorder saw.
 * Both are deltas, so switching between them mid-replay does not jump.
 *
 * THE SESSION'S CONNECTIONS. A login is one of three things, and the answer is
 * what the official client would have been answered:
 *
 *   - A RECORDED RECONNECT. RSProx writes its RECONNECT (0xFF) only when the
 *     server answered a login with RECONNECT_OK (proxy ServerGameLoginDecoder,
 *     reply 15), and the client accepts 15 only in gameState 40, connection
 *     lost, in reply to the GAMERECONNECT it sends there (deob client.java,
 *     the reply switch: `var31 == 15 && field739 == 40`; the opcode and the
 *     previous seed in place of the password: the `field739 == 40` branches of
 *     the block builder). So at that frame the recorded session's socket went
 *     away. The transport ends this connection there -- once every byte before
 *     the frame is on the bus AND executed, as a client reads what its socket
 *     holds before it sees the close -- the client's link watch sends
 *     GAMERECONNECT, and the transport answers RECONNECT_OK carrying the
 *     frame's GPI block (net/login/login_osrs.c), then goes on after the
 *     frame. The executed-tick count goes on too: it is one session.
 *   - A WORLD HOP. LOGOUT_TRANSFER ends the connection from the server's side
 *     (deob client.java, the LOGOUT_TRANSFER arm: gameState 45, the socket
 *     closed, the world switched), and the client logs in to the new world
 *     with GAMELOGIN and its credentials (state 45 takes the block builder's
 *     non-40 branches). The answer is LoginResponse.Ok, and THAT is where a hop
 *     learns its new local index: Ok's p2 (client.java's field767) -- RECONNECT_OK
 *     states none. Nothing is staged after a LOGOUT_TRANSFER until the client
 *     has closed the connection and logged in again.
 *     RSProx starts a NEW recording at a LoginResponse.Ok (ServerGameLoginDecoder
 *     LOGIN_OK_READ_DATA: a new BinaryBlob), so a hop's own session is never in
 *     the file that has the LOGOUT_TRANSFER. A RECONNECT behind one (index
 *     4282, tick 1579: 2 h 22 min of recording clock after it) is a later
 *     session's reconnect that RSProx appended to this blob (it finds "the old
 *     connection" by port), under the slot that session's unrecorded Ok gave
 *     it (frame.reconnect_index, net/proto/proto_reconnect.c). The client
 *     cannot take RECONNECT_OK on a hop's GAMELOGIN, nor under another index,
 *     so that login is answered as the unrecorded one was -- Ok with the index
 *     the session continues under -- and its connection then ends where the
 *     recording says the next one began, and the GAMERECONNECT after it gets
 *     the recorded RECONNECT_OK.
 *   - ANYTHING ELSE: the first login, a seek's RESTART (the transport ended
 *     the session itself, REPLAY_NEXT_RESTART), a client that closed the
 *     session on its own, a login after the end. LoginResponse.Ok with the
 *     recording's index, and the recording from frame 0.
 *
 * Selected with `--replay <file.bin[.gz]>` (main.c), which also picks the
 * revision from the recording's header and logs in with throwaway
 * credentials. The recording ends the session the way RSProx's replay does:
 * a LOGOUT, recorded or appended. A later login plays it from the start.
 */

#include "net_transport.h"
#include "mem/torirs_mem.h"

#include "cmd/cmdbus.h"
#include "net/isaac.h"
#include "net/net.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rsprox_recording.h"
#include "net/torirs_control.h"
#include "platform_window.h"
#include "replay/replay_scrub.h"
#include "replay/replay_checkpoint_store.h"
#include "replay/replay_world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/torirs_log.h"

/* rev 241's login prots and replies (net/login/login_osrs_block.h). Stated
 * here because this is the server side of them, which the client tree has no
 * header for. */
#define REPLAY_LOGIN_INIT_GAME_CONNECTION 14
#define REPLAY_LOGIN_GAMELOGIN 16
#define REPLAY_LOGIN_GAMERECONNECT 18
#define REPLAY_LOGINRES_OK 2
#define REPLAY_LOGINRES_RECONNECT_OK 15
/* LoginResponse.Ok's length byte: the 34-byte body plus the 3 bytes RSProt
 * has always added (ReplayClientLoginHandler.LOGIN_OK_LENGTH). */
#define REPLAY_LOGIN_OK_LENGTH 37
#define REPLAY_LOGIN_OK_BODY 34

#define REPLAY_CYCLE_MS 20
/* A locked frame advances the clock by its wall time, but never by more than
 * this: a stall (a hidden tab, a synchronous load) is not recorded time. */
#define REPLAY_MAX_STEP_MS 1000
/* Bytes enciphered ahead of the bus. Unlocked, a frame can owe many ticks;
 * this keeps the staging buffer from holding the whole recording. */
#define REPLAY_PENDING_CAP (256 * 1024)

enum ReplayPhase
{
    /** No connection. */
    REPLAY_IDLE = 0,
    /** Connected; expecting INIT_GAME_CONNECTION. */
    REPLAY_AWAIT_INIT,
    /** Expecting GAMELOGIN / GAMERECONNECT and its block. */
    REPLAY_AWAIT_LOGIN,
    /** Ok sent; waiting for the client to enter the game stream. */
    REPLAY_AWAIT_GAME,
    REPLAY_PLAYING,
    /** Every packet sent, the LOGOUT included. */
    REPLAY_ENDED,
};

struct NetTransportReplay
{
    struct NetTransport base;
    struct RsproxRecording rec;
    struct GameProtoRevTable const* rev;
    int last_status;
    enum ReplayPhase phase;
    /** Logins played so far: the second one restarts the recording. */
    int sessions;

    /** Server -> client: ours. Client -> server: the client's out cipher,
     *  mirrored to find MAP_BUILD_COMPLETE in its stream. */
    struct Isaac* enc;
    struct Isaac* dec;

    /* The client's bytes: the handshake while logging in, then the game
     * stream, which is framed but not acted on. */
    uint8_t* cin;
    int cin_len;
    int cin_cap;
    /** Game stream framing state: the opcode being read (-1: none yet), how
     *  many length bytes it still needs, and payload bytes left to skip. */
    int c_opcode;
    int c_len_bytes;
    int c_len;
    int c_skip;
    int map_build_complete_wire;

    /* Enciphered server bytes not yet on the bus. */
    uint8_t* pend;
    int pend_len;
    int pend_off;
    int pend_cap;
    /** Bytes ever staged, and ever pushed onto the bus: a staged byte is
     *  delivered once bus_total has passed it. */
    uint64_t staged_total;
    uint64_t bus_total;
    /** The REPLAY_WORLD control frame staged and not yet wholly on the bus
     *  (0: none): its keyframe's mailbox serial, and staged_total at its end.
     *  A session dropped before it went takes its keyframe back out of the
     *  mailbox (ReplayWorld_Withdraw), since no client will ever ask. */
    uint32_t world_serial;
    uint64_t world_end;

    /* Playback. What goes next, on what clock, and seeks: the scrubber
     * (replay/replay_scrub.h). */
    struct ReplayScrub* scrub;
    int logout_wire;
    int logout_transfer_wire;
    /** A recorded RECONNECT handed out (-1: none): the session goes on after
     *  it, over the connection whose login it answers. Nothing is staged
     *  until then. */
    int resume_frame;
    /** The connection is still to be ended at resume_frame: once the client
     *  has executed everything before it. */
    int resume_drop;
    /** A LOGOUT_TRANSFER has been handed out: the client closes this
     *  connection and logs in to the next world. Nothing is staged until it
     *  has. */
    int hop;
    /** This connection's login continued the session (RECONNECT_OK, or a
     *  hop's Ok): the stream goes on from the cursor, not from frame 0. */
    int continuing;
    /** Sessions continued over a new connection (status). */
    int resumed;
    /** Polls so far, and the one that put the last staged byte on the bus:
     *  bytes pushed this poll reach the client after it. */
    unsigned polls;
    unsigned last_push_poll;
    /** The client's packet pipeline is held (NetTransport_ReplayClientBusy). */
    int client_busy;

    /* The two clocks, read every poll so a switch between them is seamless. */
    int unlocked;
    unsigned long long cycles_now;
    unsigned long long cycles_last;
    uint64_t wall_last;
};

static struct NetTransportVTable const k_replay_vtable;

static struct NetTransportReplay*
as_replay(struct NetTransport* t)
{
    assert(t);
    assert(t->vtable == &k_replay_vtable);
    return (struct NetTransportReplay*)t;
}

static void
emit_status(
    struct NetTransportReplay* self,
    struct ToriRS_CmdBus* bus,
    int status)
{
    if( self->last_status == status )
        return;
    self->last_status = status;
    CmdBus_PushNetStatus(bus, status);
}

static void
grow(
    uint8_t** buf,
    int* cap,
    int need)
{
    if( need <= *cap )
        return;
    {
        int n = *cap ? *cap : 4096;

        while( n < need )
            n *= 2;
        *buf = ToriMem_Realloc(ToriMem_Net(), *buf, (size_t)n);
        assert(*buf);
        *cap = n;
    }
}

static void
pend_bytes(
    struct NetTransportReplay* self,
    uint8_t const* data,
    int len)
{
    /* Delivered bytes go first, so the buffer holds at most what is owed. */
    if( self->pend_off > 0 )
    {
        memmove(self->pend, self->pend + self->pend_off, (size_t)(self->pend_len - self->pend_off));
        self->pend_len -= self->pend_off;
        self->pend_off = 0;
    }
    grow(&self->pend, &self->pend_cap, self->pend_len + len);
    memcpy(self->pend + self->pend_len, data, (size_t)len);
    self->pend_len += len;
    self->staged_total += (uint64_t)len;
}

static void
pend_byte(
    struct NetTransportReplay* self,
    int v)
{
    uint8_t b = (uint8_t)v;

    pend_bytes(self, &b, 1);
}

/* One server packet, as RSProx's ServerPacket encodes it: the opcode through
 * the cipher (pSmart1Or2: two cipher bytes from 0x80), then the size the prot
 * table says to send, then the payload in the clear. */
static void
pend_packet(
    struct NetTransportReplay* self,
    int opcode,
    uint8_t const* payload,
    int len)
{
    int const size = self->rev->packetin_size(opcode);

    if( opcode >= 0x80 )
    {
        pend_byte(self, ((opcode >> 8) | 0x80) + isaac_next(self->enc));
        pend_byte(self, (opcode & 0xff) + isaac_next(self->enc));
    }
    else
        pend_byte(self, opcode + isaac_next(self->enc));
    if( size == PKTIN_LENGTH_VARU8 )
    {
        assert(len <= 0xff);
        pend_byte(self, len);
    }
    else if( size == PKTIN_LENGTH_VARU16 )
    {
        assert(len <= 0xffff);
        pend_byte(self, len >> 8);
        pend_byte(self, len);
    }
    else
        assert(size == len);
    if( len > 0 )
        pend_bytes(self, payload, len);
}

/*
 * One control frame (net/torirs_control.h), as torirs_server_encode.c writes
 * one: the escape opcode through the cipher as pSmart1Or2 (two cipher bytes),
 * then the magic, the type, the length and the payload in the clear. Staged
 * like a packet, so it reaches the client at exactly this byte of the stream.
 */
static void
pend_control(
    struct NetTransportReplay* self,
    int type,
    uint8_t const* payload,
    int len)
{
    uint8_t header[TORIRS_CONTROL_HEADER_BYTES];

    assert(torirs_control_type_is_replay(type));
    assert(len >= 0);
    if( len > 0 )
        assert(payload);
    pend_byte(self, ((TORIRS_CONTROL_ESCAPE >> 8) | 0x80) + isaac_next(self->enc));
    pend_byte(self, (TORIRS_CONTROL_ESCAPE & 0xff) + isaac_next(self->enc));
    torirs_control_put_header(header, type, len);
    pend_bytes(self, header, TORIRS_CONTROL_HEADER_BYTES);
    if( len > 0 )
        pend_bytes(self, payload, len);
}

static void
cin_consume(
    struct NetTransportReplay* self,
    int n)
{
    assert(n <= self->cin_len);
    memmove(self->cin, self->cin + n, (size_t)(self->cin_len - n));
    self->cin_len -= n;
}

/* A connection's own state: the client's bytes and framer, what is staged.
 * The session -- the cursor, a reconnect or hop it is owed -- is not. */
static void
connection_reset(struct NetTransportReplay* self)
{
    self->cin_len = 0;
    self->c_opcode = -1;
    self->c_len_bytes = 0;
    self->c_len = 0;
    self->c_skip = 0;
    self->pend_len = 0;
    self->pend_off = 0;
    self->resume_drop = 0;
    /* A keyframe whose frame goes with the staged bytes is never asked for:
     * the session it belonged to is over, and the client's side resets with
     * it (its socket closes; the reconnect starts a fresh framer and cipher).
     * One already on the bus is the client's, and it takes it. */
    if( self->world_serial && self->bus_total < self->world_end )
        ReplayWorld_Withdraw(self->world_serial);
    self->world_serial = 0;
}

/* The session from the start: the next login plays frame 0. */
static void
replay_reset(struct NetTransportReplay* self)
{
    connection_reset(self);
    self->resume_frame = -1;
    self->hop = 0;
    self->continuing = 0;
    ReplayScrub_Rewind(self->scrub);
}

/* A login now continues the session rather than starting it again. */
static int
session_owed(struct NetTransportReplay const* self)
{
    return self->resume_frame >= 0 || self->hop;
}

/* Staged bytes onto the bus, as far as it has room. 1 when none are left. */
static int
flush_pend(
    struct NetTransportReplay* self,
    struct ToriRS_CmdBus* bus)
{
    while( self->pend_off < self->pend_len )
    {
        int chunk = self->pend_len - self->pend_off;

        if( chunk > TORIRS_CMD_MAX_PAYLOAD )
            chunk = TORIRS_CMD_MAX_PAYLOAD;
        if( !CmdBus_CanPush(bus, (uint16_t)chunk) )
            break;
        CmdBus_Push(bus, TORIRS_CMD_NET_RECV, self->pend + self->pend_off, (uint16_t)chunk);
        self->pend_off += chunk;
        self->bus_total += (uint64_t)chunk;
        self->last_push_poll = self->polls;
    }
    if( self->world_serial && self->bus_total >= self->world_end )
        self->world_serial = 0;
    if( self->pend_off == self->pend_len )
        self->pend_off = self->pend_len = 0;
    return self->pend_len == 0;
}

/* --- the handshake ------------------------------------------------------- */

/* LoginResponse.Ok (ReplayClientLoginHandler.sendLoginOk): no authenticator,
 * no staff level, the slot `index`, a member. */
static void
pend_login_ok(
    struct NetTransportReplay* self,
    int index)
{
    uint8_t ok[2 + REPLAY_LOGIN_OK_BODY];
    int o = 0;

    assert(index >= 0);
    memset(ok, 0, sizeof(ok));
    ok[o++] = REPLAY_LOGINRES_OK;
    ok[o++] = REPLAY_LOGIN_OK_LENGTH;
    o += 1 + 4 + 1 + 1; /* authenticator kind + code, staffModLevel, playerMod */
    ok[o++] = (uint8_t)(index >> 8);
    ok[o++] = (uint8_t)index;
    ok[o++] = 1; /* member */
    o += 8 + 8 + 8; /* accountHash, userId, userHash */
    assert(o == (int)sizeof(ok));
    pend_bytes(self, ok, o);
}

/* RECONNECT_OK: the reply byte, a var-short, the recorded GPI init block. */
static void
pend_reconnect_ok(
    struct NetTransportReplay* self,
    struct RsproxRecordingFrame const* f)
{
    assert(f);
    assert(f->opcode == RSPROX_RECORDING_RECONNECT);
    assert(f->length <= 0xffff);
    pend_byte(self, REPLAY_LOGINRES_RECONNECT_OK);
    pend_byte(self, (int)(f->length >> 8));
    pend_byte(self, (int)f->length);
    if( f->length > 0 )
        pend_bytes(self, self->rec.data + f->offset, (int)f->length);
}

/*
 * A login (GAMELOGIN or GAMERECONNECT, `op`), answered as the official client
 * would have been answered at this point of the session (THE SESSION'S
 * CONNECTIONS, at the top).
 */
static void
answer_login(
    struct NetTransportReplay* self,
    struct ToriRS_Network const* net,
    int op)
{
    self->sessions++;
    if( self->resume_frame >= 0 )
    {
        struct RsproxRecordingFrame const* f = &self->rec.frames[self->resume_frame];

        /* The recorded RECONNECT_OK, to the GAMERECONNECT of a session that
         * holds the index it was written under: the deob keeps its index
         * across RECONNECT_OK. */
        assert(!self->hop);
        if( op == REPLAY_LOGIN_GAMERECONNECT && net->local_index == f->reconnect_index )
        {
            pend_reconnect_ok(self, f);
            TORIRS_REPORT("replay: tick %d: RECONNECT_OK (the recorded %u-byte player-info block); "
                          "player %d's session goes on\n",
                          f->tick, (unsigned)f->length, f->reconnect_index);
            self->resume_frame = -1;
            self->continuing = 1;
            self->resumed++;
            self->phase = REPLAY_AWAIT_GAME;
            return;
        }
        /* A session under another slot (a restored checkpoint can leave the
         * client there), or a fresh GAMELOGIN: the login whose Ok gave the
         * session the slot its RECONNECT was written under, which this
         * recording does not have. Its connection ends again at once: the
         * recorded RECONNECT is still owed. */
        pend_login_ok(self, f->reconnect_index);
        TORIRS_REPORT("replay: tick %d: %s as player %d answered LoginResponse.Ok as player %d -- "
                      "the login this recording does not have; its connection ends where the "
                      "recorded RECONNECT begins the next\n",
                      f->tick, op == REPLAY_LOGIN_GAMELOGIN ? "GAMELOGIN" : "GAMERECONNECT",
                      net->local_index, f->reconnect_index);
        self->continuing = 1;
        self->resumed++;
        self->resume_drop = 1;
        self->phase = REPLAY_AWAIT_GAME;
        return;
    }
    if( self->hop )
    {
        /* The hop's login. When a RECONNECT is what the recording has next,
         * this is the unrecorded login before it, and its Ok gives the slot
         * that RECONNECT was written under; otherwise the stream goes on
         * after the hop under the slot the client has. */
        int const next = ReplayScrub_NextReconnect(self->scrub);
        int index = net->local_index >= 0 ? net->local_index : self->rec.local_player_index;

        if( next >= 0 )
            index = self->rec.frames[next].reconnect_index;

        pend_login_ok(self, index);
        TORIRS_REPORT("replay: tick %d: the hop's GAMELOGIN answered LoginResponse.Ok as player %d%s\n",
                      ReplayScrub_LastTick(self->scrub), index,
                      next >= 0 ? " (the slot the recording's next RECONNECT was written under; the hop's "
                                  "own session went to another RSProx recording)"
                                : "");
        self->hop = 0;
        self->continuing = 1;
        self->resumed++;
        self->phase = REPLAY_AWAIT_GAME;
        return;
    }
    pend_login_ok(self, self->rec.local_player_index);
    self->continuing = 0;
    self->phase = REPLAY_AWAIT_GAME;
    TORIRS_REPORT("replay: logged in as player %d; %d packets over %d ticks (%.1f s)%s\n",
                  self->rec.local_player_index, self->rec.frame_count, self->rec.tick_count,
                  (double)self->rec.duration_ms / 1000.0, self->sessions > 1 ? ", from the start" : "");
}

static void
answer_handshake(
    struct NetTransportReplay* self,
    struct ToriRS_Network* net,
    struct ToriRS_CmdBus* bus)
{
    if( self->phase == REPLAY_AWAIT_INIT && self->cin_len >= 1 )
    {
        if( self->cin[0] != REPLAY_LOGIN_INIT_GAME_CONNECTION )
        {
            TORIRS_ERR("replay: expected INIT_GAME_CONNECTION (14), the client sent %d\n",
                       self->cin[0]);
            emit_status(self, bus, TORIRS_NET_STATUS_FAILED);
            self->phase = REPLAY_IDLE;
            return;
        }
        cin_consume(self, 1);
        /* p1 status 0, p8 session id: any value, nothing derives from it. */
        pend_byte(self, 0);
        for( int i = 0; i < 8; i++ )
            pend_byte(self, 0);
        self->phase = REPLAY_AWAIT_LOGIN;
    }
    if( self->phase == REPLAY_AWAIT_LOGIN && self->cin_len >= 3 )
    {
        int const op = self->cin[0];
        int const len = (self->cin[1] << 8) | self->cin[2];

        if( op != REPLAY_LOGIN_GAMELOGIN && op != REPLAY_LOGIN_GAMERECONNECT )
        {
            TORIRS_ERR("replay: expected GAMELOGIN (16), the client sent %d\n", op);
            emit_status(self, bus, TORIRS_NET_STATUS_FAILED);
            self->phase = REPLAY_IDLE;
            return;
        }
        if( self->cin_len < 3 + len )
            return;
        cin_consume(self, 3 + len);
        answer_login(self, net, op);
    }
    if( self->phase == REPLAY_AWAIT_GAME && net->state == TORIRS_NET_GAME && net->has_prev_seed )
    {
        /*
         * The client's published seed is the one it enciphers with: out =
         * seed, in = seed + 50. Whatever it sent since the login block was
         * enciphered from step 0 and is still in `cin`.
         */
        int seed[4];
        int seed_in[4];

        for( int i = 0; i < 4; i++ )
        {
            seed[i] = net->prev_seed[i];
            seed_in[i] = net->prev_seed[i] + 50;
        }
        isaac_seed(self->dec, seed, 4);
        isaac_seed(self->enc, seed_in, 4);
        self->wall_last = PlatformWindow_Ticks64();
        self->cycles_last = self->cycles_now;
        self->phase = REPLAY_PLAYING;
        /* A continued session keeps its cursor and its executed-tick count;
         * a new one counts from here. */
        if( self->continuing )
            ReplayScrub_ConnectionResumed(self->scrub);
        else
            ReplayScrub_SessionBegin(self->scrub);
        self->continuing = 0;
    }
}

/* --- the client's game stream: only MAP_BUILD_COMPLETE matters ------------ */

static void
read_client_stream(struct NetTransportReplay* self)
{
    int off = 0;

    while( off < self->cin_len )
    {
        if( self->c_skip > 0 )
        {
            int n = self->cin_len - off < self->c_skip ? self->cin_len - off : self->c_skip;

            off += n;
            self->c_skip -= n;
            continue;
        }
        if( self->c_opcode < 0 )
        {
            int size;

            self->c_opcode = (self->cin[off++] - isaac_next(self->dec)) & 0xff;
            if( self->c_opcode == self->map_build_complete_wire )
                ReplayScrub_MapBuilt(self->scrub);
            size = self->rev->packetout_size(self->c_opcode);
            if( size == PKTIN_LENGTH_VARU8 )
                self->c_len_bytes = 1;
            else if( size == PKTIN_LENGTH_VARU16 )
                self->c_len_bytes = 2;
            else
            {
                self->c_skip = size;
                self->c_opcode = -1;
            }
            self->c_len = 0;
            continue;
        }
        self->c_len = (self->c_len << 8) | self->cin[off++];
        if( --self->c_len_bytes == 0 )
        {
            self->c_skip = self->c_len;
            self->c_opcode = -1;
        }
    }
    self->cin_len = 0;
}

/* --- playback ------------------------------------------------------------ */

/* The clock moves with the client's cycles while unlocked, and with the wall
 * clock while locked. A seek runs unlocked whatever the setting. */
static int
replay_unlocked(struct NetTransportReplay* self)
{
    return self->unlocked || ReplayScrub_Seeking(self->scrub);
}

/*
 * A seek behind the playhead (REPLAY_NEXT_RESTART): the session ends the way a
 * server that went away ends it. What is staged goes with it, the client sees
 * the socket close, and its link watch re-establishes the session
 * (app_net_link_watch); nothing is owed, so answer_login answers its
 * GAMERECONNECT with LoginResponse.Ok and the scrubber plays from frame 0.
 */
static void
restart_session(
    struct NetTransportReplay* self,
    struct ToriRS_CmdBus* bus)
{
    TORIRS_REPORT("replay: restarting the session from the login (a seek behind tick %d)\n",
                  ReplayScrub_LastTick(self->scrub));
    replay_reset(self);
    self->phase = REPLAY_IDLE;
    emit_status(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
}

static void
play(
    struct NetTransportReplay* self,
    struct ToriRS_CmdBus* bus)
{
    uint64_t const wall = PlatformWindow_Ticks64();
    uint64_t step;

    if( replay_unlocked(self) )
        step = (uint64_t)(self->cycles_now - self->cycles_last) * REPLAY_CYCLE_MS;
    else
    {
        step = wall > self->wall_last ? wall - self->wall_last : 0;
        if( step > REPLAY_MAX_STEP_MS )
            step = REPLAY_MAX_STEP_MS;
    }
    self->cycles_last = self->cycles_now;
    self->wall_last = wall;
    ReplayScrub_Advance(self->scrub, step, wall);

    /* A recorded RECONNECT or a LOGOUT_TRANSFER ends this connection: nothing
     * more goes on it. */
    while( !session_owed(self) && self->pend_len - self->pend_off < REPLAY_PENDING_CAP )
    {
        struct ReplayStep next;
        enum ReplayNext const what = ReplayScrub_Next(self->scrub, &next);
        struct RsproxRecordingFrame const* f;

        if( what == REPLAY_NEXT_WAIT )
            break;
        if( what == REPLAY_NEXT_FRAME )
        {
            f = &self->rec.frames[next.frame];
            pend_packet(self, f->opcode, self->rec.data + f->offset, (int)f->length);
            /* The server's side of a hop: it closes the connection after
             * this, and the client moves (THE SESSION'S CONNECTIONS). */
            if( f->opcode == self->logout_transfer_wire )
            {
                self->hop = 1;
                TORIRS_REPORT("replay: tick %d: LOGOUT_TRANSFER; the client moves world and logs in "
                              "again\n",
                              f->tick);
            }
        }
        else if( what == REPLAY_NEXT_COORD )
        {
            /* The seek's three are control frames in the stream itself
             * (net/torirs_control.h), so the bytes around them keep their
             * order without anything waiting for the bus. */
            uint8_t payload[TORIRS_CONTROL_REPLAY_COORD_BYTES_MAX];
            int const len = torirs_control_replay_coord_encode(
                next.coord, next.cycle, next.cycle_first, next.name, payload);

            pend_control(self, TORIRS_CONTROL_REPLAY_COORD, payload, len);
        }
        else if( what == REPLAY_NEXT_CHAT )
        {
            static uint8_t payload[TORIRS_CONTROL_REPLAY_CHAT_BYTES_MAX];
            int const len = torirs_control_replay_chat_encode(
                next.chat_say, next.chat_crown, next.chat_colour_effect, next.name, next.chat_data,
                next.chat_len, payload);

            pend_control(self, TORIRS_CONTROL_REPLAY_CHAT, payload, len);
        }
        else if( what == REPLAY_NEXT_WORLD )
        {
            /* The keyframe waits in the mailbox; the frame names it. */
            uint8_t payload[TORIRS_CONTROL_REPLAY_WORLD_BYTES];
            int const tick = next.world->tick;
            uint32_t const serial = ReplayWorld_Post(next.world);
            int const len = torirs_control_replay_world_encode(tick, serial, payload);

            pend_control(self, TORIRS_CONTROL_REPLAY_WORLD, payload, len);
            self->world_serial = serial;
            self->world_end = self->staged_total;
        }
        else if( what == REPLAY_NEXT_RESUME )
        {
            /* The recorded session lost its connection here. Behind a
             * LOGOUT_TRANSFER the client is closing it already; otherwise the
             * transport closes it once the client has executed what is
             * before it (replay_poll). */
            self->resume_frame = next.frame;
            self->resume_drop = !self->hop;
        }
        else if( what == REPLAY_NEXT_DIALOG )
        {
            /* The recorder's client answered its chatbox prompt here
             * (torirs_control.h, REPLAY_DIALOG). */
            uint8_t payload[TORIRS_CONTROL_REPLAY_DIALOG_BYTES];
            int const len = torirs_control_replay_dialog_encode(next.dialog_name, payload);

            pend_control(self, TORIRS_CONTROL_REPLAY_DIALOG, payload, len);
        }
        else if( what == REPLAY_NEXT_RESTART )
        {
            restart_session(self, bus);
            return;
        }
        else
        {
            assert(what == REPLAY_NEXT_END);
            /* The recording ends the session, as RSProx's replay does
             * (ReplaySession.logoutClient): with its own LOGOUT when it has
             * one. */
            if( self->rec.frame_count == 0 ||
                self->rec.frames[self->rec.frame_count - 1].opcode != self->logout_wire )
                pend_packet(self, self->logout_wire, NULL, 0);
            self->phase = REPLAY_ENDED;
            TORIRS_REPORT("replay: finished at tick %d (%.1f s of recording)\n", self->rec.tick_count,
                          (double)self->rec.duration_ms / 1000.0);
            break;
        }
    }
}

/*
 * The recorded session's connection ends at resume_frame, as the socket under
 * it went away: once every byte before the frame is on the bus (pushed on an
 * earlier poll, so the client has drained it since) and the client has
 * executed all of it -- nothing parsed and waiting, its pipeline not held. A
 * client reads what its socket holds before it sees the close. Its link watch
 * then reconnects (app_net_link_watch), and answer_login answers.
 */
static void
drop_for_reconnect(
    struct NetTransportReplay* self,
    struct ToriRS_Network const* net,
    struct ToriRS_CmdBus* bus)
{
    struct RsproxRecordingFrame const* f;

    if( self->pend_len != self->pend_off || self->polls <= self->last_push_poll )
        return;
    if( net->packets_head || self->client_busy )
        return;
    f = &self->rec.frames[self->resume_frame];
    TORIRS_REPORT("replay: tick %d: the recorded session's connection ends here (RSProx's RECONNECT, "
                  "%.1f s after the packet before it); the client reconnects\n",
                  f->tick,
                  self->resume_frame > 0
                      ? (double)(f->t_ms - self->rec.frames[self->resume_frame - 1].t_ms) / 1000.0
                      : 0.0);
    connection_reset(self);
    self->phase = REPLAY_IDLE;
    ReplayScrub_ConnectionLost(self->scrub);
    emit_status(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
}

/* --- the transport ------------------------------------------------------- */

static void
replay_poll(
    struct NetTransport* transport,
    struct ToriRS_Network* net,
    struct ToriRS_CmdBus* bus)
{
    struct NetTransportReplay* self = as_replay(transport);
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    assert(net);
    assert(bus);
    self->polls++;
    ReplayScrub_Wall(self->scrub, PlatformWindow_Ticks64());

    /* 1. client -> "server" */
    while( ToriRS_Network_PopOut(net, &header, payload) )
    {
        if( header.type == TORIRS_NET_OUT_CONNECT )
        {
            /* A connection that takes the session up again keeps the
             * cursor; any other plays from the start. */
            if( session_owed(self) )
                connection_reset(self);
            else
                replay_reset(self);
            self->phase = REPLAY_AWAIT_INIT;
            /* A fresh connection reports itself even after a FAILED or a
             * DISCONNECTED: the login screen waits for this edge. */
            self->last_status = -1;
            emit_status(self, bus, TORIRS_NET_STATUS_CONNECTED);
        }
        else if( header.type == TORIRS_NET_OUT_SEND_DATA && self->phase != REPLAY_IDLE )
        {
            grow(&self->cin, &self->cin_cap, self->cin_len + (int)header.length);
            memcpy(self->cin + self->cin_len, payload, header.length);
            self->cin_len += (int)header.length;
        }
        else if( header.type == TORIRS_NET_OUT_DISCONNECT )
        {
            if( session_owed(self) )
            {
                /* The close the session is owed: a hop's, or the client's
                 * half of the one the transport made at a RECONNECT. The
                 * cursor stays where the recording's connection ended. A
                 * hop's close is the client's own; it is told nothing. */
                if( self->hop && self->phase == REPLAY_PLAYING )
                    ReplayScrub_ConnectionLost(self->scrub);
                connection_reset(self);
                self->phase = REPLAY_IDLE;
                if( !self->hop )
                    emit_status(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
                continue;
            }
            if( self->phase == REPLAY_PLAYING )
                TORIRS_REPORT("replay: the client closed the session at tick %d of %d "
                              "(%.1f s in); its next login plays from the start\n",
                              ReplayScrub_LastTick(self->scrub), self->rec.tick_count,
                              (double)ReplayScrub_ClockMs(self->scrub) / 1000.0);
            replay_reset(self);
            self->phase = REPLAY_IDLE;
            emit_status(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
        }
    }

    /* 2. the handshake, then the recording */
    if( self->phase >= REPLAY_AWAIT_INIT && self->phase <= REPLAY_AWAIT_GAME )
        answer_handshake(self, net, bus);
    if( self->phase == REPLAY_PLAYING || self->phase == REPLAY_ENDED )
        read_client_stream(self);
    if( self->phase == REPLAY_PLAYING )
        play(self, bus);

    /* 3. server -> client, as the bus has room. What the bus refuses stays
     * staged: a byte consumed and not delivered is a hole in the stream. */
    flush_pend(self, bus);
    if( self->phase == REPLAY_PLAYING && self->resume_frame >= 0 && self->resume_drop )
        drop_for_reconnect(self, net, bus);
}

static void
replay_free(struct NetTransport* transport)
{
    struct NetTransportReplay* self = as_replay(transport);

    ReplayScrub_Free(self->scrub);
    RsproxRecording_Free(&self->rec);
    isaac_free(self->enc);
    isaac_free(self->dec);
    if( self->cin )
        ToriMem_Free(ToriMem_Net(), self->cin);
    if( self->pend )
        ToriMem_Free(ToriMem_Net(), self->pend);
    ToriMem_Free(ToriMem_Net(), self);
}

static struct NetTransportVTable const k_replay_vtable = {
    .poll = replay_poll,
    .free_ = replay_free,
};

struct NetTransport*
NetTransport_NewReplay(
    struct RsproxRecording* rec,
    struct GameProtoRevTable const* rev)
{
    struct NetTransportReplay* self;

    assert(rec);
    assert(rec->frames);
    assert(rev);
    assert(rev->packetout_size);
    assert(rev->opcode_smart2);
    self = ToriMem_Calloc(ToriMem_Net(), 1, sizeof(*self));
    assert(self);
    self->base.vtable = &k_replay_vtable;
    /* Moved, not copied: the frames point into the recording's buffer. */
    self->rec = *rec;
    memset(rec, 0, sizeof(*rec));
    self->rev = rev;
    self->last_status = -1;
    self->phase = REPLAY_IDLE;
    self->enc = isaac_new(NULL, 0);
    self->dec = isaac_new(NULL, 0);
    assert(self->enc);
    assert(self->dec);
    self->map_build_complete_wire = rev->packetout_code(PKTOUT_NAME_MAP_BUILD_COMPLETE);
    self->logout_wire = rev->packetin_wire(PKT_NAME_LOGOUT);
    self->logout_transfer_wire = rev->packetin_wire(PKT_NAME_LOGOUT_TRANSFER);
    assert(self->map_build_complete_wire >= 0);
    assert(self->logout_wire >= 0);
    /* After the move: the scrubber reads the transport's copy. */
    self->scrub = ReplayScrub_New(&self->rec, rev);
    /* TORIRS_REPLAY_SEEK=fullsim|fast picks the seek plan; anything else is
     * refused aloud and the default kept. */
    {
        char const* seek = getenv("TORIRS_REPLAY_SEEK");
        char const* cycles = getenv("TORIRS_REPLAY_CHASE_CYCLES");

        ReplayScrub_SetSeekMode(self->scrub, REPLAY_SEEK_DEFAULT_MODE);
        if( seek && strcmp(seek, "fast") == 0 )
            ReplayScrub_SetSeekMode(self->scrub, REPLAY_SEEK_MODE_FAST);
        else if( seek && strcmp(seek, "fullsim") == 0 )
            ReplayScrub_SetSeekMode(self->scrub, REPLAY_SEEK_MODE_FULLSIM);
        else if( seek && seek[0] )
            TORIRS_ERR("replay: TORIRS_REPLAY_SEEK=%s is neither fast nor fullsim; ignored\n", seek);
        ReplayScrub_SetChaseCycles(self->scrub, REPLAY_CHASE_DEFAULT_CYCLES);
        if( cycles && cycles[0] )
        {
            int const k = atoi(cycles);

            if( k >= 1 && k <= REPLAY_TICK_CYCLES )
                ReplayScrub_SetChaseCycles(self->scrub, k);
            else
                TORIRS_ERR("replay: TORIRS_REPLAY_CHASE_CYCLES=%s is not 1..%d; ignored\n", cycles,
                           REPLAY_TICK_CYCLES);
        }
    }
    replay_reset(self);
    return &self->base;
}

void
NetTransport_ReplaySeekTimes(
    struct NetTransport* t,
    struct ReplaySeekTimes* out)
{
    ReplayScrub_SeekTimes(as_replay(t)->scrub, out);
}

void
NetTransport_ReplaySetSeekFast(
    struct NetTransport* t,
    int fast)
{
    ReplayScrub_SetSeekMode(as_replay(t)->scrub,
                            fast ? REPLAY_SEEK_MODE_FAST : REPLAY_SEEK_MODE_FULLSIM);
}

void
NetTransport_ReplaySetChaseCycles(
    struct NetTransport* t,
    int cycles)
{
    ReplayScrub_SetChaseCycles(as_replay(t)->scrub, cycles);
}

int
NetTransport_IsReplay(struct NetTransport const* t)
{
    assert(t);
    return t->vtable == &k_replay_vtable;
}

void
NetTransport_ReplayClock(
    struct NetTransport* t,
    unsigned long long cycles)
{
    as_replay(t)->cycles_now = cycles;
}

void
NetTransport_ReplaySetUnlocked(
    struct NetTransport* t,
    int unlocked)
{
    struct NetTransportReplay* self = as_replay(t);

    if( self->unlocked == (unlocked != 0) )
        return;
    self->unlocked = unlocked != 0;
    TORIRS_REPORT("replay: %s\n", self->unlocked
                                      ? "unlocked (as fast as the client runs, 30 cycles a tick)"
                                      : "locked to the recording's own pace");
}

int
NetTransport_ReplayUnlocked(struct NetTransport* t)
{
    return replay_unlocked(as_replay(t));
}

void
NetTransport_ReplayClientTicks(
    struct NetTransport* t,
    unsigned serial)
{
    ReplayScrub_ClientTicks(as_replay(t)->scrub, serial);
}

void
NetTransport_ReplayClientBusy(
    struct NetTransport* t,
    int busy)
{
    as_replay(t)->client_busy = busy != 0;
    ReplayScrub_ClientBusy(as_replay(t)->scrub, busy);
}

void
NetTransport_ReplaySeek(
    struct NetTransport* t,
    int tick)
{
    ReplayScrub_SeekTick(as_replay(t)->scrub, tick);
}

void
NetTransport_ReplayStep(
    struct NetTransport* t,
    int ticks)
{
    ReplayScrub_Step(as_replay(t)->scrub, ticks);
}

void
NetTransport_ReplayHold(struct NetTransport* t)
{
    ReplayScrub_Hold(as_replay(t)->scrub);
}

void
NetTransport_ReplayRun(struct NetTransport* t)
{
    ReplayScrub_Run(as_replay(t)->scrub);
}

void
NetTransport_ReplayHoldAt(
    struct NetTransport* t,
    int tick)
{
    ReplayScrub_HoldAt(as_replay(t)->scrub, tick);
}

/* Nothing of the stream is in flight between the transport and the client:
 * the staged bytes (control frames among them) are all on the bus, and the
 * connection is not owed a reconnect or a hop. */
static int
replay_quiet(struct NetTransportReplay const* self)
{
    return self->phase == REPLAY_PLAYING && self->pend_len == self->pend_off && !session_owed(self);
}

int
NetTransport_ReplayCheckpointWanted(
    struct NetTransport* t,
    int* out_tick,
    unsigned long long* out_clock_ms)
{
    struct NetTransportReplay* self = as_replay(t);
    uint64_t clock = 0;

    assert(out_tick);
    assert(out_clock_ms);
    if( !replay_quiet(self) || !ReplayScrub_CheckpointWanted(self->scrub, out_tick, &clock) )
        return 0;
    *out_clock_ms = clock;
    return 1;
}

void
NetTransport_ReplayCheckpointAdd(
    struct NetTransport* t,
    int tick,
    unsigned long long clock_ms,
    struct ReplayCheckpoint* cp)
{
    struct NetTransportReplay* self = as_replay(t);

    ReplayCheckpointStore_Add(ReplayScrub_Checkpoints(self->scrub), tick, (uint64_t)clock_ms, cp,
                              ReplayScrub_Tick(self->scrub));
}

struct ReplayCheckpoint const*
NetTransport_ReplayRestoreWanted(struct NetTransport* t)
{
    struct NetTransportReplay* self = as_replay(t);

    if( !replay_quiet(self) )
        return NULL;
    return ReplayScrub_RestoreWanted(self->scrub);
}

void
NetTransport_ReplayRestored(
    struct NetTransport* t,
    double cost_ms)
{
    struct NetTransportReplay* self = as_replay(t);

    /* At the boundary every staged byte is on the bus. */
    assert(self->pend_len == self->pend_off);
    ReplayScrub_Restored(self->scrub, cost_ms);
}

void
NetTransport_ReplaySeekFromCheckpoint(
    struct NetTransport* t,
    int tick,
    struct ReplayCheckpoint const* cp,
    int cp_tick,
    unsigned long long cp_clock_ms,
    int preroll)
{
    ReplayScrub_SeekFromCheckpoint(as_replay(t)->scrub, tick, cp, cp_tick, (uint64_t)cp_clock_ms, preroll);
}

void
NetTransport_ReplayCheckpointStats(
    struct NetTransport* t,
    int* out_count,
    unsigned long long* out_bytes)
{
    struct ReplayCheckpointStore const* store = ReplayScrub_Checkpoints(as_replay(t)->scrub);

    assert(out_count);
    assert(out_bytes);
    *out_count = ReplayCheckpointStore_Count(store);
    *out_bytes = ReplayCheckpointStore_Bytes(store);
}

int
NetTransport_ReplaySeeking(struct NetTransport* t)
{
    return ReplayScrub_Seeking(as_replay(t)->scrub);
}

int
NetTransport_ReplayHeld(struct NetTransport* t)
{
    return ReplayScrub_Held(as_replay(t)->scrub);
}

char*
NetTransport_ReplayTimelineJson(struct NetTransport* t)
{
    return ReplayTimeline_Json(ReplayScrub_Timeline(as_replay(t)->scrub));
}

void
NetTransport_ReplayGetStatus(
    struct NetTransport* t,
    struct NetTransportReplayStatus* out)
{
    struct NetTransportReplay* self = as_replay(t);
    struct ReplayScrub const* scrub = self->scrub;
    struct ReplaySeekPlan plan;
    static char const* const k_phase[] = {
        "idle", "connecting", "logging_in", "logging_in", "playing", "finished",
    };

    assert(out);
    memset(out, 0, sizeof(*out));
    out->phase = k_phase[self->phase];
    /* While seeking, the tick the work has reached; held, the tick it holds
     * at; playing, the tick of the last packet staged. */
    if( ReplayScrub_Seeking(scrub) )
        out->tick = ReplayScrub_Reached(scrub);
    else if( ReplayScrub_Held(scrub) )
        out->tick = ReplayScrub_Tick(scrub);
    else
        out->tick = ReplayScrub_LastTick(scrub);
    out->tick_count = self->rec.tick_count;
    out->clock_ms = ReplayScrub_ClockMs(scrub);
    out->duration_ms = self->rec.duration_ms;
    out->unlocked = replay_unlocked(self);
    out->waiting_map = ReplayScrub_WaitingMap(scrub);
    out->revision = self->rec.revision;
    out->world = self->rec.world_id;
    out->sessions = self->sessions;
    out->resumed = self->resumed;
    out->held = ReplayScrub_Held(scrub);
    out->seeking = ReplayScrub_Plan(scrub, &plan);
    out->seek_target = out->seeking ? plan.target : -1;
    out->seek_from = out->seeking ? plan.from : -1;
    out->seek_phase = ReplayScrub_SeekPhase(scrub);
    out->timestamp_ms = self->rec.timestamp_ms;
}
