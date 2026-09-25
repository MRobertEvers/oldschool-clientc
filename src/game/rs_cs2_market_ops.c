/*
 * The Grand Exchange offer commands (3903-3913) and the trading-post commands
 * (3914-3926), popped by their catalogue signature, and the trading-post list
 * they read.
 *
 * Behaviour is the rev-239 Java client's (Statics.method5995). Where the client
 * throws -- an offer slot outside 0..7, a trading-post index outside the list,
 * any trading-post getter with no list -- the script aborts here, with a line
 * saying which command and why. The config-reading commands of the range
 * (3931, 3932, 3939) are in rs_cs2_host.c, beside the objtype loads they wait
 * on.
 */

#include "game/rs_cs2_host.h"

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2_opcode_meta.h"
#include "cs2vm2/cs2vm2.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
script_error(int opcode, char const* why)
{
    TORIRS_LOG("cs2: %s (opcode %d): %s\n", CS2_OpCode_String(opcode), opcode, why);
    return CS2VM_EXECNO_ERROR;
}

/* ---- the list ---------------------------------------------------------------- */

struct MarketReader
{
    uint8_t const* data;
    int length;
    int position;
    bool failed;
};

static int
g1(struct MarketReader* r)
{
    if( r->failed || r->position + 1 > r->length )
    {
        r->failed = true;
        return 0;
    }
    return r->data[r->position++];
}

static int
g2(struct MarketReader* r)
{
    int const hi = g1(r);
    return (hi << 8) | g1(r);
}

static int
g4(struct MarketReader* r)
{
    uint32_t const hi = (uint32_t)g2(r);
    uint32_t const lo = (uint32_t)g2(r);
    return (int)((hi << 16) | lo);
}

static int64_t
g8(struct MarketReader* r)
{
    uint64_t const hi = (uint32_t)g4(r);
    uint64_t const lo = (uint32_t)g4(r);
    return (int64_t)((hi << 32) | lo);
}

static void
gjstr(struct MarketReader* r, char* out, int cap)
{
    int written = 0;
    for( ;; )
    {
        int const c = g1(r);
        if( r->failed || c == 0 )
            break;
        if( written + 1 < cap )
            out[written++] = (char)c;
    }
    out[written] = '\0';
}

bool
RS_CS2Host_ApplyTradingPost(
    struct RS_CS2Host* host,
    uint8_t const* data,
    int length,
    int64_t now_ms)
{
    assert(host);
    assert(data || length == 0);
    struct RS_CS2TradingPost* const post = &host->trading_post;
    struct MarketReader r = { data, length, 0, false };

    bool const present = g1(&r) == 1;
    if( r.failed )
        return false;
    if( !present )
    {
        post->present = false;
        post->count = 0;
        return true;
    }

    int64_t const server_ms = g8(&r);
    int const obj = g2(&r);
    bool const sell = g1(&r) == 1;
    int const count = g2(&r);
    if( r.failed )
        return false;

    struct RS_CS2TradingPostOffer* const offers =
        count > 0 ? malloc((size_t)count * sizeof(offers[0])) : NULL;
    assert(count == 0 || offers);
    for( int i = 0; i < count; i++ )
    {
        struct RS_CS2TradingPostOffer* const offer = &offers[i];
        gjstr(&r, offer->name, sizeof(offer->name));
        gjstr(&r, offer->previous_name, sizeof(offer->previous_name));
        offer->world = g2(&r);
        offer->time_ms = g8(&r);
        offer->price = g4(&r);
        offer->count = g4(&r);
    }
    if( r.failed )
    {
        free(offers);
        return false;
    }

    free(post->offers);
    post->offers = offers;
    post->count = count;
    post->cap = count;
    post->present = true;
    post->obj = obj;
    post->sell = sell;
    post->clock_offset_ms = now_ms - server_ms;
    return true;
}

/* ---- sorting ------------------------------------------------------------------ */

enum TradingPostKey
{
    TRADING_POST_BY_NAME,
    TRADING_POST_BY_PRICE,
    TRADING_POST_BY_WORLD,
    TRADING_POST_BY_AGE,
    TRADING_POST_BY_COUNT,
};

struct TradingPostOrder
{
    enum TradingPostKey key;
    bool ascending;
    bool own_world_first;
    int own_world;
};

static int
compare_int(int64_t a, int64_t b)
{
    return a < b ? -1 : a > b ? 1 : 0;
}

/* The client's comparators: String.compareTo on the names (the unsigned bytes
 * here), signed compares on the numbers, and class81 for the world. */
static int
trading_post_compare(
    struct TradingPostOrder const* order,
    struct RS_CS2TradingPostOffer const* a,
    struct RS_CS2TradingPostOffer const* b)
{
    int result = 0;
    switch( order->key )
    {
    case TRADING_POST_BY_NAME:
    {
        unsigned char const* x = (unsigned char const*)a->name;
        unsigned char const* y = (unsigned char const*)b->name;
        while( *x && *x == *y )
        {
            x++;
            y++;
        }
        result = compare_int(*x, *y);
        break;
    }
    case TRADING_POST_BY_PRICE:
        result = compare_int(a->price, b->price);
        break;
    case TRADING_POST_BY_WORLD:
        if( a->world == b->world )
            result = 0;
        else if( order->own_world_first && a->world == order->own_world )
            result = -1;
        else if( order->own_world_first && b->world == order->own_world )
            result = 1;
        else
            result = a->world < b->world ? -1 : 1;
        break;
    case TRADING_POST_BY_AGE:
        result = compare_int(a->time_ms, b->time_ms);
        break;
    case TRADING_POST_BY_COUNT:
        result = compare_int(a->count, b->count);
        break;
    }
    /* Collections.reverseOrder negates the comparator; the sort stays stable. */
    return order->ascending ? result : -result;
}

/* Collections.sort is a stable merge sort, and the order two equal offers keep
 * is visible to the scripts, so this one is stable too. */
static void
trading_post_sort(
    struct RS_CS2TradingPost* post,
    struct TradingPostOrder const* order)
{
    int const count = post->count;
    if( count < 2 )
        return;
    struct RS_CS2TradingPostOffer* scratch = malloc((size_t)count * sizeof(scratch[0]));
    assert(scratch);
    struct RS_CS2TradingPostOffer* from = post->offers;
    struct RS_CS2TradingPostOffer* to = scratch;
    for( int width = 1; width < count; width *= 2 )
    {
        for( int left = 0; left < count; left += 2 * width )
        {
            int const middle = left + width < count ? left + width : count;
            int const right = left + 2 * width < count ? left + 2 * width : count;
            int i = left;
            int j = middle;
            int k = left;
            while( i < middle && j < right )
            {
                if( trading_post_compare(order, &from[j], &from[i]) < 0 )
                    to[k++] = from[j++];
                else
                    to[k++] = from[i++];
            }
            while( i < middle )
                to[k++] = from[i++];
            while( j < right )
                to[k++] = from[j++];
        }
        struct RS_CS2TradingPostOffer* const swap = from;
        from = to;
        to = swap;
    }
    if( from != post->offers )
        memcpy(post->offers, from, (size_t)count * sizeof(post->offers[0]));
    free(scratch);
}

/* ---- commands ----------------------------------------------------------------- */

static int
stockmarket_offer(
    struct RS_CS2Host* host,
    int opcode,
    int slot,
    struct RS_CS2StockmarketOffer const** out_offer)
{
    if( slot < 0 || slot >= RS_CS2_STOCKMARKET_SLOTS )
        return script_error(opcode, "offer slot outside 0..7");
    *out_offer = &host->stockmarket[slot];
    return CS2VM_EXECNO_OK;
}

static int
trading_post_offer(
    struct RS_CS2Host* host,
    int opcode,
    int index,
    struct RS_CS2TradingPostOffer const** out_offer)
{
    struct RS_CS2TradingPost const* const post = &host->trading_post;
    if( !post->present )
        return script_error(opcode, "no trading-post list");
    if( index < 0 || index >= post->count )
        return script_error(opcode, "trading-post index outside the list");
    *out_offer = &post->offers[index];
    return CS2VM_EXECNO_OK;
}

/* getofferage: the offer's age on the server clock, "h:mm:ss". The hour is
 * unpadded, and the minute and second subtractions use the client's int
 * arithmetic, which wraps for an age past 596 hours. */
static void
trading_post_age_text(
    struct RS_CS2Host const* host,
    struct RS_CS2TradingPostOffer const* offer,
    char* out,
    size_t cap)
{
    int64_t const age =
        host->client.now_ms - host->trading_post.clock_offset_ms - offer->time_ms;
    int const hours = (int)(age / 3600000);
    int const hours_ms = (int)((uint32_t)hours * 3600000u);
    int const minutes = (int)((age - hours_ms) / 60000);
    int const minutes_ms = (int)((uint32_t)minutes * 60000u);
    int const seconds = (int)((age - hours_ms - minutes_ms) / 1000);
    snprintf(out, cap, "%d:%d%d:%d%d", hours, minutes / 10, minutes % 10, seconds / 10,
        seconds % 10);
}

int
RS_CS2Host_ExecMarketOp(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostSignatureArgs const* args)
{
    assert(host);
    assert(vm);
    assert(args);
    int const opcode = args->opcode;
    int const* const ints = args->ints;
    struct RS_CS2TradingPost* const post = &host->trading_post;
    int rc;

    switch( opcode )
    {
    case CS2_OP_STOCKMARKET_GETOFFERTYPE:
    case CS2_OP_STOCKMARKET_GETOFFERITEM:
    case CS2_OP_STOCKMARKET_GETOFFERPRICE:
    case CS2_OP_STOCKMARKET_GETOFFERCOUNT:
    case CS2_OP_STOCKMARKET_GETOFFERCOMPLETEDCOUNT:
    case CS2_OP_STOCKMARKET_GETOFFERCOMPLETEDGOLD:
    case CS2_OP_STOCKMARKET_ISOFFEREMPTY:
    case CS2_OP_STOCKMARKET_ISOFFERSTABLE:
    case CS2_OP_STOCKMARKET_ISOFFERFINISHED:
    case CS2_OP_STOCKMARKET_ISOFFERADDING:
    {
        struct RS_CS2StockmarketOffer const* offer;
        if( (rc = stockmarket_offer(host, opcode, ints[0], &offer)) != CS2VM_EXECNO_OK )
            return rc;
        int const state = offer->status & 7;
        switch( opcode )
        {
        case CS2_OP_STOCKMARKET_GETOFFERTYPE:
            /* 1 is a sell offer. */
            return CS2VM2_PushInt(vm, (offer->status & 8) == 8 ? 1 : 0);
        case CS2_OP_STOCKMARKET_GETOFFERITEM:
            return CS2VM2_PushInt(vm, offer->obj);
        case CS2_OP_STOCKMARKET_GETOFFERPRICE:
            return CS2VM2_PushInt(vm, offer->price);
        case CS2_OP_STOCKMARKET_GETOFFERCOUNT:
            return CS2VM2_PushInt(vm, offer->count);
        case CS2_OP_STOCKMARKET_GETOFFERCOMPLETEDCOUNT:
            return CS2VM2_PushInt(vm, offer->completed_count);
        case CS2_OP_STOCKMARKET_GETOFFERCOMPLETEDGOLD:
            return CS2VM2_PushInt(vm, offer->completed_gold);
        case CS2_OP_STOCKMARKET_ISOFFEREMPTY:
            return CS2VM2_PushInt(vm, state == 0 ? 1 : 0);
        case CS2_OP_STOCKMARKET_ISOFFERSTABLE:
            return CS2VM2_PushInt(vm, state == 2 ? 1 : 0);
        case CS2_OP_STOCKMARKET_ISOFFERFINISHED:
            return CS2VM2_PushInt(vm, state == 5 ? 1 : 0);
        default:
            return CS2VM2_PushInt(vm, state == 1 ? 1 : 0);
        }
    }

    case CS2_OP_TRADINGPOST_SORTBY_NAME:
    case CS2_OP_TRADINGPOST_SORTBY_PRICE:
    case CS2_OP_TRADINGPOST_SORTFILTERBY_WORLD:
    case CS2_OP_TRADINGPOST_SORTBY_AGE:
    case CS2_OP_TRADINGPOST_SORTBY_COUNT:
    {
        if( !post->present )
            return CS2VM_EXECNO_OK;
        struct TradingPostOrder order = {
            .ascending = ints[0] == 1,
            .own_world = host->map_world,
        };
        switch( opcode )
        {
        case CS2_OP_TRADINGPOST_SORTBY_NAME:
            order.key = TRADING_POST_BY_NAME;
            break;
        case CS2_OP_TRADINGPOST_SORTBY_PRICE:
            order.key = TRADING_POST_BY_PRICE;
            break;
        case CS2_OP_TRADINGPOST_SORTFILTERBY_WORLD:
            order.key = TRADING_POST_BY_WORLD;
            order.own_world_first = ints[1] == 1;
            break;
        case CS2_OP_TRADINGPOST_SORTBY_AGE:
            order.key = TRADING_POST_BY_AGE;
            break;
        default:
            order.key = TRADING_POST_BY_COUNT;
            break;
        }
        trading_post_sort(post, &order);
        return CS2VM_EXECNO_OK;
    }

    case CS2_OP_TRADINGPOST_GETTOTALOFFERS:
        return CS2VM2_PushInt(vm, post->present ? post->count : 0);

    case CS2_OP_TRADINGPOST_GETOFFERWORLD:
    case CS2_OP_TRADINGPOST_GETOFFERNAME:
    case CS2_OP_TRADINGPOST_GETOFFERPREVIOUSNAME:
    case CS2_OP_TRADINGPOST_GETOFFERAGE:
    case CS2_OP_TRADINGPOST_GETOFFERCOUNT:
    case CS2_OP_TRADINGPOST_GETOFFERPRICE:
    case CS2_OP_TRADINGPOST_GETOFFERITEM:
    {
        struct RS_CS2TradingPostOffer const* offer;
        if( (rc = trading_post_offer(host, opcode, ints[0], &offer)) != CS2VM_EXECNO_OK )
            return rc;
        switch( opcode )
        {
        case CS2_OP_TRADINGPOST_GETOFFERWORLD:
            return CS2VM2_PushInt(vm, offer->world);
        case CS2_OP_TRADINGPOST_GETOFFERNAME:
            return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, offer->name));
        case CS2_OP_TRADINGPOST_GETOFFERPREVIOUSNAME:
            return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, offer->previous_name));
        case CS2_OP_TRADINGPOST_GETOFFERAGE:
        {
            char text[64];
            trading_post_age_text(host, offer, text, sizeof(text));
            return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, text));
        }
        case CS2_OP_TRADINGPOST_GETOFFERCOUNT:
            return CS2VM2_PushInt(vm, offer->count);
        case CS2_OP_TRADINGPOST_GETOFFERPRICE:
            return CS2VM2_PushInt(vm, offer->price);
        default:
            /* The list's item, the same for every offer. */
            return CS2VM2_PushInt(vm, post->obj);
        }
    }

    default:
        TORIRS_LOG("RS_CS2Host_ExecMarketOp: unhandled opcode %d\n", opcode);
        assert(0 && "RS_CS2Host_ExecMarketOp: unexpected opcode");
        return CS2VM_EXECNO_ERROR;
    }
}
