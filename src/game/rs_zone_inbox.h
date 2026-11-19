#ifndef SRC_GAME_RS_ZONE_INBOX_H
#define SRC_GAME_RS_ZONE_INBOX_H

/*
 * The zone base the server last set, and the zone sub-packets that arrived
 * while the scene was still loading.
 *
 * Every UPDATE_ZONE_* header names an 8x8 zone (its south-west tile, in scene
 * tiles) and a plane; the sub-packets after it address tiles inside that zone.
 * The base is held here between the header and the packets that read it.
 *
 * A REBUILD here is an async task, and the zone packets the server sends
 * behind it can arrive before the new scene exists. Those are held, each with
 * the zone base, plane and world view as of its ARRIVAL, because the live
 * values move on before the scene is ready: a later header changes the base,
 * and the tick fence resets the view cursor. They are applied in arrival
 * order, and the count is re-read on every step of the drain, as the loop
 * this replaced did.
 *
 * The inbox holds RS_ZONE_INBOX_MAX packets. Past that a packet is refused,
 * and the caller decides what refusing it means.
 *
 * This does not know when the world is ready, which packets are zone
 * sub-packets, or how one is applied. The caller asks before holding, says
 * when to drain by calling Drain, and supplies the function that applies one
 * held packet, including any check that the view it names is still alive.
 */

#include "net/rev/revpacket.h"

#include <stdbool.h>

/** Zone sub-packets the inbox holds while the scene loads. */
#define RS_ZONE_INBOX_MAX 256

/** One held packet and where it applies, as of its arrival. */
struct RS_ZoneInboxEntry
{
    struct PktZoneSubPacket pkt;
    int base_x;
    int base_z;
    int level;
    int view;
};

struct RS_ZoneInbox
{
    /** Zone base for follows-mode zone packets (scene-local tiles). */
    int base_x;
    int base_z;
    /** Plane named by the latest UPDATE_ZONE_* header. A revision-239 header
     *  can name a plane other than the local player's. */
    int level;
    struct RS_ZoneInboxEntry held[RS_ZONE_INBOX_MAX];
    int held_count;
};

/** Base 0,0 on plane 0, nothing held. */
void
RS_ZoneInbox_Reset(struct RS_ZoneInbox* inbox);

/** A zone header arrived: the base and plane later sub-packets read. */
void
RS_ZoneInbox_SetBase(
    struct RS_ZoneInbox* inbox,
    int base_x,
    int base_z,
    int level);

int
RS_ZoneInbox_BaseX(struct RS_ZoneInbox const* inbox);

int
RS_ZoneInbox_BaseZ(struct RS_ZoneInbox const* inbox);

int
RS_ZoneInbox_Level(struct RS_ZoneInbox const* inbox);

/** Whether a Hold would be refused. */
bool
RS_ZoneInbox_IsFull(struct RS_ZoneInbox const* inbox);

/**
 * Hold one packet behind those already held, with where it applies. The
 * inbox must not be full: the caller asks IsFull first.
 */
void
RS_ZoneInbox_Hold(
    struct RS_ZoneInbox* inbox,
    struct PktZoneSubPacket const* pkt,
    int base_x,
    int base_z,
    int level,
    int view);

/** How many packets are held. */
int
RS_ZoneInbox_HeldCount(struct RS_ZoneInbox const* inbox);

/** Applies one held packet. `user` is what the caller passed to Drain. */
typedef void (*RS_ZoneInboxApplyFn)(
    void* user,
    struct RS_ZoneInboxEntry const* entry);

/**
 * Apply every held packet in arrival order, then hold none. The base is not
 * touched: each entry carries its own.
 */
void
RS_ZoneInbox_Drain(
    struct RS_ZoneInbox* inbox,
    RS_ZoneInboxApplyFn apply,
    void* user);

/** Drop every held packet unapplied. The base is not touched. */
void
RS_ZoneInbox_Discard(struct RS_ZoneInbox* inbox);

#endif /* SRC_GAME_RS_ZONE_INBOX_H */
