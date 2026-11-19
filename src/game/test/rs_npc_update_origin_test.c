/*
 * The NPC update origin: the tile SET_NPC_UPDATE_ORIGIN names, and whether
 * one is named.
 *
 * An origin that is not used once named puts every root npc a boat's length
 * from where it stands; one that loses its tile when forgotten moves a deck's
 * npcs to 0,0.
 *
 *   make -C src test-rs-npc-update-origin
 */
#include "game/rs_npc_update_origin.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_set_and_forget(void)
{
    struct RS_NpcUpdateOrigin origin;

    printf("TEST: none until set; set names the tile; forget keeps the tile\n");

    memset(&origin, 0, sizeof(origin));
    CHECK(!RS_NpcUpdateOrigin_IsSet(&origin), "a zeroed origin was set");
    CHECK(RS_NpcUpdateOrigin_X(&origin) == 0 && RS_NpcUpdateOrigin_Z(&origin) == 0,
        "a zeroed origin is at %d,%d", RS_NpcUpdateOrigin_X(&origin), RS_NpcUpdateOrigin_Z(&origin));

    RS_NpcUpdateOrigin_Set(&origin, 52, 51);
    CHECK(RS_NpcUpdateOrigin_IsSet(&origin), "a set origin was not set");
    CHECK(RS_NpcUpdateOrigin_X(&origin) == 52, "x %d", RS_NpcUpdateOrigin_X(&origin));
    CHECK(RS_NpcUpdateOrigin_Z(&origin) == 51, "z %d", RS_NpcUpdateOrigin_Z(&origin));

    RS_NpcUpdateOrigin_Forget(&origin);
    CHECK(!RS_NpcUpdateOrigin_IsSet(&origin), "a forgotten origin was set");
    CHECK(RS_NpcUpdateOrigin_X(&origin) == 52 && RS_NpcUpdateOrigin_Z(&origin) == 51,
        "forgetting moved the tile to %d,%d", RS_NpcUpdateOrigin_X(&origin),
        RS_NpcUpdateOrigin_Z(&origin));

    RS_NpcUpdateOrigin_Set(&origin, 10, 20);
    CHECK(RS_NpcUpdateOrigin_IsSet(&origin), "a re-set origin was not set");
    CHECK(RS_NpcUpdateOrigin_X(&origin) == 10 && RS_NpcUpdateOrigin_Z(&origin) == 20,
        "re-set to %d,%d", RS_NpcUpdateOrigin_X(&origin), RS_NpcUpdateOrigin_Z(&origin));
}

int
main(void)
{
    test_set_and_forget();

    if( g_failures )
    {
        printf("rs_npc_update_origin_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_npc_update_origin_test: OK\n");
    return 0;
}
