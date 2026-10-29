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
 *     session id, GAMELOGIN (or GAMERECONNECT) gets LoginResponse.Ok carrying
 *     the recording's local player index. RSProx decrypts the RSA block to
 *     learn the cipher seed; this transport is in the client's own process and
 *     reads the seed the login driver published (net->prev_seed) instead, so
 *     no private key is involved.
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
#include "platform_window.h"

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
/* LoginResponse.Ok's length byte: the 34-byte body plus the 3 bytes RSProt
 * has always added (ReplayClientLoginHandler.LOGIN_OK_LENGTH). */
#define REPLAY_LOGIN_OK_LENGTH 37
#define REPLAY_LOGIN_OK_BODY 34

#define REPLAY_CYCLE_MS 20
/* A locked frame advances the clock by its wall time, but never by more than
 * this: a stall (a hidden tab, a synchronous load) is not recorded time. */
#define REPLAY_MAX_STEP_MS 1000
/* Below NET_LINK_TIMEOUT_MS (15 s): a client that never sends
 * MAP_BUILD_COMPLETE is played on, not dropped for silence. */
#define REPLAY_MAP_WAIT_MS 10000
/* The longest recorded silence played as silence. A server writes every
 * 600 ms tick; a longer gap is the recorder's side stalling (a frozen client,
 * a suspended machine), and the recorder's own session survived it. Played in
 * full, a gap past NET_LINK_TIMEOUT_MS reads to this client as a dead server:
 * RSProx 4407 is silent for 18.8 s after tick 747, so the replay dropped and
 * restarted from the login every time it got there. */
#define REPLAY_MAX_GAP_MS 5000
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

    /* Playback. */
    int next_frame;
    uint64_t clock_ms;
    int wait_map;
    int wait_tick;
    int map_built;
    uint64_t wait_since_ms;
    int logout_wire;
    int last_tick;
    /** A RECONNECT frame waiting for the bytes before it to reach the bus
     *  (-1: none): it goes as TORIRS_CMD_NET_RESUMED, which the stream's bytes
     *  must not overtake. */
    int resume_frame;

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

static void
cin_consume(
    struct NetTransportReplay* self,
    int n)
{
    assert(n <= self->cin_len);
    memmove(self->cin, self->cin + n, (size_t)(self->cin_len - n));
    self->cin_len -= n;
}

static void
replay_reset(struct NetTransportReplay* self)
{
    self->cin_len = 0;
    self->c_opcode = -1;
    self->c_len_bytes = 0;
    self->c_len = 0;
    self->c_skip = 0;
    self->pend_len = 0;
    self->pend_off = 0;
    self->next_frame = 0;
    self->clock_ms = 0;
    self->wait_map = 0;
    self->map_built = 0;
    self->last_tick = 0;
    self->resume_frame = -1;
}

/* --- the handshake ------------------------------------------------------- */

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
        uint8_t ok[2 + REPLAY_LOGIN_OK_BODY];
        int o = 0;

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

        /* LoginResponse.Ok (ReplayClientLoginHandler.sendLoginOk): no
         * authenticator, no staff level, the recorder's slot, a member. A
         * GAMERECONNECT gets it too -- RSProx treats a reconnect it cannot
         * hand a session back to as a clean login, and so does this. */
        memset(ok, 0, sizeof(ok));
        ok[o++] = REPLAY_LOGINRES_OK;
        ok[o++] = REPLAY_LOGIN_OK_LENGTH;
        o += 1 + 4 + 1 + 1; /* authenticator kind + code, staffModLevel, playerMod */
        ok[o++] = (uint8_t)(self->rec.local_player_index >> 8);
        ok[o++] = (uint8_t)self->rec.local_player_index;
        ok[o++] = 1; /* member */
        o += 8 + 8 + 8; /* accountHash, userId, userHash */
        assert(o == (int)sizeof(ok));
        pend_bytes(self, ok, o);
        self->phase = REPLAY_AWAIT_GAME;
        TORIRS_REPORT("replay: logged in as player %d; %d packets over %d ticks (%.1f s)%s\n",
                      self->rec.local_player_index, self->rec.frame_count, self->rec.tick_count,
                      (double)self->rec.duration_ms / 1000.0,
                      self->sessions > 0 ? ", from the start" : "");
        self->sessions++;
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
            if( self->c_opcode == self->map_build_complete_wire && self->wait_map )
                self->map_built = 1;
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

static int
frame_is_rebuild(
    struct NetTransportReplay* self,
    struct RsproxRecordingFrame const* f)
{
    int const name = self->rev->packetin_code(f->opcode);

    return name == PKT_NAME_REBUILD_NORMAL || name == PKT_NAME_REBUILD_REGION;
}

static void
play(struct NetTransportReplay* self)
{
    uint64_t const wall = PlatformWindow_Ticks64();
    uint64_t step;

    if( self->unlocked )
        step = (uint64_t)(self->cycles_now - self->cycles_last) * REPLAY_CYCLE_MS;
    else
    {
        step = wall > self->wall_last ? wall - self->wall_last : 0;
        if( step > REPLAY_MAX_STEP_MS )
            step = REPLAY_MAX_STEP_MS;
    }
    self->cycles_last = self->cycles_now;
    self->wall_last = wall;

    if( self->wait_map && !self->map_built && wall - self->wait_since_ms > REPLAY_MAP_WAIT_MS )
    {
        TORIRS_ERR("replay: no MAP_BUILD_COMPLETE %d s after the tick %d rebuild; playing on\n",
                   REPLAY_MAP_WAIT_MS / 1000, self->wait_tick);
        self->map_built = 1;
    }
    /* The barrier holds the clock too: the time the client spends building
     * is not recorded time, so the packets behind it keep their spacing. */
    if( !self->wait_map || self->map_built )
        self->clock_ms += step;

    while( self->resume_frame < 0 && self->next_frame < self->rec.frame_count &&
           self->pend_len - self->pend_off < REPLAY_PENDING_CAP )
    {
        struct RsproxRecordingFrame const* f = &self->rec.frames[self->next_frame];

        if( self->wait_map && f->tick > self->wait_tick )
        {
            if( !self->map_built )
                break;
            self->wait_map = 0;
        }
        if( f->t_ms > self->clock_ms )
        {
            /* A stall in the recording: skip what is left of it. The clock
             * jumps, so everything after keeps its own spacing. */
            if( self->next_frame > 0 &&
                self->clock_ms >= self->rec.frames[self->next_frame - 1].t_ms + REPLAY_MAX_GAP_MS )
                self->clock_ms = f->t_ms;
            else
                break;
        }
        /* RSProx's pseudo-packets are the proxy's notes, not Jagex bytes. A
         * PACKET_GROUP_END closes what PACKET_GROUP_START already sized. A
         * RECONNECT is where the session moved to a new connection (a
         * reconnect, a world hop): the client restarts its player table there,
         * so nothing after it is staged until it is on the bus. */
        if( f->opcode == RSPROX_RECORDING_PACKET_GROUP_END )
        {
            self->next_frame++;
            continue;
        }
        if( f->opcode == RSPROX_RECORDING_RECONNECT )
        {
            self->resume_frame = self->next_frame++;
            self->last_tick = f->tick;
            break;
        }
        pend_packet(self, f->opcode, self->rec.data + f->offset, (int)f->length);
        self->last_tick = f->tick;
        self->next_frame++;
        if( frame_is_rebuild(self, f) )
        {
            self->wait_map = 1;
            self->wait_tick = f->tick;
            self->map_built = 0;
            self->wait_since_ms = wall;
        }
    }

    if( self->next_frame == self->rec.frame_count && self->resume_frame < 0 )
    {
        /* The recording ends the session, as RSProx's replay does
         * (ReplaySession.logoutClient): with its own LOGOUT when it has one. */
        if( self->rec.frame_count == 0 ||
            self->rec.frames[self->rec.frame_count - 1].opcode != self->logout_wire )
            pend_packet(self, self->logout_wire, NULL, 0);
        self->phase = REPLAY_ENDED;
        TORIRS_REPORT("replay: finished at tick %d (%.1f s of recording)\n", self->rec.tick_count,
                      (double)self->rec.duration_ms / 1000.0);
    }
}

/*
 * A recorded RECONNECT, as the client takes it from a real server: the index
 * the session continues under (rsprox_recording_resolve_reconnects; the
 * recording's own unless it found a world hop), then the GPI init block.
 */
static void
push_resume(
    struct NetTransportReplay* self,
    struct ToriRS_CmdBus* bus)
{
    struct RsproxRecordingFrame const* f = &self->rec.frames[self->resume_frame];
    static uint8_t cmd[TORIRS_CMD_MAX_PAYLOAD];

    assert((int)f->length + 2 <= (int)sizeof(cmd));
    if( !CmdBus_CanPush(bus, (uint16_t)(f->length + 2)) )
        return;
    cmd[0] = (uint8_t)(f->reconnect_index >> 8);
    cmd[1] = (uint8_t)f->reconnect_index;
    memcpy(cmd + 2, self->rec.data + f->offset, f->length);
    CmdBus_Push(bus, TORIRS_CMD_NET_RESUMED, cmd, (uint16_t)(f->length + 2));
    TORIRS_REPORT("replay: tick %d: the recorded session reconnected; continuing as player %d\n",
                  f->tick, f->reconnect_index);
    self->resume_frame = -1;
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

    /* 1. client -> "server" */
    while( ToriRS_Network_PopOut(net, &header, payload) )
    {
        if( header.type == TORIRS_NET_OUT_CONNECT )
        {
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
            if( self->phase == REPLAY_PLAYING )
                TORIRS_REPORT("replay: the client closed the session at tick %d of %d "
                              "(%.1f s in)\n",
                              self->last_tick, self->rec.tick_count,
                              (double)self->clock_ms / 1000.0);
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
        play(self);

    /* 3. server -> client, as the bus has room. What the bus refuses stays
     * staged: a byte consumed and not delivered is a hole in the stream. */
    while( self->pend_off < self->pend_len )
    {
        int chunk = self->pend_len - self->pend_off;

        if( chunk > TORIRS_CMD_MAX_PAYLOAD )
            chunk = TORIRS_CMD_MAX_PAYLOAD;
        if( !CmdBus_CanPush(bus, (uint16_t)chunk) )
            break;
        CmdBus_Push(bus, TORIRS_CMD_NET_RECV, self->pend + self->pend_off, (uint16_t)chunk);
        self->pend_off += chunk;
    }
    if( self->pend_off == self->pend_len )
        self->pend_off = self->pend_len = 0;
    if( self->resume_frame >= 0 && self->pend_len == 0 )
        push_resume(self, bus);
}

static void
replay_free(struct NetTransport* transport)
{
    struct NetTransportReplay* self = as_replay(transport);

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
    assert(self->map_build_complete_wire >= 0);
    assert(self->logout_wire >= 0);
    replay_reset(self);
    return &self->base;
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
    return as_replay(t)->unlocked;
}

void
NetTransport_ReplayGetStatus(
    struct NetTransport* t,
    struct NetTransportReplayStatus* out)
{
    struct NetTransportReplay* self = as_replay(t);
    static char const* const k_phase[] = {
        "idle", "connecting", "logging_in", "logging_in", "playing", "finished",
    };

    assert(out);
    memset(out, 0, sizeof(*out));
    out->phase = k_phase[self->phase];
    out->tick = self->last_tick;
    out->tick_count = self->rec.tick_count;
    out->clock_ms = self->clock_ms;
    out->duration_ms = self->rec.duration_ms;
    out->unlocked = self->unlocked;
    out->waiting_map = self->wait_map && !self->map_built;
    out->revision = self->rec.revision;
    out->world = self->rec.world_id;
    out->sessions = self->sessions;
}
