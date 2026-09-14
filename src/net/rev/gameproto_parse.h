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

/**
 * Can this packet's exec change interface or CS2-visible state?
 *
 * The client opens a visual server-tick transaction on the first such packet
 * and holds the framebuffer until the tick closes. SERVER_TICK_END is not a
 * universal reply fence -- immediate world feedback is also sent between
 * scheduled ticks, the map flag after MOVE_GAMECLICK being the common case --
 * so treating every inbound packet as a tick opener retains the frame until
 * the next 600 ms server cycle whether or not anything changed.
 *
 * Deliberately includes transmit sources as well as direct IF_* writes: a
 * later clientscript in the same tick must observe varp, inventory, stat and
 * social changes as part of the same UI transaction.
 */
int
gameproto_packet_may_mutate_ui(enum GameProtoPktName packet_type);

/**
 * Decode a REBUILD_WORLDENTITY raw grid against the target view's zone counts
 * (view size in tiles / 8 per axis — spawn-time state the wire omits, which
 * is why this cannot run in the parse arm). Fills out_zones with
 * PKT_MAP_REBUILD_ZONES ints at the same [level*13*13 + zx*13 + zz] stride
 * REBUILD_REGION uses; destination zones past the view's counts stay 0 =
 * void. Returns 1 on a clean, fully-consumed bitstream; 0 when it is short
 * or has trailing bytes — with the dimensions known, that means client and
 * server disagree about the view's size, a protocol violation the caller
 * stops on.
 */
int
PktRebuildWev_DecodeZones(
    struct PktRebuildWev const* p,
    int zones_x,
    int zones_z,
    int32_t* out_zones);

#endif
