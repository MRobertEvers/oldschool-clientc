/*
 * Shop definitions. See mock230_shop.h.
 */

#include "mock230_shop.h"

#include "mock230.h"
#include "mock230_container.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    /* One row per shared-shop-shaped inv the tree can name. Generous over the
     * catalogued roster (docs/SHOPS_PLAN.md §1.3: 593 distinct shops) because
     * overflow here is a boot-time error, not a runtime one — cheaper to size
     * past the need than to have a content author discover the ceiling. */
    MOCK230_SHOP_DEF_MAX = 700
};

static struct Mock230ShopDef g_shop_defs[MOCK230_SHOP_DEF_MAX];
static int g_shop_def_count = 0;

void
mock230_shop_reset(void)
{
    memset(g_shop_defs, 0, sizeof(g_shop_defs));
    g_shop_def_count = 0;
}

static struct Mock230ShopDef*
find_def(int32_t inv_id)
{
    for( int i = 0; i < g_shop_def_count; i++ )
        if( g_shop_defs[i].inv_id == inv_id )
            return &g_shop_defs[i];
    return NULL;
}

struct Mock230ShopDef*
mock230_shop_def_begin(int32_t inv_id)
{
    struct Mock230ShopDef* existing = find_def(inv_id);

    if( existing )
        return existing;
    if( g_shop_def_count >= MOCK230_SHOP_DEF_MAX )
    {
        fprintf(stderr, "mock230: %d shop definitions already parsed; inv %d dropped\n",
                MOCK230_SHOP_DEF_MAX, (int)inv_id);
        return NULL;
    }
    struct Mock230ShopDef* def = &g_shop_defs[g_shop_def_count++];

    memset(def, 0, sizeof(*def));
    def->inv_id = inv_id;
    return def;
}

int
mock230_shop_def_add_stock(
    struct Mock230ShopDef* def,
    int32_t obj_id,
    int32_t baseline,
    int32_t rate)
{
    if( !def )
        return 0;
    if( def->stock_count >= MOCK230_SHOP_STOCK_MAX )
        return 0;
    def->stock[def->stock_count].obj_id = obj_id;
    def->stock[def->stock_count].baseline = baseline;
    def->stock[def->stock_count].rate = rate;
    def->stock_count++;
    return 1;
}

const struct Mock230ShopDef*
mock230_shop_def(int32_t inv_id)
{
    return find_def(inv_id);
}

int
mock230_shop_is_shared(int32_t inv_id)
{
    const struct Mock230ShopDef* def = find_def(inv_id);

    return def && def->shared;
}

int
mock230_shop_stockbase(
    int32_t inv_id,
    int32_t obj_id)
{
    const struct Mock230ShopDef* def = find_def(inv_id);

    if( !def )
        return -1;
    for( int i = 0; i < def->stock_count; i++ )
        if( def->stock[i].obj_id == obj_id )
            return def->stock[i].baseline;
    return -1;
}

int
mock230_shop_allstock(int32_t inv_id)
{
    const struct Mock230ShopDef* def = find_def(inv_id);

    return def ? def->allstock : 0;
}

int
mock230_shop_def_count(void)
{
    return g_shop_def_count;
}

void
mock230_shop_seed(struct Mock230Server* srv)
{
    int seeded = 0;

    for( int i = 0; i < g_shop_def_count; i++ )
    {
        const struct Mock230ShopDef* def = &g_shop_defs[i];
        struct Mock230Container* row;

        if( !def->shared )
            continue;
        /* NULL player: mock230_container_resolve ignores it for a WORLD-scope
         * inv (mock230_container_scope now answers WORLD for these — see
         * load_inv_config in mock230_content.c). */
        row = mock230_container_resolve(srv, NULL, def->inv_id);
        if( !row )
        {
            fprintf(stderr,
                    "mock230: shop inv %d has a definition but no cache size; "
                    "not seeded\n",
                    (int)def->inv_id);
            continue;
        }
        for( int s = 0; s < def->stock_count; s++ )
            mock230_container_set(row, s, def->stock[s].obj_id, def->stock[s].baseline);
        /* Freshly seeded to its own baseline: nothing has changed from the
         * player's point of view yet, and marking it dirty here would just
         * cost the first bind's own full update a duplicate send. */
        mock230_container_clean(row);
        seeded++;
    }
    if( seeded )
        fprintf(stderr, "mock230: seeded %d shared shop(s) from baseline stock\n", seeded);
}

/* LostCity's World.ts cleanup-phase rule (docs/SHOPS_PLAN.md §3.4), ported
 * literally: a baseline slot nudges one unit toward its baseline every `rate`
 * ticks; a non-baseline slot in an `allstock=yes` shop decays by one every
 * `MOCK230_SHOP_ALLSTOCK_RATE` (100 = one minute at 600ms/tick) ticks. */
enum
{
    MOCK230_SHOP_ALLSTOCK_RATE = 100
};

void
mock230_shop_restock_tick(
    struct Mock230Server* srv,
    int tick)
{
    for( int i = 0; i < g_shop_def_count; i++ )
    {
        const struct Mock230ShopDef* def = &g_shop_defs[i];
        struct Mock230Container* row;

        if( !def->shared || !def->restock )
            continue;
        row = mock230_container_resolve(srv, NULL, def->inv_id);
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
                    mock230_container_set(row, slot, obj_id, count + 1);
                else if( count > baseline && tick % rate == 0 )
                    mock230_container_set(row, slot, obj_id, count - 1);
            }
            else if( def->allstock && tick % MOCK230_SHOP_ALLSTOCK_RATE == 0 && count > 0 )
            {
                mock230_container_set(row, slot, obj_id, count - 1);
            }
        }
    }
}
