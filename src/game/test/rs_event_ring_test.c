/*
 * The one event vocabulary and ring both clients keep (game/rs_event_ring.h).
 *
 *   1. The names are the Lua-visible strings tests await, in kind order,
 *      and name -> kind -> name round-trips ("none" is kind 0; an unknown
 *      name is -1).
 *   2. Stamps come back in stamp order with consecutive serials, a re-poll
 *      from the returned serial is empty, and a truncated read resumes where
 *      it stopped.
 *   3. The real client's ring (512, RS_EVENT_RING_COUNT_DROPS) counts what a
 *      full ring overwrites and refuses a cursor it outran; cursor 0 still
 *      reads the survivors.
 *   4. The headless client's ring (1024, RS_EVENT_RING_OVERWRITE) overwrites
 *      uncounted, and RS_EventRing_At finds every held serial, before and
 *      after the wrap.
 *   5. The effect rules keep the numbers both clients used (rs_effect_rules.h).
 */

#include "game/rs_effect_rules.h"
#include "game/rs_event_ring.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while( 0 )

static int failures;

static struct RS_Event
event_of(
    int kind,
    int a)
{
    return (struct RS_Event){ .kind = kind, .cycle = a * 30, .a = a, .b = -a, .c = 7, .d = 9 };
}

static void
test_names(void)
{
    /* The strings tests await (test/, script/): never renamed. */
    static char const* const expected[] = {
        "none",         "sub_opened",   "sub_mounted",  "sub_closed",   "chat_opened",
        "slot_mounted", "resume_answered", "varp_changed", "inv_changed", "obj_added",
        "obj_removed",  "map_flag",     "chat_message", "server_tick",  "npc_spawn",
        "npc_despawn",  "npc_retype",   "inv_packet",   "npc_seq",      "npc_face",
        "projectile",   "projectile_flight", "map_anim",
    };

    CHECK(sizeof(expected) / sizeof(expected[0]) == RS_EVENT_KIND_COUNT, "%d kinds",
          RS_EVENT_KIND_COUNT);
    for( int k = 0; k < RS_EVENT_KIND_COUNT; k++ )
    {
        CHECK(strcmp(RS_EventKindName(k), expected[k]) == 0, "kind %d is '%s', not '%s'", k,
              RS_EventKindName(k), expected[k]);
        CHECK(RS_EventKindFromName(expected[k]) == k, "'%s' -> %d", expected[k],
              RS_EventKindFromName(expected[k]));
    }
    CHECK(RS_EVENT_SERVER_TICK == 13, "server_tick is kind %d", RS_EVENT_SERVER_TICK);
    CHECK(RS_EVENT_NPC_FACE == 19, "npc_face is kind %d", RS_EVENT_NPC_FACE);
    CHECK(RS_EventKindFromName("none") == RS_EVENT_NONE, "none is kind 0");
    CHECK(RS_EventKindFromName("sub_open") == -1, "a typo is no kind");
    CHECK(RS_EventKindFromName("") == -1, "an empty name is no kind");
    printf("PASS: %d names, in kind order, round-trip\n", RS_EVENT_KIND_COUNT);
}

static void
test_order_and_repoll(void)
{
    struct RS_Event entries[8];
    struct RS_EventRing ring;
    struct RS_Event out[8];
    struct RS_Event ev;
    struct RS_Event const* stored;
    int count = -1;
    uint32_t serial = 99;

    memset(entries, 0, sizeof(entries));
    memset(&ring, 0, sizeof(ring));

    /* An empty ring: nothing, and the newest serial is 0. */
    CHECK(RS_EventRing_Read(&ring, entries, 8, 0, out, 8, &count, &serial) == 1, "empty read");
    CHECK(count == 0 && serial == 0, "empty: %d entries, serial %u", count, serial);

    ev = event_of(RS_EVENT_SUB_OPENED, 100);
    ev.serial = 12345; /* assigned by the ring, whatever the caller passes */
    stored = RS_EventRing_Push(&ring, entries, 8, RS_EVENT_RING_COUNT_DROPS, &ev);
    CHECK(stored->serial == 1, "first serial %u", stored->serial);
    ev = event_of(RS_EVENT_SUB_MOUNTED, 101);
    RS_EventRing_Push(&ring, entries, 8, RS_EVENT_RING_COUNT_DROPS, &ev);
    ev = event_of(RS_EVENT_SUB_CLOSED, 102);
    RS_EventRing_Push(&ring, entries, 8, RS_EVENT_RING_COUNT_DROPS, &ev);

    CHECK(RS_EventRing_Read(&ring, entries, 8, 0, out, 8, &count, &serial) == 1, "read");
    CHECK(count == 3, "%d entries", count);
    CHECK(out[0].kind == RS_EVENT_SUB_OPENED && out[0].a == 100 && out[0].b == -100,
          "first entry");
    CHECK(out[0].cycle == 3000 && out[0].c == 7 && out[0].d == 9, "first entry's fields");
    CHECK(out[1].kind == RS_EVENT_SUB_MOUNTED, "second entry");
    CHECK(out[2].kind == RS_EVENT_SUB_CLOSED && out[2].a == 102, "third entry");
    CHECK(out[0].serial == 1 && out[1].serial == 2 && out[2].serial == 3, "consecutive serials");
    CHECK(serial == 3, "returned serial %u", serial);

    CHECK(RS_EventRing_Read(&ring, entries, 8, serial, out, 8, &count, &serial) == 1, "re-poll");
    CHECK(count == 0 && serial == 3, "a re-poll from the returned serial is empty");

    /* A truncated read hands back its last serial, and the re-poll resumes. */
    CHECK(RS_EventRing_Read(&ring, entries, 8, 0, out, 2, &count, &serial) == 1, "short read");
    CHECK(count == 2 && serial == 2, "cap 2: %d entries to serial %u", count, serial);
    CHECK(RS_EventRing_Read(&ring, entries, 8, serial, out, 2, &count, &serial) == 1, "resume");
    CHECK(count == 1 && out[0].serial == 3 && serial == 3, "the cut-off entry is next");
    CHECK(ring.dropped == 0, "nothing dropped");
    printf("PASS: stamp order, a re-poll is empty, a truncated read resumes\n");
}

/* The real client's ring: APP_DRIVE_RING_CAPACITY (512), drops counted. */
static void
test_counted_drops_refuse_a_stale_cursor(void)
{
    enum { CAP = 512 };
    static struct RS_Event entries[CAP];
    struct RS_EventRing ring;
    struct RS_Event out[8];
    struct RS_Event ev;
    uint32_t first_serial;
    uint32_t serial = 0;
    int count = -1;

    memset(&ring, 0, sizeof(ring));
    ev = event_of(RS_EVENT_SERVER_TICK, 1);
    first_serial = RS_EventRing_Push(&ring, entries, CAP, RS_EVENT_RING_COUNT_DROPS, &ev)->serial;
    for( int i = 0; i < CAP + 1; i++ )
    {
        ev = event_of(RS_EVENT_SERVER_TICK, i + 2);
        RS_EventRing_Push(&ring, entries, CAP, RS_EVENT_RING_COUNT_DROPS, &ev);
    }
    CHECK(ring.count == CAP, "full at %d", ring.count);
    CHECK(ring.dropped == 2, "%u dropped", ring.dropped);
    CHECK(ring.newest_serial == CAP + 2, "newest %u", ring.newest_serial);
    CHECK(ring.oldest_serial == 3, "oldest %u", ring.oldest_serial);

    CHECK(RS_EventRing_Read(&ring, entries, CAP, first_serial, out, 8, &count, &serial) == 0,
          "a cursor the ring outran is refused");
    CHECK(count == 0, "a refused read writes nothing");
    /* A cursor one below the oldest survivor missed nothing. */
    CHECK(RS_EventRing_Read(&ring, entries, CAP, 2, out, 8, &count, &serial) == 1,
          "cursor at the last dropped serial");
    CHECK(count == 8 && out[0].serial == 3, "reads from the oldest survivor");
    CHECK(RS_EventRing_Read(&ring, entries, CAP, 0, out, 8, &count, &serial) == 1,
          "cursor 0 is never refused");
    CHECK(count == 8 && out[0].serial == 3 && out[0].a == 3, "cursor 0 reads the survivors");
    printf("PASS: %u dropped and counted; the stale cursor is refused, cursor 0 is not\n",
           ring.dropped);
}

/* The headless client's ring: SCRIPTRUN_EVENTS (1024), overwritten silently,
 * read by serial. */
static void
test_overwrite_reads_by_serial(void)
{
    enum { CAP = 1024 };
    static struct RS_Event entries[CAP];
    struct RS_EventRing ring;
    struct RS_Event ev;
    int bad = 0;

    memset(&ring, 0, sizeof(ring));
    for( int i = 1; i <= 10; i++ )
    {
        ev = event_of(RS_EVENT_VARP_CHANGED, i);
        RS_EventRing_Push(&ring, entries, CAP, RS_EVENT_RING_OVERWRITE, &ev);
    }
    for( uint32_t s = 1; s <= 10; s++ )
        if( RS_EventRing_At(&ring, entries, CAP, s)->a != (int)s )
            bad++;
    CHECK(bad == 0, "%d held serials read wrong before the wrap", bad);

    for( int i = 11; i <= 3 * CAP + 5; i++ )
    {
        ev = event_of(RS_EVENT_VARP_CHANGED, i);
        RS_EventRing_Push(&ring, entries, CAP, RS_EVENT_RING_OVERWRITE, &ev);
    }
    CHECK(ring.dropped == 0, "an overwriting ring counts nothing (%u)", ring.dropped);
    CHECK(ring.count == CAP, "full at %d", ring.count);
    CHECK(ring.newest_serial == 3 * CAP + 5, "newest %u", ring.newest_serial);
    /* What the bot's reader clamps to: newest - capacity + 1. */
    CHECK(ring.oldest_serial == ring.newest_serial - CAP + 1, "oldest %u", ring.oldest_serial);
    bad = 0;
    for( uint32_t s = ring.oldest_serial; s <= ring.newest_serial; s++ )
    {
        struct RS_Event const* e = RS_EventRing_At(&ring, entries, CAP, s);
        if( e->serial != s || e->a != (int)s || e->kind != RS_EVENT_VARP_CHANGED )
            bad++;
    }
    CHECK(bad == 0, "%d held serials read wrong after the wrap", bad);
    printf("PASS: the overwriting ring counts nothing and finds all %d held serials\n", CAP);
}

static void
test_effect_rules(void)
{
    /* The numbers both clients stated before there was one place for them. */
    CHECK(RS_EFFECT_PROJECTILE_CAP == 256, "projectile cap %d", RS_EFFECT_PROJECTILE_CAP);
    CHECK(RS_EFFECT_MAP_ANIM_CAP == 4096, "map anim cap %d", RS_EFFECT_MAP_ANIM_CAP);
    CHECK(RS_EFFECT_LOC_ANIM_CAP == 4096, "loc anim cap %d", RS_EFFECT_LOC_ANIM_CAP);
    CHECK(RS_EFFECT_MAP_ANIM_LIFE_CYCLES == 40 * 30, "map anim life %d cycles",
          RS_EFFECT_MAP_ANIM_LIFE_CYCLES);
    CHECK(RS_EFFECT_LOC_ANIM_LIFE_TICKS == 40, "loc anim life %d ticks",
          RS_EFFECT_LOC_ANIM_LIFE_TICKS);
    CHECK(RS_EFFECT_LOC_ANIM_CLIENT_KEEP_TICKS * APP_SERVER_TICK_LOGIC_CYCLES == (40 + 2) * 30,
          "the real client keeps a loc anim %d ticks", RS_EFFECT_LOC_ANIM_CLIENT_KEEP_TICKS);
    printf("PASS: effect lifetimes and capacities\n");
}

int
main(void)
{
    test_names();
    test_order_and_repoll();
    test_counted_drops_refuse_a_stale_cursor();
    test_overwrite_reads_by_serial();
    test_effect_rules();
    if( failures )
    {
        fprintf(stderr, "rs_event_ring: %d check(s) failed\n", failures);
        return 1;
    }
    printf("rs_event_ring: all checks passed\n");
    return 0;
}
