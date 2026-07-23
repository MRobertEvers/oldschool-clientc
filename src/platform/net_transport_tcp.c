#include "net_transport.h"

#include "net/rev/gameproto_revisions.h" /* enum NetTransportKind */
#include "platform_socket.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct NetTransport*
NetTransport_NewWs(int default_port); /* net_transport_ws.c */

/* Raw-TCP transport: a thin adapter delegating to the battle-tested
 * PlatformSocket (unchanged), so the classic lc254 path is byte-for-byte the
 * same as before the transport seam existed. */

struct NetTransportTcp
{
    struct NetTransport base;
    struct PlatformSocket* sock;
};

static void
tcp_poll(struct NetTransport* t, struct ToriRS_Network* net, struct ToriRS_CmdBus* bus)
{
    struct NetTransportTcp* self = (struct NetTransportTcp*)t;
    PlatformSocket_Poll(self->sock, net, bus);
}

static void
tcp_free(struct NetTransport* t)
{
    struct NetTransportTcp* self = (struct NetTransportTcp*)t;
    PlatformSocket_Free(self->sock);
    free(self);
}

static struct NetTransportVTable const k_tcp_vtable = {
    .poll = tcp_poll,
    .free_ = tcp_free,
};

struct NetTransport*
NetTransport_NewTcp(int default_port)
{
    struct NetTransportTcp* self = calloc(1, sizeof(*self));
    assert(self);
    self->base.vtable = &k_tcp_vtable;
    self->sock = PlatformSocket_New(default_port);
    assert(self->sock);
    return &self->base;
}

struct NetTransport*
NetTransport_New(int kind, int default_port)
{
    switch( kind )
    {
    case NET_TRANSPORT_TCP:
        return NetTransport_NewTcp(default_port);
    case NET_TRANSPORT_WS:
        return NetTransport_NewWs(default_port);
    default:
        fprintf(stderr, "NetTransport_New: unknown transport kind %d\n", kind);
        return NULL;
    }
}
