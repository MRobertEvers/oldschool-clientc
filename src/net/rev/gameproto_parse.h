#ifndef SRC_NET_REV_GAMEPROTO_PARSE_H
#define SRC_NET_REV_GAMEPROTO_PARSE_H

/*
 * Canonical-name -> tagged-struct payload parser, shared by every revision
 * (payload layouts are identical per name; only wire opcodes differ, and
 * those are resolved by the caller via rev->packetin_code before parsing).
 * The rev table is still needed here to map the sub-opcodes inside
 * UPDATE_ZONE_PARTIAL_ENCLOSED streams. Ported from v0/osrs/gameproto_parse.c
 * (buffer type swapped to RSCache_Buffer; exec/state code stays out).
 */

#include "gameproto_revisions.h"
#include "revpacket.h"

#include <stdint.h>

int
gameproto_parse(
    struct GameProtoRevTable const* rev,
    enum GameProtoPktName pkt_name,
    uint8_t* data,
    int data_size,
    struct RevPacket* packet);

/** Free heap fields inside a parsed packet (safe after exec). */
void
gameproto_free(struct RevPacket* packet);

#endif
