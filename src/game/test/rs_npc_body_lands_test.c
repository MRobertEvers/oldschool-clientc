/*
 * The npc bodies in flight (game/rs_npc_body_lands.h).
 *
 * A type is in flight from its registration to its forget, keyed on both the
 * base and the resolved type; a full table registers nothing; forgetting
 * moves the last entry into the gap and leaves every other type in flight.
 *
 * Run: make -C src test-rs-npc-body-lands
 */

#include "game/rs_npc_body_lands.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_register_and_forget(void)
{
    struct RS_NpcBodyLands lands;

    memset(&lands, 0, sizeof(lands));
    TEST_ASSERT(!RS_NpcBodyLands_InFlight(&lands, 1, 2), "empty table: nothing in flight");
    TEST_ASSERT(RS_NpcBodyLands_Register(&lands, 1, 2), "registered");
    TEST_ASSERT(RS_NpcBodyLands_InFlight(&lands, 1, 2), "in flight once registered");
    TEST_ASSERT(!RS_NpcBodyLands_InFlight(&lands, 2, 2), "the base is part of the key");
    TEST_ASSERT(!RS_NpcBodyLands_InFlight(&lands, 1, 3), "the type is part of the key");

    RS_NpcBodyLands_Register(&lands, 10, 11);
    RS_NpcBodyLands_Register(&lands, 20, 21);
    RS_NpcBodyLands_Forget(&lands, 1, 2);
    TEST_ASSERT(!RS_NpcBodyLands_InFlight(&lands, 1, 2), "forgotten");
    TEST_ASSERT(lands.count == 2, "one fewer");
    TEST_ASSERT(lands.base[0] == 20 && lands.type[0] == 21, "the last entry fills the gap");
    TEST_ASSERT(RS_NpcBodyLands_InFlight(&lands, 10, 11), "the others stay in flight");
    TEST_ASSERT(RS_NpcBodyLands_InFlight(&lands, 20, 21), "the moved one stays in flight");

    RS_NpcBodyLands_Forget(&lands, 99, 99);
    TEST_ASSERT(lands.count == 2, "forgetting an unknown type is a no-op");
}

static void
test_full_table_registers_nothing(void)
{
    struct RS_NpcBodyLands lands;

    memset(&lands, 0, sizeof(lands));
    for( int i = 0; i < RS_NPC_BODY_LANDS_MAX; i++ )
        TEST_ASSERT(RS_NpcBodyLands_Register(&lands, i, i), "registered while room");
    TEST_ASSERT(!RS_NpcBodyLands_Register(&lands, 500, 500), "a full table registers nothing");
    TEST_ASSERT(lands.count == RS_NPC_BODY_LANDS_MAX, "and holds its count");
    TEST_ASSERT(!RS_NpcBodyLands_InFlight(&lands, 500, 500), "the unregistered type is not in flight");
    RS_NpcBodyLands_Forget(&lands, 3, 3);
    TEST_ASSERT(RS_NpcBodyLands_Register(&lands, 500, 500), "room again after a forget");
}

int
main(void)
{
    test_register_and_forget();
    test_full_table_registers_nothing();
    if( g_failures )
    {
        printf("rs_npc_body_lands_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_npc_body_lands_test: ok\n");
    return 0;
}
