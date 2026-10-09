#ifndef SRC_PLATFORM_NET_TRANSPORT_H
#define SRC_PLATFORM_NET_TRANSPORT_H

/*
 * Transport seam. A NetTransport bridges the net subsystem's outbound ring to a
 * concrete wire and pushes received bytes onto the command bus as
 * TORIRS_CMD_NET_* — exactly the PlatformSocket_Poll contract, so the net stack
 * (and record/replay) is transport-blind. The revision table's transport_kind
 * (enum NetTransportKind in rev/gameproto_revisions.h) selects the impl:
 *   NET_TRANSPORT_TCP -> raw TCP (thin adapter over PlatformSocket)
 *   NET_TRANSPORT_WS  -> a minimal RFC 6455 WebSocket client (xrsps)
 *
 * NET_RECV always carries de-framed application bytes: the WS impl strips the
 * WebSocket frame headers, so the framer above (packetbuffer) never sees them.
 */

struct NetTransport;
struct ToriRS_Network;
struct ToriRS_CmdBus;

struct NetTransportVTable
{
    void (*poll)(struct NetTransport* t, struct ToriRS_Network* net, struct ToriRS_CmdBus* bus);
    void (*free_)(struct NetTransport* t);
};

struct NetTransport
{
    struct NetTransportVTable const* vtable;
};

/** Create a transport for the given enum NetTransportKind; default_port applies
 * when the CONNECT target carries no ":port". `rev_name` is the client's
 * protocol name ("osrs239", ...): the embed transport hands it to the
 * in-process server so both ends always speak the same wire; the socket
 * transports ignore it (the remote host chose its own). Returns NULL on
 * unknown kind. */
struct NetTransport*
NetTransport_New(int kind, int default_port, char const* rev_name);

/** One poll step: drain net->out (connect/send), pump the connection, and push
 * received/de-framed bytes + status changes onto the bus. */
static inline void
NetTransport_Poll(struct NetTransport* t, struct ToriRS_Network* net, struct ToriRS_CmdBus* bus)
{
    t->vtable->poll(t, net, bus);
}

static inline void
NetTransport_Free(struct NetTransport* t)
{
    if( t )
        t->vtable->free_(t);
}

/* Explicit test clock; only works for the in-process transport. */
struct ToriRSServerEmbed;
struct ToriRSServerEmbed* NetTransport_TestClock(struct NetTransport* t, unsigned long long now);

/* The unlocked clock (App_SpeedUnlock): `cycles` is every client logic cycle
 * run so far, handed over each frame before the poll. The in-process server
 * then runs on cycles x 20 ms instead of the wall clock, so its 600 ms tick is
 * exactly 30 client cycles however fast frames come. A test clock, when one is
 * handed over, still wins: its driver owns the world's time. Only the embed
 * transport has a server whose clock is this client's; `t` must be one. */
void NetTransport_CycleClock(struct NetTransport* t, unsigned long long cycles);

#endif
