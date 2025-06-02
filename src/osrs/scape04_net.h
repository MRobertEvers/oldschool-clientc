#ifndef _2004SCAPE_NET_H
#define _2004SCAPE_NET_H

#include <stdint.h>

enum Scape04PacketType
{
    PACKET_TYPE_LOGIN_REQUEST = 14,
    PACKET_TYPE_LOGIN_RESPONSE = 15,
    PACKET_TYPE_CHARACTER_SELECT = 16,
    PACKET_TYPE_CHARACTER_CREATION = 17,
    PACKET_TYPE_CHARACTER_DELETION = 18,
};

void scape04_net_decode_packet();

#endif