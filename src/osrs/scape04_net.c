#ifndef _2004SCAPE_NET_C
#define _2004SCAPE_NET_C

#include "scape04_net.h"

#include "rsbuf.h"

static void
decode_varp(uint8_t* buffer, int buffer_size, int* varp)
{
    int varp_index = 0;
    int varp_value = 0;
    int varp_shift = 0;

    struct RSBuffer packet;
    rsbuf_init(&packet, buffer, buffer_size);

    int varp = rsbuf_g1(&packet);
    int value = rsbuf_g1b(&packet);

    // TODO: VarP Message
}

void
scape04_net_decode_packet(
    uint8_t* buffer, int buffer_size, enum Scape04PacketType packet_type, int packet_size)
{
    switch( packet_type )
    {
    case PACKET_TYPE_LOGIN_REQUEST:
        break;
    case PACKET_TYPE_LOGIN_RESPONSE:
        break;
    }
}

#endif