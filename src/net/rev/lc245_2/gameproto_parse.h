#ifndef SRC_NET_REV_LC245_2_GAMEPROTO_PARSE_H
#define SRC_NET_REV_LC245_2_GAMEPROTO_PARSE_H

/*
 * Opcode -> tagged-struct payload parser for the lc245_2 revision, ported
 * from v0/osrs/gameproto_parse.c (buffer type swapped to RSCache_Buffer;
 * exec/state code stays out — parsing only).
 */

#include "revpacket_lc245_2.h"

#include <stdint.h>

int
gameproto_parse_lc245_2(
    int packet_type,
    uint8_t* data,
    int data_size,
    struct RevPacket_LC245_2* packet);

/** Free heap fields inside a parsed packet (safe after exec). */
void
gameproto_free_lc245_2(struct RevPacket_LC245_2* packet);

#endif
