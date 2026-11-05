/*
 * The square picker's navigation and panes (workbench step 6.4, design
 * section 5 "Navigation"; section 16's check for 6.4 in part):
 *
 *   Find       the parser over every form it takes, and the resolver over a
 *              synthetic atlas: square, tile (pinned), area, region,
 *              collection, lane, instance space, and the refusals
 *   focus      the arrows over the atlas through the well's keys (the K rows
 *              of the hotkey table), Enter / Shift+Enter / Backspace /
 *              Escape, the view following the focus, the focus outlined
 *   strip      the collapse rule (pure), then the open-set strip's chips: in
 *              open order, a dot on the unsaved, "+N" for the rest and never
 *              a chip past the track, click goes, Shift+click closes through
 *              the guard, a drag reorders
 *   pane       every row measured against its track: none longer without an
 *              ellipsis, a cut row's tooltip says it whole; Recent
 *   Area       the dropdown's "lane: <name>" and "Instance space" rows
 *
 *   ./editor_picker_test
 *
 * Headless and content-free, like editor_chords_test.c: the real chrome
 * model and every tools-panels module registered with no session, the atlas
 * handed in (Editor_SquarePicker_TakeAtlas), the App's hooks recorded.
 */

#include "editor/editor_hotkeys.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_picker.h"
#include "editor/panels/edpanel_picker_find.h"
#include "editor/panels/edpanel_picker_nav.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "ui/uitree_debug_overlay.h"

#include <math.h>
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

/* ---- the synthetic atlas ------------------------------------------------------------
 *
 *   the surface   m48_48 .. m53_53 (36 squares), area 0 "main"
 *   a dungeon     m51_150, area 1 (a long display name, for the pane)
 *   unmapped      m30_30
 *   instance      m5_120 (in the instance space, unmapped)
 *   a lane        ported/mylane/maps: m50_50 (area 0) and m16_101 (unmapped)
 */
static const char* const k_storages[2] = { "maps", "ported/mylane/maps" };
static struct EditorHost_CatalogArea g_areas[2];
static struct EditorHost_CatalogSquare g_rows[64];
static int g_area0[1] = { 0 };
static int g_area1[1] = { 1 };
static int g_row_count;
static struct EditorHost_CatalogSquares g_atlas;

#define LONG_AREA "The Very Long Name Of A Dungeon Beneath Lumbridge Castle Cellars"

static void
atlas_row(
    int map_x,
    int map_z,
    int storage,
    int area)
{
    struct EditorHost_CatalogSquare* row = &g_rows[g_row_count++];

    assert(g_row_count <= 64);
    memset(row, 0, sizeof(*row));
    row->map_x = map_x;
    row->map_z = map_z;
    row->storage = storage;
    row->flags = 0;
    row->spawn_count = 3;
    row->rgb = 0x406020;
    row->primary_area = area;
    row->area_count = area >= 0 ? 1 : 0;
    row->areas = area == 0 ? g_area0 : area == 1 ? g_area1 : NULL;
}

static void
atlas_build(void)
{
    g_row_count = 0;
    memset(g_areas, 0, sizeof(g_areas));
    g_areas[0].internal_name = "main";
    g_areas[0].display_name = "Gielinor Surface";
    g_areas[0].is_main = 1;
    g_areas[1].internal_name = "lumbridge_cellar";
    g_areas[1].display_name = LONG_AREA;
    for( int z = 48; z <= 53; z++ )
        for( int x = 48; x <= 53; x++ )
            atlas_row(x, z, 0, 0);
    atlas_row(51, 150, 0, 1);
    atlas_row(30, 30, 0, -1);
    atlas_row(5, 120, 0, -1);
    atlas_row(50, 50, 1, 0);
    atlas_row(16, 101, 1, -1);
    memset(&g_atlas, 0, sizeof(g_atlas));
    g_atlas.storage_count = 2;
    g_atlas.storages = k_storages;
    g_atlas.area_count = 2;
    g_atlas.areas = g_areas;
    g_atlas.square_count = g_row_count;
    g_atlas.squares = g_rows;
}

/* ---- Find, the parser ------------------------------------------------------------- */

static void
test_find_parse(void)
{
    struct Editor_PickerFindQuery q;

    CHECK(Editor_PickerFindParse("m50_50", &q) && q.kind == EDITOR_PICKER_FIND_SQUARE && q.map_x == 50
              && q.map_z == 50,
        "parse: m50_50 is a square");
    CHECK(Editor_PickerFindParse("  M51_49 ", &q) && q.kind == EDITOR_PICKER_FIND_SQUARE && q.map_x == 51
              && q.map_z == 49,
        "parse: M51_49, blanks round it, is a square");
    CHECK(Editor_PickerFindParse("50 50", &q) && q.kind == EDITOR_PICKER_FIND_SQUARE && q.map_x == 50,
        "parse: '50 50' is a square");
    CHECK(Editor_PickerFindParse("50,51", &q) && q.kind == EDITOR_PICKER_FIND_SQUARE && q.map_z == 51,
        "parse: '50,51' is a square");
    CHECK(Editor_PickerFindParse("3222 3218", &q) && q.kind == EDITOR_PICKER_FIND_TILE && q.tile_x == 3222
              && q.tile_z == 3218 && q.level == 0 && q.map_x == 50 && q.map_z == 50,
        "parse: '3222 3218' is a tile of m50_50 (%d %d -> m%d_%d)", q.tile_x, q.tile_z, q.map_x, q.map_z);
    CHECK(Editor_PickerFindParse("3222, 3218, 2", &q) && q.kind == EDITOR_PICKER_FIND_TILE && q.level == 2,
        "parse: a tile with a level");
    CHECK(!Editor_PickerFindParse("3222 3218 4", &q), "parse: level 4 is no level");
    CHECK(!Editor_PickerFindParse("1 2 3 4", &q), "parse: four numbers are nothing");
    CHECK(!Editor_PickerFindParse("42", &q), "parse: one number is nothing");
    CHECK(!Editor_PickerFindParse("3222x", &q), "parse: digits run into letters are nothing");
    CHECK(!Editor_PickerFindParse("m50", &q), "parse: m50 has no z");
    CHECK(!Editor_PickerFindParse("m300_2", &q), "parse: m300_2 is past the world");
    CHECK(!Editor_PickerFindParse("99999 3", &q), "parse: a tile past the world");
    CHECK(!Editor_PickerFindParse("", &q), "parse: empty");
    CHECK(!Editor_PickerFindParse("   ", &q), "parse: blanks");
    CHECK(Editor_PickerFindParse("lane:mylane", &q) && q.kind == EDITOR_PICKER_FIND_LANE
              && strcmp(q.name, "mylane") == 0,
        "parse: lane:mylane ('%s')", q.name);
    CHECK(Editor_PickerFindParse(" LANE: my lane ", &q) && q.kind == EDITOR_PICKER_FIND_LANE
              && strcmp(q.name, "my lane") == 0,
        "parse: LANE: is a lane, the name trimmed ('%s')", q.name);
    CHECK(!Editor_PickerFindParse("lane:", &q), "parse: lane: with no name");
    CHECK(!Editor_PickerFindParse("lane:   ", &q), "parse: lane: with blanks");
    CHECK(Editor_PickerFindParse("instance:", &q) && q.kind == EDITOR_PICKER_FIND_INSTANCE && q.name[0] == '\0',
        "parse: instance: is the instance space");
    CHECK(Editor_PickerFindParse("Instance: tutorial cave", &q) && q.kind == EDITOR_PICKER_FIND_INSTANCE
              && strcmp(q.name, "tutorial cave") == 0,
        "parse: instance:<name> ('%s')", q.name);
    CHECK(Editor_PickerFindParse("Lumbridge", &q) && q.kind == EDITOR_PICKER_FIND_NAME
              && strcmp(q.name, "Lumbridge") == 0,
        "parse: a name");
    CHECK(Editor_PickerFindParse("Quest: cooks", &q) && q.kind == EDITOR_PICKER_FIND_NAME,
        "parse: a collection's name with a colon is a name");
}

/* ---- Find, the resolver ------------------------------------------------------------ */

static int
tab_of_area(
    const struct Editor_Picker* picker,
    int area)
{
    for( int t = 0; t < picker->tab_count; t++ )
        if( picker->tabs[t].area == area )
            return t;
    return -1;
}

static void
test_find_resolve(void)
{
    struct Editor_Picker picker;
    struct Editor_PickerFindQuery q;
    struct Editor_PickerFindMatch m;
    const char* const names[2] = { "Quest: cooks", "Smithing" };
    int square;

    Editor_PickerInit(&picker);
    atlas_build();
    (void)Editor_PickerSetAtlas(&picker, &g_atlas);
    CHECK(picker.tab_count >= 4, "resolve: the atlas is cut into pages (%d)", picker.tab_count);

    CHECK(Editor_PickerFindParse("m50_50", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m),
        "resolve: m50_50 matches");
    square = Editor_PickerFind(&picker, 50, 50, 0);
    CHECK(m.kind == EDITOR_PICKER_MATCH_CELL && m.square == square && !m.pin_valid,
        "resolve: m50_50 is the maps/ square, unpinned (%d vs %d)", m.square, square);
    CHECK(Editor_PickerFindParse("m99_99", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_CELL && m.square == -1 && m.map_x == 99,
        "resolve: an empty cell is a cell New can make");
    CHECK(Editor_PickerFindParse("3222 3218 1", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_CELL && m.pin_valid && m.pin_x == 3222 && m.pin_z == 3218
              && m.pin_level == 1 && m.square == square,
        "resolve: a tile pins it, in its square");
    CHECK(Editor_PickerFindParse("m51_150", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.tab == tab_of_area(&picker, 1),
        "resolve: a dungeon square shows on its page (%d)", m.tab);
    picker.storage_filter = 1;
    CHECK(Editor_PickerFindParse("m50_50", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.square == Editor_PickerFind(&picker, 50, 50, 1),
        "resolve: from a lane's page, m50_50 is the lane's copy");
    picker.storage_filter = -1;

    CHECK(Editor_PickerFindParse("gielinor surface", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_AREA && m.tab == tab_of_area(&picker, 0),
        "resolve: an area's name, any case");
    CHECK(Editor_PickerFindParse("lumbridge_cellar", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_AREA && m.tab == tab_of_area(&picker, 1),
        "resolve: an area's internal name");
    CHECK(Editor_PickerFindParse("Beneath", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_AREA && m.tab == tab_of_area(&picker, 1),
        "resolve: part of a region's name");
    CHECK(Editor_PickerFindParse("quest: COOKS", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_COLLECTION && m.collection == 0,
        "resolve: a collection's name");
    CHECK(Editor_PickerFindParse("smith", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_COLLECTION && m.collection == 1,
        "resolve: part of a collection's name");
    CHECK(Editor_PickerFindParse("lane:mylane", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_LANE && m.storage == 1,
        "resolve: lane:mylane is storage 1");
    CHECK(Editor_PickerFindParse("lane:MY", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_LANE && m.storage == 1,
        "resolve: part of a lane's name");
    CHECK(Editor_PickerFindParse("lane:nope", &q) && !Editor_PickerFindResolve(&picker, &q, names, 2, &m),
        "resolve: no such lane");
    CHECK(Editor_PickerFindParse("lane:maps", &q) && !Editor_PickerFindResolve(&picker, &q, names, 2, &m),
        "resolve: maps/ is no lane");
    CHECK(Editor_PickerFindParse("instance:", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_INSTANCE,
        "resolve: instance: is the instance space");
    CHECK(Editor_PickerFindParse("instance: smithing", &q) && Editor_PickerFindResolve(&picker, &q, names, 2, &m)
              && m.kind == EDITOR_PICKER_MATCH_COLLECTION,
        "resolve: instance:<name> names a collection");
    CHECK(Editor_PickerFindParse("nothing is called this", &q) && !Editor_PickerFindResolve(&picker, &q, names, 2, &m),
        "resolve: an unknown name matches nothing");
    CHECK(Editor_PickerFindParse("cooks", &q) && !Editor_PickerFindResolve(&picker, &q, NULL, 0, &m),
        "resolve: with no collections, no collection matches");
    {
        char lane[EDITOR_PICKER_NAME_MAX];
        CHECK(Editor_PickerLaneName("ported/mylane/maps", lane) && strcmp(lane, "mylane") == 0, "lane name");
        CHECK(Editor_PickerLaneName("ported/mylane/maps/m16_101", lane) && strcmp(lane, "mylane") == 0,
            "lane name of a key");
        CHECK(!Editor_PickerLaneName("maps", lane) && lane[0] == '\0', "maps/ has no lane name");
        Editor_PickerChipText("ported/mylane/maps/m16_101", lane);
        CHECK(strcmp(lane, "mylane/m16_101") == 0, "chip text of a lane's square ('%s')", lane);
        Editor_PickerChipText("m50_50", lane);
        CHECK(strcmp(lane, "m50_50") == 0, "chip text of maps/ ('%s')", lane);
    }
    Editor_PickerFree(&picker);
}

/* ---- focus, pure ---------------------------------------------------------------------- */

static void
test_focus_pure(void)
{
    struct Editor_Picker picker;
    int x;
    int z;

    Editor_PickerInit(&picker);
    atlas_build();
    (void)Editor_PickerSetAtlas(&picker, &g_atlas);
    Editor_PickerSetTab(&picker, tab_of_area(&picker, 0));
    Editor_PickerFit(&picker, 300, 300);
    CHECK(!Editor_PickerFocusCell(&picker, &x, &z), "focus: nothing at first");
    CHECK(Editor_PickerFocusStep(&picker, 1, 0), "focus: an arrow from nothing starts at the middle");
    CHECK(Editor_PickerFocusCell(&picker, &x, &z) && x >= 48 && x <= 53 && z >= 48 && z <= 53,
        "focus: the middle cell is the page's (m%d_%d)", x, z);
    Editor_PickerFocusSet(&picker, 50, 50);
    CHECK(picker.focus == Editor_PickerFind(&picker, 50, 50, 0), "focus: m50_50 is the maps/ square");
    CHECK(Editor_PickerSelectedCount(&picker) == 1 && picker.selected[picker.focus], "focus: the selection alone");
    CHECK(Editor_PickerFocusStep(&picker, 0, 1) && Editor_PickerFocusCell(&picker, &x, &z) && x == 50 && z == 51,
        "focus: north is +z (m%d_%d)", x, z);
    CHECK(Editor_PickerFocusStep(&picker, -1, 0) && Editor_PickerFocusCell(&picker, &x, &z) && x == 49 && z == 51,
        "focus: west is -x (m%d_%d)", x, z);
    for( int i = 0; i < 10; i++ )
        (void)Editor_PickerFocusStep(&picker, 1, 0);
    CHECK(Editor_PickerFocusCell(&picker, &x, &z) && x == 59 && picker.focus < 0 && picker.empty_valid,
        "focus: off the squares, the empty cell is focused (m%d_%d)", x, z);
    {
        int bx;
        int by;
        int bw;
        int bh;
        Editor_PickerCellBox(&picker, x, z, &bx, &by, &bw, &bh);
        CHECK(bx >= EDITOR_PICKER_LEFT && by >= EDITOR_PICKER_TOP && bx + bw <= picker.well_w
                  && by + bh <= picker.well_h,
            "focus: the view followed it into the well (%d,%d %dx%d in %dx%d)", bx, by, bw, bh, picker.well_w,
            picker.well_h);
    }
    Editor_PickerFocusSet(&picker, 0, 0);
    CHECK(!Editor_PickerFocusStep(&picker, -1, 0) && !Editor_PickerFocusStep(&picker, 0, -1),
        "focus: the world's edge holds it");
    Editor_PickerFocusSet(&picker, 255, 255);
    CHECK(!Editor_PickerFocusStep(&picker, 1, 1), "focus: ...at both edges");
    Editor_PickerFree(&picker);
}

/* ---- the strip's collapse rule, pure ----------------------------------------------------- */

static int
fixed_more(
    void* user,
    int hidden)
{
    (void)user;
    return hidden < 10 ? 30 : 38;
}

static void
test_strip_rule(void)
{
    int const widths[4] = { 50, 50, 50, 50 };
    struct Editor_PickerStripLayout lay;

    Editor_PickerStripLay(widths, 4, 230, 4, fixed_more, NULL, &lay);
    CHECK(lay.shown == 4 && lay.hidden == 0, "strip: four fit 230 (%d)", lay.shown);
    Editor_PickerStripLay(widths, 4, 212, 4, fixed_more, NULL, &lay);
    CHECK(lay.shown == 4, "strip: exactly the width fits (%d)", lay.shown);
    Editor_PickerStripLay(widths, 4, 211, 4, fixed_more, NULL, &lay);
    CHECK(lay.shown == 3 && lay.hidden == 1 && lay.more_x == 162 && lay.more_w == 30,
        "strip: a px short, three and '+1' (%d, +%d at %d)", lay.shown, lay.hidden, lay.more_x);
    Editor_PickerStripLay(widths, 4, 150, 4, fixed_more, NULL, &lay);
    CHECK(lay.shown == 2 && lay.hidden == 2 && lay.more_x + lay.more_w <= 150,
        "strip: 150 holds two beside '+2' (%d)", lay.shown);
    Editor_PickerStripLay(widths, 4, 40, 4, fixed_more, NULL, &lay);
    CHECK(lay.shown == 0 && lay.hidden == 4 && lay.more_x == 0, "strip: too narrow, all fold into '+4' (%d)",
        lay.shown);
    Editor_PickerStripLay(NULL, 0, 100, 4, fixed_more, NULL, &lay);
    CHECK(lay.shown == 0 && lay.hidden == 0, "strip: none");
    {
        int many[EDITOR_PICKER_OPEN_SET_MAX];
        for( int i = 0; i < EDITOR_PICKER_OPEN_SET_MAX; i++ )
            many[i] = 20 + i % 7;
        for( int track = 0; track < 900; track += 13 )
        {
            int right = 0;
            Editor_PickerStripLay(many, EDITOR_PICKER_OPEN_SET_MAX, track, 4, fixed_more, NULL, &lay);
            for( int i = 0; i < lay.shown; i++ )
                right = lay.x[i] + lay.w[i] > right ? lay.x[i] + lay.w[i] : right;
            if( lay.hidden > 0 )
                right = lay.more_x + lay.more_w > right ? lay.more_x + lay.more_w : right;
            CHECK(lay.shown + lay.hidden == EDITOR_PICKER_OPEN_SET_MAX, "strip: every chip is shown or counted");
            CHECK(right <= track || lay.shown == 0, "strip: nothing past track %d (right %d)", track, right);
        }
    }
    {
        struct Editor_PickerRecent recent;
        char key[16];
        memset(&recent, 0, sizeof(recent));
        for( int i = 0; i < 10; i++ )
        {
            snprintf(key, sizeof(key), "m%d_50", 40 + i);
            Editor_PickerRecentPush(&recent, key);
        }
        CHECK(recent.count == EDITOR_PICKER_RECENT_MAX && strcmp(recent.keys[0], "m49_50") == 0
                  && strcmp(recent.keys[7], "m42_50") == 0,
            "recent: the last eight, newest first ('%s' .. '%s')", recent.keys[0], recent.keys[7]);
        Editor_PickerRecentPush(&recent, "m45_50");
        CHECK(recent.count == EDITOR_PICKER_RECENT_MAX && strcmp(recent.keys[0], "m45_50") == 0
                  && strcmp(recent.keys[1], "m49_50") == 0 && strcmp(recent.keys[5], "m44_50") == 0,
            "recent: again moves to the front, once");
    }
}

/* ---- the panel ---------------------------------------------------------------------------- */

static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static int g_app_stand_in;
static int g_open_calls;
static int g_open_x;
static int g_open_z;
static int g_open_add;
static int g_close_calls;
static int g_close_x;
static int g_close_z;
static int g_camera_calls;
static int g_camera_x;
static int g_camera_z;

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
    g_open_calls++;
    g_open_x = map_x;
    g_open_z = map_z;
    g_open_add = add;
}

static void
fake_close_square(
    void* user,
    int map_x,
    int map_z,
    const char* storage)
{
    (void)user;
    (void)storage;
    g_close_calls++;
    g_close_x = map_x;
    g_close_z = map_z;
}

static int
fake_camera_to_tile(
    void* user,
    int tile_x,
    int tile_z,
    int level)
{
    (void)user;
    (void)level;
    g_camera_calls++;
    g_camera_x = tile_x;
    g_camera_z = tile_z;
    return 1;
}

static int
fake_spawn_panel(
    void* user_data,
    const char* module,
    const struct EditorViewCamera* camera,
    char* why,
    int why_size)
{
    (void)user_data;
    (void)module;
    (void)camera;
    (void)why;
    (void)why_size;
    return 1;
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
window_of(const char* module)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(&g_ui, module);
    return panel ? panel->chrome_panel : -1;
}

static int
widget_in(
    int panel,
    int kind,
    const char* caption)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( !caption || strcmp(w->text, caption) == 0 || strcmp(w->label, caption) == 0 )
            return i;
    }
    return -1;
}

static void
push_graph(
    int graph,
    int kind,
    int x,
    int y,
    int value)
{
    struct ToriRSChromeEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.widget = graph;
    event.serial = g_chrome.widgets[graph].serial;
    event.x = x;
    event.y = y;
    event.value = value;
    event.key = ToriRSChrome_GraphKeyAt(&g_chrome, graph, x, y);
    (void)ToriRSChrome_PushEvent(&g_chrome, &event);
}

/** A key on the well, through the chrome's own key path: the well takes
 *  the focus, the chrome hands the key on as GRAPH_KEY (and Escape lets go
 *  of the focus). */
static void
well_key(
    int graph,
    int chrome_key)
{
    if( g_chrome.focus != graph )
        (void)ToriRSChrome_FocusWidget(&g_chrome, graph, 0);
    CHECK(ToriRSChrome_KeyEdit(&g_chrome, chrome_key), "the focused well takes key %d", chrome_key);
    frame();
}

static struct Editor_Picker*
picker_of(void)
{
    return (struct Editor_Picker*)edp_squares_picker(&g_ui);
}

static void
boot(void)
{
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetScale(&g_chrome, 1);
    ToriRSChrome_SetSurface(&g_chrome, 1600, 1000);
    Editor_UiInit(&g_ui, &g_chrome, NULL);
    g_ui.app_services.user_data = &g_app_stand_in;
    g_ui.app_services.spawn_panel = fake_spawn_panel;
    g_ui.app_services.camera_to_tile = fake_camera_to_tile;
    g_ui.hooks.open_square = fake_open_square;
    g_ui.hooks.close_square = fake_close_square;
    Editor_UiRegisterToolsPanels(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    for( int i = 0; i < 5; i++ )
        frame();
    if( !g_chrome.panels[window_of("squares")].visible )
        (void)Editor_UiTogglePanel(&g_ui, "squares");
    if( !Editor_UiWorldShown(&g_ui) )
        (void)Editor_UiTogglePanel(&g_ui, "world");
    atlas_build();
    Editor_SquarePicker_TakeAtlas(&g_ui, &g_atlas);
    for( int i = 0; i < 5; i++ )
        frame();
}

/** The status sentence contains `needle`. */
static int
status_says(const char* needle)
{
    return strstr(Editor_UiStatusText(&g_ui), needle) != NULL;
}

static void
test_panel_find_and_keys(void)
{
    int const panel = window_of("squares");
    int const graph = widget_in(panel, TORIRS_CHROME_W_GRAPH, NULL);
    struct Editor_Picker* picker = picker_of();
    int x;
    int z;

    CHECK(panel >= 0 && g_chrome.panels[panel].visible, "panel: Squares is open");
    CHECK(picker && picker->square_count == g_row_count, "panel: the atlas is in (%d squares)",
        picker ? picker->square_count : -1);
    if( !picker )
        return;

    /* Find: a match is shown at least 24 px a square, focused, and the
     * well takes the keyboard for the arrows. */
    picker->zoom = 4.0;
    CHECK(Editor_SquarePicker_Find(&g_ui, "m50_50"), "find: m50_50");
    frame();
    CHECK(picker->focus == Editor_PickerFind(picker, 50, 50, 0), "find: m50_50 is focused");
    CHECK(picker->zoom >= EDITOR_PICKER_ZOOM_SHOW - 1e-9, "find: zoomed to at least %.0f px (%.2f)",
        (double)EDITOR_PICKER_ZOOM_SHOW, picker->zoom);
    CHECK(g_chrome.focus == graph, "find: the well holds the keyboard (%d vs %d)", g_chrome.focus, graph);
    {
        /* The focused cell's outline is the white one. */
        const struct ToriRSChromeWidget* wg = &g_chrome.widgets[graph];
        int bx;
        int by;
        int bw;
        int bh;
        int white = 0;
        Editor_PickerCellBox(picker, 50, 50, &bx, &by, &bw, &bh);
        for( int i = 0; i < wg->graph_count; i++ )
        {
            const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[wg->graph_first + i];
            if( it->kind == TORIRS_CHROME_GRAPH_RECT_OUTLINE && it->color == EDITOR_SQUARES_FOCUS_RGB
                && it->x == bx && it->y == by )
                white++;
        }
        CHECK(white == 1, "focus: the focused cell is outlined white (%d)", white);
    }

    /* The arrows move the focus through the table's K rows. */
    well_key(graph, TORIRS_CHROME_KEY_RIGHT);
    CHECK(Editor_PickerFocusCell(picker, &x, &z) && x == 51 && z == 50, "keys: Right is east (m%d_%d)", x, z);
    well_key(graph, TORIRS_CHROME_KEY_UP);
    CHECK(Editor_PickerFocusCell(picker, &x, &z) && x == 51 && z == 51, "keys: Up is north (m%d_%d)", x, z);
    well_key(graph, TORIRS_CHROME_KEY_LEFT);
    well_key(graph, TORIRS_CHROME_KEY_DOWN);
    CHECK(Editor_PickerFocusCell(picker, &x, &z) && x == 50 && z == 50, "keys: Left, Down back to m50_50");

    /* Enter opens, Shift+Enter adds: through the window's hook. */
    g_open_calls = 0;
    well_key(graph, TORIRS_CHROME_KEY_ENTER);
    CHECK(g_open_calls == 1 && g_open_x == 50 && g_open_z == 50 && !g_open_add, "keys: Enter opens m50_50 (%d calls)",
        g_open_calls);
    g_ui.shift_held = 1;
    well_key(graph, TORIRS_CHROME_KEY_RIGHT);
    CHECK(Editor_PickerFocusCell(picker, &x, &z) && x == 50 && z == 50,
        "keys: Shift+Right is no Picker chord: the focus stays (m%d_%d)", x, z);
    well_key(graph, TORIRS_CHROME_KEY_ENTER);
    g_ui.shift_held = 0;
    CHECK(g_open_calls == 2 && g_open_x == 50 && g_open_add, "keys: Shift+Enter adds m50_50 (%d calls, add %d)",
        g_open_calls, g_open_add);
    {
        /* Shift+Return from a presentation that knows the modifier (the
         * DOM's keydown carries it in `col`). */
        struct ToriRSChromeEvent ev;
        Editor_PickerFocusSet(picker, 51, 50);
        memset(&ev, 0, sizeof(ev));
        ev.kind = TORIRS_CHROME_EVENT_GRAPH_KEY;
        ev.widget = graph;
        ev.serial = g_chrome.widgets[graph].serial;
        ev.value = -TORIRS_CHROME_KEY_ENTER;
        ev.col = (int)EDITOR_HK_MOD_SHIFT;
        (void)ToriRSChrome_PushEvent(&g_chrome, &ev);
        frame();
        CHECK(g_open_calls == 3 && g_open_x == 51 && g_open_add,
            "keys: Shift+Enter with the DOM's modifier adds m51_50 (%d calls, add %d)", g_open_calls, g_open_add);
    }

    /* Recent: the squares opened, newest first. */
    {
        const char* keys[EDITOR_PICKER_RECENT_MAX];
        int const n = Editor_SquarePicker_Recent(&g_ui, keys, EDITOR_PICKER_RECENT_MAX);
        int const dropdown = widget_in(panel, TORIRS_CHROME_W_DROPDOWN, "Recent");
        CHECK(n == 2 && strcmp(keys[0], "m51_50") == 0 && strcmp(keys[1], "m50_50") == 0,
            "recent: m51_50 then m50_50 (%d)", n);
        CHECK(dropdown >= 0, "recent: the pane has its Recent row");
        if( dropdown >= 0 )
        {
            Editor_PickerFocusSet(picker, 30, 30);
            ToriRSChrome_DropdownSetSelected(&g_chrome, dropdown, 2);
            activate(dropdown);
            CHECK(Editor_PickerFocusCell(picker, &x, &z) && x == 50 && z == 50,
                "recent: choosing m50_50 goes to it (m%d_%d)", x, z);
        }
    }

    /* An empty cell: Enter is New square there, Shift+Enter is refused. */
    Editor_PickerFocusSet(picker, 56, 50);
    g_open_calls = 0;
    well_key(graph, TORIRS_CHROME_KEY_ENTER);
    CHECK(g_open_calls == 0 && g_chrome.panels[window_of("new_square")].visible,
        "keys: Enter on an empty cell opens New square");
    if( window_of("new_square") >= 0 )
        ToriRSChrome_PanelSetVisible(&g_chrome, window_of("new_square"), 0);
    g_ui.shift_held = 1;
    well_key(graph, TORIRS_CHROME_KEY_ENTER);
    g_ui.shift_held = 0;
    CHECK(status_says("Add refused"), "keys: Shift+Enter on an empty cell says why ('%s')",
        Editor_UiStatusText(&g_ui));

    /* Backspace with nothing open says why. */
    well_key(graph, TORIRS_CHROME_KEY_BACKSPACE);
    CHECK(status_says("Back refused"), "keys: Backspace with an empty open set says why ('%s')",
        Editor_UiStatusText(&g_ui));

    /* A tile pins; Escape clears Find and its pin. */
    CHECK(Editor_SquarePicker_Find(&g_ui, "3222 3218"), "find: a tile");
    frame();
    {
        int widget = 0;
        const char* full = "";
        int track = 0;
        CHECK(Editor_SquarePicker_PaneRow(&g_ui, 3, &widget, &full, &track) && strstr(full, "pin 3222,3218"),
            "find: the pane's Tiles row says the pin ('%s')", full);
        well_key(graph, TORIRS_CHROME_KEY_ESCAPE);
        CHECK(ToriRSChrome_Text(&g_chrome, widget_in(panel, TORIRS_CHROME_W_TEXTINPUT, "Find"))[0] == '\0',
            "keys: Escape clears Find");
        CHECK(Editor_SquarePicker_PaneRow(&g_ui, 3, &widget, &full, &track) && !strstr(full, "pin"),
            "keys: ...and the pin ('%s')", full);
        CHECK(g_chrome.focus != graph, "keys: Escape lets go of the keyboard");
    }

    /* The other Find forms through the panel. */
    CHECK(Editor_SquarePicker_Find(&g_ui, "lane:mylane"), "find: lane:mylane");
    frame();
    CHECK(picker->storage_filter == 1, "find: the lane's squares only (storage %d)", picker->storage_filter);
    CHECK(Editor_PickerSquareVisible(picker, Editor_PickerFind(picker, 16, 101, 1))
              && !Editor_PickerSquareVisible(picker, Editor_PickerFind(picker, 51, 51, 0)),
        "find: the lane's m16_101 shows, maps/ m51_51 does not");
    CHECK(Editor_SquarePicker_Find(&g_ui, "instance:"), "find: instance:");
    frame();
    {
        int vx;
        int vz;
        int vw;
        int vh;
        CHECK(picker->storage_filter == -1, "find: the instance space is every storage's");
        CHECK(picker->active_tab == Editor_PickerInstanceTab(picker), "find: instance: shows the Instance space page");
        /* Framed as far as the page's zoom range allows (the page here ends
         * at z 150), its squares in view. */
        CHECK(Editor_PickerVisibleSquares(picker, &vx, &vz, &vw, &vh) && vx <= EDITOR_PICKER_INSTANCE_X0
                  && vx + vw > EDITOR_PICKER_INSTANCE_X1 && vz <= 120 && vz + vh > 120,
            "find: the instance space is framed, m5_120 in view ([%d,%d %dx%d])", vx, vz, vw, vh);
    }
    CHECK(!Editor_SquarePicker_Find(&g_ui, "lane:nope") && status_says("no lane"),
        "find: an unknown lane says so ('%s')", Editor_UiStatusText(&g_ui));
    CHECK(!Editor_SquarePicker_Find(&g_ui, "m50"), "find: m50 is refused");
    CHECK(status_says("Find:"), "find: ...with a word ('%s')", Editor_UiStatusText(&g_ui));
    CHECK(Editor_SquarePicker_Find(&g_ui, "Gielinor"), "find: an area by name");
    frame();
    CHECK(picker->tabs[picker->active_tab].area == 0, "find: the surface's page");
}

static void
test_panel_area_rows(void)
{
    int const panel = window_of("squares");
    int const area = widget_in(panel, TORIRS_CHROME_W_DROPDOWN, "Area");
    struct Editor_Picker* picker = picker_of();
    const struct ToriRSChromeWidget* w;
    int lane_row = -1;
    int instance_row = -1;
    int instance_rows = 0;

    CHECK(area >= 0, "area: the dropdown");
    if( area < 0 || !picker )
        return;
    w = &g_chrome.widgets[area];
    for( int i = 0; i < w->option_count; i++ )
    {
        if( strcmp(w->options[i], "lane: mylane") == 0 )
            lane_row = i;
        if( strcmp(w->options[i], "Instance space") == 0 )
        {
            instance_row = i;
            instance_rows++;
        }
    }
    CHECK(lane_row == picker->tab_count, "area: 'lane: mylane' follows the pages (row %d of %d)", lane_row,
        w->option_count);
    /* The picker's own Instance space page is the row (its tab), once. */
    CHECK(instance_rows == 1 && instance_row == Editor_PickerInstanceTab(picker),
        "area: 'Instance space' is the picker's page, listed once (row %d of %d rows, tab %d)", instance_row,
        instance_rows, Editor_PickerInstanceTab(picker));
    if( lane_row < 0 || instance_row < 0 )
        return;
    ToriRSChrome_DropdownSetSelected(&g_chrome, area, lane_row);
    activate(area);
    CHECK(picker->storage_filter == 1 && ToriRSChrome_DropdownSelected(&g_chrome, area) == lane_row,
        "area: the lane row shows the lane's squares and stays chosen (%d, %d)", picker->storage_filter,
        ToriRSChrome_DropdownSelected(&g_chrome, area));
    ToriRSChrome_DropdownSetSelected(&g_chrome, area, instance_row);
    activate(area);
    CHECK(picker->storage_filter == -1 && ToriRSChrome_DropdownSelected(&g_chrome, area) == instance_row
              && picker->active_tab == Editor_PickerInstanceTab(picker),
        "area: Instance space leaves the lane, shows its page and stays chosen");
    ToriRSChrome_DropdownSetSelected(&g_chrome, area, 0);
    activate(area);
    CHECK(picker->active_tab == 0 && ToriRSChrome_DropdownSelected(&g_chrome, area) == 0, "area: a page again");
}

/** The pane: every row's shown text fits its track or ends in "...", and a
 *  cut row's tooltip is not empty. */
static void
check_pane(const char* when)
{
    for( int row = 0; row < EDITOR_SQUARES_PANE_ROWS; row++ )
    {
        int widget = 0;
        const char* full = "";
        int track = 0;
        const struct ToriRSChromeWidget* w;
        int width;
        size_t n;
        CHECK(Editor_SquarePicker_PaneRow(&g_ui, row, &widget, &full, &track), "%s: pane row %d", when, row);
        w = &g_chrome.widgets[widget];
        width = ToriRSChrome_MeasureTextQ(g_chrome.theme.font_row, g_chrome.scale_q, w->text);
        n = strlen(w->text);
        CHECK(track > 0, "%s: row %d has a track (%d)", when, row, track);
        CHECK(width <= track, "%s: row %d '%s' is %d px in a %d px track", when, row, w->text, width, track);
        if( strcmp(w->text, full) != 0 )
        {
            CHECK(n >= 3 && strcmp(w->text + n - 3, "...") == 0, "%s: row %d cut ends in an ellipsis ('%s')", when,
                row, w->text);
            CHECK(w->tooltip[0] != '\0', "%s: row %d cut says it whole in its tooltip", when, row);
        }
    }
}

static void
test_panel_pane(void)
{
    struct Editor_Picker* picker = picker_of();
    int const panel = window_of("squares");
    int widget = 0;
    const char* full = "";
    int track = 0;

    if( !picker )
        return;
    /* Content-sized, the pane is open. */
    ToriRSChrome_PanelSetFixedHeight(&g_chrome, panel, 0);
    for( int i = 0; i < 3; i++ )
        frame();
    CHECK(Editor_SquarePicker_PaneRow(&g_ui, 1, &widget, &full, &track) && !g_chrome.widgets[widget].hidden,
        "pane: open in a content-sized panel");
    /* The dungeon square: its area's name is longer than any track. */
    CHECK(Editor_SquarePicker_Find(&g_ui, "m51_150"), "pane: find the dungeon square");
    frame();
    frame();
    CHECK(Editor_SquarePicker_PaneRow(&g_ui, 2, &widget, &full, &track)
              && strncmp(full, LONG_AREA, 40) == 0,
        "pane: the Area row is its area ('%s')", full);
    CHECK(strcmp(g_chrome.widgets[widget].text, full) != 0, "pane: the long name was cut ('%s')",
        g_chrome.widgets[widget].text);
    check_pane("pane, 1x");
    CHECK(Editor_SquarePicker_PaneRow(&g_ui, 1, &widget, &full, &track) && strstr(full, "m51_150"),
        "pane: the Square row ('%s')", full);
    CHECK(Editor_SquarePicker_PaneRow(&g_ui, 3, &widget, &full, &track) && strcmp(full, "3264..3327, 9600..9663") == 0,
        "pane: the Tiles row ('%s')", full);
    CHECK(Editor_SquarePicker_PaneRow(&g_ui, 5, &widget, &full, &track) && strstr(full, "on disk"),
        "pane: the State row ('%s')", full);
    /* An empty cell. */
    Editor_PickerFocusSet(picker, 7, 130);
    frame();
    CHECK(Editor_SquarePicker_PaneRow(&g_ui, 2, &widget, &full, &track) && strstr(full, "instance space"),
        "pane: an empty cell in the instance space says so ('%s')", full);
    check_pane("pane, empty cell");
    /* A narrow panel: the rows are fitted again. */
    {
        struct ToriRSChromeRect const before = ToriRSChrome_PanelRect(&g_chrome, panel);
        ToriRSChrome_PanelSetFixedWidth(&g_chrome, panel, 230);
        Editor_PickerFocusSet(picker, 51, 150);
        for( int i = 0; i < 4; i++ )
            frame();
        CHECK(ToriRSChrome_PanelRect(&g_chrome, panel).w < before.w, "pane: the panel is narrower (%d -> %d)",
            before.w, ToriRSChrome_PanelRect(&g_chrome, panel).w);
        check_pane("pane, narrow");
        ToriRSChrome_PanelSetFixedWidth(&g_chrome, panel, before.w);
        for( int i = 0; i < 4; i++ )
            frame();
    }
    /* The pane's actions say why when they cannot. */
    Editor_PickerFocusSet(picker, 7, 130);
    activate(widget_in(panel, TORIRS_CHROME_W_BUTTON, "Copy to..."));
    CHECK(status_says("Copy to refused"), "pane: Copy to on an empty cell says why ('%s')", Editor_UiStatusText(&g_ui));
    Editor_PickerFocusSet(picker, 50, 50);
    activate(widget_in(panel, TORIRS_CHROME_W_BUTTON, "Delete..."));
    CHECK(status_says("Delete refused"), "pane: Delete says why ('%s')", Editor_UiStatusText(&g_ui));
    activate(widget_in(panel, TORIRS_CHROME_W_BUTTON, "Instance..."));
    CHECK(status_says("6.5") && g_chrome.panels[window_of("new_square")].visible,
        "pane: Instance opens New square and says the wizard is step 6.5 ('%s')", Editor_UiStatusText(&g_ui));
    ToriRSChrome_PanelSetVisible(&g_chrome, window_of("new_square"), 0);
    /* Given a short height, the rows past the heading fold away and the
     * atlas keeps its room; Open and Add stay; given room, they come back. */
    {
        int const graph = widget_in(panel, TORIRS_CHROME_W_GRAPH, NULL);
        int heading = 0;
        ToriRSChrome_PanelSetFixedHeight(&g_chrome, panel, 520);
        for( int i = 0; i < 4; i++ )
            frame();
        CHECK(Editor_SquarePicker_PaneRow(&g_ui, 1, &widget, &full, &track) && g_chrome.widgets[widget].hidden,
            "pane: folded in a 520 px panel");
        CHECK(Editor_SquarePicker_PaneRow(&g_ui, 0, &heading, &full, &track) && !g_chrome.widgets[heading].hidden,
            "pane: ...its heading stays");
        CHECK(!g_chrome.widgets[widget_in(panel, TORIRS_CHROME_W_BUTTON, "Open")].hidden
                  && g_chrome.widgets[widget_in(panel, TORIRS_CHROME_W_BUTTON, "Clear...")].hidden,
            "pane: ...Open stays, Clear... folds");
        CHECK(g_chrome.widgets[graph].view_h >= 120,
            "pane: ...and the atlas keeps at least its least height (%d)", g_chrome.widgets[graph].view_h);
        ToriRSChrome_PanelSetFixedHeight(&g_chrome, panel, 980);
        for( int i = 0; i < 4; i++ )
            frame();
        CHECK(Editor_SquarePicker_PaneRow(&g_ui, 1, &widget, &full, &track) && !g_chrome.widgets[widget].hidden,
            "pane: open again in a 980 px panel");
        CHECK(g_chrome.widgets[graph].view_h >= 200, "pane: the atlas has its room beside it (%d)",
            g_chrome.widgets[graph].view_h);
        check_pane("pane, 980 px");
        ToriRSChrome_PanelSetFixedHeight(&g_chrome, panel, 0);
        for( int i = 0; i < 3; i++ )
            frame();
    }
    {
        int const open = widget_in(panel, TORIRS_CHROME_W_BUTTON, "Open");
        int const add = widget_in(panel, TORIRS_CHROME_W_BUTTON, "Add to open set");
        int const one = EDITOR_HK_PICKER_OPEN;
        int const two = EDITOR_HK_PICKER_ADD;
        char want[EDITOR_HK_HINT_CHORD_MAX];
        Editor_HotkeysJoin(&g_ui.hotkeys, &one, 1, want, (int)sizeof(want));
        CHECK(open >= 0 && want[0] && strcmp(g_chrome.widgets[open].chord, want) == 0,
            "pane: Open wears picker.open's chord ('%s', want '%s')", open >= 0 ? g_chrome.widgets[open].chord : "-",
            want);
        Editor_HotkeysJoin(&g_ui.hotkeys, &two, 1, want, (int)sizeof(want));
        CHECK(add >= 0 && want[0] && strcmp(g_chrome.widgets[add].chord, want) == 0,
            "pane: Add wears picker.add's chord ('%s', want '%s')", add >= 0 ? g_chrome.widgets[add].chord : "-",
            want);
    }
}

/** The strip's GRAPH items: the "+N" text, chip texts in order, and the
 *  rightmost edge drawn. */
static int
strip_items(
    int strip,
    char texts[][EDITOR_PICKER_NAME_MAX],
    int max,
    int* out_right,
    int* out_more)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[strip];
    int n = 0;

    *out_right = 0;
    *out_more = 0;
    for( int i = 0; i < w->graph_count; i++ )
    {
        const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
        if( it->kind == TORIRS_CHROME_GRAPH_RECT && it->key > 0 && it->x + it->w > *out_right )
            *out_right = it->x + it->w;
        if( it->kind != TORIRS_CHROME_GRAPH_TEXT )
            continue;
        if( it->text[0] == '+' )
            *out_more = atoi(it->text + 1);
        else if( n < max )
            snprintf(texts[n++], EDITOR_PICKER_NAME_MAX, "%s", it->text);
    }
    return n;
}

static void
test_panel_strip(void)
{
    struct Editor_Picker* picker = picker_of();
    int const panel = window_of("squares");
    char texts[EDITOR_PICKER_OPEN_SET_MAX][EDITOR_PICKER_NAME_MAX];
    int strip = 0;
    int count = 0;
    int shown = 0;
    int right;
    int more;
    int n;

    if( !picker )
        return;
    CHECK(Editor_SquarePicker_Strip(&g_ui, &strip, &count, &shown) && count == 0, "strip: empty at first (%d)", count);
    n = strip_items(strip, texts, EDITOR_PICKER_OPEN_SET_MAX, &right, &more);
    CHECK(n == 1 && strstr(texts[0], "nothing open"), "strip: says it holds nothing ('%s')", n ? texts[0] : "-");

    /* What the window's mirror would say (Editor_PickerNoteDocument): three
     * squares in its world, the second unsaved. */
    picker->squares[Editor_PickerFind(picker, 50, 50, 0)].flags |= EDITOR_PICKER_OPEN_HERE;
    frame();
    picker->squares[Editor_PickerFind(picker, 51, 50, 0)].flags |= EDITOR_PICKER_OPEN_HERE | EDITOR_PICKER_DIRTY_HERE;
    frame();
    picker->squares[Editor_PickerFind(picker, 16, 101, 1)].flags |= EDITOR_PICKER_OPEN_HERE;
    frame();
    CHECK(Editor_SquarePicker_Strip(&g_ui, &strip, &count, &shown) && count == 3 && shown == 3,
        "strip: three chips (%d, %d shown)", count, shown);
    n = strip_items(strip, texts, EDITOR_PICKER_OPEN_SET_MAX, &right, &more);
    CHECK(n == 3 && strcmp(texts[0], "m50_50") == 0 && strcmp(texts[1], "m51_50") == 0
              && strcmp(texts[2], "mylane/m16_101") == 0,
        "strip: in open order ('%s' '%s' '%s')", n > 0 ? texts[0] : "-", n > 1 ? texts[1] : "-",
        n > 2 ? texts[2] : "-");
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[strip];
        int dots = 0;
        for( int i = 0; i < w->graph_count; i++ )
        {
            const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
            dots += it->kind == TORIRS_CHROME_GRAPH_RECT && it->w == 4 && it->h == 4 && it->key == 2;
        }
        CHECK(dots == 1, "strip: a dot on the unsaved chip alone (%d)", dots);
    }

    /* Click goes: the camera to the square's centre, the focus with it. */
    {
        int const y = 8;
        int cx;
        Editor_PickerFocusSet(picker, 30, 30);
        g_camera_calls = 0;
        cx = 0;
        for( int x = 0; x < 400; x++ )
            if( ToriRSChrome_GraphKeyAt(&g_chrome, strip, x, y) == 2 )
            {
                cx = x + 2;
                break;
            }
        push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_PRESS, cx, y, 0);
        push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_RELEASE, cx, y, 0);
        frame();
        CHECK(g_camera_calls == 1 && g_camera_x == 51 * 64 + 32 && g_camera_z == 50 * 64 + 32,
            "strip: a click goes to m51_50's centre (%d calls, %d,%d)", g_camera_calls, g_camera_x, g_camera_z);
        CHECK(picker->focus == Editor_PickerFind(picker, 51, 50, 0), "strip: ...and focuses it");

        /* Backspace: the open square before the focused one. */
        (void)ToriRSChrome_FocusWidget(&g_chrome, widget_in(panel, TORIRS_CHROME_W_GRAPH, NULL), 0);
        well_key(widget_in(panel, TORIRS_CHROME_W_GRAPH, NULL), TORIRS_CHROME_KEY_BACKSPACE);
        CHECK(picker->focus == Editor_PickerFind(picker, 50, 50, 0) && g_camera_x == 50 * 64 + 32,
            "keys: Backspace goes to the previous open square, m50_50");
        well_key(widget_in(panel, TORIRS_CHROME_W_GRAPH, NULL), TORIRS_CHROME_KEY_BACKSPACE);
        CHECK(picker->focus == Editor_PickerFind(picker, 16, 101, 1), "keys: ...and wraps to the last");

        /* Shift+click closes it, through the guard (nothing unsaved here:
         * no session, so it runs at once). */
        g_close_calls = 0;
        g_ui.shift_held = 1;
        push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_PRESS, cx, y, 0);
        push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_RELEASE, cx, y, 0);
        frame();
        g_ui.shift_held = 0;
        CHECK(g_close_calls == 1 && g_close_x == 51 && g_close_z == 50, "strip: Shift+click closes m51_50 (%d)",
            g_close_calls);

        /* A drag reorders: the first chip dropped on the third. */
        {
            int x0 = -1;
            int x2 = -1;
            for( int x = 0; x < 400; x++ )
            {
                int const key = ToriRSChrome_GraphKeyAt(&g_chrome, strip, x, y);
                if( key == 1 && x0 < 0 )
                    x0 = x + 2;
                if( key == 3 )
                    x2 = x + 30;
            }
            push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_PRESS, x0, y, 0);
            push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_DRAG, x0 + 20, y, 0);
            push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_DRAG, x2, y, 0);
            push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_RELEASE, x2, y, 0);
            g_camera_calls = 0;
            frame();
            n = strip_items(strip, texts, EDITOR_PICKER_OPEN_SET_MAX, &right, &more);
            CHECK(n == 3 && strcmp(texts[0], "m51_50") == 0 && strcmp(texts[2], "m50_50") == 0,
                "strip: a drag moved m50_50 last ('%s' '%s' '%s')", n > 0 ? texts[0] : "-", n > 1 ? texts[1] : "-",
                n > 2 ? texts[2] : "-");
            CHECK(g_camera_calls == 0, "strip: a drag is not a click");
        }
    }

    /* Many open: the strip folds the rest into "+N" and draws nothing past
     * its width. */
    for( int z = 48; z <= 53; z++ )
        for( int x = 48; x <= 53; x++ )
            picker->squares[Editor_PickerFind(picker, x, z, 0)].flags |= EDITOR_PICKER_OPEN_HERE;
    frame();
    CHECK(Editor_SquarePicker_Strip(&g_ui, &strip, &count, &shown) && count == 37, "strip: 37 chips (%d)", count);
    n = strip_items(strip, texts, EDITOR_PICKER_OPEN_SET_MAX, &right, &more);
    {
        int const scale = ToriRSChrome_Scale(&g_chrome);
        int const track = g_chrome.widgets[strip].w / (scale > 0 ? scale : 1);
        CHECK(shown < count && more == count - shown && n == shown,
            "strip: %d shown and '+%d' (%d texts, %d chips)", shown, more, n, count);
        CHECK(right <= track, "strip: no chip past the strip (right %d, track %d)", right, track);
        CHECK(strstr(g_chrome.widgets[strip].tooltip, "") != NULL, "strip: has a tooltip");
    }
    {
        /* Hovering the "+N" lists the rest in the tooltip. */
        int mx = -1;
        for( int x = 0; x < 1600; x++ )
            if( ToriRSChrome_GraphKeyAt(&g_chrome, strip, x, 8) == 1000 )
            {
                mx = x + 1;
                break;
            }
        CHECK(mx >= 0, "strip: the '+N' chip is keyed");
        push_graph(strip, TORIRS_CHROME_EVENT_GRAPH_HOVER, mx, 8, 0);
        frame();
        CHECK(strncmp(g_chrome.widgets[strip].tooltip, "also open:", 10) == 0,
            "strip: the '+N' tooltip names the rest ('%s')", g_chrome.widgets[strip].tooltip);
    }
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    test_find_parse();
    test_find_resolve();
    test_focus_pure();
    test_strip_rule();
    boot();
    test_panel_find_and_keys();
    test_panel_area_rows();
    test_panel_pane();
    test_panel_strip();
    Editor_UiFree(&g_ui);
    printf("editor_picker_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
