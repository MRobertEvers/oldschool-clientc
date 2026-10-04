/*
 * The server, in this process.
 *
 * A third NetTransport beside TCP and WebSocket, and the only one with no wire
 * under it: instead of a socket it drives `ToriRSServer_Embed_*`, moving bytes
 * between the net subsystem's outbound ring and the server's inbound queue.
 *
 *      client PopOut   --->  ToriRSServer_EmbedWrite
 *                            ToriRSServer_EmbedPump      (and one 600 ms tick)
 *      NET_RECV        <---  ToriRSServer_EmbedRead
 *
 * Nothing above this file changes. `ToriRS_Network` never touched a descriptor
 * to begin with — bytes arrive through HandleCmd(NET_RECV) and leave through
 * PopOut — so from the client's point of view this is the same login handshake,
 * the same RSA block and the same ISAAC ciphers as a real connection. The bytes
 * are the real protocol; only the thing carrying them is different.
 *
 * Selected with `[net:boot] transport=embed` in a manifest, or `--connect
 * embed`. Requires a build with EMBED_SERVER=1 — see the bottom of this file for
 * what happens without one, and src/makefile for why it is not the default.
 */

#include "net_transport.h"

#include "cmd/cmdbus.h"
#include "net/net.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/torirs_log.h"
#ifdef TORIRS_EMBED_SERVER

/* Inside the guard: a build without the embedded server pulls in neither the
 * server's headers nor the SDL clock. */
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_zone.h"
#include "perf/torirs_perf.h"
#include "platform_window.h"

/* The party link's descriptors are POSIX; the link itself is
 * torirs_server_embed.c's, which aborts on a host without it. */
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(TORIRS_PLATFORM_WEB)
#define EMBED_PARTY_SOCKETS 1
#include <unistd.h>
#else
#define EMBED_PARTY_SOCKETS 0
#endif

/* The server's own tick. Matched to the real one rather than to the frame rate:
 * a client rendering at 144 Hz must not run the world 144 times a second. */
#define EMBED_TICK_MS 600

struct NetTransportEmbed
{
    struct NetTransport base;
    struct ToriRSServerEmbed* embed;
    /* The client's protocol name, forwarded to ToriRSServer_EmbedStart so the
     * in-process server's wire always matches the client's. Points into the
     * static revision table, so no copy is needed. */
    char const* rev_name;
    int last_status;
    long next_tick_ms;
    int test_clock;
    unsigned long long test_now;
    /* TORIRS_EMBED_CLOCK_MS: ms this transport pretends have passed per poll,
     * instead of reading the wall clock. @see embed_poll_clock_ms. */
    int poll_clock_ms;
    unsigned long long poll_clock_now;

    /*
     * The party (torirs_server_embed.h). A LEADER hosts the world as always
     * and also listens for members: `party_listener` >= 0. A MEMBER hosts no
     * world: `party_join_port` > 0, and its bytes go to the leader's world
     * over `party_fd`, which also carries the boundary handshake that keeps
     * the two clocks in lock step. Neither is set unless a knob asks, so a
     * one-client run takes none of these paths.
     */
    int party_listener;
    int party_size;
    int party_wait_ms;
    int party_trace;
    int party_join_port;
    int party_fd;
    struct ToriRSServerEmbedLinkReader party_in;
    /** Server bytes received for this client, not yet on the bus. */
    uint8_t* party_pending;
    int party_pending_len;
    int party_pending_cap;
    /** The link closed: deliver what is pending, then report it. */
    int party_closed;
    int party_last_tick;
    int party_boundaries;
    /** TORIRS_EMBED_PARTY_SEAT: the seat this member asks for (0: any). */
    int party_seat;
    /** A link was up once. A later dial is a relog, to a leader that is
     *  listening or gone -- either way it answers at once, so it does not
     *  get the boot-time wait. */
    int party_ever_joined;
};

/*
 * TORIRS_EMBED_CLOCK_MS=<ms> — drive the server's 600 ms tick off the POLL
 * COUNT instead of the wall clock.
 *
 * `TORIRS_MAX_FRAMES` already frame-locks the client half: app_frame.c pays
 * exactly one 20 ms logic tick per frame "so that three frames mean the same
 * three cycles every run". The server half was still on the wall clock, so the
 * world ticked whenever the host machine happened to get there — and a bounded
 * headless run of a live world came out DIFFERENT every time. Two runs of the
 * Zuk encounter under an identical command line put the glyph in different
 * places and its animation on a different frame, which is enough to make an
 * A/B of anything in the scene meaningless (tools/zuk_glyph/ needs exactly
 * that A/B).
 *
 * 20 matches the client's own logic tick, so 30 polls make one server tick.
 * Unset, nothing changes: a real session stays on the real clock, because a
 * client that stalls must not have the world stall with it.
 *
 * Read once.
 */
static int
embed_poll_clock_ms(void)
{
    static int cached = -1;

    if( cached < 0 )
    {
        char const* env = getenv("TORIRS_EMBED_CLOCK_MS");

        cached = (env && env[0]) ? atoi(env) : 0;
        if( cached < 0 )
            cached = 0;
    }
    return cached;
}

/*
 * The party knobs (torirs_server_embed.h; test/raids/README.md).
 *
 *   TORIRS_EMBED_PARTY_LISTEN=<port>  leader: host the world as always, and
 *                                     accept members on 127.0.0.1:<port>
 *   TORIRS_EMBED_PARTY_SIZE=<n>       leader: hold the world's boundaries
 *                                     until n-1 members have joined
 *   TORIRS_EMBED_PARTY_JOIN=<port>    member: host no world; play in the
 *                                     leader's, on 127.0.0.1:<port>
 *   TORIRS_EMBED_PARTY_SEAT=<n>       member: take seat n (2..; the leader
 *                                     is seat 1), i.e. log in n-th, so the
 *                                     pids are the same every run
 *   TORIRS_EMBED_PARTY_WAIT_S=<s>     both: longest wait for the other end at
 *                                     a boundary (default 60)
 *   TORIRS_EMBED_PARTY_TRACE=1        member: log every boundary's tick
 *
 * Read once, at transport creation, so the leader is listening before its
 * loaders run (a member dialling during the leader's boot waits in the
 * backlog instead of being refused).
 */
static int
party_env_int(
    char const* name,
    int fallback)
{
    char const* value = getenv(name);

    return value && value[0] ? atoi(value) : fallback;
}

static void
party_knobs(struct NetTransportEmbed* self)
{
    int listen_port = party_env_int("TORIRS_EMBED_PARTY_LISTEN", 0);
    int join_port = party_env_int("TORIRS_EMBED_PARTY_JOIN", 0);

    self->party_wait_ms = party_env_int("TORIRS_EMBED_PARTY_WAIT_S", 60) * 1000;
    self->party_size = party_env_int("TORIRS_EMBED_PARTY_SIZE", 0);
    self->party_trace = party_env_int("TORIRS_EMBED_PARTY_TRACE", 0);
    self->party_seat = party_env_int("TORIRS_EMBED_PARTY_SEAT", 0);
    if( listen_port <= 0 && join_port <= 0 )
        return;
    if( listen_port > 0 && join_port > 0 )
    {
        TORIRS_ERR("net: party: TORIRS_EMBED_PARTY_LISTEN and _JOIN are both set; a client "
                   "is the leader or a member, not both\n");
        abort();
    }
    if( self->party_wait_ms <= 0 || self->party_size < 0 ||
        self->party_size > TORIRSSERVER_EMBED_CLIENT_MAX || self->party_seat < 0 ||
        self->party_seat == 1 || self->party_seat > TORIRSSERVER_EMBED_CLIENT_MAX )
    {
        TORIRS_ERR("net: party: TORIRS_EMBED_PARTY_WAIT_S must be > 0, "
                   "TORIRS_EMBED_PARTY_SIZE 0..%d and TORIRS_EMBED_PARTY_SEAT 0 or 2..%d\n",
                   TORIRSSERVER_EMBED_CLIENT_MAX, TORIRSSERVER_EMBED_CLIENT_MAX);
        abort();
    }
    if( join_port > 0 )
    {
        self->party_join_port = join_port;
        return;
    }
    self->party_listener = ToriRSServer_EmbedPartyListen(listen_port);
    if( self->party_listener < 0 )
    {
        /* A party run whose leader cannot listen is a run whose members
         * would each wait out their dial and fail one by one; stop here. */
        TORIRS_ERR("net: party: the leader cannot listen on %d\n", listen_port);
        abort();
    }
}

static void
emit_status(
    struct NetTransportEmbed* self,
    struct ToriRS_CmdBus* bus,
    int status)
{
    if( self->last_status == status )
        return;
    self->last_status = status;
    CmdBus_PushNetStatus(bus, status);
}

/*
 * Advance this transport's clock to this poll and say whether it crossed the
 * next 600 ms boundary. The leader runs the world's tick on it; a member
 * sends READY on it and waits for the leader's TICK.
 */
static int
embed_clock_step(struct NetTransportEmbed* self)
{
    long now;
    int run_tick;

    if( self->test_clock )
        now = (long)self->test_now;
    else if( self->poll_clock_ms > 0 )
    {
        self->poll_clock_now += (unsigned long long)self->poll_clock_ms;
        now = (long)self->poll_clock_now;
    }
    else
        now = (long)PlatformWindow_Ticks64();
    run_tick = self->next_tick_ms == 0 || now >= self->next_tick_ms;
    if( run_tick )
    {
        /* Anchored to the schedule rather than to `now`, so a slow frame does
         * not push every later tick out — same rule as the socket server's. */
        if( self->next_tick_ms == 0 || now - self->next_tick_ms > 5 * EMBED_TICK_MS )
            self->next_tick_ms = now + EMBED_TICK_MS;
        else
            self->next_tick_ms += EMBED_TICK_MS;
    }
    return run_tick;
}

static void party_member_poll(
    struct NetTransportEmbed* self,
    struct ToriRS_Network* net,
    struct ToriRS_CmdBus* bus);

static void
embed_poll(
    struct NetTransport* transport,
    struct ToriRS_Network* net,
    struct ToriRS_CmdBus* bus)
{
    struct NetTransportEmbed* self = (struct NetTransportEmbed*)transport;
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    static uint8_t inbound[65536];
    int run_tick;
    int got;

    if( self->party_join_port > 0 )
    {
        party_member_poll(self, net, bus);
        return;
    }

    /* 1. client -> server */
    while( ToriRS_Network_PopOut(net, &header, payload) )
    {
        if( header.type == TORIRS_NET_OUT_CONNECT )
        {
            /*
             * There is no host to dial, so "connecting" and "connected" are the
             * same instant. Saying so matters: the client holds its login block
             * until the transport reports CONNECTED, because over a socket that
             * is the platform layer's job to report.
             */
            if( !self->embed )
            {
                self->embed = ToriRSServer_EmbedStart(self->rev_name);
                if( !self->embed )
                {
                    TORIRS_ERR("net: embedded server failed to start\n");
                    emit_status(self, bus, TORIRS_NET_STATUS_FAILED);
                    return;
                }
                if( self->party_listener >= 0 )
                    ToriRSServer_EmbedPartyAttach(self->embed, self->party_listener,
                                                  self->party_size, self->party_wait_ms);
                self->next_tick_ms = 0;
            }
            emit_status(self, bus, TORIRS_NET_STATUS_CONNECTED);
        }
        else if( header.type == TORIRS_NET_OUT_SEND_DATA && self->embed )
        {
            /* One client: this host is the game itself, playing alone. */
            ToriRSServer_EmbedWrite(self->embed, 0, payload, header.length);
        }
        else if( header.type == TORIRS_NET_OUT_DISCONNECT && self->embed )
        {
            /*
             * The embedded server dies with the session it served, exactly as
             * the forked child of the socket host does — including the save it
             * writes on the way out. A following CONNECT starts a fresh one,
             * which is what makes a reconnect over this transport equivalent
             * to one over a socket.
             */
            ToriRSServer_EmbedStop(self->embed);
            self->embed = NULL;
            self->next_tick_ms = 0;
            emit_status(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
        }
    }

    if( !self->embed )
        return;

    /* 2. let the server act, and tick it on its own schedule */
    run_tick = embed_clock_step(self);

    {
        int alive = 1;
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_SERVER)
        {
            alive = ToriRSServer_EmbedPump(self->embed, run_tick);
            /* With members attached the pump's "anyone alive" outlives this
             * client's own session; the leader's link is client 0's. */
            if( self->party_listener >= 0 )
                alive = alive && ToriRSServer_EmbedClientAlive(self->embed, 0);
            /* Growth gauges: sample after the pump so a tick's zone/npc work is
             * reflected this frame. Cheap (two ints + one field). */
            {
                struct ToriRSServer* srv = ToriRSServer_EmbedWorld(self->embed);
                int zone_count = 0;
                int zone_cap = 0;
                if( srv )
                {
                    ToriRSServer_ZoneMapStats(srv, &zone_count, &zone_cap);
                    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_ZONE_MAP_COUNT, zone_count);
                    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_ZONE_MAP_CAPACITY, zone_cap);
                    TORIRS_PERF_COUNT_SET(
                        TORIRS_PERF_CTR_NPC_SLOT_MAX, srv->npc_slot_max);
                }
            }
        }
        if( !alive )
        {
            emit_status(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
            return;
        }
    }

    /*
     * 3. server -> client, in bus-sized pieces.
     *
     * Room first: ToriRSServer_EmbedRead consumes what it returns, so a read whose
     * bytes the bus then refuses is a hole in the byte stream rather than a
     * delay. Requiring space for a whole `inbound` (plus one header per chunk
     * it could be split into) before reading keeps the remainder queued in the
     * embedded server until the next poll.
     */
    while( CmdBus_FreeBytes(bus) >=
               sizeof(inbound) + (sizeof(inbound) / TORIRS_CMD_MAX_PAYLOAD + 1) *
                                     sizeof(struct ToriRS_CmdHeader) &&
           (got = ToriRSServer_EmbedRead(self->embed, 0, inbound, (int)sizeof(inbound))) > 0 )
    {
        int off = 0;

        while( off < got )
        {
            int chunk = got - off;

            if( chunk > TORIRS_CMD_MAX_PAYLOAD )
                chunk = TORIRS_CMD_MAX_PAYLOAD;
            CmdBus_Push(bus, TORIRS_CMD_NET_RECV, inbound + off, (uint16_t)chunk);
            off += chunk;
        }
    }
}

static void
party_close(struct NetTransportEmbed* self)
{
#if EMBED_PARTY_SOCKETS
    if( self->party_fd >= 0 )
        close(self->party_fd);
#endif
    self->party_fd = -1;
    ToriRSServer_EmbedLinkReaderFree(&self->party_in);
}

static void
embed_free(struct NetTransport* transport)
{
    struct NetTransportEmbed* self = (struct NetTransportEmbed*)transport;

    if( self->embed )
        ToriRSServer_EmbedStop(self->embed);
    party_close(self);
#if EMBED_PARTY_SOCKETS
    if( self->party_listener >= 0 )
        close(self->party_listener);
#endif
    free(self->party_pending);
    free(self);
}

/* ------------------------------------------------------------------ */
/* Party member: this client's world is in the leader's process        */
/* ------------------------------------------------------------------ */

static void
party_pending_append(
    struct NetTransportEmbed* self,
    const uint8_t* data,
    int len)
{
    if( self->party_pending_len + len > self->party_pending_cap )
    {
        int cap = self->party_pending_cap ? self->party_pending_cap : 65536;

        while( cap < self->party_pending_len + len )
            cap *= 2;
        self->party_pending = (uint8_t*)realloc(self->party_pending, (size_t)cap);
        assert(self->party_pending);
        self->party_pending_cap = cap;
    }
    memcpy(self->party_pending + self->party_pending_len, data, (size_t)len);
    self->party_pending_len += len;
}

/* The link went away (or was given up on): deliver what arrived, then say so. */
static void
party_member_lost(
    struct NetTransportEmbed* self,
    char const* why)
{
    TORIRS_ERR("net: party: link to the leader %s after %d boundar%s\n", why,
               self->party_boundaries, self->party_boundaries == 1 ? "y" : "ies");
    party_close(self);
    self->party_closed = 1;
}

/*
 * This client reached its boundary: READY, then wait for the leader's TICK,
 * collecting the tick's output on the way. Blocking here is the lock step --
 * this client does not run past a tick its world has not run.
 */
static void
party_member_boundary(struct NetTransportEmbed* self)
{
    long deadline = ToriRSServer_EmbedNowMs() + self->party_wait_ms;

    if( !ToriRSServer_EmbedLinkSend(self->party_fd, TORIRSSERVER_EMBED_LINK_READY, NULL, 0) )
    {
        party_member_lost(self, "closed");
        return;
    }
    for( ;; )
    {
        int type;
        const uint8_t* payload;
        int len;
        long left;
        int filled;

        while( ToriRSServer_EmbedLinkNext(&self->party_in, &type, &payload, &len) )
        {
            if( type == TORIRSSERVER_EMBED_LINK_DATA )
                party_pending_append(self, payload, len);
            else if( type == TORIRSSERVER_EMBED_LINK_TICK && len == 4 )
            {
                int tick = (int)(((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                                 ((uint32_t)payload[2] << 8) | (uint32_t)payload[3]);

                self->party_boundaries++;
                if( self->party_trace )
                    TORIRS_ERR("net: party: boundary %d -> server tick %d (pending %d bytes)\n",
                               self->party_boundaries, tick, self->party_pending_len);
                self->party_last_tick = tick;
                return;
            }
            else
            {
                party_member_lost(self, "sent a frame the leader never sends");
                return;
            }
        }
        left = deadline - ToriRSServer_EmbedNowMs();
        if( left <= 0 )
        {
            party_member_lost(self, "sent no TICK within TORIRS_EMBED_PARTY_WAIT_S");
            return;
        }
        filled = ToriRSServer_EmbedLinkFill(self->party_fd, &self->party_in,
                                            left > 1000 ? 1000 : (int)left);
        if( filled < 0 )
        {
            /* Whatever the leader sent before it went still counts. */
            while( ToriRSServer_EmbedLinkNext(&self->party_in, &type, &payload, &len) )
                if( type == TORIRSSERVER_EMBED_LINK_DATA )
                    party_pending_append(self, payload, len);
            party_member_lost(self, "closed");
            return;
        }
    }
}

static void
party_member_poll(
    struct NetTransportEmbed* self,
    struct ToriRS_Network* net,
    struct ToriRS_CmdBus* bus)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    /* 1. client -> the leader's world */
    while( ToriRS_Network_PopOut(net, &header, payload) )
    {
        if( header.type == TORIRS_NET_OUT_CONNECT )
        {
            if( self->party_fd < 0 )
            {
                uint8_t seat[4];

                self->party_fd = ToriRSServer_EmbedPartyDial(
                    self->party_join_port, self->party_ever_joined ? 2000 : self->party_wait_ms);
                if( self->party_fd < 0 )
                {
                    emit_status(self, bus, TORIRS_NET_STATUS_FAILED);
                    return;
                }
                seat[0] = (uint8_t)(self->party_seat >> 24);
                seat[1] = (uint8_t)(self->party_seat >> 16);
                seat[2] = (uint8_t)(self->party_seat >> 8);
                seat[3] = (uint8_t)self->party_seat;
                if( !ToriRSServer_EmbedLinkSend(self->party_fd, TORIRSSERVER_EMBED_LINK_SEAT, seat,
                                                4) )
                {
                    party_close(self);
                    emit_status(self, bus, TORIRS_NET_STATUS_FAILED);
                    return;
                }
                self->party_ever_joined = 1;
                TORIRS_ERR("net: party: joined the leader on 127.0.0.1:%d (seat %d)\n",
                           self->party_join_port, self->party_seat);
                self->party_closed = 0;
                self->party_pending_len = 0;
                self->party_boundaries = 0;
                self->next_tick_ms = 0;
            }
            emit_status(self, bus, TORIRS_NET_STATUS_CONNECTED);
        }
        else if( header.type == TORIRS_NET_OUT_SEND_DATA && self->party_fd >= 0 )
        {
            if( !ToriRSServer_EmbedLinkSend(self->party_fd, TORIRSSERVER_EMBED_LINK_DATA, payload,
                                            header.length) )
                party_member_lost(self, "closed");
        }
        else if( header.type == TORIRS_NET_OUT_DISCONNECT && self->party_fd >= 0 )
        {
            /* The leader's world logs this player out when the link ends,
             * as a socket server does when a socket ends. */
            party_close(self);
            self->party_pending_len = 0;
            self->next_tick_ms = 0;
            emit_status(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
        }
    }

    /* 2. the boundary, on this client's own clock */
    if( self->party_fd >= 0 && embed_clock_step(self) )
        party_member_boundary(self);

    /* 3. the leader's bytes -> this client, as the bus has room */
    if( self->party_pending_len > 0 )
    {
        int off = 0;

        while( off < self->party_pending_len &&
               CmdBus_FreeBytes(bus) >=
                   (size_t)TORIRS_CMD_MAX_PAYLOAD + sizeof(struct ToriRS_CmdHeader) )
        {
            int chunk = self->party_pending_len - off;

            if( chunk > TORIRS_CMD_MAX_PAYLOAD )
                chunk = TORIRS_CMD_MAX_PAYLOAD;
            CmdBus_Push(bus, TORIRS_CMD_NET_RECV, self->party_pending + off, (uint16_t)chunk);
            off += chunk;
        }
        memmove(self->party_pending, self->party_pending + off,
                (size_t)(self->party_pending_len - off));
        self->party_pending_len -= off;
    }
    if( self->party_closed && self->party_pending_len == 0 )
    {
        self->party_closed = 0;
        self->next_tick_ms = 0;
        emit_status(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
    }
}

static struct NetTransportVTable const k_embed_vtable = {
    .poll = embed_poll,
    .free_ = embed_free,
};

struct ToriRSServerEmbed*
NetTransport_TestClock(struct NetTransport* t, unsigned long long now)
{
    if( !t || t->vtable != &k_embed_vtable ) return NULL;
    struct NetTransportEmbed* self = (struct NetTransportEmbed*)t;
    self->test_clock = 1;
    self->test_now = now;
    return self->embed;
}

struct NetTransport*
NetTransport_NewEmbed(int default_port, char const* rev_name)
{
    struct NetTransportEmbed* self = calloc(1, sizeof(*self));

    (void)default_port; /* nothing to bind, nothing to dial */
    assert(self);
    self->base.vtable = &k_embed_vtable;
    self->rev_name = rev_name;
    self->last_status = -1;
    self->poll_clock_ms = embed_poll_clock_ms();
    self->party_listener = -1;
    self->party_fd = -1;
    party_knobs(self);
    /* The server is started on CONNECT rather than here, so a client that never
     * logs in does not pay for loading the cache and the content tree. */
    return &self->base;
}

#else /* !TORIRS_EMBED_SERVER */

/*
 * Not built in.
 *
 * The embedded server is opt-in because linking it puts the whole server —
 * the tick, the script VM, the content loaders — inside the client binary, and
 * makes the client's build depend on a content tree it otherwise never reads.
 * That is the right default for a *client*; it is the wrong default for anyone
 * who wants one process.
 *
 *     make -C src torirs EMBED_SERVER=1
 *
 * Failing loudly here beats silently falling back to TCP and leaving someone to
 * work out why a manifest that says `transport=embed` is dialling port 43594.
 */
struct NetTransport*
NetTransport_NewEmbed(int default_port, char const* rev_name)
{
    (void)default_port;
    (void)rev_name;
    TORIRS_LOG("net: this build has no embedded server — rebuild with "
            "`make -C src torirs EMBED_SERVER=1`\n");
    return NULL;
}

struct ToriRSServerEmbed*
NetTransport_TestClock(struct NetTransport* t, unsigned long long now)
{
    (void)t; (void)now;
    return NULL;
}

#endif
