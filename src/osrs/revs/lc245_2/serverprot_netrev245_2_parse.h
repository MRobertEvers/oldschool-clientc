#ifndef SERVERPROT_NETREV245_2_PARSE_H
#define SERVERPROT_NETREV245_2_PARSE_H

/* serverprot_netrev245_2_parse — inbound opcode switch + deserializer for LC245_2.
 *
 * Translates raw inbound packet bytes (opcode + payload) into typed
 * RevServerProtPacket structs and enqueues them on the revision's pending queue.
 *
 * Zero GGame state mutation here; that happens in gamenet_rev245_2_exec.
 * Renamed from gameproto_rev245_2_parse.{c,h}.
 */

#include "osrs/game.h"
#include "osrs/gameproto_revisions.h"
#include "osrs/core/serverprot_packets.h"

int
serverprot_netrev245_2_parse(
    int packet_type,
    uint8_t* data,
    int data_size,
    struct RevServerProtPacket* packet);

/** Parse one inbound game packet and enqueue on `game->revision` (LC245_2). */
int
serverprot_netrev245_2_parse_and_enqueue(
    struct GGame* game,
    int opcode,
    uint8_t* data,
    int n);

/** Free heap fields inside `item->packet` (safe after exec; IF_SETTEXT pointer nulled by exec). */
void
gameproto_free_lc245_2_item(struct RevServerProtPacket_Item* item);

#endif
