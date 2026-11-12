/*
 * Workbench 6.5, part 2: the instance helper's planner (editor_instance.h)
 * and the world-map area codec (editor_wma.h), pure and content-free.
 *
 *   where     the first free rectangle in instance space over synthetic
 *             atlases, the free count
 *   rebasing  keys, tiles, spawn rows (a level range, suppressions, outside
 *             rows), coords both ways
 *   copy      the commands per "Copy" choice over hand-built squares
 *   trigger   the script text for both entries: the tree's own instance
 *             procs, the exit (its own loc, or the trigger loc both ways),
 *             the npc added on entering, and the sentences that say where
 *             the template is and how it is stamped
 *   wma       parse -> emit byte-identical, the refusals with their line, a
 *             new area's text parsed back to what it claims
 */

#include "editor/editor_instance.h"
#include "editor/editor_wma.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_find_free(void)
{
    int taken[64] = { 0 };
    int n = 0;
    int x = -1;
    int z = -1;

    /* An empty space: the very corner. */
    CHECK(Editor_InstanceFindFree(taken, 0, 1, 2, &x, &z) && x == 0 && z == 100,
        "an empty instance space: a 1x2 lands at m0_100 (m%d_%d)", x, z);
    CHECK(Editor_InstanceFreeCount(taken, 0) == 20 * 156, "20 x 156 squares free (%d)",
        Editor_InstanceFreeCount(taken, 0));

    /* m0_101 taken: a 1x2 cannot start at x 0 in row 100; it takes m1_100. */
    taken[n++] = 0;
    taken[n++] = 101;
    CHECK(Editor_InstanceFindFree(taken, n / 2, 1, 2, &x, &z) && x == 1 && z == 100,
        "m0_101 taken: the 1x2 goes beside it at m1_100 (m%d_%d)", x, z);
    /* A 1x1 still takes the corner. */
    CHECK(Editor_InstanceFindFree(taken, n / 2, 1, 1, &x, &z) && x == 0 && z == 100,
        "a 1x1 still takes m0_100 (m%d_%d)", x, z);

    /* Row 100 entirely taken: the lowest free row is 101 (x 1, beside m0_101). */
    n = 0;
    for( int i = 0; i < 20; i++ )
    {
        taken[n++] = i;
        taken[n++] = 100;
    }
    taken[n++] = 0;
    taken[n++] = 101;
    CHECK(Editor_InstanceFindFree(taken, n / 2, 2, 2, &x, &z) && x == 1 && z == 101,
        "row 100 full and m0_101 taken: a 2x2 at m1_101 (m%d_%d)", x, z);
    CHECK(Editor_InstanceFreeCount(taken, n / 2) == 20 * 156 - 21, "21 taken (%d free)",
        Editor_InstanceFreeCount(taken, n / 2));

    /* Squares outside the space neither block nor count. */
    taken[0] = 50;
    taken[1] = 50;
    CHECK(Editor_InstanceFreeCount(taken, 1) == 20 * 156, "m50_50 is not in instance space");
    CHECK(Editor_InstanceInSpace(19, 255) && !Editor_InstanceInSpace(20, 100) && !Editor_InstanceInSpace(0, 99),
        "the space is 0..19 x 100..255");

    /* Too wide to fit, and a column full of squares leaving a gap of 1. */
    CHECK(!Editor_InstanceFindFree(taken, 0, 21, 1, &x, &z), "21 squares wide never fits");
    {
        static int all[20 * 156 * 2];
        int m = 0;
        for( int zz = 100; zz <= 255; zz++ )
            for( int xx = 0; xx < 20; xx++ )
                if( !(xx == 7 && zz == 200) )
                {
                    all[m++] = xx;
                    all[m++] = zz;
                }
        CHECK(Editor_InstanceFindFree(all, m / 2, 1, 1, &x, &z) && x == 7 && z == 200,
            "one hole at m7_200: a 1x1 finds it (m%d_%d)", x, z);
        CHECK(!Editor_InstanceFindFree(all, m / 2, 1, 2, &x, &z), "a 1x2 does not fit a one-square hole");
        CHECK(Editor_InstanceFreeCount(all, m / 2) == 1, "one free square");
    }
}

static void
test_rebase(void)
{
    static const char* const keys[2] = { "m50_50", "ported/tutorial/maps/m50_51" };
    static const char* const twice[2] = { "m50_50", "maps/m50_50" };
    static const char* const bad[1] = { "m50" };
    struct Editor_InstancePlan plan;
    struct Editor_SpawnRow row;
    struct Editor_SpawnRow out;
    char coord[EDITOR_INSTANCE_COORD_MAX];
    int x;
    int z;
    int level;

    {
        static const char* const backwards[3] = { "m51_51", "m50_51", "m51_50" };
        CHECK(Editor_InstancePlanInit(&plan, backwards, 3) && plan.sources[0].map_x == 51 && plan.sources[0].map_z == 50
                  && plan.sources[1].map_x == 50 && plan.sources[1].map_z == 51 && plan.sources[2].map_x == 51
                  && plan.w == 2 && plan.h == 2 && plan.src_x0 == 50,
            "sources are ordered south-west first (z, then x), the box 2x2 from m50_50");
    }
    {
        /* The open set after an Undo of a Create: the world square and the
         * template still open beside it. Their box is 50x51 squares. */
        static const char* const apart[2] = { "m50_50", "m1_100" };
        static const char* const block[4] = { "m50_50", "m53_50", "m50_53", "m53_53" };
        static const char* const one[1] = { "m50_50" };
        char why[256];
        why[0] = '\0';
        CHECK(Editor_InstancePlanInit(&plan, one, 1) && Editor_InstanceSourceCompact(&plan, why, sizeof(why)),
            "one square is a block");
        CHECK(Editor_InstancePlanInit(&plan, block, 4) && plan.w == 4 && plan.h == 4
                  && Editor_InstanceSourceCompact(&plan, why, sizeof(why)),
            "four corners of a 4x4 box are one block (16 squares)");
        CHECK(Editor_InstancePlanInit(&plan, apart, 2) && !Editor_InstanceSourceCompact(&plan, why, sizeof(why)),
            "m50_50 with m1_100 is not one block");
        CHECK(strstr(why, "span 50x51 (m1_50 .. m50_100)") != NULL && strstr(why, "Squares picker") != NULL,
            "and says the box and where to pick a block (%s)", why);
        CHECK(strlen(why) < 160, "in a sentence the dialog's row holds (%u bytes)", (unsigned)strlen(why));
    }
    CHECK(!Editor_InstancePlanInit(&plan, bad, 1), "a bad key refuses the plan");
    CHECK(!Editor_InstancePlanInit(&plan, twice, 2), "one place named twice refuses the plan");
    CHECK(Editor_InstancePlanInit(&plan, keys, 2), "two squares, one with its storage");
    CHECK(plan.src_x0 == 50 && plan.src_z0 == 50 && plan.w == 1 && plan.h == 2, "the box is m50_50 1x2 (%d %d %dx%d)",
        plan.src_x0, plan.src_z0, plan.w, plan.h);
    CHECK(plan.sources[1].map_x == 50 && plan.sources[1].map_z == 51
              && strcmp(plan.sources[1].key, "ported/tutorial/maps/m50_51") == 0,
        "the storage key keeps its storage and parses its coordinates");
    plan.dst_x0 = 16;
    plan.dst_z0 = 101;
    Editor_InstanceDest(&plan, 1, &x, &z);
    CHECK(x == 16 && z == 102, "m50_51 lands at m16_102 (m%d_%d)", x, z);

    CHECK(Editor_InstanceRebaseTile(&plan, 50 * 64 + 10, 51 * 64 + 63, &x, &z) && x == 16 * 64 + 10
              && z == 102 * 64 + 63,
        "a tile keeps its local x, z (%d,%d)", x, z);
    CHECK(!Editor_InstanceRebaseTile(&plan, 51 * 64, 50 * 64, &x, &z), "a tile of m51_50 is outside the source");
    CHECK(!Editor_InstanceRebaseTile(&plan, -1, 0, &x, &z), "a negative tile is outside");

    /* Lands at / Npc at: a template tile, a source tile, or an offset in the block all mean one template tile. */
    CHECK(Editor_InstanceTemplateTile(&plan, 16 * 64 + 5, 102 * 64 + 6, &x, &z) && x == 16 * 64 + 5
              && z == 102 * 64 + 6,
        "a template tile is taken as typed (%d,%d)", x, z);
    CHECK(Editor_InstanceTemplateTile(&plan, 50 * 64 + 32, 50 * 64 + 20, &x, &z) && x == 16 * 64 + 32
              && z == 101 * 64 + 20,
        "a source tile moves with the copy (%d,%d)", x, z);
    CHECK(Editor_InstanceTemplateTile(&plan, 32, 84, &x, &z) && x == 16 * 64 + 32 && z == 102 * 64 + 20,
        "an offset in the 1x2 block counts from the template's south-west (%d,%d)", x, z);
    CHECK(!Editor_InstanceTemplateTile(&plan, 64, 20, &x, &z), "an offset past the block's width is refused");
    CHECK(!Editor_InstanceTemplateTile(&plan, 51 * 64 + 3, 50 * 64 + 3, &x, &z), "a tile of m51_50 is refused");

    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "goblin");
    row.abs_x = 3205;
    row.abs_z = 3266;
    row.level = 1;
    row.count = 1;
    CHECK(Editor_InstanceRebaseSpawn(&plan, &row, &out) && out.abs_x == 3205 - 34 * 64 && out.abs_z == 3266 + 51 * 64
              && out.level == 1 && strcmp(out.name, "goblin") == 0,
        "a spawn moves by the square offset, level and name kept (%d,%d L%d)", out.abs_x, out.abs_z, out.level);
    row.suppress = 1;
    CHECK(!Editor_InstanceRebaseSpawn(&plan, &row, &out), "a suppression row is not copied");
    row.suppress = 0;
    plan.level_lo = plan.level_hi = 0;
    CHECK(!Editor_InstanceRebaseSpawn(&plan, &row, &out), "a level-1 spawn is dropped when only level 0 is copied");
    row.level = 0;
    CHECK(Editor_InstanceRebaseSpawn(&plan, &row, &out), "a level-0 one is not");
    row.abs_x = 0;
    CHECK(!Editor_InstanceRebaseSpawn(&plan, &row, &out), "a spawn outside every source square is not copied");

    Editor_InstanceEntryTile(&plan, &level, &x, &z);
    CHECK(level == 0 && x == 16 * 64 + 32 && z == 101 * 64 + 32, "the entry is the middle of the first destination");
    Editor_InstanceCoordText(level, x, z, coord);
    CHECK(strcmp(coord, "0_16_101_32_32") == 0, "its coord is 0_16_101_32_32 (%s)", coord);
    CHECK(Editor_InstanceCoordParse("0_16_101_32_32", &level, &x, &z) && level == 0 && x == 16 * 64 + 32
              && z == 101 * 64 + 32,
        "the coord parses back");
    CHECK(Editor_InstanceCoordParse("1056 6496 2", &level, &x, &z) && level == 2 && x == 1056 && z == 6496,
        "x z level parses");
    CHECK(Editor_InstanceCoordParse("1056,6496", &level, &x, &z) && level == 0 && x == 1056,
        "x,z parses at level 0");
    CHECK(!Editor_InstanceCoordParse("0_16_101_64_0", &level, &x, &z), "a local 64 is refused");
    CHECK(!Editor_InstanceCoordParse("4_16_101_0_0", &level, &x, &z), "level 4 is refused");
    CHECK(!Editor_InstanceCoordParse("16_101", &level, &x, &z), "two underscored numbers are refused");
    CHECK(!Editor_InstanceCoordParse("1056", &level, &x, &z), "one number is refused");
    CHECK(!Editor_InstanceCoordParse("1056 6496 x", &level, &x, &z), "a word is refused");

    CHECK(Editor_InstanceNameValid("tutorial_cave") && !Editor_InstanceNameValid("Tutorial")
              && !Editor_InstanceNameValid("") && !Editor_InstanceNameValid("a-b"),
        "names are [a-z0-9_]");
    CHECK(Editor_InstanceLocNameValid("cave_entrance") && !Editor_InstanceLocNameValid("cave entrance"),
        "loc names are [a-z0-9_]");
}

static void
test_copy_commands(void)
{
    static struct Editor_Square created;
    static struct Editor_Square source;
    static const char* const keys[1] = { "m50_50" };
    struct Editor_Loc locs[3] = {
        { 1902, 0, 1, 0, 10, 10 },
        { 1903, 22, 0, 1, 10, 10 },
        { 2097, 10, 0, 2, 12, 11 },
    };
    struct Editor_InstancePlan plan;
    struct Editor_Cmd commands[16];
    int n;

    memset(&created, 0, sizeof(created));
    created.map_x = 16;
    created.map_z = 101;
    created.locs = locs;
    created.loc_count = 3;
    created.tiles[Editor_TileIndex(1, 1, 0)].has_height = 1;
    created.tiles[Editor_TileIndex(1, 1, 0)].height = 5;
    created.tiles[Editor_TileIndex(2, 2, 1)].underlay_id = 3;
    created.tiles[Editor_TileIndex(3, 3, 3)].underlay_id = 4;
    source = created;
    source.map_x = 50;
    source.map_z = 50;

    CHECK(Editor_InstancePlanInit(&plan, keys, 1), "a one-square plan");
    CHECK(Editor_InstanceTemplate(EDITOR_INSTANCE_COPY_TERRAIN) == EDITOR_DOC_TEMPLATE_COPY
              && Editor_InstanceTemplate(EDITOR_INSTANCE_COPY_LOCS) == EDITOR_DOC_TEMPLATE_EMPTY,
        "terrain copies by the copy template, locs alone start empty");

    n = Editor_InstanceCopyCommands(&plan, EDITOR_INSTANCE_COPY_TERRAIN | EDITOR_INSTANCE_COPY_LOCS, &created, NULL,
        commands, 16);
    CHECK(n == 0, "terrain and locs, every level: the copy is already right (%d)", n);

    n = Editor_InstanceCopyCommands(&plan, EDITOR_INSTANCE_COPY_TERRAIN, &created, NULL, commands, 16);
    CHECK(n == 3, "terrain alone: the three locs removed (%d)", n);
    CHECK(n == 3 && commands[0].kind == EDITOR_CMD_LOC && commands[0].has_before && !commands[0].has_after
              && commands[0].u.loc.before.loc_id == 1902 && strcmp(commands[0].doc_key, "m16_101") == 0,
        "each a LOC remove on m16_101");

    plan.level_lo = plan.level_hi = 1;
    n = Editor_InstanceCopyCommands(&plan, EDITOR_INSTANCE_COPY_TERRAIN | EDITOR_INSTANCE_COPY_LOCS, &created, NULL,
        commands, 16);
    CHECK(n == 4, "level 1 only: two tiles cleared (levels 0 and 3) and two locs removed (levels 0, 2) (%d)", n);
    CHECK(n == 4 && commands[0].kind == EDITOR_CMD_TILE && commands[0].u.tile.level == 0 && commands[0].has_before
              && commands[0].u.tile.before.height == 5 && !commands[0].u.tile.after.has_height
              && commands[1].u.tile.level == 3 && commands[2].kind == EDITOR_CMD_LOC
              && commands[2].u.loc.before.level == 0 && commands[3].u.loc.before.level == 2,
        "tiles first, then the locs, each carrying what it clears");
    CHECK(Editor_InstanceCopyCommands(&plan, EDITOR_INSTANCE_COPY_TERRAIN | EDITOR_INSTANCE_COPY_LOCS, &created, NULL,
              NULL, 0)
              == 4,
        "counting writes nothing and says the same");

    plan.level_lo = 0;
    plan.level_hi = 3;
    memset(&created, 0, sizeof(created));
    created.map_x = 16;
    created.map_z = 101;
    n = Editor_InstanceCopyCommands(&plan, EDITOR_INSTANCE_COPY_LOCS, &created, &source, commands, 16);
    CHECK(n == 3 && commands[0].has_after && !commands[0].has_before && commands[2].u.loc.after.loc_id == 2097
              && commands[2].u.loc.after.x == 12 && strcmp(commands[2].doc_key, "m16_101") == 0,
        "locs alone: the source's three added onto the empty square at their local tiles (%d)", n);
    CHECK(Editor_InstanceCopyCommands(&plan, 0, &created, NULL, commands, 16) == 0, "nothing copied: nothing to do");
}

/** `needle` in `text` exactly `times` times. */
static int
count_of(
    const char* text,
    const char* needle)
{
    int n = 0;
    for( const char* at = strstr(text, needle); at; at = strstr(at + 1, needle) )
        n++;
    return n;
}

static void
test_trigger(void)
{
    static const char* const keys[2] = { "m50_50", "m50_51" };
    struct Editor_InstancePlan plan;
    struct Editor_InstanceScript script;
    char key[EDITOR_DOC_KEY_MAX];
    char text[8192];
    char where[EDITOR_INSTANCE_WHERE_MAX];
    char stamp[EDITOR_INSTANCE_WHERE_MAX];
    char status[EDITOR_INSTANCE_WHERE_MAX];
    size_t n;

    Editor_InstancePlanInit(&plan, keys, 2);
    plan.dst_x0 = 16;
    plan.dst_z0 = 101;
    Editor_InstanceTriggerKey("tutorial_cave", key);
    CHECK(strcmp(key, "rs2:edited/tutorial_cave/scripts/tutorial_cave.rs2") == 0, "the trigger's key (%s)", key);

    /* A teleport: everyone into the template itself. */
    memset(&script, 0, sizeof(script));
    script.entry = EDITOR_INSTANCE_ENTRY_TELEPORT;
    snprintf(script.loc, sizeof(script.loc), "cave_entrance");
    script.level = 0;
    script.x = 16 * 64 + 32;
    script.z = 101 * 64 + 32;
    n = Editor_InstanceTriggerText(&plan, "tutorial_cave", &script, text, sizeof(text));
    CHECK(n == strlen(text), "the text fits and its length is said");
    CHECK(strstr(text, "\n[oploc1,cave_entrance]\np_telejump(0_16_101_32_32);\n") != NULL,
        "teleport: [oploc1,cave_entrance] p_telejump(0_16_101_32_32)");
    CHECK(strncmp(text, "// server/scripts/edited/tutorial_cave/scripts/tutorial_cave.rs2: written by the ToriRSEd "
                        "instance helper",
              80)
              == 0,
        "the header names the file and its writer");
    CHECK(strstr(text, "Template   m16_101..m16_102 (1 x 2 squares), in instance space") != NULL
              && strstr(text, "Scenes > instance:tutorial_cave") != NULL,
        "the header says where the template is and how to open it");
    CHECK(strstr(text, "teleports every player into the template itself") != NULL,
        "and that a teleport's room is shared");
    CHECK(strstr(text, "~map_instance") == NULL && strstr(text, "$handle") == NULL,
        "a teleport calls no instance proc");
    CHECK(Editor_InstanceTriggerText(&plan, "tutorial_cave", &script, NULL, 0) == n,
        "sizing with no buffer says the same length");
    {
        /* snprintf's contract: a short buffer is filled and terminated, and
         * the length is still the whole text's. */
        char small[64];
        CHECK(Editor_InstanceTriggerText(&plan, "tutorial_cave", &script, small, sizeof(small)) == n
                  && strlen(small) == sizeof(small) - 1 && strncmp(small, text, sizeof(small) - 1) == 0,
            "a short buffer holds the text's start and the whole length is said");
    }

    /* A teleport's exit: another loc, straight home. */
    snprintf(script.exit_loc, sizeof(script.exit_loc), "cave_rope");
    script.exit_level = 0;
    script.exit_x = 3222;
    script.exit_z = 3218;
    n = Editor_InstanceTriggerText(&plan, "tutorial_cave", &script, text, sizeof(text));
    CHECK(n == strlen(text) && strstr(text, "\n[oploc1,cave_rope]\np_telejump(0_50_50_22_18);\n") != NULL,
        "a teleport's exit is [oploc1,cave_rope] p_telejump(0_50_50_22_18)");
    CHECK(strstr(text, "~map_instance_release_here") == NULL, "with no copy to hand back");

    /* The runtime copy: the tree's own procs, not chunk loops. */
    memset(&script, 0, sizeof(script));
    script.entry = EDITOR_INSTANCE_ENTRY_RUNTIME;
    snprintf(script.loc, sizeof(script.loc), "cave_entrance");
    script.level = 0;
    script.x = 16 * 64 + 32;
    script.z = 101 * 64 + 32;
    n = Editor_InstanceTriggerText(&plan, "tutorial_cave", &script, text, sizeof(text));
    CHECK(n == strlen(text), "the runtime text fits");
    CHECK(strstr(text, "def_int $handle = ~map_instance_from_square_block(0_16_101_0_0, 1, 2);\n") != NULL,
        "runtime: the template's south-west coord and its 1 x 2 squares to ~map_instance_from_square_block");
    CHECK(strstr(text, "map_instance_alloc") == NULL && strstr(text, "map_instance_setchunk") == NULL
              && strstr(text, "map_instance_build") == NULL,
        "no raw alloc / setchunk / build");
    CHECK(strstr(text, "if ($handle = ^map_instance_none) {\n"
                       "    mes(\"There is no room for another instance just now.\");\n"
                       "    return;\n"
                       "}\n"
                       "%varp6600_map_instance_handle = $handle;\n"
                       "p_telejump(map_instance_coord($handle, 32, 32, 0));\n")
              != NULL,
        "the no-room refusal, the handle kept in %%varp6600_map_instance_handle, the entry tile's offset");
    CHECK(strstr(text, "[oploc1,cave_entrance]\nif (map_instance_find(coord) ! ^map_instance_none) {\n") != NULL,
        "entering from inside a copy is refused (the template holds the loc too)");
    CHECK(count_of(text, "[oploc1,") == 1 && strstr(text, "npc_add") == NULL
              && strstr(text, "~map_instance_release_here;") == NULL,
        "no exit and no npc were asked: one trigger, neither block");
    CHECK(strstr(text, "each player who enters gets a private copy stamped from the template") != NULL
              && strstr(text, "south-west corner 0_16_101_0_0") != NULL,
        "the header says how the copy is stamped and where its offsets count from");
    CHECK(strstr(text, "map_instance_spawnnpcs") == NULL, "spawns not copied into the template: none stamped");
    /* Spawns copied into the template: each copy gets them too. */
    script.spawns = 1;
    n = Editor_InstanceTriggerText(&plan, "tutorial_cave", &script, text, sizeof(text));
    CHECK(n == strlen(text)
              && strstr(text, "p_telejump(map_instance_coord($handle, 32, 32, 0));\n"
                              "// The template's own npcs (its spawn rows) stand in this copy too, each on its\n"
                              "// tile; they go with the copy. Delete the line for an empty copy.\n"
                              "map_instance_spawnnpcs($handle);\n")
                     != NULL
              && strstr(text, "every level),\n//              and its npcs (map_instance_spawnnpcs, below).\n")
                     != NULL,
        "spawns copied: the copy's npcs stood with map_instance_spawnnpcs, once the player is in");
    script.spawns = 0;

    /* The exit on a loc of its own, and an npc on entering. */
    snprintf(script.exit_loc, sizeof(script.exit_loc), "cave_rope");
    script.exit_level = 0;
    script.exit_x = 3222;
    script.exit_z = 3218;
    snprintf(script.npc, sizeof(script.npc), "cave_guardian");
    script.npc_level = 0;
    script.npc_x = 16 * 64 + 32;
    script.npc_z = 101 * 64 + 34;
    n = Editor_InstanceTriggerText(&plan, "tutorial_cave", &script, text, sizeof(text));
    CHECK(n == strlen(text) && n < sizeof(text), "the full text fits (%u bytes)", (unsigned)n);
    CHECK(strstr(text, "// SPAWN ON ENTER: one cave_guardian on template tile 0_16_101_32_34") != NULL
              && strstr(text, "\nnpc_add(map_instance_coord($handle, 32, 34, 0), cave_guardian, 0);\n") != NULL,
        "the npc is added at its offset in the copy, in a block of its own");
    CHECK(strstr(text, "p_telejump(map_instance_coord($handle, 32, 32, 0));\n\n// SPAWN ON ENTER") != NULL,
        "after the player is in");
    CHECK(strstr(text, "\n[oploc1,cave_rope]\n"
                       "if (map_instance_find(coord) = ^map_instance_none) {\n")
              != NULL
              && strstr(text, "}\n~map_instance_release_here;\np_telejump(0_50_50_22_18);\n") != NULL,
        "the exit: outside a copy nothing; inside, the tree's release proc, then home");
    CHECK(count_of(text, "[oploc1,") == 2, "two triggers (%d)", count_of(text, "[oploc1,"));
    CHECK(strstr(text, "// ENTER: the first option of cave_entrance.") != NULL
              && strstr(text, "// EXIT: the first option of cave_rope, standing inside a copy.") != NULL,
        "each block is headed by what it is");

    /* The one loc both ways: its copy inside the instance is the way out. */
    snprintf(script.exit_loc, sizeof(script.exit_loc), "cave_entrance");
    script.npc[0] = '\0';
    n = Editor_InstanceTriggerText(&plan, "tutorial_cave", &script, text, sizeof(text));
    CHECK(n == strlen(text) && count_of(text, "[oploc1,") == 1, "exit loc = trigger loc: one trigger (%d)",
        count_of(text, "[oploc1,"));
    CHECK(strstr(text, "[oploc1,cave_entrance]\n"
                       "if (map_instance_find(coord) ! ^map_instance_none) {\n")
              != NULL
              && strstr(text, "    ~map_instance_release_here;\n    p_telejump(0_50_50_22_18);\n    return;\n}\n")
                     != NULL
              && strstr(text, "// ENTER.\ndef_int $handle = ~map_instance_from_square_block(0_16_101_0_0, 1, 2);\n")
                     != NULL,
        "inside a copy it leaves, outside it enters");

    /* Where things are, for the dialog and the status bar. */
    Editor_InstanceWhereText(&plan, "tutorial_cave", EDITOR_INSTANCE_ENTRY_RUNTIME, where, stamp);
    CHECK(strcmp(where, "Template: m16_101..m16_102 (1 x 2 squares) in instance space. Open it from Squares > "
                        "Instance space, or Scenes > instance:tutorial_cave.")
              == 0,
        "where the template is (%s)", where);
    CHECK(strcmp(stamp, "At play time each player who enters gets a private copy stamped from the template; edits "
                        "to the template show on the next entry.")
              == 0,
        "what happens at play time (%s)", stamp);
    Editor_InstanceWhereText(&plan, "tutorial_cave", EDITOR_INSTANCE_ENTRY_TELEPORT, where, stamp);
    CHECK(strstr(stamp, "one shared room") != NULL, "a teleport's room is shared (%s)", stamp);
    Editor_InstanceWhereText(&plan, "tutorial_cave", EDITOR_INSTANCE_ENTRY_NONE, where, stamp);
    CHECK(strstr(stamp, "Nothing brings a player there yet") != NULL, "no entry says so (%s)", stamp);

    plan.count = 1;
    plan.w = 1;
    plan.h = 1;
    plan.dst_x0 = 0;
    plan.dst_z0 = 100;
    Editor_InstanceStatusText(&plan, "cellar", EDITOR_INSTANCE_ENTRY_RUNTIME, status);
    CHECK(strcmp(status, "Template m0_100: open it from Squares > Instance space or Scenes > instance:cellar. Each "
                         "player who enters gets a private copy stamped from it.")
              == 0,
        "the status line says both (%s)", status);
    Editor_InstanceStatusText(&plan, "a_very_long_instance_name_that_fills_the_line_x", EDITOR_INSTANCE_ENTRY_RUNTIME,
        status);
    CHECK(strcmp(status, "Template m0_100: open it from Squares > Instance space or Scenes > "
                         "instance:a_very_long_instance_name_that_fills_the_line_x.")
              == 0,
        "a name that leaves no room keeps the first sentence whole (%s)", status);
}

static const char k_wma[] = "// a hand-written area file\r\n"
                            "\r\n"
                            "[tutorial_cave]\r\n"
                            "internal=tutorial_cave\r\n"
                            "external=Tutorial cave\r\n"
                            "origin=1\r\n"
                            "main=no\r\n"
                            "// two sections\r\n"
                            "section1=0,0,4,16,101,16,102,0,0,0,0,16,101,16,102,0,0,0\r\n"
                            "section1_dst_chunk_y_high=0\r\n"
                            "section2=1,0,1,18,110,0,0,0,0,7,7,18,110,0,0,0,0,7\r\n"
                            "[surface]\n"
                            "main=yes\n"
                            "zoom=4";

static void
test_wma(void)
{
    struct Editor_WmaFile file;
    struct Editor_WmaParseResult result;
    char out[2048];
    char text[2048];
    size_t n;

    Editor_WmaInit(&file);
    result = Editor_WmaParse(&file, k_wma, strlen(k_wma));
    CHECK(result.ok, "the hand-written file parses (line %d)", result.line);
    n = Editor_WmaEmit(&file, out, sizeof(out));
    CHECK(n == strlen(k_wma) && memcmp(out, k_wma, n) == 0, "emit is byte-identical (CRLF, comments, no final newline)");
    CHECK(file.area_count == 2 && strcmp(file.areas[0].external, "Tutorial cave") == 0 && !file.areas[0].is_main
              && file.areas[1].is_main && file.areas[0].section_count == 2,
        "two areas, the first's display name and sections, the second main");
    CHECK(Editor_WmaFindArea(&file, "surface") == 1 && Editor_WmaFindArea(&file, "nowhere") < 0, "areas by name");
    CHECK(Editor_WmaAreaClaims(&file.areas[0], 16, 102) && Editor_WmaAreaClaims(&file.areas[0], 18, 110)
              && !Editor_WmaAreaClaims(&file.areas[0], 17, 101) && !Editor_WmaAreaClaims(&file.areas[0], 18, 111),
        "a type-0 section claims its source rectangle, another type its one source square");
    CHECK(Editor_WmaEmit(&file, NULL, 0) == n, "sizing says the same length");

    {
        static const char* const bad[] = {
            "[a]\nsection1=0,0,4\n",
            "[a]\nmain=maybe\n",
            "[a]\n[a]\n",
            "[]\n",
            "[a]\nsection1=0,0,4,16,101,16,102,0,0,0,0,16,101,16,102,0,0,0,9\n",
            "[a]\nsection1=0,0,4,16,101,16,102,0,0,0,0,16,101,16,102,0,0,x\n",
        };
        static const int lines[] = { 2, 2, 2, 1, 2, 2 };
        for( size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++ )
        {
            result = Editor_WmaParse(&file, bad[i], strlen(bad[i]));
            n = Editor_WmaEmit(&file, out, sizeof(out));
            CHECK(!result.ok && result.line == lines[i] && n == strlen(bad[i]) && memcmp(out, bad[i], n) == 0,
                "refused on line %d and still emitted as read: %s", lines[i], bad[i]);
        }
    }

    n = Editor_WmaNewAreaText("tutorial_cave", "Tutorial cave", 16, 101, 16, 102, 0, 4, text, sizeof(text));
    CHECK(n == strlen(text) && Editor_WmaNewAreaText("tutorial_cave", "Tutorial cave", 16, 101, 16, 102, 0, 4, NULL, 0)
                                   == n,
        "a new area's text, sized");
    result = Editor_WmaParse(&file, text, n);
    CHECK(result.ok && file.area_count == 1 && strcmp(file.areas[0].name, "tutorial_cave") == 0
              && strcmp(file.areas[0].external, "Tutorial cave") == 0 && file.areas[0].section_count == 1,
        "it parses back to one area with one section");
    CHECK(Editor_WmaAreaClaims(&file.areas[0], 16, 101) && Editor_WmaAreaClaims(&file.areas[0], 16, 102)
              && !Editor_WmaAreaClaims(&file.areas[0], 16, 103) && !Editor_WmaAreaClaims(&file.areas[0], 15, 101),
        "claiming exactly m16_101 and m16_102");
    CHECK(strstr(text, "\norigin=17308032\n") != NULL,
        "its origin is the middle tile packed: 1056 << 14 | 6528");
    n = Editor_WmaEmit(&file, out, sizeof(out));
    CHECK(n == strlen(text) && memcmp(out, text, n) == 0, "and emits byte-identical");
    {
        char key[EDITOR_DOC_KEY_MAX];
        Editor_WmaDocKey(key, sizeof(key), "tutorial_cave");
        CHECK(strcmp(key, "wma:tutorial_cave") == 0, "the document key (%s)", key);
    }
    Editor_WmaFree(&file);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    test_find_free();
    test_rebase();
    test_copy_commands();
    test_trigger();
    test_wma();
    printf("editor_instance_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
