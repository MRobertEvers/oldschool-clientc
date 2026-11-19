/*
 * The zone inbox: the base a zone header sets, and the zone sub-packets held
 * while the scene loads.
 *
 * A held packet that applies at the live base instead of its own lands its
 * loc or ground item in the wrong zone or on the wrong plane; one applied out
 * of order leaves a loc the server changed back; one applied twice duplicates
 * an obj stack. None of it is an error on screen.
 *
 *   make -C src test-rs-zone-inbox
 */
#include "game/rs_zone_inbox.h"

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

/* What the apply step saw, in order. */
struct Seen
{
    int count;
    int obj_id[RS_ZONE_INBOX_MAX + 4];
    int base_x[RS_ZONE_INBOX_MAX + 4];
    int base_z[RS_ZONE_INBOX_MAX + 4];
    int level[RS_ZONE_INBOX_MAX + 4];
    int view[RS_ZONE_INBOX_MAX + 4];
};

static void
record(
    void* user,
    struct RS_ZoneInboxEntry const* entry)
{
    struct Seen* seen = user;

    seen->obj_id[seen->count] = entry->pkt._obj_add.obj_id;
    seen->base_x[seen->count] = entry->base_x;
    seen->base_z[seen->count] = entry->base_z;
    seen->level[seen->count] = entry->level;
    seen->view[seen->count] = entry->view;
    seen->count++;
}

static struct PktZoneSubPacket
obj_add(int obj_id)
{
    struct PktZoneSubPacket pkt;

    memset(&pkt, 0, sizeof(pkt));
    pkt.name = PKT_NAME_OBJ_ADD;
    pkt._obj_add.obj_id = obj_id;
    return pkt;
}

static void
test_a_reset_inbox_holds_nothing_at_the_origin(void)
{
    struct RS_ZoneInbox inbox;

    printf("TEST: a reset inbox holds nothing and its base is 0,0 on plane 0\n");

    memset(&inbox, 0x5a, sizeof(inbox));
    RS_ZoneInbox_Reset(&inbox);
    CHECK(RS_ZoneInbox_HeldCount(&inbox) == 0, "held %d", RS_ZoneInbox_HeldCount(&inbox));
    CHECK(!RS_ZoneInbox_IsFull(&inbox), "an empty inbox was full");
    CHECK(RS_ZoneInbox_BaseX(&inbox) == 0, "base x %d", RS_ZoneInbox_BaseX(&inbox));
    CHECK(RS_ZoneInbox_BaseZ(&inbox) == 0, "base z %d", RS_ZoneInbox_BaseZ(&inbox));
    CHECK(RS_ZoneInbox_Level(&inbox) == 0, "level %d", RS_ZoneInbox_Level(&inbox));
}

static void
test_the_base_is_the_last_header(void)
{
    struct RS_ZoneInbox inbox;

    printf("TEST: the base and plane are the last header's\n");

    RS_ZoneInbox_Reset(&inbox);
    RS_ZoneInbox_SetBase(&inbox, 48, 56, 1);
    RS_ZoneInbox_SetBase(&inbox, 16, 24, 3);
    CHECK(RS_ZoneInbox_BaseX(&inbox) == 16, "base x %d", RS_ZoneInbox_BaseX(&inbox));
    CHECK(RS_ZoneInbox_BaseZ(&inbox) == 24, "base z %d", RS_ZoneInbox_BaseZ(&inbox));
    CHECK(RS_ZoneInbox_Level(&inbox) == 3, "level %d", RS_ZoneInbox_Level(&inbox));
}

static void
test_held_packets_drain_in_arrival_order_with_their_own_base(void)
{
    struct RS_ZoneInbox inbox;
    struct PktZoneSubPacket pkt;
    struct Seen seen;

    printf("TEST: held packets drain in arrival order, each at its own base, view and plane\n");

    RS_ZoneInbox_Reset(&inbox);
    memset(&seen, 0, sizeof(seen));
    RS_ZoneInbox_SetBase(&inbox, 8, 8, 0);
    pkt = obj_add(101);
    RS_ZoneInbox_Hold(&inbox, &pkt, 8, 8, 0, 0);
    /* A later header moves the live base; the held packet keeps its own. */
    RS_ZoneInbox_SetBase(&inbox, 40, 32, 2);
    pkt = obj_add(102);
    RS_ZoneInbox_Hold(&inbox, &pkt, 40, 32, 2, 3);
    pkt = obj_add(103);
    RS_ZoneInbox_Hold(&inbox, &pkt, 0, 64, 1, 0);
    CHECK(RS_ZoneInbox_HeldCount(&inbox) == 3, "held %d", RS_ZoneInbox_HeldCount(&inbox));

    RS_ZoneInbox_Drain(&inbox, record, &seen);
    CHECK(seen.count == 3, "applied %d", seen.count);
    CHECK(seen.obj_id[0] == 101 && seen.obj_id[1] == 102 && seen.obj_id[2] == 103,
        "order %d %d %d", seen.obj_id[0], seen.obj_id[1], seen.obj_id[2]);
    CHECK(seen.base_x[0] == 8 && seen.base_z[0] == 8 && seen.level[0] == 0 && seen.view[0] == 0,
        "first at %d,%d plane %d view %d", seen.base_x[0], seen.base_z[0], seen.level[0],
        seen.view[0]);
    CHECK(seen.base_x[1] == 40 && seen.base_z[1] == 32 && seen.level[1] == 2 && seen.view[1] == 3,
        "second at %d,%d plane %d view %d", seen.base_x[1], seen.base_z[1], seen.level[1],
        seen.view[1]);
    CHECK(seen.base_x[2] == 0 && seen.base_z[2] == 64 && seen.level[2] == 1,
        "third at %d,%d plane %d", seen.base_x[2], seen.base_z[2], seen.level[2]);
    CHECK(RS_ZoneInbox_HeldCount(&inbox) == 0, "still held %d", RS_ZoneInbox_HeldCount(&inbox));
    /* The drain leaves the live base where the last header put it. */
    CHECK(RS_ZoneInbox_BaseX(&inbox) == 40 && RS_ZoneInbox_BaseZ(&inbox) == 32 &&
              RS_ZoneInbox_Level(&inbox) == 2,
        "drain moved the base to %d,%d plane %d", RS_ZoneInbox_BaseX(&inbox),
        RS_ZoneInbox_BaseZ(&inbox), RS_ZoneInbox_Level(&inbox));

    /* A second drain has nothing to apply. */
    RS_ZoneInbox_Drain(&inbox, record, &seen);
    CHECK(seen.count == 3, "a second drain applied %d more", seen.count - 3);
}

static void
test_the_inbox_fills_at_its_cap(void)
{
    static struct RS_ZoneInbox inbox;
    static struct Seen seen;
    struct PktZoneSubPacket pkt;

    printf("TEST: the inbox is full at RS_ZONE_INBOX_MAX and drains every one\n");

    RS_ZoneInbox_Reset(&inbox);
    memset(&seen, 0, sizeof(seen));
    for( int i = 0; i < RS_ZONE_INBOX_MAX; i++ )
    {
        CHECK(!RS_ZoneInbox_IsFull(&inbox), "full after %d", i);
        pkt = obj_add(i);
        RS_ZoneInbox_Hold(&inbox, &pkt, i, 0, 0, 0);
    }
    CHECK(RS_ZoneInbox_IsFull(&inbox), "not full at %d", RS_ZoneInbox_HeldCount(&inbox));
    RS_ZoneInbox_Drain(&inbox, record, &seen);
    CHECK(seen.count == RS_ZONE_INBOX_MAX, "applied %d", seen.count);
    CHECK(seen.obj_id[RS_ZONE_INBOX_MAX - 1] == RS_ZONE_INBOX_MAX - 1, "last applied %d",
        seen.obj_id[RS_ZONE_INBOX_MAX - 1]);
    CHECK(!RS_ZoneInbox_IsFull(&inbox), "full after the drain");
}

static void
test_discard_drops_the_held_and_keeps_the_base(void)
{
    struct RS_ZoneInbox inbox;
    struct PktZoneSubPacket pkt;
    struct Seen seen;

    printf("TEST: discard drops every held packet unapplied and keeps the base\n");

    RS_ZoneInbox_Reset(&inbox);
    memset(&seen, 0, sizeof(seen));
    RS_ZoneInbox_SetBase(&inbox, 24, 16, 1);
    pkt = obj_add(7);
    RS_ZoneInbox_Hold(&inbox, &pkt, 24, 16, 1, 0);
    RS_ZoneInbox_Discard(&inbox);
    CHECK(RS_ZoneInbox_HeldCount(&inbox) == 0, "held %d", RS_ZoneInbox_HeldCount(&inbox));
    RS_ZoneInbox_Drain(&inbox, record, &seen);
    CHECK(seen.count == 0, "a discarded packet was applied");
    CHECK(RS_ZoneInbox_BaseX(&inbox) == 24 && RS_ZoneInbox_BaseZ(&inbox) == 16 &&
              RS_ZoneInbox_Level(&inbox) == 1,
        "discard moved the base to %d,%d plane %d", RS_ZoneInbox_BaseX(&inbox),
        RS_ZoneInbox_BaseZ(&inbox), RS_ZoneInbox_Level(&inbox));
}

int
main(void)
{
    test_a_reset_inbox_holds_nothing_at_the_origin();
    test_the_base_is_the_last_header();
    test_held_packets_drain_in_arrival_order_with_their_own_base();
    test_the_inbox_fills_at_its_cap();
    test_discard_drops_the_held_and_keeps_the_base();

    if( g_failures )
    {
        printf("rs_zone_inbox_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_zone_inbox_test: OK\n");
    return 0;
}
