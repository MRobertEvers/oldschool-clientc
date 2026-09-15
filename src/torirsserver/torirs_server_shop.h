#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_SHOP_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_SHOP_H

/*
 * Shop definitions: the server-only half of the inv namespace `fields/inv.ini`
 * explicitly reserves (`scope=`, `restock=`, `stockN=`, `allstock=`). Split out
 * of torirs_server_container.c the way torirs_server_bank.h is split out of the world file:
 * this is a definition table plus a boot-time seed plus a tick-time nudge, and
 * the only thing it shares with the container registry is the row it seeds.
 *
 * See docs/SHOPS_PLAN.md §3.1/§3.4.
 */

#include <stdint.h>

struct ToriRSServer;

enum
{
    /* The largest wiki-catalogued shop stock table that fits its cache inv's
     * slot count is well under this (docs/SHOPS_PLAN.md §2.4); headroom over
     * the general stores' 40-slot cache size. */
    TORIRSSERVER_SHOP_STOCK_MAX = 64
};

struct ToriRSServerShopStock
{
    int32_t obj_id;
    int32_t baseline; /* target count a restock walks toward */
    int32_t rate;     /* ticks between +-1 nudges toward baseline */
};

struct ToriRSServerShopDef
{
    int32_t inv_id;
    uint8_t shared;   /* scope=shared: a world container, not per-player */
    uint8_t restock;  /* baseline items tick back toward `baseline` */
    uint8_t allstock; /* non-baseline slots decay to 0, one per minute */
    uint8_t stackall; /* every obj stacks here, whatever its own record says */
    int32_t content_size; /* `size=` for an inv the cache doesn't size — see
                            * ToriRSServer_ShopContentSize */
    int stock_count;
    struct ToriRSServerShopStock stock[TORIRSSERVER_SHOP_STOCK_MAX];
};

/** Reset the definition table. Called once at content-load start. */
void
ToriRSServer_ShopReset(void);

/** Begin (or resume, `[section]` reopened) authoring `inv_id`'s definition.
 *  Returns the row to write into, creating it if this is the first mention. */
struct ToriRSServerShopDef*
ToriRSServer_ShopDefBegin(int32_t inv_id);

/** Append one `stockN=obj,count,rate` entry. Returns 0 (and logs nothing —
 *  the caller has the file:line) if the table is full. */
int
ToriRSServer_ShopDefAddStock(
    struct ToriRSServerShopDef* def,
    int32_t obj_id,
    int32_t baseline,
    int32_t rate);

/** NULL if `inv_id` names no authored shop definition. */
const struct ToriRSServerShopDef*
ToriRSServer_ShopDef(int32_t inv_id);

/** `ToriRSServer_ContainerScope`'s answer for a shop: shared -> WORLD. */
int
ToriRSServer_ShopIsShared(int32_t inv_id);

/**
 * Set from a shop `.inv`'s `size=N` — only meaningful when the inv has no
 * cache size of its own (a `pack/inv.alloc` id with nothing in config group
 * 5 behind it, e.g. a shop for a region this cache snapshot never packed).
 * `inv_config_key` in torirs_server_content.c is the sole writer; it checks
 * `ToriRSServer_BankInvSize` first and refuses to shadow a real cache fact.
 */
void
ToriRSServer_ShopDefSetSize(
    struct ToriRSServerShopDef* def,
    int32_t size);

/** `size=` for `inv_id` if one was declared and the inv has no cache size —
 *  0 otherwise. `ToriRSServer_ContainerResolve`'s fallback when
 *  `ToriRSServer_BankInvSize` comes back empty. */
int
ToriRSServer_ShopContentSize(int32_t inv_id);

/** `SS_OP_INV_STOCKBASE` — baseline count for `obj_id` in `inv_id`'s
 *  definition, or -1 when the obj has no declared baseline (a non-baseline
 *  slot in an `allstock=yes` general store). */
int
ToriRSServer_ShopStockbase(
    int32_t inv_id,
    int32_t obj_id);

/** `SS_OP_INV_ALLSTOCK`. */
int
ToriRSServer_ShopAllstock(int32_t inv_id);

/**
 * `stackall=yes` — every obj in this inv occupies one slot however many there
 * are, whatever the obj record says about stackability.
 *
 * This is the stack policy `ToriRSServer_ContainerAdd`'s own comment says is
 * missing (LostCity reads `stackType` out of its server-side `inv.dat`, and
 * this cache's inv config carries only size). It is not missing for shops: the
 * flag is in every generated `.inv` and was being parsed and thrown away.
 *
 * A shop needs it because its seed already assumes it — `ToriRSServer_ShopSeed`
 * writes `stock1=pot_empty,5,10` as ONE slot holding five pots, and pots are
 * not stackable — so without it the container disagrees with itself the first
 * time anything is added: selling a pot back to a store that already shows
 * "Pot 5" opened a *second* pot cell instead of making it six.
 */
int
ToriRSServer_ShopStackall(int32_t inv_id);

/**
 * Is `obj_id` one of `inv_id`'s baseline stock lines?
 *
 * `ToriRSServer_ShopStockbase` cannot answer this — a legitimate baseline of 0 and
 * "no such line" are both -1 there, and the general stores do declare
 * zero-baseline lines. This is the predicate `ToriRSServer_ContainerClearSlot`
 * needs: a baseline slot is emptied to a count of 0 rather than removed.
 */
int
ToriRSServer_ShopHasStockLine(
    int32_t inv_id,
    int32_t obj_id);

/** How many distinct shop definitions were parsed, for the boot log. */
int
ToriRSServer_ShopDefCount(void);

/**
 * Seed every shared shop's container from its baseline stock.
 *
 * Called once at boot, after content load and after the container registry
 * exists. Deliberately not persisted (docs/SHOPS_PLAN.md §3.3): a shared shop
 * has no save file, so a restart hands every shop back its baseline — the
 * same behaviour a 2004-era world reset had, and it removes durable
 * world-container state from this slice's critical path.
 */
void
ToriRSServer_ShopSeed(struct ToriRSServer* srv);

/**
 * One tick's worth of restock nudging, across every shared shop.
 *
 * Ported from LostCity's `World.ts` cleanup phase, not content: a baseline
 * slot moves one unit toward `baseline` every `rate` ticks; a non-baseline
 * slot in an `allstock=yes` shop decays by one every `INV_STOCKRATE` (100)
 * ticks. Marks the container dirty on any change so the client's own
 * `if_setoninvtransmit` repaints it with no further server push.
 */
void
ToriRSServer_ShopRestockTick(
    struct ToriRSServer* srv,
    int tick);

#endif
