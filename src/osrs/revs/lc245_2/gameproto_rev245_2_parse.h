#ifndef GAMEPROTO_H
#define GAMEPROTO_H

#include "osrs/game.h"
#include "osrs/gameproto_revisions.h"
#include "osrs/revs/lc245_2/gameproto_rev245_2_packets.h"

int
gameproto_rev245_2_parse(
    int packet_type,
    uint8_t* data,
    int data_size,
    struct RevPacket_LC245_2* packet);

/** Parse one inbound game packet and enqueue on `game->revision` (LC245_2). */
int
gameproto_rev245_2_parse_and_enqueue(
    struct GGame* game,
    int opcode,
    uint8_t* data,
    int n);

/** Free heap fields inside `item->packet` (safe after exec; IF_SETTEXT pointer nulled by exec). */
void
gameproto_free_lc245_2_item(struct RevPacket_LC245_2_Item* item);

#endif