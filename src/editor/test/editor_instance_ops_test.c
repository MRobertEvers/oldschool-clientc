/*
 * Workbench 6.5, part 2, headless: the instance helper's dialog
 * (panels/edpanel_instance.c) against the real chrome model and an embedded
 * ToriRSMapEd over a SYNTHETIC content tree written under the directory
 * given -- two hand-written squares, their spawns, a lane square in instance
 * space, an AREAS file; no cache, no OSRS-Content:
 *
 *   ./editor_instance_ops_test <tmp-content-dir>
 *
 *   open      the picker's Instance... opens it over the selection
 *   refusals  no name, no destination, a taken destination (its owner
 *             named), a bad loc, a landing tile outside, the runtime chip
 *             disabled with its reason, a .wma name already in the tree, a
 *             source past 16 squares, a runtime box past 2x2
 *   find      Find free lands past a lane's square in instance space and
 *             marks the box in the picker
 *   create    ONE undo step (two DOC_CREATEs in the stroke, the spawns
 *             rebased, the area copy, the collection, the .wma, the .rs2),
 *             nothing on disk; Undo empties every document, Redo brings it
 *             back; Save writes the files; the catalog's atlas reader takes
 *             the .wma (the squares claimed by the new area)
 *   trim      level 0 only and no locs, through the authority
 */

#include "editor/editor.h"
#include "editor/editor_areas.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_instance.h"
#include "editor/editor_scenes.h"
#include "editor/editor_spawn.h"
#include "editor/editor_ui.h"
#include "editor/editor_wma.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_instance.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static struct Editor g_editor;
static struct RSCache g_profile;
static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static int g_opened_x = -1;
static int g_opened_z = -1;

static const char k_jm2_a[] = "==== MAP ====\n"
                              "0 10 10: h5 u2\n"
                              "0 11 10: h6 u2\n"
                              "1 12 11: u3\n";
static const char k_jl2_a[] = "==== LOC ====\n"
                              "0 10 10: 1902 0 1\n"
                              "1 12 11: 1903 22\n";
static const char k_jm2_b[] = "==== MAP ====\n"
                              "0 1 1: h9\n";
static const char k_jl2_b[] = "==== LOC ====\n"
                              "0 2 2: 1902 10 0\n";
static const char k_spawn_a[] = "==== NPC ====\n"
                                "goblin                                        3210  3220 0\n";
static const char k_spawn_b[] = "==== NPC ====\n"
                                "guard                                         3205  3270 0\n"
                                "rat                                           3206  3271 1\n";
static const char k_areas[] = "# ToriRSMapEd named areas and square collections (editor/areas.txt).\n"
                              "area smithy | level 0 | rects 3205 3208 3210 3212\n"
                              "area across | level 0 | rects 3200 3260 3210 3270\n"
                              "collection src | squares m50_50 m50_51\n";
static const char k_taken_wma[] = "[taken_area]\n"
                                  "external=Taken\n"
                                  "section1=0,0,4,30,200,30,200,0,0,0,0,30,200,30,200,0,0,0\n";

static void
write_text(
    const char* root,
    const char* relative,
    const char* text)
{
    char path[2048];
    FILE* f;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    f = fopen(path, "wb");
    if( !f )
    {
        printf("FAIL: cannot write %s\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

static char*
read_text(
    const char* root,
    const char* relative)
{
    char path[2048];
    FILE* f;
    long size;
    char* text;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    f = fopen(path, "rb");
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)size + 1);
    assert(text);
    if( fread(text, 1, (size_t)size, f) != (size_t)size )
        size = 0;
    text[size] = '\0';
    fclose(f);
    return text;
}

static int
exists(
    const char* root,
    const char* relative)
{
    char path[2048];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    return stat(path, &st) == 0;
}

static void
make_dir(
    const char* root,
    const char* relative)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    mkdir(path, 0755);
}

static void
write_fixture(const char* root)
{
    static const char* const dirs[] = { "", "configs", "maps", "pack", "editor", "ported", "ported/testlane",
        "ported/testlane/maps", "worldmap", "worldmap/areas", "server", "server/scripts", "server/scripts/areas",
        "server/scripts/areas/edited", "server/scripts/areas/edited/configs", "server/scripts/areas/world",
        "server/scripts/areas/world/configs" };
    for( size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++ )
        make_dir(root, dirs[i]);
    write_text(root, "configs/all.loc", "[test_wall]\nname=Wall\n\n[cave_entrance]\nname=Cave\n");
    write_text(root, "configs/all.loc.compack", "1902=test_wall\n1903=cave_entrance\n");
    write_text(root, "maps/m50_50.jm2", k_jm2_a);
    write_text(root, "maps/m50_50.jl2", k_jl2_a);
    write_text(root, "maps/m50_51.jm2", k_jm2_b);
    write_text(root, "maps/m50_51.jl2", k_jl2_b);
    write_text(root, "ported/testlane/maps/m0_100.jm2", "==== MAP ====\n0 1 1: h3\n");
    write_text(root, "server/scripts/areas/edited/configs/m50_50.spawn", k_spawn_a);
    write_text(root, "server/scripts/areas/world/configs/m50_51.spawn", k_spawn_b);
    write_text(root, "editor/areas.txt", k_areas);
    write_text(root, "worldmap/areas/taken_area.wma", k_taken_wma);
}

static void
frame(void)
{
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
activate(int widget)
{
    assert(widget >= 0);
    g_chrome.activated = widget;
    frame();
}

static int
W(enum Editor_InstanceWidget which)
{
    return Editor_Instance_Widget(&g_ui, which);
}

static void
set_text(
    enum Editor_InstanceWidget which,
    const char* text)
{
    ToriRSChrome_SetText(&g_chrome, W(which), text);
    activate(W(which));
}

static const char*
check_row(void)
{
    return ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_CHECK));
}

static void
fake_open_square(
    void* user,
    int map_x,
    int map_z,
    const char* storage,
    int add)
{
    (void)user;
    (void)storage;
    (void)add;
    g_opened_x = map_x;
    g_opened_z = map_z;
}

static int
groups(void)
{
    return Editor_GetHistory(&g_editor)->applied;
}

static const char*
text_doc(
    const char* key,
    size_t* out_length)
{
    struct Editor_DocHandle handle;
    *out_length = 0;
    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_TEXT, key, &handle) || !handle.text )
        return NULL;
    *out_length = handle.text->length;
    return handle.text->text ? handle.text->text : "";
}

/** Scene `name` of the window's SCENES document into `out`; 0 when the
 *  document has no such scene. */
static int
scene_get(
    const char* name,
    struct Editor_Scene* out)
{
    struct Editor_DocHandle handle;
    struct Editor_ScenesFile file;
    int found = 0;
    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY, &handle) || !handle.text )
        return 0;
    Editor_ScenesInit(&file);
    if( Editor_ScenesParse(&file, handle.text->text, handle.text->length).ok )
        found = Editor_ScenesGet(&file, name, out);
    Editor_ScenesFree(&file);
    return found;
}

static int
spawn_rows(
    int map_x,
    int map_z,
    struct Editor_SpawnRow* out,
    int max)
{
    char key[EDITOR_DOC_KEY_MAX];
    struct Editor_DocHandle handle;
    int n = 0;
    Editor_SquareKey(key, map_x, map_z);
    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, key, &handle) || !handle.spawn )
        return 0;
    for( int l = 0; l < handle.spawn->file.line_count && n < max; l++ )
        if( handle.spawn->file.lines[l].kind == EDITOR_SPAWN_LINE_ROW )
            out[n++] = handle.spawn->file.lines[l].row;
    return n;
}

static int
tiles_equal(
    int ax,
    int az,
    int bx,
    int bz,
    int level)
{
    struct Editor_Square* a = Editor_DocFindSquare(&g_editor.doc, ax, az);
    struct Editor_Square* b = Editor_DocFindSquare(&g_editor.doc, bx, bz);
    if( !a || !b )
        return 0;
    for( int z = 0; z < EDITOR_SQUARE_Z; z++ )
        for( int x = 0; x < EDITOR_SQUARE_X; x++ )
            if( memcmp(&a->tiles[Editor_TileIndex(x, z, level)], &b->tiles[Editor_TileIndex(x, z, level)],
                    sizeof(struct Editor_Tile))
                != 0 )
                return 0;
    return 1;
}

static int
square_empty(
    int map_x,
    int map_z)
{
    static const struct Editor_Tile empty;
    struct Editor_Square* s = Editor_DocFindSquare(&g_editor.doc, map_x, map_z);
    if( !s )
        return 1;
    for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
        if( memcmp(&s->tiles[i], &empty, sizeof(empty)) != 0 )
            return 0;
    return s->loc_count == 0;
}

/** Select the two sources in the picker, as Shift+clicks do. */
static void
select_sources(void)
{
    struct Editor_Picker* picker = (struct Editor_Picker*)edp_squares_picker(&g_ui);
    int a;
    int b;
    assert(picker);
    a = Editor_PickerFind(picker, 50, 50, -1);
    b = Editor_PickerFind(picker, 50, 51, -1);
    CHECK(a >= 0 && b >= 0, "the atlas has both source squares (%d %d)", a, b);
    if( a < 0 || b < 0 )
        exit(1);
    Editor_PickerSelect(picker, a, 0);
    Editor_PickerSelect(picker, b, 1);
}

static void
test_open_and_refusals(const char* root)
{
    const struct Editor_Picker* picker;
    int const panel = W(EDITOR_INSTANCE_W_PANEL);
    int find;

    select_sources();
    Editor_Instance_Open(&g_ui);
    frame();
    CHECK(g_chrome.panels[panel].visible, "Instance... opens the dialog");
    CHECK(strcmp(ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_SOURCE_ROW)), "m50_50 m50_51") == 0,
        "its source is the selection (%s)", ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_SOURCE_ROW)));

    /* No destination yet. */
    CHECK(strstr(check_row(), "type the destination") != NULL, "no destination: said (%s)", check_row());
    CHECK(ToriRSChrome_Disabled(&g_chrome, W(EDITOR_INSTANCE_W_CREATE)), "Create is disabled");
    CHECK(!Editor_Instance_Create(&g_ui) && strstr(Editor_UiStatusText(&g_ui), "Create instance refused") != NULL,
        "Create refuses in the status bar too (%s)", Editor_UiStatusText(&g_ui));

    /* A taken destination, its owner named. */
    set_text(EDITOR_INSTANCE_W_DEST, "m50_51");
    CHECK(strstr(check_row(), "m50_51 is taken (maps/, unmapped)") != NULL, "a taken key names its owner (%s)",
        check_row());
    set_text(EDITOR_INSTANCE_W_DEST, "m0_100");
    CHECK(strstr(check_row(), "m0_100 is taken (ported/testlane/maps, ") != NULL,
        "taken in another storage counts (%s)", check_row());

    /* A free one, then no name. */
    set_text(EDITOR_INSTANCE_W_DEST, "m5_150");
    set_text(EDITOR_INSTANCE_W_NAME, "Bad Name");
    CHECK(strstr(check_row(), "name the instance") != NULL, "a bad name: said (%s)", check_row());
    set_text(EDITOR_INSTANCE_W_NAME, "tutorial_cave");
    CHECK(strstr(check_row(), "name the loc") != NULL, "a teleport needs its loc (%s)", check_row());
    set_text(EDITOR_INSTANCE_W_LOC, "cave_entrance");
    set_text(EDITOR_INSTANCE_W_COORD, "0_50_50_1_1");
    CHECK(strstr(check_row(), "is not in the instance") != NULL, "a landing tile outside: said (%s)", check_row());
    set_text(EDITOR_INSTANCE_W_COORD, "nonsense");
    CHECK(strstr(check_row(), "the landing coord is") != NULL, "a bad coord: said (%s)", check_row());
    set_text(EDITOR_INSTANCE_W_COORD, "");
    CHECK(strncmp(check_row(), "creates 2 squares at m5_150 in maps/, a .wma, a trigger to 0_5_150_32_32", 70) == 0,
        "a good form says what Create makes (%s)", check_row());
    CHECK(!ToriRSChrome_Disabled(&g_chrome, W(EDITOR_INSTANCE_W_CREATE)), "and Create is enabled");

    /* The runtime chip: no server reports map instances. */
    CHECK(ToriRSChrome_Disabled(&g_chrome, W(EDITOR_INSTANCE_W_ENTRY_RUNTIME)),
        "the runtime map instance chip is disabled");
    {
        char tip[TORIRS_CHROME_INPUT_MAX];
        (void)ToriRSChrome_TooltipText(&g_chrome, W(EDITOR_INSTANCE_W_ENTRY_RUNTIME), tip, (int)sizeof(tip));
        CHECK(strstr(tip, "reports no map instances") != NULL, "and its tooltip says why (%s)", tip);
    }
    ToriRSChrome_RadioSelect(&g_chrome, W(EDITOR_INSTANCE_W_ENTRY_RUNTIME));
    activate(W(EDITOR_INSTANCE_W_ENTRY_RUNTIME));
    CHECK(strstr(Editor_UiStatusText(&g_ui), "Runtime map instance unavailable") != NULL
              && ToriRSChrome_Checked(&g_chrome, W(EDITOR_INSTANCE_W_ENTRY_TELEPORT)),
        "picking it anyway says why and falls back to the teleport (%s)", Editor_UiStatusText(&g_ui));

    /* A .wma name already in the tree. */
    set_text(EDITOR_INSTANCE_W_NAME, "taken_area");
    CHECK(!Editor_Instance_Create(&g_ui) && strstr(check_row(), "worldmap/areas/taken_area.wma already exists") != NULL,
        "a .wma that exists is refused (%s)", check_row());
    CHECK(groups() == groups(), "nothing was applied");

    /* The pure form's refusals the dialog cannot reach by typing. */
    picker = edp_squares_picker(&g_ui);
    {
        struct Editor_InstanceForm form;
        struct Editor_InstanceCheck check;
        memset(&form, 0, sizeof(form));
        form.source_count = EDITOR_INSTANCE_SQUARES_MAX + 1;
        form.level_hi = 3;
        CHECK(!Editor_InstanceValidate(picker, &form, &check) && strstr(check.message, "at most 16") != NULL,
            "more than 16 sources: said (%s)", check.message);
        form.source_count = 3;
        snprintf(form.sources[0], sizeof(form.sources[0]), "m50_50");
        snprintf(form.sources[1], sizeof(form.sources[1]), "m51_50");
        snprintf(form.sources[2], sizeof(form.sources[2]), "m52_50");
        form.dst_x = 5;
        form.dst_z = 150;
        snprintf(form.name, sizeof(form.name), "wide");
        snprintf(form.loc, sizeof(form.loc), "cave_entrance");
        form.entry = EDITOR_INSTANCE_ENTRY_RUNTIME;
        form.runtime_capable = 1;
        CHECK(!Editor_InstanceValidate(picker, &form, &check) && strstr(check.message, "2x2") != NULL,
            "a runtime box of 3x1 squares: said (%s)", check.message);
        form.dst_x = 254;
        CHECK(!Editor_InstanceValidate(picker, &form, &check) && strstr(check.message, "leaves the world") != NULL,
            "a box past 255: said (%s)", check.message);
        form.dst_x = 5;
        form.storage = -1;
        form.entry = EDITOR_INSTANCE_ENTRY_TELEPORT;
        CHECK(!Editor_InstanceValidate(picker, &form, &check) && strstr(check.message, "name the new lane") != NULL,
            "a new lane with no name: said (%s)", check.message);
    }

    /* Find free: the lane's m0_100 is in the way of a 1x2 at x 0. */
    find = W(EDITOR_INSTANCE_W_FIND);
    activate(find);
    CHECK(strcmp(ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_DEST)), "m1_100") == 0,
        "Find free lands beside the lane's square, at m1_100 (%s)", ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_DEST)));
    picker = edp_squares_picker(&g_ui);
    CHECK(picker->mark_x == 1 && picker->mark_z == 100 && picker->mark_w == 1 && picker->mark_h == 2,
        "and marks m1_100 1x2 in the picker (%d %d %dx%d)", picker->mark_x, picker->mark_z, picker->mark_w,
        picker->mark_h);
    CHECK(strstr(ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_FREE)), "has 3119 free") != NULL,
        "and says how much is free (%s)", ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_FREE)));
    (void)root;
}

static void
test_create_undo_save(const char* root)
{
    struct Editor_SpawnRow rows[8];
    struct Editor_DocHandle handle;
    const struct Editor_AreasFile* areas;
    const char* text;
    size_t length;
    char expected[1024];
    size_t expected_length;
    char* on_disk;
    int g0;
    int n;

    set_text(EDITOR_INSTANCE_W_NAME, "tutorial_cave");
    ToriRSChrome_SetChecked(&g_chrome, W(EDITOR_INSTANCE_W_AREAS), 1);
    activate(W(EDITOR_INSTANCE_W_AREAS));
    CHECK(!ToriRSChrome_Disabled(&g_chrome, W(EDITOR_INSTANCE_W_CREATE)), "the form is good (%s)", check_row());

    g0 = groups();
    activate(W(EDITOR_INSTANCE_W_CREATE));
    CHECK(!g_chrome.panels[W(EDITOR_INSTANCE_W_PANEL)].visible, "Create closes the dialog");
    CHECK(groups() == g0 + 1, "ONE undo group (%d -> %d)", g0, groups());
    CHECK(strstr(Editor_UiStatusText(&g_ui), "Created instance tutorial_cave: 2 squares at m1_100, 3 spawns") != NULL,
        "the status bar says what was made (%s)", Editor_UiStatusText(&g_ui));
    CHECK(g_opened_x == 1 && g_opened_z == 100, "the first square opens in the world");
    {
        const struct Editor_History* history = Editor_GetHistory(&g_editor);
        CHECK(history->applied > 0 && strcmp(history->groups[history->applied - 1].verb, "instance tutorial_cave") == 0,
            "its History row is the instance's (%s)", history->groups[history->applied - 1].verb);
    }

    /* The squares: copies, at the destination. */
    for( int level = 0; level < 4; level++ )
    {
        CHECK(tiles_equal(1, 100, 50, 50, level), "m1_100 L%d is m50_50's terrain", level);
        CHECK(tiles_equal(1, 101, 50, 51, level), "m1_101 L%d is m50_51's terrain", level);
    }
    CHECK(Editor_DocFindSquare(&g_editor.doc, 1, 100) && Editor_DocFindSquare(&g_editor.doc, 1, 100)->loc_count == 2,
        "m1_100 has m50_50's two locs");

    /* The spawns, rebased: the edited one, and the generated ones. */
    n = spawn_rows(1, 100, rows, 8);
    CHECK(n == 1 && strcmp(rows[0].name, "goblin") == 0 && rows[0].abs_x == 64 + 10 && rows[0].abs_z == 100 * 64 + 20,
        "m1_100's spawn is the goblin at %d,%d (%d rows)", n ? rows[0].abs_x : -1, n ? rows[0].abs_z : -1, n);
    n = spawn_rows(1, 101, rows, 8);
    CHECK(n == 2 && strcmp(rows[0].name, "guard") == 0 && rows[0].abs_x == 64 + 5 && rows[0].abs_z == 101 * 64 + 6
              && rows[1].level == 1,
        "m1_101 has the generated file's two rows, moved, levels kept (%d)", n);

    /* The areas: smithy moved, "across" (in two squares' gap) not. */
    areas = Editor_Areas_File(&g_ui, NULL);
    CHECK(areas && Editor_AreasFindArea(areas, "tutorial_cave: smithy") >= 0, "the smithy area is copied, renamed");
    if( areas && Editor_AreasFindArea(areas, "tutorial_cave: smithy") >= 0 )
    {
        const struct Editor_Area* a = areas->lines[Editor_AreasFindArea(areas, "tutorial_cave: smithy")].area;
        CHECK(a->rect_count == 1 && a->rects[0].x0 == 64 + 5 && a->rects[0].z0 == 100 * 64 + 8,
            "its rectangle moved with the square (%d,%d)", a->rects[0].x0, a->rects[0].z0);
    }
    CHECK(areas && Editor_AreasFindArea(areas, "tutorial_cave: across") < 0,
        "an area spanning two source squares is not copied");
    CHECK(areas && Editor_AreasFindCollection(areas, "tutorial_cave") >= 0, "a collection names the new squares");

    /* The .wma and the trigger, through the text path. */
    text = text_doc("wma:tutorial_cave", &length);
    expected_length = Editor_WmaNewAreaText("tutorial_cave", "Tutorial cave", 1, 100, 1, 101, 0, 4, expected,
        sizeof(expected));
    CHECK(text && length == expected_length && memcmp(text, expected, length) == 0, "the .wma document holds the new area");
    text = text_doc("rs2:edited/tutorial_cave/scripts/tutorial_cave.rs2", &length);
    CHECK(text && strstr(text, "[oploc1,cave_entrance]\np_telejump(0_1_100_32_32);\n") != NULL,
        "the trigger teleports to the middle of m1_100");

    /* The scene "instance:<name>", in the same step (the 6.5 seam). */
    {
        struct Editor_Scene scene;
        CHECK(scene_get("instance:tutorial_cave", &scene), "the SCENES document holds instance:tutorial_cave");
        CHECK(scene.square_count == 2 && strcmp(scene.squares[0], "m1_100") == 0
                  && strcmp(scene.squares[1], "m1_101") == 0,
            "the scene opens the two new squares (%d: %s)", scene.square_count, scene.squares[0]);
        CHECK((scene.segments & EDITOR_SCENE_SEG_CAMERA) && scene.camera.x == (64 + 32) * 128 + 64
                  && scene.camera.z == (100 * 64 + 32) * 128 + 64 && scene.camera.level == 0,
            "its camera looks at the landing tile (%d,%d)", scene.camera.x / 128, scene.camera.z / 128);
        CHECK(!exists(root, "editor/scenes.txt"), "the scene is not written before Save");
    }

    /* Nothing written until Save. */
    CHECK(!exists(root, "maps/m1_100.jm2") && !exists(root, "worldmap/areas/tutorial_cave.wma")
              && !exists(root, "server/scripts/edited/tutorial_cave/scripts/tutorial_cave.rs2")
              && !exists(root, "server/scripts/areas/edited/configs/m1_100.spawn"),
        "nothing is on disk before Save");

    /* Undo: the whole instance, every document. */
    n = Editor_Undo(&g_editor);
    CHECK(groups() == g0, "Undo takes the one group back (%d commands)", n);
    CHECK(square_empty(1, 100) && square_empty(1, 101), "both squares are empty again");
    CHECK(spawn_rows(1, 100, rows, 8) == 0 && spawn_rows(1, 101, rows, 8) == 0, "their spawns are gone");
    text = text_doc("wma:tutorial_cave", &length);
    CHECK(text && length == 0, "the .wma is empty again");
    text = text_doc("rs2:edited/tutorial_cave/scripts/tutorial_cave.rs2", &length);
    CHECK(text && length == 0, "the trigger is empty again");
    areas = Editor_Areas_File(&g_ui, NULL);
    CHECK(areas && Editor_AreasFindCollection(areas, "tutorial_cave") < 0
              && Editor_AreasFindArea(areas, "tutorial_cave: smithy") < 0,
        "the collection and the copied area are gone");
    {
        struct Editor_Scene scene;
        CHECK(!scene_get("instance:tutorial_cave", &scene), "and the scene");
    }
    Editor_Redo(&g_editor);
    CHECK(groups() == g0 + 1 && tiles_equal(1, 100, 50, 50, 0) && spawn_rows(1, 101, rows, 8) == 2,
        "Redo brings it all back");

    /* Save, then the files. */
    CHECK(Editor_SaveAll(&g_editor) > 0, "Save writes the documents");
    CHECK(exists(root, "maps/m1_100.jm2") && exists(root, "maps/m1_101.jl2"), "the squares are on disk");
    on_disk = read_text(root, "worldmap/areas/tutorial_cave.wma");
    CHECK(on_disk && strcmp(on_disk, expected) == 0, "worldmap/areas/tutorial_cave.wma is the area, byte for byte");
    free(on_disk);
    on_disk = read_text(root, "server/scripts/edited/tutorial_cave/scripts/tutorial_cave.rs2");
    CHECK(on_disk && strstr(on_disk, "p_telejump(0_1_100_32_32);") != NULL, "the trigger is on disk");
    free(on_disk);
    on_disk = read_text(root, "server/scripts/areas/edited/configs/m1_101.spawn");
    CHECK(on_disk && strstr(on_disk, "guard") != NULL && strstr(on_disk, "rat") != NULL, "m1_101's spawns are on disk");
    free(on_disk);

    /* The server catalog's atlas reader takes the .wma it wrote. */
    {
        struct ToriRSMapEdCatalog* catalog = ToriRSMapEdCatalog_Create(root);
        struct ToriRSMapEdCatalogSquare square;
        while( ToriRSMapEdCatalog_Service(catalog, 50.0) )
        {
        }
        CHECK(ToriRSMapEdCatalog_Square(catalog, "m1_100", &square) && strcmp(square.area, "tutorial_cave") == 0
                  && !(square.flags & TORIRSMAPED_SQUARE_INSTANCE),
            "the atlas reader puts m1_100 in the area tutorial_cave, no longer unmapped (%s, flags %u)",
            square.area, square.flags);
        CHECK(ToriRSMapEdCatalog_Square(catalog, "m1_101", &square) && strcmp(square.area, "tutorial_cave") == 0,
            "and m1_101 (%s)", square.area);
        CHECK(ToriRSMapEdCatalog_Square(catalog, "ported/testlane/maps/m0_100", &square)
                  ? (square.flags & TORIRSMAPED_SQUARE_INSTANCE) != 0
                  : 1,
            "the lane square stays unmapped instance space");
        ToriRSMapEdCatalog_Free(catalog);
    }
    (void)handle;
}

static void
test_trim(void)
{
    struct Editor_Square* made;
    int g0 = groups();

    /* From the collection, level 0 only, no locs. */
    Editor_Instance_Open(&g_ui);
    frame();
    ToriRSChrome_DropdownSetSelected(&g_chrome, W(EDITOR_INSTANCE_W_SOURCE), 2);
    activate(W(EDITOR_INSTANCE_W_SOURCE));
    CHECK(strcmp(ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_SOURCE_ROW)), "m50_50 m50_51") == 0,
        "the collection src is a source (%s)", ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_SOURCE_ROW)));
    ToriRSChrome_DropdownSetSelected(&g_chrome, W(EDITOR_INSTANCE_W_LEVELS), 1);
    activate(W(EDITOR_INSTANCE_W_LEVELS));
    ToriRSChrome_SetChecked(&g_chrome, W(EDITOR_INSTANCE_W_LOCS), 0);
    activate(W(EDITOR_INSTANCE_W_LOCS));
    ToriRSChrome_RadioSelect(&g_chrome, W(EDITOR_INSTANCE_W_MAP_NONE));
    activate(W(EDITOR_INSTANCE_W_MAP_NONE));
    ToriRSChrome_RadioSelect(&g_chrome, W(EDITOR_INSTANCE_W_ENTRY_NONE));
    activate(W(EDITOR_INSTANCE_W_ENTRY_NONE));
    set_text(EDITOR_INSTANCE_W_NAME, "flat_copy");
    activate(W(EDITOR_INSTANCE_W_FIND));
    CHECK(strcmp(ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_DEST)), "m2_100") == 0,
        "Find free now steps past the first instance (%s)", ToriRSChrome_Text(&g_chrome, W(EDITOR_INSTANCE_W_DEST)));
    CHECK(Editor_Instance_Create(&g_ui), "Create (%s)", check_row());
    CHECK(groups() == g0 + 1, "one group");
    made = Editor_DocFindSquare(&g_editor.doc, 2, 100);
    CHECK(made && made->loc_count == 0, "no locs copied");
    CHECK(tiles_equal(2, 100, 50, 50, 0), "level 0 copied");
    CHECK(made && !made->tiles[Editor_TileIndex(12, 11, 1)].underlay_id, "level 1's tile cleared");
    {
        size_t length;
        CHECK(!text_doc("wma:flat_copy", &length) || length == 0, "no .wma");
        CHECK(!text_doc("rs2:edited/flat_copy/scripts/flat_copy.rs2", &length) || length == 0, "no trigger");
    }
    Editor_Undo(&g_editor);
    CHECK(groups() == g0 && square_empty(2, 100), "Undo empties it");
}

int
main(
    int argc,
    char** argv)
{
    struct EditorHost host;
    const char* root;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <tmp-content-dir>\n", argv[0]);
        return 2;
    }
    root = argv[1];
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    if( !RSCache_ProfileByName("osrs242", &g_profile) )
    {
        printf("FAIL: no osrs242 profile\n");
        return 1;
    }
    write_fixture(root);
    if( !Editor_HostOpenMapEdEmbed(&host, root, NULL) )
    {
        printf("FAIL: the embedded ToriRSMapEd did not open %s\n", root);
        return 1;
    }
    Editor_OpenHost(&g_editor, &host, root, &g_profile);
    CHECK(g_editor.writable, "the tmp tree is writable");
    CHECK((g_editor.capabilities & TORIRSMAPED_CAP_DOC_CREATE_STROKE) != 0, "the server creates inside a stroke");
    CHECK(!(g_editor.capabilities & TORIRSMAPED_CAP_MAP_INSTANCE), "and reports no map instances");

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1530, 1006);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.hooks.open_square = fake_open_square;
    Editor_UiRegisterToolsPanels(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    for( int i = 0; i < 4; i++ )
        frame();
    if( !edp_squares_picker(&g_ui) )
    {
        printf("FAIL: the picker has no atlas\n");
        return 1;
    }

    test_open_and_refusals(root);
    test_create_undo_save(root);
    test_trim();

    Editor_UiFree(&g_ui);
    Editor_Close(&g_editor);
    printf("editor_instance_ops_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
