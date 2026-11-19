/*
 * The server's five player ops and the two Attack options.
 *
 * The menu reads these by index, so a slot written one off shows the server's
 * "Trade with" where its "Follow" belongs, and an option that did not change
 * when its varp did leaves every Attack row ranked by the old setting.
 *
 *   make -C src test-rs-player-ops
 */
#include "game/rs_player_ops.h"

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
test_set_writes_one_slot(void)
{
    struct RS_PlayerOps ops;

    printf("TEST: an op lands in its own slot and leaves the others alone\n");

    memset(&ops, 0, sizeof(ops));
    RS_PlayerOps_Set(&ops, 0, "Follow", 1);
    RS_PlayerOps_Set(&ops, 4, "Report", 0);
    CHECK(strcmp(ops.text[0], "Follow") == 0, "slot 0 holds \"%s\"", ops.text[0]);
    CHECK(ops.primary[0] == 1, "slot 0 primary %d", ops.primary[0]);
    CHECK(strcmp(ops.text[4], "Report") == 0, "slot 4 holds \"%s\"", ops.text[4]);
    CHECK(ops.primary[4] == 0, "slot 4 primary %d", ops.primary[4]);
    for( int i = 1; i < 4; i++ )
        CHECK(ops.text[i][0] == '\0', "slot %d was written: \"%s\"", i, ops.text[i]);

    RS_PlayerOps_Set(&ops, 0, "", 0);
    CHECK(ops.text[0][0] == '\0', "an empty text left \"%s\"", ops.text[0]);
}

static void
test_a_long_text_is_cut_to_fit(void)
{
    struct RS_PlayerOps ops;
    char longer[RS_PLAYER_OPS_TEXT_MAX + 20];

    printf("TEST: a text longer than the slot is cut and terminated\n");

    memset(longer, 'a', sizeof(longer) - 1);
    longer[sizeof(longer) - 1] = '\0';
    memset(&ops, 0, sizeof(ops));
    RS_PlayerOps_Set(&ops, 2, longer, 1);
    CHECK(strlen(ops.text[2]) == RS_PLAYER_OPS_TEXT_MAX - 1, "kept %zu bytes",
        strlen(ops.text[2]));
    CHECK(ops.text[3][0] == '\0', "the cut text ran into the next slot");
}

static void
test_attack_options(void)
{
    struct RS_PlayerOps ops;

    printf("TEST: the Attack options are set together and one at a time\n");

    memset(&ops, 0, sizeof(ops));
    RS_PlayerOps_SetAttackOptions(&ops, 3, 0);
    CHECK(ops.player_attack_option == 3 && ops.npc_attack_option == 0,
        "set together as %d/%d", ops.player_attack_option, ops.npc_attack_option);
    RS_PlayerOps_SetPlayerAttackOption(&ops, 4);
    CHECK(ops.player_attack_option == 4, "player option %d", ops.player_attack_option);
    CHECK(ops.npc_attack_option == 0, "setting the player option moved the npc one to %d",
        ops.npc_attack_option);
    RS_PlayerOps_SetNpcAttackOption(&ops, 2);
    CHECK(ops.npc_attack_option == 2, "npc option %d", ops.npc_attack_option);
    CHECK(ops.player_attack_option == 4, "setting the npc option moved the player one to %d",
        ops.player_attack_option);
}

int
main(void)
{
    test_set_writes_one_slot();
    test_a_long_text_is_cut_to_fit();
    test_attack_options();

    if( g_failures )
    {
        printf("rs_player_ops_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_player_ops_test: OK\n");
    return 0;
}
