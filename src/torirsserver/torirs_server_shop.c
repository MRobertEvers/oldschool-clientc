/*
 * Shop definitions. See torirs_server_shop.h.
 */

#include "torirs_server_shop.h"
#include <assert.h>

#include "torirs_server.h"
#include "torirs_server_container.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    /* One row per shared-shop-shaped inv the tree can name. Generous over the
     * catalogued roster (docs/SHOPS_PLAN.md §1.3: 593 distinct shops) because
     * overflow here is a boot-time error, not a runtime one — cheaper to size
     * past the need than to have a content author discover the ceiling. */
    TORIRSSERVER_SHOP_DEF_MAX = 700
};

static struct ToriRSServerShopDef g_shop_defs[TORIRSSERVER_SHOP_DEF_MAX];
static int g_shop_def_count = 0;

void
ToriRSServer_ShopReset(void)
{
    memset(g_shop_defs, 0, sizeof(g_shop_defs));
    g_shop_def_count = 0;
}

static struct ToriRSServerShopDef*
find_def(int32_t inv_id)
{
    for( int i = 0; i < g_shop_def_count; i++ )
        if( g_shop_defs[i].inv_id == inv_id )
            return &g_shop_defs[i];
    return NULL;
}

struct ToriRSServerShopDef*
ToriRSServer_ShopDefBegin(int32_t inv_id)
{
    struct ToriRSServerShopDef* existing = find_def(inv_id);

    if( existing )
        return existing;
    if( g_shop_def_count >= TORIRSSERVER_SHOP_DEF_MAX )
    {
        fprintf(stderr, "torirsserver: %d shop definitions already parsed; inv %d dropped\n",
                TORIRSSERVER_SHOP_DEF_MAX, (int)inv_id);
        return NULL;
    }
    struct ToriRSServerShopDef* def = &g_shop_defs[g_shop_def_count++];

    memset(def, 0, sizeof(*def));
    def->inv_id = inv_id;
    return def;
}

int
ToriRSServer_ShopDefAddStock(
    struct ToriRSServerShopDef* def,
    int32_t obj_id,
    int32_t baseline,
    int32_t rate)
{
    assert(def);
    if( def->stock_count >= TORIRSSERVER_SHOP_STOCK_MAX )
        return 0;
    def->stock[def->stock_count].obj_id = obj_id;
    def->stock[def->stock_count].baseline = baseline;
    def->stock[def->stock_count].rate = rate;
    def->stock_count++;
    return 1;
}

const struct ToriRSServerShopDef*
ToriRSServer_ShopDef(int32_t inv_id)
{
    return find_def(inv_id);
}

int
ToriRSServer_ShopIsShared(int32_t inv_id)
{
    const struct ToriRSServerShopDef* def = find_def(inv_id);

    return def && def->shared;
}

void
ToriRSServer_ShopDefSetSize(
    struct ToriRSServerShopDef* def,
    int32_t size)
{
    assert(def);
    def->content_size = size;
}

int
ToriRSServer_ShopContentSize(int32_t inv_id)
{
    const struct ToriRSServerShopDef* def = find_def(inv_id);

    return def ? def->content_size : 0;
}

int
ToriRSServer_ShopStockbase(
    int32_t inv_id,
    int32_t obj_id)
{
    const struct ToriRSServerShopDef* def = find_def(inv_id);

    if( !def )
        return -1;
    for( int i = 0; i < def->stock_count; i++ )
        if( def->stock[i].obj_id == obj_id )
            return def->stock[i].baseline;
    return -1;
}

int
ToriRSServer_ShopAllstock(int32_t inv_id)
{
    const struct ToriRSServerShopDef* def = find_def(inv_id);

    return def ? def->allstock : 0;
}

int
ToriRSServer_ShopStackall(int32_t inv_id)
{
    const struct ToriRSServerShopDef* def = find_def(inv_id);

    return def ? def->stackall : 0;
}

int
ToriRSServer_ShopHasStockLine(
    int32_t inv_id,
    int32_t obj_id)
{
    const struct ToriRSServerShopDef* def = find_def(inv_id);

    if( !def )
        return 0;
    for( int i = 0; i < def->stock_count; i++ )
        if( def->stock[i].obj_id == obj_id )
            return 1;
    return 0;
}

int
ToriRSServer_ShopDefCount(void)
{
    return g_shop_def_count;
}

void
ToriRSServer_ShopSeed(struct ToriRSServer* srv)
{
    int seeded = 0;

    for( int i = 0; i < g_shop_def_count; i++ )
    {
        const struct ToriRSServerShopDef* def = &g_shop_defs[i];
        struct ToriRSServerContainer* row;

        if( !def->shared )
            continue;
        /* NULL player: ToriRSServer_ContainerResolve ignores it for a WORLD-scope
         * inv (ToriRSServer_ContainerScope now answers WORLD for these — see
         * load_inv_config in torirs_server_content.c). */
        row = ToriRSServer_ContainerResolve(srv, NULL, def->inv_id);
        if( !row )
        {
            fprintf(stderr,
                    "torirsserver: shop inv %d has a definition but no cache size; "
                    "not seeded\n",
                    (int)def->inv_id);
            continue;
        }
        for( int s = 0; s < def->stock_count; s++ )
        {
            ToriRSServer_ContainerSet(row, s, def->stock[s].obj_id, def->stock[s].baseline);
            if( def->inv_id == 2002 && getenv("TORIRSSERVER_SHOP_SEED_DEBUG") )
                fprintf(stderr, "SHOPSEED inv=2002 slots=%d slot=%d obj=%d baseline=%d row=(%d,%d)\n",
                        row->slots, s, def->stock[s].obj_id, def->stock[s].baseline,
                        row->items[s].obj_id, row->items[s].count);
        }
        /* Freshly seeded to its own baseline: nothing has changed from the
         * player's point of view yet, and marking it dirty here would just
         * cost the first bind's own full update a duplicate send. */
        ToriRSServer_ContainerClean(row);
        seeded++;
    }
    if( seeded )
        fprintf(stderr, "torirsserver: seeded %d shared shop(s) from baseline stock\n", seeded);
}

/* LostCity's World.ts cleanup-phase rule (docs/SHOPS_PLAN.md §3.4), ported
 * literally: a baseline slot nudges one unit toward its baseline every `rate`
 * ticks; a non-baseline slot in an `allstock=yes` shop decays by one every
 * `TORIRSSERVER_SHOP_ALLSTOCK_RATE` (100 = one minute at 600ms/tick) ticks. */
enum
{
    TORIRSSERVER_SHOP_ALLSTOCK_RATE = 100
};

void
ToriRSServer_ShopRestockTick(
    struct ToriRSServer* srv,
    int tick)
{
    for( int i = 0; i < g_shop_def_count; i++ )
    {
        const struct ToriRSServerShopDef* def = &g_shop_defs[i];
        struct ToriRSServerContainer* row;

        if( !def->shared || !def->restock )
            continue;
        row = ToriRSServer_ContainerResolve(srv, NULL, def->inv_id);
        if( !row )
            continue;

        for( int slot = 0; slot < row->slots; slot++ )
        {
            int obj_id = row->items[slot].obj_id;
            int count;
            int baseline = -1;
            int rate = 0;

            if( obj_id < 0 )
                continue;
            for( int s = 0; s < def->stock_count; s++ )
            {
                if( def->stock[s].obj_id == obj_id )
                {
                    baseline = def->stock[s].baseline;
                    rate = def->stock[s].rate;
                    break;
                }
            }
            count = row->items[slot].count;

            if( baseline >= 0 && rate > 0 )
            {
                if( count < baseline && tick % rate == 0 )
                    ToriRSServer_ContainerSet(row, slot, obj_id, count + 1);
                else if( count > baseline && tick % rate == 0 )
                    ToriRSServer_ContainerSet(row, slot, obj_id, count - 1);
            }
            else if( def->allstock && tick % TORIRSSERVER_SHOP_ALLSTOCK_RATE == 0 && count > 0 )
            {
                ToriRSServer_ContainerSet(row, slot, obj_id, count - 1);
            }
        }
    }
}
