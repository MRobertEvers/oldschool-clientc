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

/*
 * One row per inv. Every inv states its server fields (`scope`, `restock`,
 * `allstock`, `stackall`, `stock`), so every inv has a row, and what makes one
 * a shop is `scope=shared` — not, as it was while only shops stated anything,
 * whether a row exists. Grown on demand and found by id through `g_shop_index`
 * (row + 1, 0 for none), since a container asks on every write.
 */
static struct ToriRSServerShopDef* g_shop_defs;
static int g_shop_def_count;
static int g_shop_def_capacity;
static int* g_shop_index;
static int g_shop_index_capacity;

void
ToriRSServer_ShopReset(void)
{
    free(g_shop_defs);
    free(g_shop_index);
    g_shop_defs = NULL;
    g_shop_index = NULL;
    g_shop_def_count = 0;
    g_shop_def_capacity = 0;
    g_shop_index_capacity = 0;
}

static struct ToriRSServerShopDef*
find_def(int32_t inv_id)
{
    if( inv_id < 0 || inv_id >= g_shop_index_capacity || !g_shop_index[inv_id] )
        return NULL;
    return &g_shop_defs[g_shop_index[inv_id] - 1];
}

struct ToriRSServerShopDef*
ToriRSServer_ShopDefBegin(int32_t inv_id)
{
    struct ToriRSServerShopDef* existing = find_def(inv_id);
    struct ToriRSServerShopDef* def;

    assert(inv_id >= 0);
    if( existing )
        return existing;
    if( inv_id >= g_shop_index_capacity )
    {
        int capacity = g_shop_index_capacity ? g_shop_index_capacity : 1024;

        while( capacity <= inv_id )
            capacity *= 2;
        g_shop_index = (int*)realloc(g_shop_index, (size_t)capacity * sizeof(*g_shop_index));
        assert(g_shop_index);
        memset(g_shop_index + g_shop_index_capacity, 0,
               (size_t)(capacity - g_shop_index_capacity) * sizeof(*g_shop_index));
        g_shop_index_capacity = capacity;
    }
    if( g_shop_def_count == g_shop_def_capacity )
    {
        g_shop_def_capacity = g_shop_def_capacity ? g_shop_def_capacity * 2 : 256;
        g_shop_defs = (struct ToriRSServerShopDef*)realloc(
            g_shop_defs, (size_t)g_shop_def_capacity * sizeof(*g_shop_defs));
        assert(g_shop_defs);
    }
    def = &g_shop_defs[g_shop_def_count++];
    memset(def, 0, sizeof(*def));
    def->inv_id = inv_id;
    g_shop_index[inv_id] = g_shop_def_count;
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
