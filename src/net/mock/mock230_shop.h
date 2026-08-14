#ifndef SRC_NET_MOCK_MOCK230_SHOP_H
#define SRC_NET_MOCK_MOCK230_SHOP_H

/*
 * Shop definitions: the server-only half of the inv namespace `fields/inv.ini`
 * explicitly reserves (`scope=`, `restock=`, `stockN=`, `allstock=`). Split out
 * of mock230_container.c the way mock230_bank.h is split out of the world file:
 * this is a definition table plus a boot-time seed plus a tick-time nudge, and
 * the only thing it shares with the container registry is the row it seeds.
 *
 * See docs/SHOPS_PLAN.md §3.1/§3.4.
 */

#include <stdint.h>

struct Mock230Server;

enum
{
    /* The largest wiki-catalogued shop stock table that fits its cache inv's
     * slot count is well under this (docs/SHOPS_PLAN.md §2.4); headroom over
     * the general stores' 40-slot cache size. */
    MOCK230_SHOP_STOCK_MAX = 64
};

struct Mock230ShopStock
{
    int32_t obj_id;
    int32_t baseline; /* target count a restock walks toward */
    int32_t rate;     /* ticks between +-1 nudges toward baseline */
};

struct Mock230ShopDef
{
    int32_t inv_id;
    uint8_t shared;   /* scope=shared: a world container, not per-player */
    uint8_t restock;  /* baseline items tick back toward `baseline` */
    uint8_t allstock; /* non-baseline slots decay to 0, one per minute */
    int stock_count;
    struct Mock230ShopStock stock[MOCK230_SHOP_STOCK_MAX];
};

/** Reset the definition table. Called once at content-load start. */
void
mock230_shop_reset(void);

/** Begin (or resume, `[section]` reopened) authoring `inv_id`'s definition.
 *  Returns the row to write into, creating it if this is the first mention. */
struct Mock230ShopDef*
mock230_shop_def_begin(int32_t inv_id);

/** Append one `stockN=obj,count,rate` entry. Returns 0 (and logs nothing —
 *  the caller has the file:line) if the table is full. */
int
mock230_shop_def_add_stock(
    struct Mock230ShopDef* def,
    int32_t obj_id,
    int32_t baseline,
    int32_t rate);

/** NULL if `inv_id` names no authored shop definition. */
const struct Mock230ShopDef*
mock230_shop_def(int32_t inv_id);

/** `mock230_container_scope`'s answer for a shop: shared -> WORLD. */
int
mock230_shop_is_shared(int32_t inv_id);

/** `SS_OP_INV_STOCKBASE` — baseline count for `obj_id` in `inv_id`'s
 *  definition, or -1 when the obj has no declared baseline (a non-baseline
 *  slot in an `allstock=yes` general store). */
int
mock230_shop_stockbase(
    int32_t inv_id,
    int32_t obj_id);

/** `SS_OP_INV_ALLSTOCK`. */
int
mock230_shop_allstock(int32_t inv_id);

/** How many distinct shop definitions were parsed, for the boot log. */
int
mock230_shop_def_count(void);

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
mock230_shop_seed(struct Mock230Server* srv);

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
mock230_shop_restock_tick(
    struct Mock230Server* srv,
    int tick);

#endif
