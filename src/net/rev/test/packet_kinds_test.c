/*
 * The one table of packet kinds (net/rev/revpacket.c): what the packet pump
 * latches and flushes on, read off one row per packet.
 *
 * Two hand-kept lists used to decide it, and they had gaps: VARP_LONG did not
 * latch where VARP_SMALL and VARP_LARGE did, MESSAGE_GAME did not where the
 * friend lists did, TUT_FLASH wrote the side tab IF_SETTAB_ACTIVE writes and
 * did not, and IF_CLEARINV, LAST_LOGIN_INFO and TRIGGER_ONDIALOGABORT did not
 * flush the held client scripts ahead of themselves.
 *
 *   make -C src test-packet-kinds
 *
 * The same target runs tools/packet_kinds_check.py, which reads the executor's
 * sources and fails on a packet name it handles that has no row.
 */
#include "net/rev/revpacket.h"

#include <stdio.h>

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

/* What the hand-kept lists said, before the table: nothing they latched or
 * flushed stops doing so. */
static enum GameProtoPktName const k_old_latch[] = {
    PKT_NAME_TUT_OPEN,          PKT_NAME_UPDATE_INV_STOP_TRANSMIT, PKT_NAME_UPDATE_INV_FULL,
    PKT_NAME_UPDATE_INV_PARTIAL, PKT_NAME_UPDATE_IGNORELIST,       PKT_NAME_CHAT_FILTER_SETTINGS,
    PKT_NAME_UPDATE_FRIENDLIST, PKT_NAME_FRIENDLIST_LOADED,        PKT_NAME_UPDATE_RUNWEIGHT,
    PKT_NAME_UPDATE_STAT,       PKT_NAME_UPDATE_RUNENERGY,         PKT_NAME_TRIGGER_ONDIALOGABORT,
    PKT_NAME_RUNCLIENTSCRIPT,   PKT_NAME_VARP_SMALL,               PKT_NAME_VARP_LARGE,
    PKT_NAME_VARP_SYNC,         PKT_NAME_VARP_RESET,
};

static enum GameProtoPktName const k_old_flush[] = {
    PKT_NAME_IF_OPENCHAT,
    PKT_NAME_IF_OPENMAIN_SIDE,
    PKT_NAME_IF_CLOSE,
    PKT_NAME_IF_SETTAB,
    PKT_NAME_IF_OPENMAIN,
    PKT_NAME_IF_OPENSIDE,
    PKT_NAME_IF_OPENOVERLAY,
    PKT_NAME_IF_OPENTOP,
    PKT_NAME_IF_OPENSUB,
    PKT_NAME_IF_CLOSESUB,
    PKT_NAME_IF_MOVESUB,
    PKT_NAME_IF_RESYNC_V2,
    PKT_NAME_TUT_OPEN,
    PKT_NAME_IF_SETANGLE,
    PKT_NAME_IF_SETANIM,
    PKT_NAME_IF_SETCOLOUR,
    PKT_NAME_IF_SETEVENTS,
    PKT_NAME_IF_SETHIDE,
    PKT_NAME_IF_SETMODEL,
    PKT_NAME_IF_SETNPCHEAD,
    PKT_NAME_IF_SETNPCHEAD_ACTIVE,
    PKT_NAME_IF_SETOBJECT,
    PKT_NAME_IF_SETPLAYERHEAD,
    PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR,
    PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE,
    PKT_NAME_IF_SETPLAYERMODEL_OBJ,
    PKT_NAME_IF_SETPLAYERMODEL_SELF,
    PKT_NAME_IF_SETPOSITION,
    PKT_NAME_IF_SETROTATESPEED,
    PKT_NAME_IF_SETSCROLLPOS,
    PKT_NAME_IF_SETTEXT,
};

static void
test_every_name_has_a_row(void)
{
    printf("TEST: every packet name, and the control frame, has a row\n");

    for( int name = PKT_NAME_NONE; name < PKT_NAME_COUNT; name++ )
    {
        struct GameProtoPacketKind const* kind = gameproto_packet_kind((enum GameProtoPktName)name);

        CHECK(kind->stated, "packet name %d has no row", name);
    }
}

static void
test_the_rows_are_consistent(void)
{
    printf("TEST: a row that flushes latches; a row that waits says for what\n");

    for( int name = PKT_NAME_NONE; name < PKT_NAME_COUNT; name++ )
    {
        struct GameProtoPacketKind const* kind = gameproto_packet_kind((enum GameProtoPktName)name);

        CHECK(kind->latch <= GAMEPROTO_LATCH_VARP, "name %d latch %d", name, kind->latch);
        CHECK(kind->flush <= GAMEPROTO_FLUSH_HOOKS, "name %d flush %d", name, kind->flush);
        CHECK((kind->wait & ~(GAMEPROTO_WAIT_REQUIRED | GAMEPROTO_WAIT_PARALLEL)) == 0,
            "name %d wait bits %d", name, kind->wait);
        if( kind->flush != GAMEPROTO_FLUSH_NONE )
            CHECK(kind->latch != GAMEPROTO_LATCH_NONE,
                "name %d flushes the held scripts and does not latch", name);
        if( kind->wait != GAMEPROTO_WAIT_NONE )
            CHECK(kind->waits_for && kind->waits_for[0], "name %d waits for nothing named", name);
        else
            CHECK(kind->waits_for == NULL, "name %d names a wait it does not have", name);
    }
}

static void
test_nothing_the_old_lists_held_was_dropped(void)
{
    printf("TEST: everything the hand-kept lists latched or flushed still does\n");

    /* The whole interface range latched by range. */
    for( int name = PKT_NAME_IF_OPENCHAT; name <= PKT_NAME_IF_SETPLAYERMODEL_SELF; name++ )
        CHECK(gameproto_packet_latches((enum GameProtoPktName)name), "interface name %d", name);
    for( size_t i = 0; i < sizeof(k_old_latch) / sizeof(k_old_latch[0]); i++ )
        CHECK(gameproto_packet_latches(k_old_latch[i]), "name %d no longer latches", k_old_latch[i]);
    for( size_t i = 0; i < sizeof(k_old_flush) / sizeof(k_old_flush[0]); i++ )
        CHECK(gameproto_packet_flushes_held_scripts(k_old_flush[i]), "name %d no longer flushes",
            k_old_flush[i]);
    CHECK(gameproto_packet_latches_varp_only(PKT_NAME_VARP_SMALL), "VARP_SMALL is a plain varp");
    CHECK(gameproto_packet_latches_varp_only(PKT_NAME_VARP_LARGE), "VARP_LARGE is a plain varp");
    CHECK(!gameproto_packet_latches_varp_only(PKT_NAME_VARP_SYNC), "VARP_SYNC is not");
    CHECK(!gameproto_packet_latches_varp_only(PKT_NAME_VARP_RESET), "VARP_RESET is not");
    /* Inventory and varp writes never flush: the price checker. */
    CHECK(!gameproto_packet_flushes_held_scripts(PKT_NAME_UPDATE_INV_PARTIAL), "inv partial");
    CHECK(!gameproto_packet_flushes_held_scripts(PKT_NAME_UPDATE_INV_FULL), "inv full");
    CHECK(!gameproto_packet_flushes_held_scripts(PKT_NAME_VARP_SMALL), "varp small");
    CHECK(!gameproto_packet_flushes_held_scripts(PKT_NAME_RUNCLIENTSCRIPT), "a script");
}

static void
test_the_gaps_are_closed(void)
{
    printf("TEST: the six gaps of the hand-kept lists\n");

    CHECK(gameproto_packet_latches(PKT_NAME_VARP_LONG), "VARP_LONG latches");
    CHECK(gameproto_packet_latches_varp_only(PKT_NAME_VARP_LONG), "as a plain varp");
    CHECK(gameproto_packet_latches(PKT_NAME_MESSAGE_GAME), "MESSAGE_GAME latches like the friend lists");
    CHECK(gameproto_packet_latches(PKT_NAME_MESSAGE_PRIVATE), "and every other chat line");
    CHECK(gameproto_packet_latches(PKT_NAME_MESSAGE_FRIENDCHANNEL), "friend channel");
    CHECK(gameproto_packet_latches(PKT_NAME_MESSAGE_CLANCHANNEL), "clan channel");
    CHECK(gameproto_packet_latches(PKT_NAME_TUT_FLASH), "TUT_FLASH latches like IF_SETTAB_ACTIVE");
    CHECK(gameproto_packet_kind(PKT_NAME_IF_CLEARINV)->flush == GAMEPROTO_FLUSH_COMPONENT,
        "IF_CLEARINV flushes as a component write");
    CHECK(gameproto_packet_kind(PKT_NAME_LAST_LOGIN_INFO)->flush == GAMEPROTO_FLUSH_MOUNT,
        "LAST_LOGIN_INFO opens the welcome screen: a mount");
    CHECK(gameproto_packet_latches(PKT_NAME_LAST_LOGIN_INFO), "and latches");
    CHECK(gameproto_packet_kind(PKT_NAME_TRIGGER_ONDIALOGABORT)->flush == GAMEPROTO_FLUSH_HOOKS,
        "TRIGGER_ONDIALOGABORT runs hooks: a script sent before it runs first");
}

static void
test_what_must_not_latch(void)
{
    printf("TEST: the fence, a logout and world feedback do not latch\n");

    /* The fence closes the tick; a logout is followed by no fence at all. */
    CHECK(!gameproto_packet_latches(PKT_NAME_SERVER_TICK_END), "SERVER_TICK_END");
    CHECK(!gameproto_packet_latches(PKT_NAME_LOGOUT), "LOGOUT");
    CHECK(!gameproto_packet_latches(PKT_NAME_LOGOUT_WITHREASON), "LOGOUT_WITHREASON");
    CHECK(!gameproto_packet_latches(PKT_NAME_NONE), "a control frame");
    /* Immediate world feedback between ticks (the map flag after a click). */
    CHECK(!gameproto_packet_latches(PKT_NAME_UNSET_MAP_FLAG), "the map flag");
    CHECK(!gameproto_packet_latches(PKT_NAME_PLAYER_INFO), "PLAYER_INFO");
    CHECK(!gameproto_packet_latches(PKT_NAME_LOC_ADD_CHANGE), "a loc change");
    CHECK(!gameproto_packet_latches(PKT_NAME_CAM_MOVETO), "the camera");
    CHECK(!gameproto_packet_latches(PKT_NAME_MIDI_SONG), "a song");
}

static void
test_what_waits(void)
{
    printf("TEST: the rows that hold the packet queue say so\n");

    CHECK(gameproto_packet_kind(PKT_NAME_IF_OPENSUB)->wait & GAMEPROTO_WAIT_REQUIRED, "a mount");
    CHECK(gameproto_packet_kind(PKT_NAME_REBUILD_NORMAL)->wait & GAMEPROTO_WAIT_REQUIRED, "a rebuild");
    CHECK(gameproto_packet_kind(PKT_NAME_NPC_INFO)->wait & GAMEPROTO_WAIT_REQUIRED, "npc configs");
    CHECK(gameproto_packet_kind(PKT_NAME_LOC_ADD_CHANGE)->wait == GAMEPROTO_WAIT_PARALLEL,
        "a loc change waits on the parallel queue only");
    CHECK(gameproto_packet_kind(PKT_NAME_IF_SETTEXT)->wait == GAMEPROTO_WAIT_NONE, "a text write");
}

int
main(void)
{
    test_every_name_has_a_row();
    test_the_rows_are_consistent();
    test_nothing_the_old_lists_held_was_dropped();
    test_the_gaps_are_closed();
    test_what_must_not_latch();
    test_what_waits();

    if( g_failures )
    {
        printf("packet_kinds_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("packet_kinds_test: all passed\n");
    return 0;
}
