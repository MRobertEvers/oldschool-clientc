/*
 * The SCENES codec (editor/editor_scenes.h): named snapshots of the session,
 * line-preserving.
 *
 *   ./editor_scenes_test
 *
 * No content tree: the file is small and authored by the editor, so the
 * fixtures are written here, including the hand-formatted shapes a person
 * leaves behind (extra spaces, CRLF, a comment between rows, segments spread
 * over lines in another order, no final newline).
 */

#include "editor/editor_scenes.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"

#include <assert.h>
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

static char g_emit[16384];

static const char*
emit(const struct Editor_ScenesFile* file)
{
    size_t const n = Editor_ScenesEmit(file, g_emit, sizeof(g_emit));
    if( n >= sizeof(g_emit) )
    {
        printf("FAIL: a fixture outgrew the emit buffer\n");
        exit(1);
    }
    return g_emit;
}

static int
parse(
    struct Editor_ScenesFile* file,
    const char* text)
{
    struct Editor_ScenesParseResult r = Editor_ScenesParse(file, text, strlen(text));
    return r.ok ? 0 : r.line;
}

/* The design's example, a hand-written one with its segments elsewhere, and
 * the shapes a person leaves: CRLF, extra spaces, a comment between rows, no
 * final newline. */
static const char k_fixture[] =
    "# ToriRSMapEd scenes (editor/scenes.txt). One record per line, |-separated segments.\n"
    "scene lumbridge-smithy | squares m50_50 m50_51 | camera 3222.5,3218,1,512,400,1800\n"
    "scene lumbridge-smithy | level 1 above | lane working | preview varb1200=1 | account maped43620 at 3224,3219,1\n"
    "scene lumbridge-smithy | note Fitting the forge walls; the anvil loc is a stamp\n"
    "\n"
    "// a cave nobody maps\r\n"
    "scene   tutorial-cave |  note  lit by the trigger | squares ported/tutorial/maps/m16_101\r\n"
    "scene cooks-kitchen | level 0 | preview varp29=11 varb4153_golem=-2\n"
    "scene cooks-kitchen | squares m50_50";

static void
test_round_trip(void)
{
    struct Editor_ScenesFile file;
    struct Editor_Scene scene;
    static const char* const k_shapes[] = {
        "",
        "\n",
        "# only a comment",
        "scene a\n",
        "scene a\r\nscene b | note x\r\n",
        "\n\n\nscene a | level auto\n\n",
    };

    Editor_ScenesInit(&file);
    CHECK(parse(&file, k_fixture) == 0, "the fixture parses");
    CHECK(strcmp(emit(&file), k_fixture) == 0, "parse -> emit is byte-identical");
    CHECK(Editor_ScenesCount(&file) == 3, "three scenes (%d)", Editor_ScenesCount(&file));
    CHECK(strcmp(Editor_ScenesName(&file, 0), "lumbridge-smithy") == 0
              && strcmp(Editor_ScenesName(&file, 1), "tutorial-cave") == 0
              && strcmp(Editor_ScenesName(&file, 2), "cooks-kitchen") == 0 && Editor_ScenesName(&file, 3) == NULL,
        "in the order of their first lines");

    CHECK(Editor_ScenesGet(&file, "lumbridge-smithy", &scene), "the smithy is there");
    CHECK(scene.segments == EDITOR_SCENE_SEG_ALL, "the union of its three lines holds every segment (%#x)",
        scene.segments);
    CHECK(scene.square_count == 2 && strcmp(scene.squares[0], "m50_50") == 0 && strcmp(scene.squares[1], "m50_51") == 0,
        "its squares");
    CHECK(scene.camera.x == 3222 * 128 + 64 && scene.camera.z == 3218 * 128 && scene.camera.level == 1
              && scene.camera.yaw == 512 && scene.camera.pitch == 400 && scene.camera.zoom == 1800,
        "its camera, in fine units (%d %d)", (int)scene.camera.x, (int)scene.camera.z);
    CHECK(scene.level == 1 && scene.above, "level 1 above");
    CHECK(strcmp(scene.lane, "working") == 0, "its lane");
    CHECK(scene.preview_count == 1 && strcmp(scene.preview[0].var, "varb1200") == 0 && scene.preview[0].value == 1,
        "its preview");
    CHECK(strcmp(scene.account, "maped43620") == 0 && scene.account_x == 3224 && scene.account_z == 3219
              && scene.account_level == 1,
        "its account");
    CHECK(strcmp(scene.note, "Fitting the forge walls; the anvil loc is a stamp") == 0, "its note");

    CHECK(Editor_ScenesGet(&file, "tutorial-cave", &scene)
              && scene.segments == (EDITOR_SCENE_SEG_NOTE | EDITOR_SCENE_SEG_SQUARES)
              && strcmp(scene.note, "lit by the trigger") == 0
              && strcmp(scene.squares[0], "ported/tutorial/maps/m16_101") == 0,
        "a hand-written line: segments in any order, spaces trimmed, a lane storage");
    CHECK(Editor_ScenesGet(&file, "cooks-kitchen", &scene) && scene.level == 0 && !scene.above
              && scene.preview_count == 2 && scene.preview[1].value == -2
              && strcmp(scene.preview[1].var, "varb4153_golem") == 0,
        "a negative preview value and a var's _name");
    CHECK(!Editor_ScenesGet(&file, "nowhere", &scene), "no such scene");

    for( size_t i = 0; i < sizeof(k_shapes) / sizeof(k_shapes[0]); i++ )
    {
        CHECK(parse(&file, k_shapes[i]) == 0, "shape %zu parses", i);
        CHECK(strcmp(emit(&file), k_shapes[i]) == 0, "shape %zu is byte-identical", i);
    }
    Editor_ScenesFree(&file);
}

static void
test_refusals(void)
{
    struct Editor_ScenesFile file;
    static const struct
    {
        const char* text;
        int line;
    } k_bad[] = {
        { "scene a | camera 1,2,3,4,5,6\nscene a | camera 1,2,3,4,5,6\n", 2 },
        { "scene a\nscene a\n", 2 },
        { "scene a | squares m50_50 | level 1\nscene a | level 2\n", 2 },
        { "scene a | squares m50_50 | squares m50_51\n", 1 },
        { "# fine\nscene a b | note x\n", 2 },
        { "scene a | camera 1,2,4,0,0,0\n", 1 },
        { "scene a | camera 1,2,0,2048,0,0\n", 1 },
        { "scene a | camera 1.12345678,2,0,0,0,0\n", 1 },
        { "scene a | camera 1,2,0,0,0\n", 1 },
        { "scene a | level 4\n", 1 },
        { "scene a | level 1 below\n", 1 },
        { "scene a | level auto above\n", 1 },
        { "scene a | lane Working\n", 1 },
        { "scene a | preview varx1=2\n", 1 },
        { "scene a | preview varp1\n", 1 },
        { "scene a | preview varp1=2 varp2=3 varp3=4 varp4=5 varp5=6 varp6=7\n", 1 },
        { "scene a | account bob 3,4,0\n", 1 },
        { "scene a | account bob at 3,4,5\n", 1 },
        { "scene a | squares 50_50\n", 1 },
        { "scene a | squares\n", 1 },
        { "scene a | weather rain\n", 1 },
        { "scenes a\n", 1 },
    };

    Editor_ScenesInit(&file);
    for( size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++ )
    {
        int const line = parse(&file, k_bad[i].text);
        CHECK(line == k_bad[i].line, "refusal %zu at line %d (got %d): %s", i, k_bad[i].line, line, k_bad[i].text);
    }
    Editor_ScenesFree(&file);
}

static void
test_format(void)
{
    struct Editor_Scene scene;
    struct Editor_Scene back;
    char lines[EDITOR_SCENE_FORMAT_LINES][EDITOR_SCENES_LINE_MAX + 1];
    unsigned masks[EDITOR_SCENE_FORMAT_LINES];
    char text[64];
    int n;

    /* The fine units: the fewest digits that read back exactly. */
    for( int32_t fine = 0; fine < 128 * 3; fine++ )
    {
        struct Editor_Scene row;
        char line[128];
        Editor_SceneFormatFine(fine, text, sizeof(text));
        snprintf(line, sizeof(line), "scene f | camera %s,0,0,0,0,0", text);
        CHECK(Editor_ScenesParseLine(line, &row) == EDITOR_SCENES_LINE_RECORD && row.camera.x == fine,
            "fine %d -> %s reads back (%d)", (int)fine, text, (int)row.camera.x);
    }
    Editor_SceneFormatFine(3222 * 128 + 64, text, sizeof(text));
    CHECK(strcmp(text, "3222.5") == 0, "half a tile is .5 (%s)", text);
    Editor_SceneFormatFine(3222 * 128 + 1, text, sizeof(text));
    CHECK(strcmp(text, "3222.0078125") == 0, "a fine unit is seven decimals (%s)", text);

    Editor_SceneInitRow(&scene, "lumbridge-smithy");
    scene.segments = EDITOR_SCENE_SEG_ALL;
    scene.square_count = 2;
    snprintf(scene.squares[0], sizeof(scene.squares[0]), "m50_50");
    snprintf(scene.squares[1], sizeof(scene.squares[1]), "m50_51");
    scene.camera.x = 3222 * 128 + 64;
    scene.camera.z = 3218 * 128;
    scene.camera.level = 1;
    scene.camera.yaw = 512;
    scene.camera.pitch = 400;
    scene.camera.zoom = 1800;
    scene.level = 1;
    scene.above = 1;
    snprintf(scene.lane, sizeof(scene.lane), "working");
    scene.preview_count = 1;
    snprintf(scene.preview[0].var, sizeof(scene.preview[0].var), "varb1200");
    scene.preview[0].value = 1;
    snprintf(scene.account, sizeof(scene.account), "maped43620");
    scene.account_x = 3224;
    scene.account_z = 3219;
    scene.account_level = 1;
    snprintf(scene.note, sizeof(scene.note), "Fitting the forge walls; the anvil loc is a stamp");
    CHECK(Editor_SceneValid(&scene), "the design's scene is valid");
    n = Editor_ScenesFormat(&scene, lines, masks);
    CHECK(n == 3, "written as three lines (%d)", n);
    CHECK(n == 3 && strcmp(lines[0], "scene lumbridge-smithy | squares m50_50 m50_51 | camera 3222.5,3218,1,512,400,1800") == 0,
        "where it looks: %s", lines[0]);
    CHECK(n == 3
              && strcmp(lines[1], "scene lumbridge-smithy | level 1 above | lane working | preview varb1200=1 | account "
                                  "maped43620 at 3224,3219,1")
                     == 0,
        "what it pins: %s", lines[1]);
    CHECK(n == 3 && strcmp(lines[2], "scene lumbridge-smithy | note Fitting the forge walls; the anvil loc is a stamp") == 0,
        "the note: %s", lines[2]);
    for( int i = 0; i < n; i++ )
        CHECK(Editor_ScenesParseLine(lines[i], &back) == EDITOR_SCENES_LINE_RECORD && back.segments == masks[i],
            "line %d parses back with its mask", i);

    Editor_SceneInitRow(&scene, "bare");
    n = Editor_ScenesFormat(&scene, lines, masks);
    CHECK(n == 1 && strcmp(lines[0], "scene bare") == 0 && masks[0] == 0, "a scene with nothing is its header");

    scene.segments = EDITOR_SCENE_SEG_LEVEL;
    scene.level = -1;
    scene.above = 1;
    CHECK(!Editor_SceneValid(&scene), "Above needs a pinned level");
    scene.above = 0;
    CHECK(Editor_SceneValid(&scene), "Auto is a level");
    scene.segments = EDITOR_SCENE_SEG_SQUARES;
    scene.square_count = EDITOR_SCENE_SQUARES_MAX;
    for( int i = 0; i < scene.square_count; i++ )
        snprintf(scene.squares[i], sizeof(scene.squares[i]), "ported/a_lane_with_a_long_name/maps/m%d_%d", 100 + i, 100);
    CHECK(Editor_SceneValid(&scene) && Editor_ScenesFormat(&scene, lines, masks) == 0,
        "sixteen long keys do not fit a line: refused, never cut");
    CHECK(!Editor_SceneNameValid("has space") && !Editor_SceneNameValid("#x") && !Editor_SceneNameValid("")
              && Editor_SceneNameValid("instance:tutorial-cave"),
        "names");
    CHECK(Editor_SceneVarValid("varp29") && Editor_SceneVarValid("varb4153_golem") && !Editor_SceneVarValid("varp")
              && !Editor_SceneVarValid("varpx") && !Editor_SceneVarValid("foo1"),
        "preview vars");

    {
        char summary[160];
        struct Editor_ScenesFile file;
        Editor_ScenesInit(&file);
        parse(&file, k_fixture);
        Editor_ScenesGet(&file, "lumbridge-smithy", &back);
        Editor_SceneSummary(&back, summary, sizeof(summary));
        CHECK(strcmp(summary, "m50_50 m50_51 | L1 above | working | preview varb1200=1 | maped43620") == 0,
            "the list's summary: %s", summary);
        Editor_ScenesFree(&file);
    }
}

/* Apply `cmd[0..n)` forward, then backward, checking the text at each end. */
static void
check_plan(
    const char* what,
    const char* start,
    const struct Editor_Cmd* cmd,
    int n,
    const char* want)
{
    struct Editor_ScenesFile file;
    int ok = 1;

    Editor_ScenesInit(&file);
    parse(&file, start);
    for( int i = 0; i < n; i++ )
        ok &= Editor_ScenesApply(&file, &cmd[i], 0);
    CHECK(ok, "%s: every command applies", what);
    CHECK(strcmp(emit(&file), want) == 0, "%s: the text after (%s)", what, g_emit);
    ok = 1;
    for( int i = n - 1; i >= 0; i-- )
        ok &= Editor_ScenesApply(&file, &cmd[i], 1);
    CHECK(ok, "%s: every command undoes", what);
    CHECK(strcmp(emit(&file), start) == 0, "%s: undone byte for byte (%s)", what, g_emit);
    Editor_ScenesFree(&file);
}

static void
test_plans(void)
{
    struct Editor_ScenesFile file;
    struct Editor_Scene scene;
    struct Editor_Cmd* cmd = calloc(EDITOR_SCENE_PLAN_MAX, sizeof(*cmd));
    char label[EDITOR_HISTORY_LABEL_MAX];
    char want[4096];
    int n;

    assert(cmd);
    Editor_ScenesInit(&file);

    /* A new scene into an empty file brings the header, and its undo takes
     * it back. */
    parse(&file, "");
    Editor_SceneInitRow(&scene, "door");
    scene.segments = EDITOR_SCENE_SEG_SQUARES | EDITOR_SCENE_SEG_LEVEL;
    scene.square_count = 1;
    snprintf(scene.squares[0], sizeof(scene.squares[0]), "m50_50");
    scene.level = 0;
    n = Editor_ScenePlanSave(cmd, EDITOR_SCENE_PLAN_MAX, &file, &scene);
    CHECK(n == 2, "save: two lines (%d)", n);
    snprintf(want, sizeof(want),
        "# ToriRSMapEd scenes (editor/scenes.txt). One record per line, |-separated segments.\n"
        "# scene <name> | squares <key> ... | camera <x>,<z>,<level>,<yaw>,<pitch>,<zoom>\n"
        "# scene <name> | level <0..3|auto> [above] | lane <name> | preview <var>=<value> ... | account <name> at "
        "<x>,<z>,<level>\n"
        "# scene <name> | note <text>\n"
        "scene door | squares m50_50\n"
        "scene door | level 0\n");
    check_plan("save into an empty file", "", cmd, n, want);
    Editor_ScenesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Save scene door") == 0, "label: %s", label);

    /* A new scene after the others. */
    parse(&file, k_fixture);
    n = Editor_ScenePlanSave(cmd, EDITOR_SCENE_PLAN_MAX, &file, &scene);
    snprintf(want, sizeof(want), "%s\nscene door | squares m50_50\nscene door | level 0", k_fixture);
    CHECK(n == 2, "save beside others (%d)", n);
    check_plan("a new scene at the end", k_fixture, cmd, n, want);

    /* Update in place: the smithy at level 2, nothing else. Its first line
     * is already what the editor writes and stays; the second is rewritten
     * (removed, then added after the first); the note stays. */
    Editor_ScenesGet(&file, "lumbridge-smithy", &scene);
    scene.level = 2;
    n = Editor_ScenePlanSave(cmd, EDITOR_SCENE_PLAN_MAX, &file, &scene);
    CHECK(n == 2 && cmd[0].kind == EDITOR_CMD_SCENE_REMOVE && cmd[1].kind == EDITOR_CMD_SCENE_SET,
        "update: one line out, its rewrite in (%d)", n);
    {
        const char* const k_after =
            "# ToriRSMapEd scenes (editor/scenes.txt). One record per line, |-separated segments.\n"
            "scene lumbridge-smithy | squares m50_50 m50_51 | camera 3222.5,3218,1,512,400,1800\n"
            "scene lumbridge-smithy | note Fitting the forge walls; the anvil loc is a stamp\n"
            "scene lumbridge-smithy | level 2 above | lane working | preview varb1200=1 | account maped43620 at "
            "3224,3219,1\n"
            "\n"
            "// a cave nobody maps\r\n"
            "scene   tutorial-cave |  note  lit by the trigger | squares ported/tutorial/maps/m16_101\r\n"
            "scene cooks-kitchen | level 0 | preview varp29=11 varb4153_golem=-2\n"
            "scene cooks-kitchen | squares m50_50";
        check_plan("update the smithy", k_fixture, cmd, n, k_after);
    }
    CHECK(Editor_ScenePlanSave(cmd, EDITOR_SCENE_PLAN_MAX, &file, &scene) > 0, "(still a change)");
    Editor_ScenesGet(&file, "lumbridge-smithy", &scene);
    CHECK(Editor_ScenePlanSave(cmd, EDITOR_SCENE_PLAN_MAX, &file, &scene) == 0, "update to the same: nothing to do");

    /* Update of a hand-written scene: its first line keeps its place. */
    Editor_ScenesGet(&file, "cooks-kitchen", &scene);
    scene.segments |= EDITOR_SCENE_SEG_NOTE;
    snprintf(scene.note, sizeof(scene.note), "the range");
    n = Editor_ScenePlanSave(cmd, EDITOR_SCENE_PLAN_MAX, &file, &scene);
    {
        const char* const k_after =
            "# ToriRSMapEd scenes (editor/scenes.txt). One record per line, |-separated segments.\n"
            "scene lumbridge-smithy | squares m50_50 m50_51 | camera 3222.5,3218,1,512,400,1800\n"
            "scene lumbridge-smithy | level 1 above | lane working | preview varb1200=1 | account maped43620 at "
            "3224,3219,1\n"
            "scene lumbridge-smithy | note Fitting the forge walls; the anvil loc is a stamp\n"
            "\n"
            "// a cave nobody maps\r\n"
            "scene   tutorial-cave |  note  lit by the trigger | squares ported/tutorial/maps/m16_101\r\n"
            "scene cooks-kitchen | squares m50_50\n"
            "scene cooks-kitchen | level 0 | preview varp29=11 varb4153_golem=-2\n"
            "scene cooks-kitchen | note the range";
        CHECK(n == 4, "a hand-written update: remove, rewrite the first, add two (%d)", n);
        check_plan("update the kitchen", k_fixture, cmd, n, k_after);
        (void)k_after;
    }

    /* Rename: every line, one step. */
    n = Editor_ScenePlanRename(cmd, EDITOR_SCENE_PLAN_MAX, &file, "tutorial-cave", "cave");
    CHECK(n == 1, "rename: one line (%d)", n);
    Editor_ScenesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Rename scene tutorial-cave to cave") == 0, "label: %s", label);
    {
        char after[4096];
        const char* old_line = "scene   tutorial-cave |  note  lit by the trigger | squares ported/tutorial/maps/m16_101\r";
        const char* new_line = "scene cave | squares ported/tutorial/maps/m16_101 | note lit by the trigger";
        char* at;
        snprintf(after, sizeof(after), "%s", k_fixture);
        at = strstr(after, old_line);
        assert(at);
        memmove(at + strlen(new_line), at + strlen(old_line), strlen(at + strlen(old_line)) + 1);
        memcpy(at, new_line, strlen(new_line));
        check_plan("rename the cave", k_fixture, cmd, n, after);
    }
    CHECK(Editor_ScenePlanRename(cmd, EDITOR_SCENE_PLAN_MAX, &file, "cave-x", "cave") == 0, "rename: no such scene");
    CHECK(Editor_ScenePlanRename(cmd, EDITOR_SCENE_PLAN_MAX, &file, "tutorial-cave", "cooks-kitchen") == 0,
        "rename: a taken name");
    CHECK(Editor_ScenePlanRename(cmd, EDITOR_SCENE_PLAN_MAX, &file, "tutorial-cave", "a b") == 0,
        "rename: not a name");

    /* Delete: every line, one step; the undo puts each back where it was. */
    n = Editor_ScenePlanDelete(cmd, EDITOR_SCENE_PLAN_MAX, &file, "lumbridge-smithy");
    CHECK(n == 3, "delete: three lines (%d)", n);
    Editor_ScenesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Delete scene lumbridge-smithy") == 0, "label: %s", label);
    {
        const char* const k_after =
            "# ToriRSMapEd scenes (editor/scenes.txt). One record per line, |-separated segments.\n"
            "\n"
            "// a cave nobody maps\r\n"
            "scene   tutorial-cave |  note  lit by the trigger | squares ported/tutorial/maps/m16_101\r\n"
            "scene cooks-kitchen | level 0 | preview varp29=11 varb4153_golem=-2\n"
            "scene cooks-kitchen | squares m50_50";
        check_plan("delete the smithy", k_fixture, cmd, n, k_after);
    }
    CHECK(Editor_ScenePlanDelete(cmd, EDITOR_SCENE_PLAN_MAX, &file, "nowhere") == 0, "delete: no such scene");

    /* The last scene of a file that is the header takes the header back. */
    parse(&file, "");
    Editor_SceneInitRow(&scene, "one");
    n = Editor_ScenePlanSave(cmd, EDITOR_SCENE_PLAN_MAX, &file, &scene);
    CHECK(n == 1, "save a bare scene (%d)", n);
    {
        int ok = Editor_ScenesApply(&file, &cmd[0], 0);
        n = Editor_ScenePlanDelete(cmd, EDITOR_SCENE_PLAN_MAX, &file, "one");
        ok &= n == 1 && Editor_ScenesApply(&file, &cmd[0], 0);
        CHECK(ok && strcmp(emit(&file), "") == 0, "deleting the last scene empties the file (%s)", g_emit);
    }

    /* A stale command is refused: the line's bytes moved under it. */
    parse(&file, k_fixture);
    n = Editor_ScenePlanDelete(cmd, EDITOR_SCENE_PLAN_MAX, &file, "cooks-kitchen");
    parse(&file, "scene cooks-kitchen | level 1 | preview varp29=11 varb4153_golem=-2\nscene cooks-kitchen | squares m50_50");
    CHECK(n == 2 && !Editor_ScenesApply(&file, &cmd[0], 0), "a remove whose line changed is refused");
    /* And a set whose segment another line holds. */
    {
        struct Editor_Cmd* set = &cmd[4];
        struct Editor_ScenesFile other;
        Editor_ScenesInit(&other);
        parse(&other, "scene x | level 1\n");
        Editor_SceneInitRow(&scene, "x");
        scene.segments = EDITOR_SCENE_SEG_SQUARES;
        scene.square_count = 1;
        snprintf(scene.squares[0], sizeof(scene.squares[0]), "m1_1");
        scene.segments |= EDITOR_SCENE_SEG_NOTE;
        snprintf(scene.note, sizeof(scene.note), "n");
        CHECK(Editor_ScenePlanSave(set, 1, &other, &scene) == 0, "a plan that needs two commands does not fit in one");
        scene.segments &= ~(unsigned)EDITOR_SCENE_SEG_NOTE;
        parse(&other, "");
        CHECK(Editor_ScenePlanSave(set, 1, &other, &scene) == 1, "into an empty file: one SET");
        parse(&other, "scene x | squares m2_2 | level 0\n");
        CHECK(!Editor_ScenesApply(&other, set, 0), "its squares are held by another line of x: refused");
        Editor_ScenesFree(&other);
    }
    Editor_ScenesFree(&file);
    free(cmd);
}

static void
test_wire(void)
{
    struct Editor_ScenesFile file;
    struct Editor_Cmd* cmd = calloc(EDITOR_SCENE_PLAN_MAX, sizeof(*cmd));
    struct Editor_Scene scene;

    assert(cmd);
    Editor_ScenesInit(&file);
    parse(&file, k_fixture);
    for( int k = 0; k < 2; k++ )
    {
        struct ToriRSMapEdBuf buf;
        int made;

        if( k == 0 )
        {
            Editor_ScenesGet(&file, "lumbridge-smithy", &scene);
            scene.level = 3;
            made = Editor_ScenePlanSave(cmd, EDITOR_SCENE_PLAN_MAX, &file, &scene) == 2;
            cmd[0] = cmd[1];
        }
        else
            made = Editor_ScenePlanDelete(cmd, EDITOR_SCENE_PLAN_MAX, &file, "tutorial-cave") == 1;
        CHECK(made, "kind %d made", k);
        CHECK(cmd[0].kind == (k == 0 ? EDITOR_CMD_SCENE_SET : EDITOR_CMD_SCENE_REMOVE)
                  && cmd[0].doc_kind == EDITOR_DOC_SCENES && strcmp(cmd[0].doc_key, "scenes") == 0,
            "kind %d addressed to the scenes", k);
        memset(&buf, 0, sizeof(buf));
        ToriRSMapEd_CmdEncode(&cmd[0], &buf);
        CHECK(ToriRSMapEd_CmdDecode(ToriRSMapEd_BufPeek(&buf), (uint32_t)ToriRSMapEd_BufAvailable(&buf), &cmd[1])
                  == TORIRSMAPED_CMD_DECODE_OK,
            "kind %d decodes", k);
        CHECK(memcmp(&cmd[0], &cmd[1], sizeof(cmd[0])) == 0, "kind %d round-trips the wire", k);
        CHECK(ToriRSMapEd_CmdDecode(ToriRSMapEd_BufPeek(&buf), (uint32_t)ToriRSMapEd_BufAvailable(&buf) - 1, &cmd[1])
                  == TORIRSMAPED_CMD_DECODE_MALFORMED,
            "kind %d truncated is malformed", k);
        ToriRSMapEd_BufFree(&buf);
    }
    CHECK(Editor_CmdKindFits(EDITOR_DOC_SCENES, EDITOR_CMD_SCENE_SET)
              && Editor_CmdKindFits(EDITOR_DOC_SCENES, EDITOR_CMD_SCENE_REMOVE)
              && !Editor_CmdKindFits(EDITOR_DOC_SCENES, EDITOR_CMD_PALETTE_SET)
              && !Editor_CmdKindFits(EDITOR_DOC_PALETTES, EDITOR_CMD_SCENE_SET),
        "SCENE_* fit the SCENES document only");
    Editor_ScenesFree(&file);
    free(cmd);
}

int
main(void)
{
    ToriMem_Init();
    test_round_trip();
    test_refusals();
    test_format();
    test_plans();
    test_wire();
    printf("editor_scenes_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
