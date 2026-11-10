/*
 * Workbench 6.3, part 2, headless (editor/panels/edpanel_ops_stamps.h, the
 * Locs panel's new rows, the Stamps panel), against the real chrome model and
 * an embedded ToriRSMapEd over a SYNTHETIC content tree written under the
 * directory given -- one hand-written square, no cache, no OSRS-Content:
 *
 *   ./editor_stamps_ops_test <tmp-content-dir>
 *
 *   stamps    a selection saved as a stamp (its file and the index's line
 *             written through the authority, one undo step, saved to disk);
 *             armed, turned a quarter, dropped as ONE undo step (the locs at
 *             their turned tiles, the 2x1 table on the tiles it covered); its
 *             undo puts the square's .jl2 and .jm2 back byte for byte
 *   paste     the marquee's Ctrl+C / Ctrl+V: the tile commands exactly the
 *             tile paste's, the rectangle's locs after them, one step; the
 *             spawns standing in the rectangle (an edited row, a generated
 *             npc and a generated ground item; not a suppressed one, not one
 *             on another level or outside) pasted as edited rows in the same
 *             step, not stood twice by a second paste, left out with "with
 *             spawns" off
 *   level     Ctrl+PgUp / PgDn: the selection a level, each loc keeping its
 *             tile; refused (no level 4, a taken key) with a status sentence
 *             and no history row
 *   gizmo     Alt-arrow's copy by an offset and the ring's turn by quarters
 *   inspector Find same, the footprint / multiloc rows, Follow, the strip's
 *             thumbnails clickable through its GRAPH, the Catalog's Stamps
 *             chip opening the Stamps list
 */

#include "editor/editor.h"
#include "editor/editor_cfg.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_jm2.h"
#include "editor/editor_stamps.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_ops_stamps.h"
#include "editor/panels/edpanels.h"
#include "engine/torirs_types.h"
#include "mem/torirs_mem.h"
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
static int g_app_stand_in;
static int g_camera_x = -1;
static int g_camera_z = -1;

/* The App's loc records, faked: every loc has wall, centrepiece and ground
 * decor models; the table is 2x1 and walks through, the walls block. */
static int g_fake_shapes[3] = { 0, 10, 22 };
static int g_fake_lengths[3] = { 1, 1, 1 };
static int g_fake_model_ids[1] = { 1 };
static int* g_fake_models[3] = { g_fake_model_ids, g_fake_model_ids, g_fake_model_ids };
static struct ToriRS_Location g_fake_wall;
static struct ToriRS_Location g_fake_table;

static struct ToriRS_Location*
fake_loc_record(
    void* user,
    int loc_id)
{
    struct ToriRS_Location* cfg = loc_id == 2097 ? &g_fake_table : &g_fake_wall;
    (void)user;
    cfg->id = loc_id;
    cfg->shapes = g_fake_shapes;
    cfg->lengths = g_fake_lengths;
    cfg->models = g_fake_models;
    cfg->shapes_and_model_count = 3;
    cfg->size_x = loc_id == 2097 ? 2 : 1;
    cfg->size_z = 1;
    cfg->blocks_walk = loc_id == 2097 ? 0 : 1;
    cfg->transform_varbit = -1;
    cfg->transform_varp = -1;
    return cfg;
}

#define SQ_X 50
#define SQ_Z 50
#define BASE_X (SQ_X * 64)
#define BASE_Z (SQ_Z * 64)

/* The synthetic square: a few authored tiles, a two-piece wall, a 2x1
 * table, an upper floor's decor and a wall above the second piece. The
 * .jl2 is in the cache's order (loc id, level, x, z), as every content
 * square is. */
static const char k_jm2[] = "==== MAP ====\n"
                            "0 10 10: h5 u2\n"
                            "0 11 10: h6 u2\n"
                            "0 12 11: u3\n"
                            "0 30 30: h9\n";
static const char k_jl2[] = "==== LOC ====\n"
                            "0 10 10: 1902 0 1\n"
                            "0 11 10: 1902 0 1\n"
                            "1 10 10: 1903 22\n"
                            "1 11 10: 1904 0\n"
                            "0 12 11: 2097 10\n";
/* Spawns around local (30,30)..(31,31), clear of every loc above: the edited
 * file's own npc and its suppression of a generated rat; the generated
 * file's goblin, that rat, an npc a level up, one outside, and a ground
 * item. */
static const char k_spawn_edited[] = "==== NPC ====\n"
                                     "hans                                          3230  3230 0\n"
                                     "-rat                                          3231  3230 0\n";
static const char k_spawn_generated[] = "==== NPC ====\n"
                                        "goblin                                        3231  3231 0\n"
                                        "rat                                           3231  3230 0\n"
                                        "man                                           3230  3231 1\n"
                                        "cow                                           3240  3240 0\n"
                                        "\n"
                                        "==== OBJ ====\n"
                                        "knife                                         3230  3231 0 1\n";

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
    make_dir(root, "");
    make_dir(root, "configs");
    make_dir(root, "maps");
    make_dir(root, "pack");
    make_dir(root, "editor");
    make_dir(root, "server");
    make_dir(root, "server/scripts");
    make_dir(root, "server/scripts/areas");
    make_dir(root, "server/scripts/areas/edited");
    make_dir(root, "server/scripts/areas/edited/configs");
    make_dir(root, "server/scripts/areas/world");
    make_dir(root, "server/scripts/areas/world/configs");
    write_text(root, "server/scripts/areas/edited/configs/m50_50.spawn", k_spawn_edited);
    write_text(root, "server/scripts/areas/world/configs/m50_50.spawn", k_spawn_generated);
    write_text(root, "configs/all.loc", "[test_wall]\nname=Wall\n\n[test_table]\nname=Table\n");
    write_text(root, "configs/all.loc.compack", "1902=test_wall\n2097=test_table\n");
    write_text(root, "maps/m50_50.jm2", k_jm2);
    write_text(root, "maps/m50_50.jl2", k_jl2);
}

static void
frame(void)
{
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static int
fake_camera_to_tile(
    void* user,
    int x,
    int z,
    int level)
{
    (void)user;
    (void)level;
    g_camera_x = x;
    g_camera_z = z;
    return 1;
}

static struct Editor_Square*
square(void)
{
    struct Editor_Square* sq = Editor_DocFindSquare(&g_editor.doc, SQ_X, SQ_Z);
    assert(sq);
    return sq;
}

static char*
jl2(void)
{
    size_t const n = Editor_Jl2Emit(square(), NULL, 0);
    char* text = malloc(n + 1);
    assert(text);
    Editor_Jl2Emit(square(), text, n + 1);
    return text;
}

static char*
jm2(void)
{
    size_t const n = Editor_Jm2Emit(square(), NULL, 0);
    char* text = malloc(n + 1);
    assert(text);
    Editor_Jm2Emit(square(), text, n + 1);
    return text;
}

static int
groups(void)
{
    return Editor_GetHistory(&g_editor)->applied;
}

/** The loc at local (x, z, level, shape), its id (or -1). */
static int
loc_at(
    int x,
    int z,
    int level,
    int shape,
    int* out_rotation)
{
    int const i = Editor_SquareLocFind(square(), level, x, z, shape);
    if( i < 0 )
        return -1;
    if( out_rotation )
        *out_rotation = square()->locs[i].rotation;
    return square()->locs[i].loc_id;
}

static void
select_locs(
    const int (*keys)[4],
    int n)
{
    g_ui.tools.loc_count = 0;
    for( int i = 0; i < n; i++ )
    {
        int const index = Editor_SquareLocFind(square(), keys[i][2], keys[i][0], keys[i][1], keys[i][3]);
        assert(index >= 0);
        Editor_LocRefAt(&g_ui.tools.locs[g_ui.tools.loc_count++], BASE_X + keys[i][0], BASE_Z + keys[i][1], keys[i][2],
            keys[i][3], square()->locs[index].loc_id, square()->locs[index].rotation);
    }
}

static const char*
status(void)
{
    return Editor_UiStatusText(&g_ui);
}

static int
find_widget(
    int panel,
    int kind,
    const char* caption)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( !caption || strcmp(w->label, caption) == 0 || strcmp(w->text, caption) == 0 )
            return i;
    }
    return -1;
}

static int
module_panel(const char* name)
{
    struct EditorUiPanel* p = Editor_UiFindPanel(&g_ui, name);
    return p ? p->chrome_panel : -1;
}

static void
activate(int widget)
{
    assert(widget >= 0);
    g_chrome.activated = widget;
    frame();
}

/** The primary's configs/all.loc block holds this exact line. */
static int
wall_block_has(const char* line)
{
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];
    size_t const length = strlen(line);
    int const key_fits = Editor_CfgOverlayKey(key, "loc", "test_wall", NULL);

    assert(key_fits);
    (void)key_fits;
    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_CFG, key, &handle) || !handle.text || !handle.text->text )
        return 0;
    for( const char* at = strstr(handle.text->text, line); at; at = strstr(at + 1, line) )
        if( (at == handle.text->text || at[-1] == '\n') && (at[length] == '\n' || at[length] == '\r' || !at[length]) )
            return 1;
    return 0;
}

/* ---- stamps ------------------------------------------------------------------- */

static void
test_stamp_save_turn_drop(const char* root)
{
    static const int k_sel[3][4] = { { 10, 10, 0, 0 }, { 11, 10, 0, 0 }, { 12, 11, 0, 10 } };
    char* before_jl2;
    char* before_jm2;
    char* on_disk;
    char* index;
    int g0;
    int rotation = -1;
    const struct Editor_Stamp* armed;

    select_locs(k_sel, 3);
    g0 = groups();
    CHECK(!edp_stamp_save_selection(&g_ui, "bad name", 1, 0, 0), "a name with a space is refused");
    CHECK(strstr(status(), "not a stamp name") != NULL, "and says why (%s)", status());
    CHECK(groups() == g0, "with no history row");
    CHECK(edp_stamp_save_selection(&g_ui, "smithy", 1, 0, 0), "Save as stamp saves the selection");
    CHECK(groups() == g0 + 1, "as ONE undo step (%d -> %d)", g0, groups());
    on_disk = read_text(root, "editor/stamps/smithy.stamp");
    CHECK(on_disk && strstr(on_disk, "\nstamp smithy\nloc 0 0 0 1902 0 1\nloc 1 0 0 1902 0 1\nloc 2 1 0 2097 10 0 2x1\n"),
        "the file holds the three locs relative to the primary (%s)", on_disk ? on_disk : "(none)");
    CHECK(on_disk && strstr(on_disk, "\ntile 0 0 0 h5 u2\n") && strstr(on_disk, "\ntile 2 1 0 u3\n")
              && strstr(on_disk, "\ntile 1 1 0\n"),
        "and the terrain of the box under them, an absent tile a bare line");
    index = read_text(root, "editor/stamps/index.txt");
    CHECK(index && strstr(index, "\nstamp smithy\n"), "the index lists it");
    CHECK(!edp_stamp_save_selection(&g_ui, "smithy", 1, 0, 0), "saving over it without Overwrite is refused");
    CHECK(strstr(status(), "exists") != NULL, "and says why (%s)", status());
    free(on_disk);
    free(index);

    /* Arm, turn, drop. */
    g_ui.tools.loc_count = 0;
    CHECK(edp_stamp_arm(&g_ui, "smithy"), "the stamp arms");
    armed = edp_stamp_armed(&g_ui);
    CHECK(armed && g_ui.tools.tool == EDITOR_TOOL_LOC_PLACE, "the Place tool holds it");
    CHECK(armed && Editor_StampCount(armed, EDITOR_STAMP_LOC) == 3 && Editor_StampCount(armed, EDITOR_STAMP_TILE) == 8,
        "three locs and the 4x2 box of tiles the 2x1 table widens");
    edp_stamp_turn(&g_ui, 1);
    CHECK(armed && armed->turned == 1, "Q turns it a quarter");
    before_jl2 = jl2();
    before_jm2 = jm2();
    g0 = groups();
    CHECK(edp_stamp_drop(&g_ui, BASE_X + 40, BASE_Z + 40, 0) > 0, "a click drops it");
    CHECK(groups() == g0 + 1, "as ONE undo step (%d -> %d)", g0, groups());
    CHECK(loc_at(40, 40, 0, 0, &rotation) == 1902 && rotation == 2, "the origin wall turned 1 -> 2 (%d)", rotation);
    CHECK(loc_at(40, 39, 0, 0, &rotation) == 1902 && rotation == 2, "the wall east of it now south of it");
    CHECK(loc_at(41, 37, 0, 10, &rotation) == 2097 && rotation == 1,
        "the 2x1 table on the tiles it covered, turned: SW (41,37) (%d)", rotation);
    CHECK(square()->tiles[Editor_TileIndex(40, 40, 0)].has_height && square()->tiles[Editor_TileIndex(40, 40, 0)].height == 5,
        "the origin's tile came too");
    CHECK(g_ui.tools.loc_count == 3, "the dropped locs are the selection (%d)", g_ui.tools.loc_count);
    CHECK(strstr(status(), "Placed stamp smithy") != NULL, "the status bar says so (%s)", status());
    Editor_Undo(&g_editor);
    {
        char* after_jl2 = jl2();
        char* after_jm2 = jm2();
        CHECK(strcmp(after_jl2, before_jl2) == 0, "undo: the .jl2 is the file before the drop, byte for byte");
        CHECK(strcmp(after_jm2, before_jm2) == 0, "undo: the .jm2 too");
        free(after_jl2);
        free(after_jm2);
    }
    Editor_Redo(&g_editor);
    CHECK(loc_at(40, 39, 0, 0, NULL) == 1902, "redo puts it back");
    Editor_Undo(&g_editor);
    free(before_jl2);
    free(before_jm2);

    /* A drop over a square that is not open says so. */
    CHECK(edp_stamp_drop(&g_ui, BASE_X + 64 * 3, BASE_Z, 0) == 0, "a drop on a closed square places nothing");
    CHECK(strstr(status(), "not in an open square") != NULL, "and says why (%s)", status());
    /* Leaving Place disarms it. */
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_SELECT);
    CHECK(edp_stamp_armed(&g_ui) == NULL, "another tool disarms the stamp");
}

/* ---- paste -------------------------------------------------------------------- */

static void
test_paste_carries_locs(void)
{
    struct Editor_TileRect rect = { 1, BASE_X + 10, BASE_Z + 10, BASE_X + 12, BASE_Z + 11, 0 };
    static struct Editor_TileClip clip;
    static struct Editor_Cmd expected[64];
    char* before_jl2;
    char* before_jm2;
    int expected_count;
    int same = 1;
    int g0;

    g_ui.tools.rect = rect;
    CHECK(edp_rect_copy(&g_ui) == 6, "Ctrl+C copies the 3x2 tiles");
    CHECK(edp_stamp_clip_locs() == 3, "and the rectangle's three locs on its level (%d)", edp_stamp_clip_locs());
    /* What the tile paste alone makes, before the paste lands. */
    Editor_TileClipCopy(&g_editor.doc, &rect, &clip);
    expected_count = Editor_TileClipPaste(&g_editor.doc, &clip, BASE_X + 20, BASE_Z + 20, 0, expected, 64);
    before_jl2 = jl2();
    before_jm2 = jm2();
    g0 = groups();
    CHECK(edp_rect_paste_at(&g_ui, BASE_X + 20, BASE_Z + 20, 0) == expected_count + 3,
        "Ctrl+V pastes the tiles and the locs");
    CHECK(groups() == g0 + 1, "in ONE undo step");
    for( int i = 0; i < expected_count; i++ )
    {
        const struct Editor_Tile* t = &square()->tiles[Editor_TileIndex(expected[i].u.tile.x, expected[i].u.tile.z, 0)];
        if( memcmp(t, &expected[i].u.tile.after, sizeof(*t)) != 0 )
            same = 0;
    }
    CHECK(same, "every tile exactly as the tile paste writes it");
    CHECK(loc_at(20, 20, 0, 0, NULL) == 1902 && loc_at(21, 20, 0, 0, NULL) == 1902 && loc_at(22, 21, 0, 10, NULL) == 2097,
        "the locs at the same offsets from the corner");
    Editor_Undo(&g_editor);
    {
        char* after_jl2 = jl2();
        char* after_jm2 = jm2();
        CHECK(strcmp(after_jl2, before_jl2) == 0 && strcmp(after_jm2, before_jm2) == 0,
            "undo: both halves of the square back byte for byte");
        free(after_jl2);
        free(after_jm2);
    }
    free(before_jl2);
    free(before_jm2);
    g_ui.tools.rect.valid = 0;
}

/** Whether the mirror's edited spawn file of the test square holds the row. */
static int
edited_spawn_at(
    const char* name,
    int is_obj,
    int x,
    int z,
    int level)
{
    struct Editor_DocHandle handle;
    struct Editor_SpawnRow row;

    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_SPAWN, "m50_50", &handle) || !handle.spawn->loaded )
        return 0;
    memset(&row, 0, sizeof(row));
    row.is_obj = is_obj;
    snprintf(row.name, sizeof(row.name), "%s", name);
    row.abs_x = BASE_X + x;
    row.abs_z = BASE_Z + z;
    row.level = level;
    row.count = 1;
    return Editor_SpawnFind(&handle.spawn->file, &row) >= 0;
}

static void
test_paste_carries_spawns(void)
{
    struct Editor_TileRect rect = { 1, BASE_X + 30, BASE_Z + 30, BASE_X + 31, BASE_Z + 31, 0 };
    int g0;

    g_ui.tools.rect = rect;
    CHECK(edp_rect_with_spawns(), "\"with spawns\" is on until unticked");
    CHECK(edp_rect_copy(&g_ui) == 4, "Ctrl+C copies the 2x2 tiles");
    CHECK(edp_stamp_clip_locs() == 0, "no loc stands there (%d)", edp_stamp_clip_locs());
    CHECK(edp_stamp_clip_spawns() == 3,
        "and its three live spawns on that level: the edited npc, the generated npc and ground item (%d)",
        edp_stamp_clip_spawns());
    CHECK(strstr(status(), "Copied 2x2 tiles, 0 locs, 3 spawns") != NULL, "the status bar counts them (%s)", status());

    g0 = groups();
    CHECK(edp_rect_paste_at(&g_ui, BASE_X + 50, BASE_Z + 50, 0) >= 3, "Ctrl+V pastes them");
    CHECK(groups() == g0 + 1, "tiles and spawns in ONE undo step (%d -> %d)", g0, groups());
    CHECK(edited_spawn_at("hans", 0, 50, 50, 0), "the edited npc at the same offset from the corner");
    CHECK(edited_spawn_at("goblin", 0, 51, 51, 0), "the generated npc, now an EDITED row of the square it landed in");
    CHECK(edited_spawn_at("knife", 1, 50, 51, 0), "the ground item too");
    CHECK(!edited_spawn_at("rat", 0, 51, 50, 0), "a suppressed spawn is not live and is not carried");
    CHECK(!edited_spawn_at("man", 0, 50, 51, 1) && !edited_spawn_at("man", 0, 50, 51, 0),
        "nor one standing on another level");
    CHECK(strstr(status(), "3 spawns") != NULL && strstr(status(), "at 3250,3250") != NULL,
        "the status bar says how many and where (%s)", status());

    g0 = groups();
    CHECK(edp_rect_paste_at(&g_ui, BASE_X + 50, BASE_Z + 50, 0) == 0, "the same paste again changes nothing");
    CHECK(groups() == g0, "no second npc on each tile, and no history row");
    CHECK(strstr(status(), "already there") != NULL, "said in the status bar (%s)", status());

    Editor_Undo(&g_editor);
    CHECK(!edited_spawn_at("hans", 0, 50, 50, 0) && !edited_spawn_at("goblin", 0, 51, 51, 0)
              && !edited_spawn_at("knife", 1, 50, 51, 0),
        "one undo takes the pasted spawns back with the tiles");
    CHECK(edited_spawn_at("hans", 0, 30, 30, 0), "the original stays where it was");

    /* Off: the copy holds none, and says so. */
    edp_rect_set_with_spawns(0);
    CHECK(edp_rect_copy(&g_ui) == 4 && edp_stamp_clip_spawns() == 0, "with the box off Ctrl+C leaves the spawns");
    CHECK(strstr(status(), "spawns stay behind") != NULL, "and says so (%s)", status());
    edp_rect_set_with_spawns(1);
    g_ui.tools.rect.valid = 0;
}

/* ---- level move --------------------------------------------------------------- */

static void
test_level_move(void)
{
    static const int k_wall[1][4] = { { 10, 10, 0, 0 } };
    static const int k_second[1][4] = { { 11, 10, 0, 0 } };
    int g0;

    select_locs(k_wall, 1);
    g0 = groups();
    CHECK(edp_locs_level_move(&g_ui, 1) == 1, "Ctrl+PgUp moves the wall up a level");
    CHECK(loc_at(10, 10, 1, 0, NULL) == 1902 && loc_at(10, 10, 0, 0, NULL) < 0, "it stands on level 1 now");
    CHECK(loc_at(10, 10, 1, 22, NULL) == 1903, "beside the level's own decor (another shape, another key)");
    CHECK(g_ui.tools.loc_count == 1 && g_ui.tools.locs[0].level == 1, "the selection follows it");
    CHECK(groups() == g0 + 1, "one undo step");
    CHECK(edp_locs_level_move(&g_ui, 1) == 1 && edp_locs_level_move(&g_ui, 1) == 1, "up to level 3");
    g0 = groups();
    CHECK(edp_locs_level_move(&g_ui, 1) == 0, "there is no level 4");
    CHECK(strstr(status(), "refused") && strstr(status(), "no level above"), "said in the status bar (%s)", status());
    CHECK(groups() == g0, "and no history row");
    CHECK(edp_locs_level_move(&g_ui, -1) == 1 && edp_locs_level_move(&g_ui, -1) == 1
              && edp_locs_level_move(&g_ui, -1) == 1,
        "Ctrl+PgDn brings it back down");
    CHECK(loc_at(10, 10, 0, 0, NULL) == 1902, "home");

    select_locs(k_second, 1);
    g0 = groups();
    CHECK(edp_locs_level_move(&g_ui, 1) == 0, "the second wall cannot go up: 11,10 L1 holds a wall");
    CHECK(strstr(status(), "already holds loc 1904") != NULL, "the status names the holder (%s)", status());
    CHECK(groups() == g0 && loc_at(11, 10, 0, 0, NULL) == 1902, "nothing moved");

    /* Both walls of a stack go up together: the top one first. */
    {
        static const int k_stack[2][4] = { { 11, 10, 0, 0 }, { 11, 10, 1, 0 } };
        select_locs(k_stack, 2);
        CHECK(edp_locs_level_move(&g_ui, 1) == 2, "a stack moves up as one");
        CHECK(loc_at(11, 10, 1, 0, NULL) == 1902 && loc_at(11, 10, 2, 0, NULL) == 1904, "each a level higher");
        Editor_Undo(&g_editor);
        CHECK(loc_at(11, 10, 0, 0, NULL) == 1902 && loc_at(11, 10, 1, 0, NULL) == 1904, "and one undo takes both back");
    }
}

/* ---- the gizmo's actions and Find same ---------------------------------------- */

static void
test_gizmo_actions(void)
{
    static const int k_wall[1][4] = { { 10, 10, 0, 0 } };
    int rotation = -1;
    int g0;

    select_locs(k_wall, 1);
    g0 = groups();
    CHECK(edp_locs_duplicate_by(&g_ui, 0, 3) == 1, "Alt-arrow north three copies the wall three tiles north");
    CHECK(loc_at(10, 13, 0, 0, &rotation) == 1902 && rotation == 1, "the copy stands there, as the original");
    CHECK(loc_at(10, 10, 0, 0, NULL) == 1902, "and the original stays");
    CHECK(g_ui.tools.locs[0].z == 13, "the copy is the selection");
    CHECK(edp_locs_rotate_by(&g_ui, 2) == 1, "the ring turns it half round");
    CHECK(loc_at(10, 13, 0, 0, &rotation) == 1902 && rotation == 3, "1 -> 3 (%d)", rotation);
    CHECK(groups() == g0 + 2, "two steps");
    Editor_Undo(&g_editor);
    Editor_Undo(&g_editor);
    CHECK(loc_at(10, 13, 0, 0, NULL) < 0, "both undone");

    select_locs(k_wall, 1);
    CHECK(edp_locs_find_same(&g_ui) == 2, "Find same selects both walls of id 1902 on level 0");
    CHECK(g_ui.tools.loc_count == 2 && g_ui.tools.locs[0].x == 10 && g_ui.tools.locs[1].x == 11,
        "the primary stays first");
    CHECK(strstr(status(), "selected 2 loc 1902") != NULL, "and says so (%s)", status());
}

/* ---- the panels --------------------------------------------------------------- */

static void
test_panels(void)
{
    static const int k_two[2][4] = { { 10, 10, 0, 0 }, { 12, 11, 0, 10 } };
    int const locs = module_panel("locs");
    int const catalog = module_panel("catalog");
    int const stamps = module_panel("stamps");
    int up;
    int down;
    int find;
    int save;
    int follow;
    int list;
    int strip;
    int chip;
    int footprint;
    int stamp_list;

    CHECK(locs >= 0 && catalog >= 0 && stamps >= 0, "Locs, Catalog and Stamps are registered");
    if( locs < 0 || catalog < 0 || stamps < 0 )
        return;
    up = find_widget(locs, TORIRS_CHROME_W_BUTTON, "Up a level");
    down = find_widget(locs, TORIRS_CHROME_W_BUTTON, "Down a level");
    find = find_widget(locs, TORIRS_CHROME_W_BUTTON, "Find same");
    save = find_widget(locs, TORIRS_CHROME_W_BUTTON, "Save as stamp...");
    follow = find_widget(locs, TORIRS_CHROME_W_CHECKBOX, "Follow");
    list = find_widget(locs, TORIRS_CHROME_W_VLIST, NULL);
    strip = find_widget(locs, TORIRS_CHROME_W_GRAPH, NULL);
    chip = find_widget(catalog, TORIRS_CHROME_W_BUTTON, "Stamps...");
    stamp_list = find_widget(stamps, TORIRS_CHROME_W_VLIST, NULL);
    CHECK(up >= 0 && down >= 0 && find >= 0 && save >= 0 && follow >= 0 && list >= 0 && strip >= 0 && chip >= 0
              && stamp_list >= 0,
        "the new rows are there");
    CHECK(find_widget(stamps, TORIRS_CHROME_W_TEXTINPUT, "Find") >= 0 && find_widget(locs, TORIRS_CHROME_W_TEXTINPUT, "Find") >= 0,
        "both lists have a search box");
    if( up < 0 || down < 0 || find < 0 || save < 0 || follow < 0 || list < 0 || strip < 0 || chip < 0 || stamp_list < 0 )
        return;
    CHECK(strcmp(g_chrome.widgets[up].chord, "Ctrl+PageUp") == 0 || g_chrome.widgets[up].chord[0],
        "Up a level wears its chord (%s)", g_chrome.widgets[up].chord);

    select_locs(k_two, 2);
    frame();
    frame();
    footprint = -1;
    for( int i = 0; i < g_chrome.widget_count; i++ )
        if( g_chrome.widgets[i].panel == locs && g_chrome.widgets[i].kind == TORIRS_CHROME_W_LABEL
            && strncmp(g_chrome.widgets[i].text, "Footprint", 9) == 0 )
            footprint = i;
    CHECK(footprint >= 0, "the footprint row");
    /* The primary is the wall: the faked record says 1x1 and blocking. */
    CHECK(footprint >= 0 && strcmp(g_chrome.widgets[footprint].text, "Footprint 1x1 | wall layer | blocks: yes") == 0,
        "the footprint row (%s)", footprint >= 0 ? g_chrome.widgets[footprint].text : "");
    for( int i = 0; i < g_chrome.widget_count; i++ )
        if( g_chrome.widgets[i].panel == locs && g_chrome.widgets[i].kind == TORIRS_CHROME_W_LABEL
            && strncmp(g_chrome.widgets[i].text, "Multiloc", 8) == 0 )
            CHECK(strcmp(g_chrome.widgets[i].text, "Multiloc: no shell") == 0, "the multiloc row (%s)",
                g_chrome.widgets[i].text);

    /* Type flags: the primary's record, each edit one undo step. */
    {
        int const share = find_widget(locs, TORIRS_CHROME_W_CHECKBOX, "Share light");
        int const width = find_widget(locs, TORIRS_CHROME_W_TEXTINPUT, "Wall width");
        int const contour = find_widget(locs, TORIRS_CHROME_W_DROPDOWN, "Contour");
        int const height = find_widget(locs, TORIRS_CHROME_W_TEXTINPUT, "Contour height");
        int const apply = find_widget(locs, TORIRS_CHROME_W_BUTTON, "Apply values");
        int const before = groups();
        CHECK(share >= 0 && width >= 0 && contour >= 0 && height >= 0 && apply >= 0, "the Type flags rows");
        if( share < 0 || width < 0 || contour < 0 || height < 0 || apply < 0 )
            return;
        CHECK(!g_chrome.widgets[share].hidden, "shown with a loc selected");
        {
            int const north = find_widget(locs, TORIRS_CHROME_W_BUTTON, "Nudge north");
            CHECK(north >= 0 && g_chrome.widgets[north].w > 0 && g_chrome.widgets[north].w == g_chrome.widgets[list].w,
                "a command button is as wide as its row (%d, list %d)", north >= 0 ? g_chrome.widgets[north].w : -1,
                g_chrome.widgets[list].w);
        }

        ToriRSChrome_SetChecked(&g_chrome, share, 1);
        activate(share);
        CHECK(wall_block_has("sharelight=1"), "Share light writes the record");
        CHECK(groups() == before + 1, "as one undo step");
        frame();

        ToriRSChrome_SetText(&g_chrome, width, "32");
        ToriRSChrome_DropdownSetSelected(&g_chrome, contour, 2);
        ToriRSChrome_SetText(&g_chrome, height, "512");
        activate(apply);
        CHECK(wall_block_has("wallwidth=32"), "Apply writes Wall width");
        CHECK(wall_block_has("contourgroundtype=2") && wall_block_has("contourground=512")
                  && wall_block_has("contourgroundparam=512"),
            "Keep height writes all three contour keys, as cachepack requires");
        CHECK(groups() == before + 2, "all of it one undo step");
        frame();
        CHECK(strcmp(ToriRSChrome_Text(&g_chrome, height), "512") == 0, "the height reads back from the record");

        ToriRSChrome_SetText(&g_chrome, height, "100");
        activate(apply);
        CHECK(groups() == before + 2 && wall_block_has("contourground=512"),
            "a height cachepack would refuse is refused here");
        frame();

        Editor_Undo(&g_editor);
        frame();
        CHECK(!wall_block_has("wallwidth=32") && !wall_block_has("contourgroundtype=2"), "undo takes the values back");
        CHECK(ToriRSChrome_Checked(&g_chrome, share), "and Share light is still on");
        Editor_Undo(&g_editor);
        frame();
        CHECK(!wall_block_has("sharelight=1") && !ToriRSChrome_Checked(&g_chrome, share),
            "undo unticks Share light from the record");
    }

    /* Follow: a row click puts the camera on that loc. */
    g_ui.app_services.camera_to_tile = fake_camera_to_tile;
    ToriRSChrome_SetChecked(&g_chrome, follow, 1);
    ToriRSChrome_VListSetSelected(&g_chrome, list, 1);
    activate(list);
    CHECK(g_camera_x == BASE_X + 12 && g_camera_z == BASE_Z + 11, "Follow re-frames the camera on row 2 (%d,%d)",
        g_camera_x, g_camera_z);

    /* The strip's thumbnails click: slot 2's shape for the selection. */
    {
        int shapes[EDITOR_LOC_SHAPE_COUNT];
        int loc_id;
        int view;
        int const count = Editor_Locs_ShapeStrip(&g_ui, &loc_id, shapes, &view);
        static const int k_one[1][4] = { { 12, 11, 0, 10 } };
        struct ToriRSChromeEvent event;
        int target = -1;
        select_locs(k_one, 1);
        frame();
        CHECK(Editor_Locs_ShapeStrip(&g_ui, &loc_id, shapes, &view) > 1 && view == strip, "the strip is the GRAPH");
        (void)count;
        for( int i = 0; i < EDITOR_LOC_SHAPE_COUNT && i < 16; i++ )
            if( shapes[i] == 11 )
                target = i;
        CHECK(target >= 0, "the table's strip offers the diagonal centrepiece");
        Editor_Locs_ShapeStripSprite(&g_ui, 0x40000100, 224, 44, 14, 1);
        memset(&event, 0, sizeof(event));
        event.kind = TORIRS_CHROME_EVENT_GRAPH_PRESS;
        event.widget = strip;
        event.serial = g_chrome.widgets[strip].serial;
        event.x = target * 14 + 5;
        event.y = 20;
        event.key = ToriRSChrome_GraphKeyAt(&g_chrome, strip, event.x, event.y);
        CHECK(event.key == target + 1, "the thumbnail under the pointer is keyed (%d)", event.key);
        (void)ToriRSChrome_PushEvent(&g_chrome, &event);
        frame();
        CHECK(loc_at(12, 11, 0, 11, NULL) == 2097 && loc_at(12, 11, 0, 10, NULL) < 0,
            "a click on its thumbnail reshapes the table to 11");
        Editor_Undo(&g_editor);
    }

    /* The Catalog's chip opens the Stamps list, which lists smithy. */
    activate(chip);
    CHECK(g_chrome.panels[stamps].visible, "the Stamps... chip opens the Stamps panel");
    frame();
    frame();
    CHECK(g_chrome.widgets[stamp_list].imax == 1, "it lists one stamp (%d)", g_chrome.widgets[stamp_list].imax);
    ToriRSChrome_VListSetSelected(&g_chrome, stamp_list, 0);
    activate(stamp_list);
    CHECK(edp_stamp_armed(&g_ui) != NULL, "Enter on its row arms it");
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_SELECT);
    (void)save;
    (void)find;
    (void)down;
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
    CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_SQUARE, "m50_50"), "the synthetic square opens");
    CHECK(square()->loaded && square()->loc_count == 5, "with its five locs");

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1530, 1006);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.app_services.user_data = &g_app_stand_in;
    g_ui.app_services.loc_record = fake_loc_record;
    Editor_UiRegisterToolsPanels(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    frame();

    test_stamp_save_turn_drop(root);
    test_paste_carries_locs();
    test_paste_carries_spawns();
    test_level_move();
    test_gizmo_actions();
    test_panels();

    Editor_UiFree(&g_ui);
    Editor_Close(&g_editor);
    printf("editor_stamps_ops_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
