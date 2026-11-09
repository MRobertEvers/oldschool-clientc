/**
 * The npc effective-mode rule and the route spellings (editor/editor_npc.h).
 *
 *   editor_npc_test
 *
 * Pure: no content tree, no chrome. The fixture is Hans as the tree states
 * him (docs/NPC_EDITOR_DESIGN.md 1.1): the generated layer's wanderrange=5
 * and facing=south, lumbridge.npc's maxrange=50, defaultmode=patrol and ten
 * patrol rows.
 */

#include "editor/editor_cfg.h"
#include "editor/editor_cmd.h"
#include "editor/editor_npc.h"
#include "editor/map_editor_ghost.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                                     \
    do                                                                                                       \
    {                                                                                                        \
        g_checks++;                                                                                          \
        if( !(cond) )                                                                                        \
        {                                                                                                    \
            g_failed++;                                                                                      \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                             \
            fprintf(stderr, __VA_ARGS__);                                                                    \
            fprintf(stderr, "\n");                                                                           \
        }                                                                                                    \
    } while( 0 )

static const char* const k_hans[][2] = {
    { "wanderrange", "5" },
    { "facing", "south" },
    { "huntmode", "none" },
    { "maxrange", "50" },
    { "defaultmode", "patrol" },
    { "patrol1", "0_50_50_7_33,0" },
    { "patrol2", "0_50_50_11_30,0" },
    { "patrol3", "0_50_50_19_30,0" },
    { "patrol4", "0_50_50_19_22,10" },
    { "patrol5", "0_50_50_19_14,0" },
    { "patrol6", "0_50_50_14_10,0" },
    { "patrol7", "0_50_50_7_12,0" },
    { "patrol8", "0_50_50_2_20,0" },
    { "patrol9", "0_50_50_2_30,0" },
    { "patrol10", "0_50_50_5_33,0" },
};

static void
hans(struct Editor_NpcMovement* out)
{
    Editor_NpcMovementInit(out);
    for( size_t i = 0; i < sizeof(k_hans) / sizeof(k_hans[0]); i++ )
        (void)Editor_NpcMovementRead(out, k_hans[i][0], k_hans[i][1]);
}

static void
test_coords(void)
{
    int x;
    int z;
    int level;
    char text[48];
    struct Editor_NpcWaypoint waypoint;

    CHECK(Editor_NpcCoordParse("0_50_50_7_33", &x, &z, &level) && x == 3207 && z == 3233 && level == 0,
        "Hans's first waypoint reads as 3207,3233 lvl 0 (%d,%d,%d)", x, z, level);
    CHECK(Editor_NpcCoordParse("2_48_53_63_0", &x, &z, &level) && x == 48 * 64 + 63 && z == 53 * 64 && level == 2,
        "a level-2 coord reads");
    CHECK(!Editor_NpcCoordParse("50_50_7_33", &x, &z, &level), "four parts are not a coord");
    CHECK(!Editor_NpcCoordParse("0_50_50_64_33", &x, &z, &level), "a local past 63 is refused");
    CHECK(!Editor_NpcCoordParse("hans", &x, &z, &level), "a word is not a coord");
    Editor_NpcCoordFormat(3207, 3233, 0, text, sizeof(text));
    CHECK(strcmp(text, "0_50_50_7_33") == 0, "the coord formats back as LostCity spells it (%s)", text);
    CHECK(Editor_NpcWaypointParse("0_50_50_19_22,10", &waypoint) && waypoint.x == 3219 && waypoint.z == 3222
              && waypoint.pause == 10,
        "a waypoint reads its pause");
    CHECK(Editor_NpcWaypointParse("0_50_50_19_22", &waypoint) && waypoint.pause == 0, "no pause is pause 0");
    Editor_NpcWaypointFormat(&waypoint, text, sizeof(text));
    CHECK(strcmp(text, "0_50_50_19_22,0") == 0, "a waypoint formats as <coord>,<pause> (%s)", text);
    CHECK(Editor_NpcFacingParse("south") == 6 && strcmp(Editor_NpcFacingWord(6), "south") == 0
              && Editor_NpcFacingParse("up") < 0,
        "the compass words are the register's");
    CHECK(Editor_NpcRestrictParse("outdoors") == EDITOR_NPC_RESTRICT_OUTDOORS
              && Editor_NpcRestrictParse("los") == EDITOR_NPC_RESTRICT_BLOCKED_NORMAL
              && Editor_NpcRestrictParse("5") == EDITOR_NPC_RESTRICT_NOMOVE,
        "moverestrict's words, alias and numbers read");
}

static void
test_hans_patrols(void)
{
    struct Editor_NpcMovement movement;
    struct Editor_NpcEffective effective;
    char sentence[192];

    hans(&movement);
    CHECK(movement.patrol_count == 10 && movement.wanderrange == 5 && movement.maxrange == 50
              && movement.defaultmode_stated && movement.defaultmode == EDITOR_NPC_MODE_PATROL
              && movement.facing == 6,
        "Hans's statements land (%d rows, wander %d, leash %d)", movement.patrol_count, movement.wanderrange,
        movement.maxrange);
    Editor_NpcEffectiveMode(&movement, &effective);
    CHECK(effective.mode == EDITOR_NPC_MODE_PATROL && strcmp(effective.why, "patrol rows") == 0,
        "the patrol rows make him a patroller");
    CHECK(effective.wander == 0 && effective.unused_wanderrange, "his wanderrange 5 is dead data");
    CHECK(!effective.route_contradicts_mode, "defaultmode=patrol agrees with the route");
    /* Chebyshev legs: 4,8,8,8,5,7,8,10,3,2 = 63 steps; one pause of 10. */
    CHECK(effective.lap_steps == 63 && effective.lap_pause == 10, "the lap is 63 steps + 10 pause (%d + %d)",
        effective.lap_steps, effective.lap_pause);
    Editor_NpcEffectiveSentence(&movement, &effective, sentence, sizeof(sentence));
    CHECK(strcmp(sentence, "patrols 10 waypoints, 73 ticks a lap; wanderrange 5 is unused") == 0,
        "the header sentence (%s)", sentence);

    /* The same rows with defaultmode=wander: the route still wins, and the
     * contradiction is named. */
    (void)Editor_NpcMovementRead(&movement, "defaultmode", "wander");
    Editor_NpcEffectiveMode(&movement, &effective);
    CHECK(effective.mode == EDITOR_NPC_MODE_PATROL && effective.route_contradicts_mode,
        "a route under defaultmode=wander is still a patrol, and flagged");
}

static void
test_wander_none_nomove(void)
{
    struct Editor_NpcMovement movement;
    struct Editor_NpcEffective effective;
    char sentence[192];

    /* Nothing stated: the engine's 5 and 7. */
    Editor_NpcMovementInit(&movement);
    Editor_NpcEffectiveMode(&movement, &effective);
    CHECK(effective.mode == EDITOR_NPC_MODE_WANDER && effective.wander == 5 && effective.leash == 7
              && strcmp(effective.why, "wanderrange") == 0,
        "a record stating nothing wanders 5 with leash 7 (%d/%d)", effective.wander, effective.leash);

    /* wanderrange=0: none. */
    (void)Editor_NpcMovementRead(&movement, "wanderrange", "0");
    Editor_NpcEffectiveMode(&movement, &effective);
    CHECK(effective.mode == EDITOR_NPC_MODE_NONE && effective.wander == 0 && !effective.unused_wanderrange,
        "wanderrange 0 stands");

    /* A stated defaultmode=none over wanderrange=8: none, the 8 unused. */
    Editor_NpcMovementInit(&movement);
    (void)Editor_NpcMovementRead(&movement, "wanderrange", "8");
    (void)Editor_NpcMovementRead(&movement, "defaultmode", "none");
    Editor_NpcEffectiveMode(&movement, &effective);
    CHECK(effective.mode == EDITOR_NPC_MODE_NONE && effective.unused_wanderrange
              && strcmp(effective.why, "defaultmode") == 0,
        "defaultmode=none outranks the radius");

    /* nomove zeroes the radius. */
    Editor_NpcMovementInit(&movement);
    (void)Editor_NpcMovementRead(&movement, "wanderrange", "3");
    (void)Editor_NpcMovementRead(&movement, "moverestrict", "nomove");
    Editor_NpcEffectiveMode(&movement, &effective);
    CHECK(effective.nomove && effective.wander == 0 && effective.unused_wanderrange && movement.nomove,
        "moverestrict=nomove zeroes the radius and sets the boolean");
    Editor_NpcEffectiveSentence(&movement, &effective, sentence, sizeof(sentence));
    CHECK(strncmp(sentence, "stands still (nomove)", 21) == 0, "a nomove's sentence (%s)", sentence);

    /* The wanderer's sentence carries the restriction. */
    Editor_NpcMovementInit(&movement);
    (void)Editor_NpcMovementRead(&movement, "wanderrange", "2");
    (void)Editor_NpcMovementRead(&movement, "moverestrict", "indoors");
    (void)Editor_NpcMovementRead(&movement, "param", "huntrange,3");
    Editor_NpcEffectiveMode(&movement, &effective);
    Editor_NpcEffectiveSentence(&movement, &effective, sentence, sizeof(sentence));
    CHECK(strcmp(sentence, "wanders 2 tiles from its spawn; leash 7; indoors") == 0 && movement.huntrange == 3,
        "a wanderer's sentence (%s)", sentence);
    CHECK(!Editor_NpcMovementRead(&movement, "op1", "Talk-to"), "an unrelated key is not taken");
}

static void
test_route_rects(void)
{
    struct Editor_NpcMovement movement;
    struct MapEditorWanderRect rects[64];
    int n;
    int waypoints = 0;
    int legs = 0;
    int closing = 0;

    hans(&movement);
    n = Editor_NpcPreviewRects(&movement, 3207, 3233, 0, rects, 64);
    for( int i = 0; i < n; i++ )
    {
        waypoints += rects[i].kind == MAP_EDITOR_WANDER_WAYPOINT;
        legs += rects[i].kind == MAP_EDITOR_WANDER_LEG;
        closing += rects[i].kind == MAP_EDITOR_WANDER_LEG && (rects[i].flags & MAP_EDITOR_WANDER_FLAG_CLOSING);
    }
    CHECK(n == 20 && waypoints == 10 && legs == 10 && closing == 1,
        "Hans's preview is ten waypoints and ten legs, one closing (%d: %d/%d/%d)", n, waypoints, legs, closing);
    CHECK(rects[0].kind == MAP_EDITOR_WANDER_WAYPOINT && rects[0].x0 == 3207 && rects[0].index == 1,
        "waypoint 1 stands on his spawn tile");
    CHECK(rects[19].kind == MAP_EDITOR_WANDER_LEG && rects[19].x0 == 3205 && rects[19].x1 == 3207
              && rects[19].index == 10,
        "the closing leg runs from waypoint 10 back to 1");
    for( int i = 0; i < n; i++ )
        CHECK(rects[i].kind != MAP_EDITOR_WANDER_AREA, "a patroller draws no wander square");

    /* A leg to another level is a teleport. */
    movement.patrol[3].level = 1;
    n = Editor_NpcRouteRects(movement.patrol, movement.patrol_count, rects, 64);
    CHECK(n == 20 && (rects[12].flags & MAP_EDITOR_WANDER_FLAG_TELEPORT)
              && (rects[13].flags & MAP_EDITOR_WANDER_FLAG_TELEPORT),
        "the legs into and out of waypoint 4 are teleports");

    /* The bound is kept; a lone waypoint has no leg. */
    CHECK(Editor_NpcRouteRects(movement.patrol, movement.patrol_count, rects, 3) == 3, "the output bound is kept");
    CHECK(Editor_NpcRouteRects(movement.patrol, 1, rects, 64) == 1 && rects[0].kind == MAP_EDITOR_WANDER_WAYPOINT,
        "one waypoint, no leg");

    /* A nomove: the pin only. */
    Editor_NpcMovementInit(&movement);
    (void)Editor_NpcMovementRead(&movement, "nomove", "yes");
    n = Editor_NpcPreviewRects(&movement, 10, 20, 0, rects, 64);
    CHECK(n == 1 && rects[0].kind == MAP_EDITOR_WANDER_PIN && rects[0].x0 == 10 && rects[0].z0 == 20,
        "a nomove draws a pin");

    /* A wanderer: the square and the leash, as before. */
    Editor_NpcMovementInit(&movement);
    (void)Editor_NpcMovementRead(&movement, "wanderrange", "3");
    (void)Editor_NpcMovementRead(&movement, "maxrange", "9");
    n = Editor_NpcPreviewRects(&movement, 3200, 3238, 1, rects, 64);
    CHECK(n == 2 && rects[0].kind == MAP_EDITOR_WANDER_AREA && MapEditorWander_RectTiles(&rects[0]) == 7 * 7
              && rects[1].kind == MAP_EDITOR_WANDER_LEASH,
        "a wanderer draws the square and the leash");
    /* A stander with a leash: the chase bound only. */
    (void)Editor_NpcMovementRead(&movement, "wanderrange", "0");
    n = Editor_NpcPreviewRects(&movement, 3200, 3238, 1, rects, 64);
    CHECK(n == 1 && rects[0].kind == MAP_EDITOR_WANDER_LEASH, "a stander draws only the leash");
}

/* The route as commands over Hans's two layers: the generated one (never
 * written) and lumbridge.npc (the write target). */
static void
test_route_plan(void)
{
    static const char k_generated[] = "[hans]\nwanderrange=5\nfacing=south\n";
    static const char k_authored[] =
        "[hans]\nmaxrange=50\ndefaultmode=patrol\npatrol1=0_50_50_7_33,0\npatrol2=0_50_50_11_30,0\npatrol3=0_50_50_19_30,0\n";
    struct Editor_CfgRecord record;
    struct Editor_NpcWaypoint route[4];
    struct Editor_Cmd commands[48];
    int layer;
    int n;
    int sets = 0;
    int removes = 0;

    Editor_CfgRecordInit(&record, "npc", "hans");
    CHECK(Editor_CfgRecordAddLayer(&record, NULL, "[hans]\nname=Hans\n", strlen("[hans]\nname=Hans\n")), "plan: cache layer");
    CHECK(Editor_CfgRecordAddLayer(&record, "_explicit/configs/server_fields.generated.npc", k_generated, strlen(k_generated)),
        "plan: generated layer");
    CHECK(Editor_CfgRecordAddLayer(&record, "areas/lumbridge/configs/lumbridge.npc", k_authored, strlen(k_authored)),
        "plan: authored layer");
    Editor_CfgRecordResolve(&record, NULL, 0);
    layer = Editor_CfgRecordWriteLayerFor(&record, -1);
    CHECK(layer >= 0 && record.layers[layer].rank == 2 && strstr(record.layers[layer].label, "lumbridge.npc"),
        "plan: the write layer is lumbridge.npc, never the generated one (%d)", layer);

    /* Two waypoints replace three: patrol1 changes, patrol2 stays, patrol3 goes. */
    route[0].x = 3207; route[0].z = 3233; route[0].level = 0; route[0].pause = 4;
    route[1].x = 3211; route[1].z = 3230; route[1].level = 0; route[1].pause = 0;
    n = Editor_NpcRoutePlan(&record, route, 2, EDITOR_NPC_MODE_PATROL, layer, commands, 48);
    CHECK(n == 2, "plan: patrol1's new pause and patrol3's removal are the two commands (%d)", n);
    for( int i = 0; i < n; i++ )
    {
        CHECK(commands[i].kind == EDITOR_CMD_CFG_FIELD && strcmp(commands[i].doc_key, record.layers[layer].doc_key) == 0,
            "plan: every command is a CFG_FIELD on lumbridge.npc's block (%s)", commands[i].doc_key);
        sets += commands[i].has_after;
        removes += !commands[i].has_after;
    }
    CHECK(sets == 1 && removes == 1, "plan: one set, one removal (%d/%d)", sets, removes);

    /* No waypoints with Wander chosen: the three rows go, defaultmode=wander. */
    n = Editor_NpcRoutePlan(&record, NULL, 0, EDITOR_NPC_MODE_WANDER, layer, commands, 48);
    CHECK(n == 4, "plan: clearing the route removes three rows and sets defaultmode (%d)", n);
    CHECK(Editor_NpcRoutePlan(&record, route, 2, EDITOR_NPC_MODE_PATROL, layer, commands, 1) == -1,
        "plan: too small an output is refused whole");
    Editor_CfgRecordFree(&record);
}

/* The .move ledger: the generated file pinned to patrols, lines kept. */
static void
test_move_ledger(void)
{
    static const char k_ledger[] =
        "// Hans\n"
        "// npc 3105 - m50_50\n"
        "\n"
        "source       = generated\n"
        "\n"
        "movement     = wanders      // m1  rsmod rev-231 NpcEditor\n"
        "restrict     = outdoors     // r2  LostCity 2004 moverestrict\n"
        "\n"
        "m1           = wanders      // rsmod rev-231 NpcEditor\n";
    char out[1024];
    char key[128];
    size_t length = 0;

    CHECK(Editor_NpcMoveTextSet(k_ledger, strlen(k_ledger), "hans", "patrols", NULL, out, sizeof(out), &length), "ledger: edits");
    CHECK(strstr(out, "source       = authored\n") != NULL, "ledger: source is authored (%s)", out);
    CHECK(strstr(out, "movement     = patrols      // m1  rsmod rev-231 NpcEditor\n") != NULL,
        "ledger: movement is patrols with its note where it stood");
    CHECK(strstr(out, "restrict     = outdoors     // r2") != NULL, "ledger: the restrict line is untouched");
    CHECK(strstr(out, "m1           = wanders") != NULL && strncmp(out, "// Hans\n// npc 3105", 19) == 0,
        "ledger: every other line is kept");
    CHECK(length == strlen(out), "ledger: the length is the text's (%zu)", length);

    /* A restriction word, and a ledger that never had one. */
    CHECK(Editor_NpcMoveTextSet("source = generated\nmovement = wanders\n", strlen("source = generated\nmovement = wanders\n"),
              "cook", "wanders", "indoors", out, sizeof(out), &length)
              && strstr(out, "source = authored\nmovement = wanders\nrestrict     = indoors\n"),
        "ledger: a missing restrict line is added after the last field (%s)", out);
    /* No ledger yet: a header naming the writer. */
    CHECK(Editor_NpcMoveTextSet("", 0, "banker", "stationary", NULL, out, sizeof(out), &length)
              && strstr(out, "NPC editor") && strstr(out, "source       = authored\nmovement     = stationary\n"),
        "ledger: a new file says who wrote it (%s)", out);
    CHECK(!Editor_NpcMoveTextSet("", 0, "x", "two words", NULL, out, sizeof(out), &length), "ledger: a bad word is refused");
    Editor_NpcMoveDocKey(key, "hans");
    CHECK(strcmp(key, "move:h/hans") == 0, "ledger: the document key is move:h/hans (%s)", key);
    Editor_NpcMoveDocKey(key, "9th_squad");
    CHECK(strcmp(key, "move:9/9th_squad") == 0, "ledger: a digit shards itself (%s)", key);
}

int
main(void)
{
    test_coords();
    test_hans_patrols();
    test_wander_none_nomove();
    test_route_rects();
    test_route_plan();
    test_move_ledger();
    printf("editor_npc_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
