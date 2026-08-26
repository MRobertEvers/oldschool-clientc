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
#include "platform_sdl2.h"

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
};

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
    long now;
    int run_tick;
    int got;

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
    now = (long)PlatformSDL2_Ticks64();
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

    {
        int alive = 1;
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_SERVER)
        {
            alive = ToriRSServer_EmbedPump(self->embed, run_tick);
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
embed_free(struct NetTransport* transport)
{
    struct NetTransportEmbed* self = (struct NetTransportEmbed*)transport;

    if( self->embed )
        ToriRSServer_EmbedStop(self->embed);
    free(self);
}

static struct NetTransportVTable const k_embed_vtable = {
    .poll = embed_poll,
    .free_ = embed_free,
};

struct NetTransport*
NetTransport_NewEmbed(int default_port, char const* rev_name)
{
    struct NetTransportEmbed* self = calloc(1, sizeof(*self));

    (void)default_port; /* nothing to bind, nothing to dial */
    assert(self);
    self->base.vtable = &k_embed_vtable;
    self->rev_name = rev_name;
    self->last_status = -1;
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

#endif
