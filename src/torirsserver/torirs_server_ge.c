#include "torirs_server_ge.h"

#include "torirs_server_savefile.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * See torirs_server_ge.h for what this is and the rules it applies. The store is
 * a flat array of offers, one per (owner, slot) in use, grown on demand; the
 * whole book is written to <saves>/grandexchange.tsv after every change and
 * read back the first time anything asks.
 */

struct GeOffer
{
    int64_t owner;
    int slot;
    int state;
    int sell;
    int obj;
    int price;
    int count;
    int done;
    int64_t gold;
    int pend_items;
    int64_t pend_coins;
    /* Placement order: the older offer wins a tie on price. */
    uint64_t seq;
};

enum
{
    GE_CHANGED_MAX = 64,
};

static struct GeOffer* g_offers;
static int g_count;
static int g_cap;
static uint64_t g_seq;
static int g_loaded;

static int64_t g_changed_owner[GE_CHANGED_MAX];
static int g_changed_slot[GE_CHANGED_MAX];
static int g_changed_count;

static void
ge_path(char* out, size_t cap)
{
    snprintf(out, cap, "%s/grandexchange.tsv", ToriRSServer_SaveDir());
}

static struct GeOffer*
ge_append(void)
{
    if( g_count == g_cap )
    {
        int cap = g_cap ? g_cap * 2 : 64;
        struct GeOffer* grown = realloc(g_offers, (size_t)cap * sizeof(*grown));

        assert(grown);
        g_offers = grown;
        g_cap = cap;
    }
    memset(&g_offers[g_count], 0, sizeof(g_offers[g_count]));
    return &g_offers[g_count++];
}

static void
ge_load(void)
{
    char path[1024];
    char line[512];
    FILE* f;

    if( g_loaded )
        return;
    g_loaded = 1;
    ge_path(path, sizeof(path));
    f = fopen(path, "r");
    if( !f )
        return;
    while( fgets(line, sizeof(line), f) )
    {
        struct GeOffer o;
        long long owner, gold, pend_coins;
        unsigned long long seq;

        memset(&o, 0, sizeof(o));
        if( line[0] == '#' )
            continue;
        if( sscanf(line, "%lld %d %d %d %d %d %d %d %lld %d %lld %llu", &owner, &o.slot,
                   &o.state, &o.sell, &o.obj, &o.price, &o.count, &o.done, &gold,
                   &o.pend_items, &pend_coins, &seq) != 12 )
            continue;
        o.owner = owner;
        o.gold = gold;
        o.pend_coins = pend_coins;
        o.seq = seq;
        if( o.seq > g_seq )
            g_seq = o.seq;
        *ge_append() = o;
    }
    fclose(f);
}

static void
ge_save(void)
{
    char path[1024];
    char tmp[1040];
    FILE* f;

    ge_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "w");
    if( !f )
        return;
    fprintf(f, "# owner37 slot state sell obj price count done gold pend_items pend_coins seq\n");
    for( int i = 0; i < g_count; i++ )
    {
        const struct GeOffer* o = &g_offers[i];

        fprintf(f, "%lld %d %d %d %d %d %d %d %lld %d %lld %llu\n", (long long)o->owner,
                o->slot, o->state, o->sell, o->obj, o->price, o->count, o->done,
                (long long)o->gold, o->pend_items, (long long)o->pend_coins,
                (unsigned long long)o->seq);
    }
    fclose(f);
    rename(tmp, path);
}

static struct GeOffer*
ge_find(int64_t owner, int slot)
{
    ge_load();
    for( int i = 0; i < g_count; i++ )
    {
        if( g_offers[i].owner == owner && g_offers[i].slot == slot )
            return &g_offers[i];
    }
    return NULL;
}

static void
ge_remove(struct GeOffer* o)
{
    int i = (int)(o - g_offers);

    g_offers[i] = g_offers[g_count - 1];
    g_count--;
}

static void
ge_mark_changed(int64_t owner, int slot)
{
    for( int i = 0; i < g_changed_count; i++ )
    {
        if( g_changed_owner[i] == owner && g_changed_slot[i] == slot )
            return;
    }
    if( g_changed_count < GE_CHANGED_MAX )
    {
        g_changed_owner[g_changed_count] = owner;
        g_changed_slot[g_changed_count] = slot;
        g_changed_count++;
    }
}

/* Is `them` a candidate for `me`: the other side, the same item, active, not
 * mine, and at a compatible price (the seller's price at or under the
 * buyer's). */
static int
ge_compatible(const struct GeOffer* me, const struct GeOffer* them)
{
    if( them->state != TORIRSSERVER_GE_ACTIVE || them->sell == me->sell ||
        them->obj != me->obj || them->owner == me->owner || them->done >= them->count )
        return 0;
    if( me->sell )
        return them->price >= me->price;
    return them->price <= me->price;
}

/* The better of two candidates for `me`: the cheapest seller for a buyer, the
 * highest buyer for a seller, the older on a tie. */
static int
ge_better(const struct GeOffer* me, const struct GeOffer* a, const struct GeOffer* b)
{
    if( a->price != b->price )
        return me->sell ? a->price > b->price : a->price < b->price;
    return a->seq < b->seq;
}

static void
ge_fill(struct GeOffer* buy, struct GeOffer* sell, int qty)
{
    int64_t paid = (int64_t)qty * sell->price;

    buy->done += qty;
    buy->gold += paid;
    buy->pend_items += qty;
    buy->pend_coins += (int64_t)qty * buy->price - paid;
    sell->done += qty;
    sell->gold += paid;
    sell->pend_coins += paid;
    if( buy->done >= buy->count )
        buy->state = TORIRSSERVER_GE_COMPLETE;
    if( sell->done >= sell->count )
        sell->state = TORIRSSERVER_GE_COMPLETE;
}

static void
ge_match(struct GeOffer* me)
{
    while( me->state == TORIRSSERVER_GE_ACTIVE && me->done < me->count )
    {
        struct GeOffer* best = NULL;
        int qty;

        for( int i = 0; i < g_count; i++ )
        {
            struct GeOffer* them = &g_offers[i];

            if( them == me || !ge_compatible(me, them) )
                continue;
            if( !best || ge_better(me, them, best) )
                best = them;
        }
        if( !best )
            return;
        qty = me->count - me->done;
        if( best->count - best->done < qty )
            qty = best->count - best->done;
        if( me->sell )
            ge_fill(best, me, qty);
        else
            ge_fill(me, best, qty);
        ge_mark_changed(best->owner, best->slot);
    }
}

int
ToriRSServer_GePlace(int64_t owner, int slot, int sell, int obj, int price, int count)
{
    struct GeOffer* o;

    assert(slot >= 0);
    assert(slot < TORIRSSERVER_GE_SLOTS);
    assert(obj >= 0);
    assert(price >= 1);
    assert(count >= 1);
    if( ge_find(owner, slot) )
        return 0;
    o = ge_append();
    o->owner = owner;
    o->slot = slot;
    o->state = TORIRSSERVER_GE_ACTIVE;
    o->sell = sell ? 1 : 0;
    o->obj = obj;
    o->price = price;
    o->count = count;
    o->seq = ++g_seq;
    /* The match only fills existing offers, so `o` does not move under it. */
    ge_match(o);
    ge_save();
    return 1;
}

int
ToriRSServer_GeAbort(int64_t owner, int slot)
{
    struct GeOffer* o;
    int left;

    assert(slot >= 0);
    assert(slot < TORIRSSERVER_GE_SLOTS);
    o = ge_find(owner, slot);

    if( !o || o->state != TORIRSSERVER_GE_ACTIVE )
        return 0;
    left = o->count - o->done;
    if( o->sell )
        o->pend_items += left;
    else
        o->pend_coins += (int64_t)left * o->price;
    o->state = TORIRSSERVER_GE_ABORTED;
    ge_save();
    return 1;
}

static int32_t
ge_saturate(int64_t v)
{
    if( v > INT32_MAX )
        return INT32_MAX;
    if( v < 0 )
        return 0;
    return (int32_t)v;
}

int32_t
ToriRSServer_GeGet(int64_t owner, int slot, int field)
{
    const struct GeOffer* o;

    assert(slot >= 0);
    assert(slot < TORIRSSERVER_GE_SLOTS);
    o = ge_find(owner, slot);
    if( !o )
        return 0;
    switch( field )
    {
    case TORIRSSERVER_GE_FIELD_STATE:
        return o->state;
    case TORIRSSERVER_GE_FIELD_SELL:
        return o->sell;
    case TORIRSSERVER_GE_FIELD_OBJ:
        return o->obj;
    case TORIRSSERVER_GE_FIELD_PRICE:
        return o->price;
    case TORIRSSERVER_GE_FIELD_COUNT:
        return o->count;
    case TORIRSSERVER_GE_FIELD_DONE:
        return o->done;
    case TORIRSSERVER_GE_FIELD_GOLD:
        return ge_saturate(o->gold);
    case TORIRSSERVER_GE_FIELD_PEND_ITEMS:
        return o->pend_items;
    case TORIRSSERVER_GE_FIELD_PEND_COINS:
        return ge_saturate(o->pend_coins);
    default:
        assert(!"ge_offer_get: no such field");
        return 0;
    }
}

void
ToriRSServer_GeCollect(int64_t owner, int slot, int items, int coins)
{
    struct GeOffer* o;

    assert(slot >= 0);
    assert(slot < TORIRSSERVER_GE_SLOTS);
    assert(items >= 0);
    assert(coins >= 0);
    o = ge_find(owner, slot);
    if( !o )
        return;
    if( items > o->pend_items )
        items = o->pend_items;
    if( coins > o->pend_coins )
        coins = (int)ge_saturate(o->pend_coins);
    if( items > 0 )
        o->pend_items -= items;
    if( coins > 0 )
        o->pend_coins -= coins;
    if( o->state != TORIRSSERVER_GE_ACTIVE && o->pend_items == 0 && o->pend_coins == 0 )
        ge_remove(o);
    ge_save();
}

int
ToriRSServer_GeTakeChanged(int64_t* owners, int* slots, int cap)
{
    int n = g_changed_count < cap ? g_changed_count : cap;

    for( int i = 0; i < n; i++ )
    {
        owners[i] = g_changed_owner[i];
        slots[i] = g_changed_slot[i];
    }
    g_changed_count = 0;
    return n;
}

void
ToriRSServer_GeReset(void)
{
    free(g_offers);
    g_offers = NULL;
    g_count = 0;
    g_cap = 0;
    g_seq = 0;
    g_loaded = 0;
    g_changed_count = 0;
}
