#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_GE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_GE_H

#include <stdint.h>

/*
 * The Grand Exchange offer book: every player's eight offer slots, matched
 * against each other's (players only: no simulated market), kept across logouts
 * and restarts.
 *
 * Like the friend service (torirs_server_friends.h) it is keyed by base-37 name,
 * never by player slot, because a fill has to reach a seller who is not logged
 * in; and it is process-scoped, a file-static, for the same reason. It writes no
 * packet and no message: content asks it what an offer is and paints the
 * interface itself (interface_grandexchange/scripts/ge_offers.rs2), and
 * `ToriRSServer_GeTakeChanged` hands the world the online players whose offers
 * another player's offer has just filled.
 *
 * The rules are the matching engine's (ge_matching.rs2, ported from
 * RSPS-NEAR-REALITY's GrandExchangeOfferExecutor): a buyer takes any seller at
 * or below their price, a seller any buyer at or above; a trade goes at the
 * seller's price and the buyer is refunded the difference; a buyer is filled
 * from the cheapest seller first, a seller by the highest buyer first, the
 * older offer first on a tie; nobody matches their own offers.
 *
 * Proceeds wait in the offer until the owner collects them: `pend_items` of
 * the offer's item (a buy's purchases, a sell's unsold remainder after an
 * abort) and `pend_coins` (a sale's takings, a buy's refunds and unspent
 * coins after an abort). An offer that is finished and fully collected is
 * removed, which empties the slot.
 */

enum
{
    TORIRSSERVER_GE_SLOTS = 8,

    TORIRSSERVER_GE_EMPTY = 0,
    TORIRSSERVER_GE_ACTIVE = 1,
    TORIRSSERVER_GE_COMPLETE = 2,
    TORIRSSERVER_GE_ABORTED = 3,
};

/* `ge_offer_get`'s field numbers. */
enum
{
    TORIRSSERVER_GE_FIELD_STATE = 0,
    TORIRSSERVER_GE_FIELD_SELL = 1,
    TORIRSSERVER_GE_FIELD_OBJ = 2,
    TORIRSSERVER_GE_FIELD_PRICE = 3,
    TORIRSSERVER_GE_FIELD_COUNT = 4,
    TORIRSSERVER_GE_FIELD_DONE = 5,
    TORIRSSERVER_GE_FIELD_GOLD = 6,
    TORIRSSERVER_GE_FIELD_PEND_ITEMS = 7,
    TORIRSSERVER_GE_FIELD_PEND_COINS = 8,
};

/** Place an offer in `owner`'s empty `slot` and match it at once. 1 when
 *  placed; 0 for an occupied or invalid slot or a nonsensical offer. */
int ToriRSServer_GePlace(int64_t owner, int slot, int sell, int obj, int price, int count);

/** Abort an active offer: its unfilled part comes back as pending items (a
 *  sell) or coins (a buy). 1 when it was active. */
int ToriRSServer_GeAbort(int64_t owner, int slot);

/** One field of `owner`'s `slot` (TORIRSSERVER_GE_FIELD_*); 0 for an empty
 *  slot. Gold and coins saturate at INT32_MAX. */
int32_t ToriRSServer_GeGet(int64_t owner, int slot, int field);

/** Take `items` and `coins` off the slot's pending proceeds (clamped); a
 *  finished offer with nothing left pending is removed. */
void ToriRSServer_GeCollect(int64_t owner, int slot, int items, int coins);

/**
 * The owners whose offers changed in the last match other than the placer's
 * own, and the slot of each: fills `owners`/`slots` up to `cap`, returns the
 * count, and clears the list. The world tells those who are online.
 */
int ToriRSServer_GeTakeChanged(int64_t* owners, int* slots, int cap);

/** Drop everything held in memory (selftests). The file is not touched. */
void ToriRSServer_GeReset(void);

#endif
