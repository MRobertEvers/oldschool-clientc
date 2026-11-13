#ifndef SRC_REPLAY_REPLAY_CLASSIFY_H
#define SRC_REPLAY_REPLAY_CLASSIFY_H

/*
 * What a seek's chase does with each server packet (the replay scrubber
 * design, "Why three sources"): one row per GameProtoPktName.
 *
 *   WORLD      the world's state: scenes, zones, entity info. Withheld during
 *              a chase; the ledger's keyframe at the chase's end stands in for
 *              every one of them.
 *   SESSION    what only the client can interpret: varps, inventories, stats,
 *              interfaces, clientscripts, chat, camera, music. Executed, every
 *              one, in recorded order.
 *   TRANSIENT  one-shot effects: projectiles, spot and loc animations, sounds.
 *              Withheld during a chase; the pre-roll plays the ones that still
 *              show at the target.
 *   STREAM     the stream's own structure: the tick end, packet groups, the
 *              session's end. Executed as they are now. (The replay's own
 *              messages -- a chase's tile, a withheld chat line, the keyframe,
 *              a recorded dialog answer -- are control frames, and a recorded
 *              RECONNECT is the connection ending: neither has a name.)
 *
 * The table (replay_classify.c) states every name; ReplayClassify_Check
 * asserts none is missing, so a name appended to pktnames.h fails the first
 * seek (and test-replay-scrub) until it is given a class.
 */

#include "net/rev/pktnames.h"

enum ReplayPktClass
{
    /* 0 is "no row": the check refuses it. */
    REPLAY_PKT_UNCLASSIFIED = 0,
    REPLAY_PKT_WORLD,
    REPLAY_PKT_SESSION,
    REPLAY_PKT_TRANSIENT,
    REPLAY_PKT_STREAM,
};

/** Asserts every GameProtoPktName has a class. */
void
ReplayClassify_Check(void);

enum ReplayPktClass
ReplayClassify_Packet(enum GameProtoPktName name);

/** "world", "session", "transient", "stream". */
char const*
ReplayClassify_Name(enum ReplayPktClass cls);

#endif
