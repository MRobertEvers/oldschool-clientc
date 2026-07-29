#include "net_transport.h"

#include "net/rev/gameproto_revisions.h" /* enum NetTransportKind */
#include "platform_socket.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct NetTransport*
NetTransport_NewWs(int default_port); /* net_transport_ws.c */

struct NetTransport*
NetTransport_NewEmbed(int default_port); /* net_transport_embed.c */

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
    /* Not remapped for the web below: an embedded server has no socket to be
     * wrong about, and works in a browser exactly as it does natively. */
    if( kind == NET_TRANSPORT_EMBED )
        return NetTransport_NewEmbed(default_port);

#if defined(TORIRS_PLATFORM_WEB)
    /*
     * This host has no raw socket to put a WebSocket on top of: emscripten's
     * connect() *is* a WebSocket, and the browser does the handshake and the
     * framing. Stacking our own client on that would send an HTTP upgrade as
     * the first frame's payload and hang waiting for a 101 that the server
     * already answered to someone else. The plain transport is the right one
     * here for every rev — one send is one binary frame, which is exactly what
     * a WebSocket server expects.
     */
    if( kind == NET_TRANSPORT_WS )
        kind = NET_TRANSPORT_TCP;
#endif

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
