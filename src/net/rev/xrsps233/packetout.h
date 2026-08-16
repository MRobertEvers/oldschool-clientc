#ifndef SRC_NET_REV_XRSPS233_PACKETOUT_H
#define SRC_NET_REV_XRSPS233_PACKETOUT_H

/*
 * xrsps client->server opcodes (the active 180-255 set in
 * xrsps-typescript/src/shared/packets/ClientPacketId.ts). Phase 1: the login
 * driver builds its own hello/login/handshake frames, so packetout_code maps
 * nothing yet (returns -1). Phase 5 wires the outbound serializer (widget_action
 * etc.) through here.
 */

static inline int
packetout_code_xrsps233(int pkt_out_name)
{
    (void)pkt_out_name;
    return -1;
}

#endif
