/*
 * Rev 241 NPC_INFO_LARGE_V6: the NPC_INFO_SMALL_V6 stream with 8-bit
 * low-resolution deltas instead of 6 (RSProx NpcInfoClient.decodeDelta). Jagex
 * sends it whenever the npc view distance grows -- Brutus's arena (RSProx
 * index 4381 [640]), Count Draynor's crypt (index 4373 [465]) -- and a client
 * that read it as SMALL, or dropped it, never showed those npcs.
 *
 * One entering-view record, hand-packed bit by bit in RSProx's read order,
 * with deltas a 6-bit field cannot hold.
 */
#include "streams/rsprot_info.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int osrs241_npc_info_read(uint8_t const* data, int len, RsprotInfoList* out);
int osrs241_npc_info_read_large(uint8_t const* data, int len, RsprotInfoList* out);

/* The readers write a list; these hand the test its ops, with the strings
 * in one arena the next read reuses. */
static uint8_t g_info_bytes[1 << 16];

static int
osrs241_npc_info_read_ops(uint8_t const* data, int len, struct RsprotInfoOp* ops, int cap)
{
    RsprotInfoList list;

    rsprot_info_list_init(&list, ops, cap, g_info_bytes, (int)sizeof(g_info_bytes));
    return osrs241_npc_info_read(data, len, &list);
}

static int
osrs241_npc_info_read_large_ops(uint8_t const* data, int len, struct RsprotInfoOp* ops, int cap)
{
    RsprotInfoList list;

    rsprot_info_list_init(&list, ops, cap, g_info_bytes, (int)sizeof(g_info_bytes));
    return osrs241_npc_info_read_large(data, len, &list);
}


#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

struct Bits
{
    uint8_t data[64];
    int pos;
};

static void
put(struct Bits* b, int count, uint32_t value)
{
    for( int i = count - 1; i >= 0; i-- )
    {
        if( (value >> i) & 1u )
            b->data[b->pos >> 3] |= (uint8_t)(0x80 >> (b->pos & 7));
        b->pos++;
    }
}

/* count=0 high-resolution npcs, one add, the 0xffff terminator, padding. */
static int
pack_add(struct Bits* b, int delta_bits, int slot, int type, int dx, int dz)
{
    memset(b, 0, sizeof(*b));
    put(b, 8, 0);
    put(b, 16, (uint32_t)slot);
    put(b, delta_bits, (uint32_t)dz & ((1u << delta_bits) - 1));
    put(b, 1, 0);  /* jump */
    put(b, 3, 2);  /* facing */
    put(b, 1, 0);  /* no spawn cycle */
    put(b, 1, 0);  /* no extended info */
    put(b, 2, 1);  /* type width selector -> 14 bits */
    put(b, 14, (uint32_t)type);
    put(b, delta_bits, (uint32_t)dx & ((1u << delta_bits) - 1));
    put(b, 16, 0xffff);
    return (b->pos + 7) / 8 + 4; /* room for the 28-bit lookahead guard */
}

static void
expect_add(struct RsprotInfoOp const* ops, int n, int slot, int type, int dx, int dz)
{
    int found_slot = 0;
    int found_type = 0;
    int found_delta = 0;

    for( int i = 0; i < n; i++ )
    {
        if( ops[i].kind == RSPROT_OP_ADD_NEW )
            found_slot = (int)ops[i].value == slot;
        else if( ops[i].kind == RSPROT_OP_NPCTYPE )
            found_type = (int)ops[i].value == type;
        else if( ops[i].kind == RSPROT_OP_DELTA_XZ )
            found_delta = ops[i].delta_xz.dx == dx && ops[i].delta_xz.dz == dz;
    }
    TEST_CHECK(found_slot);
    TEST_CHECK(found_type);
    TEST_CHECK(found_delta);
}

int
main(void)
{
    struct Bits b;
    struct RsprotInfoOp ops[32];
    int len;
    int n;

    /* LARGE: Brutus, index 41628, npc 15626, deltas past +-31. */
    len = pack_add(&b, 8, 41628, 15626, 90, -100);
    n = osrs241_npc_info_read_large_ops(b.data, len, ops, 32);
    expect_add(ops, n, 41628, 15626, 90, -100);

    /* SMALL is unchanged: 6-bit deltas. */
    len = pack_add(&b, 6, 26476, 15624, -12, 31);
    n = osrs241_npc_info_read_ops(b.data, len, ops, 32);
    expect_add(ops, n, 26476, 15624, -12, 31);

    printf("osrs241_npc_info_large_test: ok\n");
    return 0;
}
