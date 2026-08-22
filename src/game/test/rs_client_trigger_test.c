#include "game/rs_client_trigger.h"
#include "game/rs_entity_overlay.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failures++;                                                                          \
            printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                               \
        }                                                                                          \
    } while( 0 )

/*
 * The hashes, against cache.osrs239.
 *
 * These are not derived from the formula they test. Each row is a group
 * identifier read straight out of the clientscript index's reference table,
 * paired with the script that group holds -- so a formula that is subtly wrong
 * (hashing the integer instead of its decimal string, 33 instead of 31, the
 * category bias off by 0x100) fails here rather than answering -1 for every
 * trigger in the cache, which is indistinguishable from a cache that binds
 * none. That was the whole risk of this subsystem: silence is the failure
 * mode, and silence is also what "not implemented" looked like.
 */
struct TriggerCase
{
    char const* what;
    int trigger;
    int subject; /* subject id, category id, or 0 for the global form */
    int form;    /* 0 subject, 1 category, 2 global */
    int name_hash;
    int script; /* what that group holds -- documentation, not asserted */
};

static struct TriggerCase const CASES[] = {
    /* Agility shortcut handlers: bound per LOC TYPE at trigger 37. */
    { "loc 19032 add", RS_TRIGGER_LOC_ADD, 19032, 0, 561116474, 5621 },
    { "loc 7527 add", RS_TRIGGER_LOC_ADD, 7527, 0, -2077257070, 5559 },
    /* Fishing spots: one npc type, and one whole npc CATEGORY. */
    { "npc 3317 add", RS_TRIGGER_NPC_ADD, 3317, 0, 1653002515, 4526 },
    { "npc category 283 add", RS_TRIGGER_NPC_ADD, 283, 1, 1340673665, 4528 },
    { "loc category 2203 add", RS_TRIGGER_LOC_ADD, 2203, 1, -1443189328, 7941 },
    /* Global forms: every npc gets its name plate, and the hovered-tile
     * refresher runs on its own trigger with no subject at all. */
    { "any npc add", RS_TRIGGER_NPC_ADD, 0, 2, 1392327, 6693 },
    { "global trigger 48", 48, 0, 2, 1392293, 5197 },
};

static void
test_trigger_hashes(void)
{
    printf("TEST: client trigger name hashes against cache.osrs239\n");
    for( size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++ )
    {
        struct TriggerCase const* c = &CASES[i];
        int hash;
        int name_hash;

        if( c->form == 0 )
            hash = RS_ClientTriggerHashSubject(c->trigger, c->subject);
        else if( c->form == 1 )
            hash = RS_ClientTriggerHashCategory(c->trigger, c->subject);
        else
            hash = RS_ClientTriggerHashGlobal(c->trigger);

        name_hash = RS_ClientTriggerNameHash(hash);
        if( name_hash != c->name_hash )
            printf(
                "      %s: hash=%d name_hash=%d, cache says %d (script %d)\n",
                c->what,
                hash,
                name_hash,
                c->name_hash,
                c->script);
        CHECK(name_hash == c->name_hash, c->what);
    }
}

/*
 * The three forms live in disjoint ranges.
 *
 * That is what lets one namespace hold all three, and it is a property of the
 * biases rather than of any particular id: a subject hash is non-negative, a
 * global hash is a small negative, and a category hash is below both. If a
 * category hash could reach a subject hash, a loc type would silently claim
 * some other category's script.
 */
static void
test_trigger_forms_disjoint(void)
{
    printf("TEST: the three trigger hash forms cannot collide\n");
    CHECK(RS_ClientTriggerHashSubject(RS_TRIGGER_LOC_ADD, 0) >= 0, "subject form is non-negative");
    CHECK(
        RS_ClientTriggerHashGlobal(127) < 0 && RS_ClientTriggerHashGlobal(0) >= -0x200,
        "global form sits in [-0x200, 0)");
    CHECK(
        RS_ClientTriggerHashCategory(127, 0) < RS_ClientTriggerHashGlobal(0),
        "category form sits below every global form");
    CHECK(
        RS_ClientTriggerHashCategory(0, 1) < RS_ClientTriggerHashCategory(0, 0),
        "a larger category is a smaller hash");
}

/* The overlay store: what the CS2 ops do to it, without a tree. */
static void
test_overlay_store(void)
{
    struct RS_OverlayState st;
    int a;
    int b;

    printf("TEST: scripted entity overlay store\n");
    RS_OverlayReset(&st);
    CHECK(RS_OverlayCount(&st) == 0, "a reset store is empty");
    CHECK(RS_OverlayGet(&st, 0) == NULL, "a free index reads as absent");

    a = RS_OverlayCreateEntity(&st, RS_OVERLAY_ANCHOR_NPC, 7, 3, RS_OVERLAY_BAND_ABOVE, 72, 64);
    CHECK(a >= 0, "npc overlay created");
    CHECK(RS_OverlayFindEntity(&st, RS_OVERLAY_ANCHOR_NPC, 7, 3) == a, "found by (npc, slot)");
    CHECK(
        RS_OverlayFindEntity(&st, RS_OVERLAY_ANCHOR_NPC, 8, 3) == -1,
        "a different npc has no overlay in that slot");
    CHECK(
        RS_OverlayFindEntity(&st, RS_OVERLAY_ANCHOR_PLAYER, 7, 3) == -1,
        "a player with the same uid is a different subject");

    /* Same subject, same slot: the reference destroys the old one first, so a
     * script that rebuilds every tick costs one overlay rather than all of
     * them. Re-taking the freed index is what proves it was freed. */
    b = RS_OverlayCreateEntity(&st, RS_OVERLAY_ANCHOR_NPC, 7, 3, RS_OVERLAY_BAND_MIDDLE, 1, 1);
    CHECK(b == a, "recreating in the same slot reuses the record");
    CHECK(RS_OverlayCount(&st) == 1, "and does not leak the old one");
    CHECK(RS_OverlayGet(&st, b)->band == RS_OVERLAY_BAND_MIDDLE, "the new band took");

    /* A second slot on the same npc is a second overlay -- clientscript 6694
     * gives every npc two. */
    CHECK(
        RS_OverlayCreateEntity(&st, RS_OVERLAY_ANCHOR_NPC, 7, 2, RS_OVERLAY_BAND_ABOVE, 1, 1) != b,
        "a different slot on the same npc is a different overlay");
    CHECK(RS_OverlayCount(&st) == 2, "two overlays on one npc");

    RS_OverlayDestroyEntity(&st, RS_OVERLAY_ANCHOR_NPC, 7);
    CHECK(RS_OverlayCount(&st) == 0, "despawning the npc drops both");

    /* Statics key on (coord, type, slot): two locs on one tile are two
     * subjects, which is the whole reason the layer is part of the key. */
    a = RS_OverlayCreateStatic(&st, 12345, 0, 5, RS_OVERLAY_BAND_ABOVE, 60, 60);
    b = RS_OverlayCreateStatic(&st, 12345, 3, 5, RS_OVERLAY_BAND_ABOVE, 60, 60);
    CHECK(a >= 0 && b >= 0 && a != b, "wall and ground decor on one tile are separate");
    CHECK(RS_OverlayFindStatic(&st, 12345, 0, 5) == a, "found by (coord, layer, slot)");
    CHECK(RS_OverlayFindStatic(&st, 12345, 1, 5) == -1, "an empty layer on that tile has none");

    CHECK(!RS_OverlayTypeValid(-1), "a negative static type is refused");
    CHECK(RS_OverlayTypeValid(4), "the bare-coord type is the last valid one");
    CHECK(!RS_OverlayTypeValid(5), "IsTypeValid bounds the type at 5");
    CHECK(
        RS_OverlayCreateStatic(&st, 1, 5, 0, 0, 1, 1) == -1,
        "creating with an invalid static type is refused, not clamped");

    /* Destroying a free index is a no-op: the DESTROY ops pass the result of a
     * find, which is -1 when there is nothing there. */
    RS_OverlayDestroy(&st, -1);
    RS_OverlayDestroy(&st, RS_OVERLAY_MAX + 10);
    CHECK(RS_OverlayCount(&st) == 2, "destroying nothing destroys nothing");
}

/* The table refuses rather than evicting when it is full. */
static void
test_overlay_full(void)
{
    struct RS_OverlayState st;
    int last = 0;

    printf("TEST: a full overlay table refuses\n");
    RS_OverlayReset(&st);
    for( int i = 0; i < RS_OVERLAY_MAX; i++ )
        last = RS_OverlayCreateEntity(&st, RS_OVERLAY_ANCHOR_NPC, i, 0, 0, 1, 1);
    CHECK(last >= 0, "the table fills");
    CHECK(RS_OverlayCount(&st) == RS_OVERLAY_MAX, "to exactly its capacity");
    CHECK(
        RS_OverlayCreateEntity(&st, RS_OVERLAY_ANCHOR_NPC, RS_OVERLAY_MAX, 0, 0, 1, 1) == -1,
        "and then answers -1 instead of evicting somebody");
    CHECK(
        RS_OverlayFindEntity(&st, RS_OVERLAY_ANCHOR_NPC, 0, 0) == 0,
        "the first overlay is still there");
}

int
main(void)
{
    test_trigger_hashes();
    test_trigger_forms_disjoint();
    test_overlay_store();
    test_overlay_full();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
