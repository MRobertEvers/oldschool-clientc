/*
 * Workbench 6.8 (design section 3 "Workspaces" and "Command palette",
 * section 15 "Hotkeys at the action"): the four workspaces and their round
 * trip through the layout file, the command palette's grouping, its
 * 100-row footer and its keys, and the Hotkeys panel's sort, filters and
 * two-slot capture.
 *
 *   ./editor_workspaces_test
 *
 * Headless and content-free, as editor_groups_test.c: the real chrome model
 * and every tools-panels module registered with no session and App services
 * standing in for an App, so the App-bound modules (Catalog, Record,
 * Inspector) are there too. Without a session the palette has no catalog:
 * its Squares and Records groups are the model's, tested over a synthetic
 * table here.
 */

#include "editor/editor_layout.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static int g_app_stand_in;

static void
frame(void)
{
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
boot_with(
    int w,
    int h,
    int scale,
    const struct EditorLayoutFile* file)
{
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetScale(&g_chrome, scale);
    ToriRSChrome_SetSurface(&g_chrome, w, h);
    Editor_UiInit(&g_ui, &g_chrome, NULL);
    g_ui.app_services.user_data = &g_app_stand_in;
    Editor_UiRegisterToolsPanels(&g_ui);
    if( file )
        Editor_UiLayoutHold(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    frame();
    if( file )
        Editor_UiLayoutLoaded(&g_ui, file);
    for( int i = 0; i < 4; i++ )
        frame();
}

static int
window_of(const char* module)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(&g_ui, module);
    return panel ? panel->chrome_panel : -1;
}

/** Shown: its window is, or it is open in its group's strip. */
static int
open_now(const char* module)
{
    int const w = window_of(module);
    return (w >= 0 && g_chrome.panels[w].visible) || Editor_UiPanelInStrip(&g_ui, module);
}

static const char*
shown_of(const char* group)
{
    struct EditorUiGroup* g = Editor_UiFindGroup(&g_ui, group);
    return g ? Editor_UiGroupShown(&g_ui, (int)(g - g_ui.groups)) : NULL;
}

/** Was `needle` said in the console's last `lines` lines? */
static int
console_says(
    const char* needle,
    int lines)
{
    for( int i = 0; i < lines; i++ )
    {
        const char* line = Editor_UiConsoleLine(&g_ui, i);
        if( line && strstr(line, needle) )
            return 1;
    }
    return 0;
}

/** The text a VLIST holds for absolute row `row`, or NULL (off screen). */
static const char*
vrow_text(
    int list,
    int row)
{
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
        if( g_chrome.vrows[i].owner == list && g_chrome.vrows[i].index == row )
            return g_chrome.vrows[i].text;
    return NULL;
}

/** Field `field` of a cells row, into `out`. */
static const char*
cell_field(
    const char* text,
    int field,
    char* out,
    int out_size)
{
    const char* at = text;
    out[0] = '\0';
    for( int f = 0; f < field && at; f++ )
    {
        at = strchr(at, TORIRS_CHROME_CELL_SEP);
        if( at )
            at++;
    }
    if( at )
    {
        const char* end = strchr(at, TORIRS_CHROME_CELL_SEP);
        int const len = end ? (int)(end - at) : (int)strlen(at);
        snprintf(out, (size_t)out_size, "%.*s", len, at);
    }
    return out;
}

/* ---- workspaces ------------------------------------------------------------------ */

/** No shown module window or group frame covers the World, and every one is
 *  on the surface: the workspace's column is a dock, not a pile. What the
 *  layout DOCKED is held to it; a flyout (the Preview opened beside the
 *  Catalog, a builder's own outline and dialogue windows) is its module's
 *  placement and opens over the world on purpose (editor_groups_test.c's
 *  note), as it did before workspaces. */
static void
dock_check(const char* what)
{
    struct EditorUiRect const world = Editor_UiWorldRect(&g_ui);
    int covered = 0;
    int off = 0;
    int docked = 0;

    for( int i = g_ui.panel_first; i < g_chrome.panel_count; i++ )
    {
        struct ToriRSChromeRect const r = g_chrome.panels[i].last_rect;
        if( !g_chrome.panels[i].visible || g_chrome.panels[i].modal || i == g_ui.world_panel || r.w <= 0 )
            continue;
        if( i >= g_ui.panel_end + EDITOR_UI_GROUPS_MAX || !g_ui.docked[i - g_ui.panel_first] )
            continue;
        docked++;
        if( r.x < world.x + world.w && world.x < r.x + r.w && r.y < world.y + world.h && world.y < r.y + r.h )
        {
            printf("  %s: '%s' %d,%d %dx%d covers the world\n", what, g_chrome.panels[i].title, r.x, r.y, r.w, r.h);
            covered++;
        }
        if( r.x < 0 || r.y < 0 || r.x + r.w > g_chrome.surface_w || r.y + r.h > g_chrome.surface_h )
            off++;
    }
    CHECK(world.w > 0 && world.h > 0, "%s: the World has its rect", what);
    CHECK(docked > 0, "%s: the column docks something (%d)", what, docked);
    CHECK(covered == 0, "%s: nothing covers the World (%d)", what, covered);
    CHECK(off == 0, "%s: everything is on the surface (%d)", what, off);
}

static void
test_workspace_defaults(void)
{
    int skipped;

    boot_with(1530, 1006, 1, NULL);
    CHECK(g_ui.workspace == EDITOR_UI_WS_BUILD, "the editor boots in Build");
    dock_check("Build");
    CHECK(open_now("catalog") && open_now("console"), "Build shows the Catalog and the Console");

    /* Terrain: Terrain and Palette (Terrain in front), Squares, Inspector. */
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_WORKSPACE_TERRAIN) == 1, "Ctrl+2's action runs");
    frame();
    frame();
    CHECK(g_ui.workspace == EDITOR_UI_WS_TERRAIN, "Ctrl+2 is Terrain");
    CHECK(open_now("terrain") && open_now("palette") && open_now("squares") && open_now("inspector"),
        "Terrain shows Terrain, Palette, Squares and the Inspector");
    CHECK(shown_of("right-mid") && strcmp(shown_of("right-mid"), "terrain") == 0, "Terrain's tab is in front (%s)",
        shown_of("right-mid") ? shown_of("right-mid") : "-");
    CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "inspector") == 0,
        "the Inspector is the right-top group's front tab (%s)", shown_of("right-top") ? shown_of("right-top") : "-");
    CHECK(!open_now("catalog") && !open_now("locs") && !open_now("preview"), "Build's panels are put away");
    CHECK(open_now("console") && open_now("history"), "the bottom strip keeps Console and History");
    CHECK(Editor_UiWorldShown(&g_ui) && g_ui.laid_out, "the World stays, laid out");
    CHECK(g_chrome.panels[window_of("squares")].last_rect.x >= g_ui.dock_x, "Squares docks in the column");
    dock_check("Terrain");
    /* The workspace is for its tools (F14, 2026-11-11): the Terrain /
     * Palette group takes what it asks for before the rest of the column
     * shares what is left, and the Inspector's group asks only for the
     * Inspector -- not for the closed Catalog beside it in the strip. The
     * group had 269 px of an 856 px column, under Squares and an Inspector
     * of four rows in 287. */
    {
        struct ToriRSChromeRect const tools = g_chrome.panels[window_of("terrain")].last_rect;
        struct ToriRSChromeRect const squares = g_chrome.panels[window_of("squares")].last_rect;
        struct ToriRSChromeRect const inspector = g_chrome.panels[window_of("inspector")].last_rect;

        CHECK(tools.h >= 400 && tools.h > squares.h + inspector.h,
            "Terrain's tools have the column: %d px, over Squares' %d and the Inspector's %d", tools.h, squares.h,
            inspector.h);
        CHECK(inspector.y + inspector.h == g_chrome.surface_h - g_chrome.panels[window_of("statusbar")].last_rect.h,
            "and the column ends at the status bar, with no hole under it (%d+%d)", inspector.y, inspector.h);
    }

    /* Script: the panels this tree has, the rest skipped with one line. */
    skipped = Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_SCRIPT);
    frame();
    frame();
    /* Workbench 6.6 part 1 built Problems: two of the design's are left. */
    CHECK(skipped == 2, "Script skips the two panels this tree lacks (%d)", skipped);
    CHECK(console_says("workspace Script: no vars, dialogue_preview panel", 4),
        "the console names what was skipped");
    /* The Record panel shows only once a record is open (its own rule,
     * edpanel_catalog.c record_tick): with none, the workspace's show of it
     * is withdrawn on its next tick. */
    for( const char* const* name = Editor_UiWorkspacePanels(EDITOR_UI_WS_SCRIPT); *name; name++ )
        if( Editor_UiFindPanel(&g_ui, *name) && strcmp(*name, "record") != 0 )
            CHECK(open_now(*name), "Script shows %s", *name);
    CHECK(Editor_UiFindPanel(&g_ui, "record") && Editor_UiFindPanel(&g_ui, "triggers")
              && Editor_UiFindPanel(&g_ui, "locbuilder"),
        "Record, Triggers and the Loc builder are this harness's");
    CHECK(!open_now("terrain") && !open_now("squares"), "Terrain's panels are put away");
    CHECK(g_chrome.panels[window_of("triggers")].visible
              && g_chrome.panels[window_of("triggers")].last_rect.x >= g_ui.dock_x,
        "Triggers docks in the column (%d; dock %d)", g_chrome.panels[window_of("triggers")].last_rect.x,
        g_ui.dock_x);
    /* Scripts are written in the owner's editor: the workspace opens no
     * builder (they are on the View menu). */
    CHECK(!open_now("locbuilder"), "Script opens no builder: the Loc builder stays on the View menu");
    if( Editor_UiFindPanel(&g_ui, "npcbuilder") )
        CHECK(!open_now("npcbuilder"), "nor the NPC editor");
    dock_check("Script");

    skipped = Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_PLAYTEST);
    frame();
    frame();
    /* Live and Accounts need a session; the harness has none. */
    CHECK(skipped == 1 + !Editor_UiFindPanel(&g_ui, "live") + !Editor_UiFindPanel(&g_ui, "accounts")
                         + !Editor_UiFindPanel(&g_ui, "scenes") + !Editor_UiFindPanel(&g_ui, "encounter"),
        "Playtest skips live_log and what needs a session (%d)", skipped);
    for( const char* const* name = Editor_UiWorkspacePanels(EDITOR_UI_WS_PLAYTEST); *name; name++ )
        if( Editor_UiFindPanel(&g_ui, *name) )
            CHECK(open_now(*name), "Playtest shows %s", *name);
    CHECK(!open_now("record"), "and puts Script's away");
    dock_check("Playtest");

    /* The menu bar's chips press the current one. */
    {
        int pressed = -1;
        int count = 0;
        for( int i = 0; i < g_chrome.widget_count; i++ )
        {
            struct ToriRSChromeWidget const* w = &g_chrome.widgets[i];
            if( w->panel != window_of("menubar") )
                continue;
            for( int ws = 0; ws < EDITOR_UI_WORKSPACES; ws++ )
                if( w->kind == TORIRS_CHROME_W_MENUITEM && strcmp(w->text, Editor_UiWorkspaceTitle(ws)) == 0 )
                {
                    count++;
                    if( w->checked )
                        pressed = ws;
                    CHECK(strstr(w->tooltip, "Ctrl+") != NULL, "the %s chip's tooltip names its chord (%s)",
                        w->text, w->tooltip);
                }
        }
        CHECK(count == EDITOR_UI_WORKSPACES, "four workspace chips on the menu bar (%d)", count);
        CHECK(pressed == EDITOR_UI_WS_PLAYTEST, "the Playtest chip is pressed (%d)", pressed);
    }
    (void)Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_PLAYTEST);
    CHECK(strstr(Editor_UiStatusText(&g_ui), "already") != NULL, "the same workspace again says so");

    /* Back to Build: its own set, as it booted. */
    (void)Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_BUILD);
    frame();
    frame();
    CHECK(open_now("catalog") && open_now("console") && !open_now("live") && !open_now("accounts"),
        "Build comes back as it was left");
    dock_check("Build again");
    Editor_UiFree(&g_ui);
}

static void
test_workspace_edits(void)
{
    int squares;
    struct EditorLayoutFile* file = calloc(1, sizeof(*file));
    struct EditorLayoutFile* again = calloc(1, sizeof(*again));
    char* text = NULL;
    char* text2 = NULL;
    int size = 0;
    int size2 = 0;

    assert(file);
    assert(again);
    boot_with(1530, 1006, 1, NULL);
    (void)Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_TERRAIN);
    frame();
    frame();
    /* The user drags Squares out over the world. */
    squares = window_of("squares");
    ToriRSChrome_PanelMove(&g_chrome, squares, 400, 300);
    g_chrome.panels[squares].user_placed = 1;
    frame();
    CHECK(g_chrome.panels[squares].x == 400 && g_chrome.panels[squares].y == 300, "Squares was dragged");

    /* Leaving saves it; coming back finds it where it was left. */
    (void)Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_BUILD);
    frame();
    CHECK(!open_now("squares") || g_chrome.panels[squares].x != 400, "Build does not wear Terrain's drag");
    (void)Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_TERRAIN);
    frame();
    frame();
    CHECK(open_now("squares") && g_chrome.panels[squares].x == 400 && g_chrome.panels[squares].y == 300
              && g_chrome.panels[squares].user_placed,
        "Terrain comes back with Squares where it was dragged (%d,%d)", g_chrome.panels[squares].x,
        g_chrome.panels[squares].y);
    CHECK(console_says("workspace Terrain (as you left it)", 2), "and says it is as it was left");

    /* The layout file: every workspace's section, byte-exact through a
     * decode and an encode. */
    (void)Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_BUILD);
    frame();
    Editor_UiLayoutCapture(&g_ui, file);
    CHECK(file->has_workspace && strcmp(file->workspace, "build") == 0, "the file names the current workspace");
    CHECK(EditorLayout_FindWorkspace(file, "build") && EditorLayout_FindWorkspace(file, "terrain"),
        "it holds Build's and Terrain's sections");
    CHECK(!EditorLayout_FindWorkspace(file, "script"), "a workspace never visited has none (its default)");
    EditorLayout_Encode(file, &text, &size);
    CHECK(strstr(text, "[workspace terrain]") && strstr(text, "workspace = build"), "the text says both");
    {
        const struct EditorLayoutWorkspace* t = EditorLayout_FindWorkspace(file, "terrain");
        char line[96];
        const struct EditorLayoutPanel* p = NULL;
        for( int i = 0; t && i < t->panel_count; i++ )
            if( strcmp(t->panels[i].name, "squares") == 0 )
                p = &t->panels[i];
        CHECK(p && p->user_placed && p->x == 400 && p->y == 300, "Terrain's section keeps the dragged Squares");
        if( p )
        {
            snprintf(line, sizeof(line), "panel.squares = 1 1 400 300 %d %d 0", p->w, p->h);
            CHECK(strstr(text, line) != NULL, "as one line: %s", line);
        }
    }
    CHECK(EditorLayout_Decode(again, text, size), "the text decodes");
    EditorLayout_Encode(again, &text2, &size2);
    CHECK(size == size2 && memcmp(text, text2, (size_t)size) == 0, "and encodes back byte for byte");
    ToriMem_Free(ToriMem_Process(), text2);
    text2 = NULL;
    Editor_UiFree(&g_ui);

    /* A fresh window from the file: Build, then Terrain as it was left. */
    boot_with(1530, 1006, 1, again);
    CHECK(g_ui.workspace == EDITOR_UI_WS_BUILD && g_ui.workspace_has[EDITOR_UI_WS_TERRAIN],
        "the boot reads the current workspace and Terrain's layout");
    (void)Editor_UiSetWorkspace(&g_ui, EDITOR_UI_WS_TERRAIN);
    frame();
    frame();
    squares = window_of("squares");
    CHECK(open_now("squares") && g_chrome.panels[squares].x == 400 && g_chrome.panels[squares].y == 300,
        "after a restart Terrain's Squares is where it was dragged (%d,%d)", g_chrome.panels[squares].x,
        g_chrome.panels[squares].y);

    /* Reset layout resets the CURRENT workspace to its default. */
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_RESET_LAYOUT);
    frame();
    frame();
    CHECK(g_ui.workspace == EDITOR_UI_WS_TERRAIN, "Reset layout stays in Terrain");
    CHECK(open_now("squares") && !g_chrome.panels[squares].user_placed && g_chrome.panels[squares].x >= g_ui.dock_x,
        "and docks Squares again (%d,%d)", g_chrome.panels[squares].x, g_chrome.panels[squares].y);
    CHECK(open_now("terrain") && !open_now("catalog"), "Terrain's default, not Build's");
    Editor_UiFree(&g_ui);

    /* A file from before workspaces is Build's. */
    {
        static const char k_old[] = "[layout]\nversion = 1\nsurface_w = 1530\nsurface_h = 1006\nscale = 1\n"
                                    "\n[panel squares]\nvisible = 1\n";
        CHECK(EditorLayout_Decode(again, k_old, (int)strlen(k_old)) && !again->has_workspace
                  && again->workspace_count == 0,
            "an old file has no workspace");
        boot_with(1530, 1006, 1, again);
        CHECK(g_ui.workspace == EDITOR_UI_WS_BUILD && open_now("squares"), "and applies as Build, as before");
        Editor_UiFree(&g_ui);
    }
    /* A hand-edited row: unstated fields are "-", a comment is cut, a name
     * the editor does not know is kept for a later build. */
    {
        static const char k_hand[] = "[layout]\nworkspace = terrain ; by hand\n"
                                     "[workspace terrain]\nsurface = 1530 1006 1\n"
                                     "panel.squares = 1 - 12 34 ; moved\n"
                                     "group.right-mid = 1 0 - - - - 0 palette\n"
                                     "[workspace later]\npanel.x = 1\n";
        const struct EditorLayoutWorkspace* t;
        CHECK(EditorLayout_Decode(again, k_hand, (int)strlen(k_hand)), "a hand-edited file decodes");
        t = EditorLayout_FindWorkspace(again, "terrain");
        CHECK(again->has_workspace && strcmp(again->workspace, "terrain") == 0, "its current workspace is Terrain");
        CHECK(t && t->surface_w == 1530 && t->panel_count == 1 && t->panels[0].has_visible
                  && !t->panels[0].has_user_placed && t->panels[0].x == 12 && t->panels[0].y == 34
                  && !t->panels[0].has_w,
            "a short row states what it says and no more");
        CHECK(t && t->group_count == 1 && t->groups[0].has_shown && strcmp(t->groups[0].shown, "palette") == 0
                  && !t->groups[0].has_x,
            "a group row's front tab");
        CHECK(EditorLayout_FindWorkspace(again, "later") != NULL, "an unknown workspace is kept");
        /* Written back, an unstated field is "-" again, not a number the
         * next read would take as stated. */
        {
            char* hand = NULL;
            int hand_size = 0;
            EditorLayout_Encode(again, &hand, &hand_size);
            CHECK(strstr(hand, "panel.squares = 1 - 12 34 - - -\n") != NULL, "the short row encodes with its dashes");
            CHECK(strstr(hand, "group.right-mid = 1 0 - - - - 0 palette\n") != NULL, "and the group row too");
            ToriMem_Free(ToriMem_Process(), hand);
        }
    }
    ToriMem_Free(ToriMem_Process(), text);
    free(file);
    free(again);
}

/* ---- the command palette ------------------------------------------------------------ */

static void
test_palette_model(void)
{
    struct Editor_CmdPalList list;
    struct Editor_CmdPalItem item;
    char text[TORIRS_CHROME_INPUT_MAX];
    char field[64];
    int rows;
    int r;

    /* A synthetic table: 250 actions, 3 panels, no squares, 1 record whose
     * page said 40 more. */
    Editor_CmdPalListInit(&list);
    memset(&item, 0, sizeof(item));
    for( int i = 0; i < 250; i++ )
    {
        item.group = EDITOR_CMDPAL_ACTIONS;
        item.kind = EDITOR_CMDPAL_KIND_ACTION;
        snprintf(item.label, sizeof(item.label), "action %d", i);
        snprintf(item.chord, sizeof(item.chord), "F%d", i % 12 + 1);
        CHECK(Editor_CmdPalListAdd(&list, &item) == (i < EDITOR_CMDPAL_GROUP_MAX), "action %d kept iff under 100", i);
    }
    memset(&item, 0, sizeof(item));
    for( int i = 0; i < 3; i++ )
    {
        item.group = EDITOR_CMDPAL_PANELS;
        item.kind = EDITOR_CMDPAL_KIND_PANEL;
        snprintf(item.label, sizeof(item.label), "panel %d", i);
        (void)Editor_CmdPalListAdd(&list, &item);
    }
    item.group = EDITOR_CMDPAL_RECORDS;
    item.kind = EDITOR_CMDPAL_KIND_RECORD;
    snprintf(item.label, sizeof(item.label), "loc:oaktree");
    snprintf(item.right, sizeof(item.right), "1276");
    (void)Editor_CmdPalListAdd(&list, &item);
    Editor_CmdPalListAddUnseen(&list, EDITOR_CMDPAL_RECORDS, 40);
    rows = Editor_CmdPalListFinish(&list);
    /* Actions: header, 100, footer; Panels: header, 3; Records: header, 1,
     * footer. No Squares, no Accounts: no header. */
    CHECK(rows == 1 + 100 + 1 + 1 + 3 + 1 + 1 + 1, "the rows: three groups with headers and two footers (%d)", rows);
    CHECK(list.row_kind[0] == EDITOR_CMDPAL_ROW_HEADER && list.row_group[0] == EDITOR_CMDPAL_ACTIONS,
        "Actions comes first, under its header");
    Editor_CmdPalRowText(&list, 0, text, (int)sizeof(text));
    CHECK(strcmp(cell_field(text, 1, field, (int)sizeof(field)), "Actions") == 0
              && strcmp(cell_field(text, 4, field, (int)sizeof(field)), "250") == 0
              && strcmp(cell_field(text, 0, field, (int)sizeof(field)), "-") == 0,
        "the header names the group and counts every match, with no well");
    Editor_CmdPalRowText(&list, 1, text, (int)sizeof(text));
    CHECK(strcmp(cell_field(text, 1, field, (int)sizeof(field)), "action 0") == 0
              && strcmp(cell_field(text, 3, field, (int)sizeof(field)), "F1") == 0,
        "an action row carries its chord as a keycap");
    CHECK(list.row_kind[101] == EDITOR_CMDPAL_ROW_FOOTER, "the 102nd row is Actions' footer");
    Editor_CmdPalRowText(&list, 101, text, (int)sizeof(text));
    CHECK(strstr(text, "and 150 more") != NULL, "it says 'and 150 more' (%s)", text);
    CHECK(list.row_kind[102] == EDITOR_CMDPAL_ROW_HEADER && list.row_group[102] == EDITOR_CMDPAL_PANELS,
        "Panels follow");
    Editor_CmdPalRowText(&list, rows - 1, text, (int)sizeof(text));
    CHECK(strstr(text, "and 40 more") != NULL, "a page's unseen total is a footer too (%s)", text);
    Editor_CmdPalRowText(&list, rows - 2, text, (int)sizeof(text));
    CHECK(strcmp(cell_field(text, 4, field, (int)sizeof(field)), "1276") == 0, "a record's id is its right column");

    /* The keys over it. */
    CHECK(Editor_CmdPalStep(&list, -1, 1) == 1, "the first item is row 1, the header skipped");
    CHECK(Editor_CmdPalStep(&list, 100, 1) == 103, "Down from the last action skips the footer and the header");
    CHECK(Editor_CmdPalStep(&list, 103, -1) == 100, "Up comes back over them");
    CHECK(Editor_CmdPalStep(&list, 1, -1) == 1, "Up at the first item stays");
    CHECK(Editor_CmdPalNextGroup(&list, 5) == 103, "Tab goes to the Panels group");
    CHECK(Editor_CmdPalNextGroup(&list, 104) == rows - 2, "Tab again to Records");
    CHECK(Editor_CmdPalNextGroup(&list, rows - 2) == 1, "and round to Actions");
    CHECK(Editor_CmdPalMatches("Fit walls to neighbours", "fit NEIGH") && !Editor_CmdPalMatches("Undo", "fit"),
        "the match is every word, case-blind");

    /* Cleared, the same allocations, an empty list has no rows. */
    Editor_CmdPalListClear(&list);
    CHECK(Editor_CmdPalListFinish(&list) == 0, "an empty list has no rows");
    Editor_CmdPalListFree(&list);
}

static void
test_palette_module(void)
{
    const struct Editor_CmdPalList* list;
    struct Editor_HotkeyInput in;
    int fit_row = -1;
    int row;

    boot_with(1530, 1006, 1, NULL);
    CHECK(!Editor_CmdPalette_IsOpen(&g_ui), "the palette starts closed");
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_COMMAND_PALETTE) == 1 && Editor_CmdPalette_IsOpen(&g_ui),
        "Ctrl+K's action opens it");
    CHECK(g_chrome.panels[window_of("cmdpalette")].modal, "it is a modal");
    frame();
    list = Editor_CmdPalette_List(&g_ui);
    CHECK(list && list->total[EDITOR_CMDPAL_ACTIONS] > EDITOR_CMDPAL_GROUP_MAX
              && list->kept[EDITOR_CMDPAL_ACTIONS] == EDITOR_CMDPAL_GROUP_MAX,
        "an empty search lists the table, 100 actions kept of %d", list ? list->total[EDITOR_CMDPAL_ACTIONS] : -1);
    {
        int footers = 0;
        for( int r = 0; list && r < list->row_count; r++ )
            footers += list->row_kind[r] == EDITOR_CMDPAL_ROW_FOOTER;
        CHECK(footers == 1, "and Actions ends with its footer (%d)", footers);
    }
    CHECK(list && list->total[EDITOR_CMDPAL_PANELS] > 10, "the Panels group lists the View menu's panels");
    CHECK(list && list->total[EDITOR_CMDPAL_SQUARES] == 0 && list->total[EDITOR_CMDPAL_RECORDS] == 0,
        "no session: no catalog groups");
    CHECK(Editor_CmdPalette_Selected(&g_ui) == 1, "the first item is selected");

    Editor_CmdPalette_SetQuery(&g_ui, "fit walls");
    frame();
    list = Editor_CmdPalette_List(&g_ui);
    for( int r = 0; list && r < list->row_count; r++ )
        if( list->row_kind[r] == EDITOR_CMDPAL_ROW_ITEM && list->items[list->row_item[r]].target == EDITOR_HK_PLACE_FIT
            && list->items[list->row_item[r]].kind == EDITOR_CMDPAL_KIND_ACTION )
            fit_row = r;
    CHECK(fit_row >= 0, "'fit walls' finds Fit walls to neighbours");
    if( fit_row >= 0 )
        CHECK(strcmp(list->items[list->row_item[fit_row]].chord, "G") == 0, "its right column is its chord, G");
    {
        /* A wall run is a Place drag, so its row is the Place tool's. */
        int wall_run_row = -1;
        Editor_CmdPalette_SetQuery(&g_ui, "wall run");
        frame();
        list = Editor_CmdPalette_List(&g_ui);
        for( int r = 0; list && r < list->row_count; r++ )
            if( list->row_kind[r] == EDITOR_CMDPAL_ROW_ITEM
                && list->items[list->row_item[r]].target == EDITOR_HK_TOOL_PLACE
                && list->items[list->row_item[r]].kind == EDITOR_CMDPAL_KIND_ACTION )
                wall_run_row = r;
        CHECK(wall_run_row >= 0, "'wall run' finds the Place tool");
        Editor_CmdPalette_SetQuery(&g_ui, "fit walls");
        frame();
        list = Editor_CmdPalette_List(&g_ui);
    }
    {
        /* The row on screen, as the list draws it. */
        int list_widget = -1;
        for( int i = 0; i < g_chrome.widget_count; i++ )
            if( g_chrome.widgets[i].panel == window_of("cmdpalette") && g_chrome.widgets[i].kind == TORIRS_CHROME_W_VLIST )
                list_widget = i;
        CHECK(list_widget >= 0 && fit_row >= 0 && vrow_text(list_widget, fit_row)
                  && strstr(vrow_text(list_widget, fit_row), "Fit walls"),
            "the VLIST holds the row's text");
    }

    /* The keys: Down, Tab, Enter runs, Esc closes. */
    Editor_CmdPalette_SetQuery(&g_ui, "terrain");
    frame();
    list = Editor_CmdPalette_List(&g_ui);
    row = Editor_CmdPalette_Selected(&g_ui);
    CHECK(list && row >= 0 && list->row_group[row] == EDITOR_CMDPAL_ACTIONS, "'terrain' selects an action first");
    memset(&in, 0, sizeof(in));
    in.down[TORIRSK_TAB] = in.held[TORIRSK_TAB] = 1;
    CHECK(Editor_CmdPalette_Feed(&g_ui, &in) == 1, "the palette takes the keys while open");
    row = Editor_CmdPalette_Selected(&g_ui);
    CHECK(list && row >= 0 && list->row_group[row] == EDITOR_CMDPAL_PANELS
              && strcmp(list->items[list->row_item[row]].label, "Terrain") == 0,
        "Tab goes to the Panels group: Terrain");
    memset(&in, 0, sizeof(in));
    in.down[TORIRSK_RETURN] = in.held[TORIRSK_RETURN] = 1;
    (void)Editor_CmdPalette_Feed(&g_ui, &in);
    frame();
    CHECK(!Editor_CmdPalette_IsOpen(&g_ui), "Enter runs the row and closes");
    CHECK(open_now("terrain"), "the Terrain panel is shown");

    /* An action row runs the action: the Terrain workspace. */
    (void)Editor_CmdPalette_Open(&g_ui);
    Editor_CmdPalette_SetQuery(&g_ui, "workspace terrain");
    frame();
    row = Editor_CmdPalette_Selected(&g_ui);
    list = Editor_CmdPalette_List(&g_ui);
    CHECK(list && row >= 0 && list->items[list->row_item[row]].target == EDITOR_HK_VIEW_WORKSPACE_TERRAIN,
        "'workspace terrain' is the Terrain workspace action");
    CHECK(row >= 0 && Editor_CmdPalette_RunRow(&g_ui, row) == 1 && g_ui.workspace == EDITOR_UI_WS_TERRAIN,
        "and running it switches");
    frame();

    /* A World-bound action is the App's: handed over for its next frame. */
    (void)Editor_CmdPalette_Open(&g_ui);
    Editor_CmdPalette_SetQuery(&g_ui, "frame the selection");
    frame();
    row = Editor_CmdPalette_Selected(&g_ui);
    CHECK(row >= 0 && Editor_CmdPalette_RunRow(&g_ui, row) == 1
              && Editor_CmdPalette_TakePendingAction(&g_ui) == EDITOR_HK_CAM_FRAME,
        "an action of the World under the pointer goes to the App");

    /* Esc closes; nothing matched, Enter says so. */
    (void)Editor_CmdPalette_Open(&g_ui);
    Editor_CmdPalette_SetQuery(&g_ui, "zzzz no such thing");
    frame();
    CHECK(Editor_CmdPalette_List(&g_ui)->row_count == 0, "nothing matches: no rows");
    memset(&in, 0, sizeof(in));
    in.down[TORIRSK_RETURN] = in.held[TORIRSK_RETURN] = 1;
    (void)Editor_CmdPalette_Feed(&g_ui, &in);
    CHECK(Editor_CmdPalette_IsOpen(&g_ui) && strstr(Editor_UiStatusText(&g_ui), "Nothing to run") != NULL,
        "Enter on nothing says so and stays open");
    memset(&in, 0, sizeof(in));
    in.down[TORIRSK_ESCAPE] = in.held[TORIRSK_ESCAPE] = 1;
    (void)Editor_CmdPalette_Feed(&g_ui, &in);
    CHECK(!Editor_CmdPalette_IsOpen(&g_ui), "Esc closes it");
    memset(&in, 0, sizeof(in));
    CHECK(Editor_CmdPalette_Feed(&g_ui, &in) == 0, "closed, it takes no keys");
    Editor_UiFree(&g_ui);
}

/* ---- the Hotkeys panel ------------------------------------------------------------- */

static int
hotkeys_feed(const char* chord_text)
{
    struct Editor_HotkeyInput in;
    struct Editor_Chord chord;
    int const ok = Editor_ChordParse(chord_text, &chord);

    assert(ok);
    (void)ok;
    memset(&in, 0, sizeof(in));
    if( chord.mods & EDITOR_HK_MOD_CTRL )
        in.held[TORIRSK_CTRL] = 1;
    if( chord.mods & EDITOR_HK_MOD_SHIFT )
        in.held[TORIRSK_SHIFT] = 1;
    if( chord.mods & EDITOR_HK_MOD_ALT )
        in.held[TORIRSK_ALT] = 1;
    in.down[chord.key] = 1;
    in.held[chord.key] = 1;
    return Editor_Hotkeys_Feed(&g_ui, &in);
}

/** The list's row for `action`, -1 when not listed. */
static int
hotkeys_row_of(int action)
{
    static int rows[EDITOR_HK_COUNT];
    int const n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    for( int i = 0; i < n; i++ )
        if( rows[i] == action )
            return i;
    return -1;
}

/** Bring `row` on screen and the frame's fill, and return its text. */
static const char*
hotkeys_text(int row)
{
    int const list = Editor_Hotkeys_ListWidget(&g_ui);
    ToriRSChrome_VListSetScroll(&g_chrome, list, row);
    frame();
    return vrow_text(list, row);
}

static void
test_hotkeys_panel(void)
{
    static int rows[EDITOR_HK_COUNT];
    int n;
    int list;
    char field[64];
    int ordered;

    boot_with(1530, 1006, 1, NULL);
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_HOTKEYS);
    frame();
    frame();
    list = Editor_Hotkeys_ListWidget(&g_ui);
    CHECK(list >= 0 && g_chrome.widgets[list].kind == TORIRS_CHROME_W_VLIST, "the panel's rows are one VLIST");
    n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    CHECK(n == EDITOR_HK_COUNT && g_chrome.widgets[list].imax == EDITOR_HK_COUNT, "every action listed (%d)", n);
    ordered = 1;
    for( int i = 0; i < n; i++ )
        ordered &= rows[i] == i;
    CHECK(ordered, "by group: the table's order");

    /* A row: name, group and context, two chord cells, the gesture. */
    {
        const char* text = hotkeys_text(hotkeys_row_of(EDITOR_HK_EDIT_REDO));
        CHECK(text && strcmp(cell_field(text, 1, field, (int)sizeof(field)), "Redo") == 0, "Redo's row");
        CHECK(text && strcmp(cell_field(text, 2, field, (int)sizeof(field)), "Edit G") == 0, "group and context");
        CHECK(text && strcmp(cell_field(text, 3, field, (int)sizeof(field)), "Ctrl+Shift+Z Ctrl+Y") == 0,
            "both chords, one cell each (%s)", text ? field : "-");
        text = hotkeys_text(hotkeys_row_of(EDITOR_HK_EDIT_UNDO));
        CHECK(text && strcmp(cell_field(text, 3, field, (int)sizeof(field)), "Ctrl+Z -") == 0,
            "an empty second slot is its own cell, '-'");
        text = hotkeys_text(hotkeys_row_of(EDITOR_HK_CAM_TURN_LEFT));
        CHECK(text && strcmp(cell_field(text, 4, field, (int)sizeof(field)), "twist") == 0,
            "the gesture column from the table's gesture spelling");
    }

    /* Sort by action: names ascending, then descending. */
    Editor_Hotkeys_SetSort(&g_ui, EDITOR_HOTKEYS_COL_ACTION, 0);
    n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    ordered = 1;
    for( int i = 1; i < n; i++ )
        ordered &= strcasecmp(Editor_HotkeyInfo(rows[i - 1])->name, Editor_HotkeyInfo(rows[i])->name) <= 0;
    CHECK(ordered && n == EDITOR_HK_COUNT, "sorted by action, A to Z");
    Editor_Hotkeys_SetSort(&g_ui, EDITOR_HOTKEYS_COL_ACTION, 1);
    n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    ordered = 1;
    for( int i = 1; i < n; i++ )
        ordered &= strcasecmp(Editor_HotkeyInfo(rows[i - 1])->name, Editor_HotkeyInfo(rows[i])->name) >= 0;
    CHECK(ordered, "the second press, Z to A");
    /* By chord 2: the few with a second chord first, the unbound after. */
    Editor_Hotkeys_SetSort(&g_ui, EDITOR_HOTKEYS_COL_CHORD2, 0);
    n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    {
        int seen_empty = 0;
        int bad = 0;
        for( int i = 0; i < n; i++ )
        {
            int const empty = g_ui.hotkeys.bind[rows[i]].chord[1].key == 0;
            bad += seen_empty && !empty;
            seen_empty |= empty;
        }
        CHECK(bad == 0 && g_ui.hotkeys.bind[rows[0]].chord[1].key != 0, "by chord 2, the unbound slots last");
    }
    Editor_Hotkeys_SetSort(&g_ui, EDITOR_HOTKEYS_COL_GROUP, 0);

    /* Filters. */
    Editor_Hotkeys_SetFilter(&g_ui, "Level", 0, 0, 0);
    n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    CHECK(n == EDITOR_HK_LEVEL_ABOVE - EDITOR_HK_LEVEL_DOWN + 1, "the Level group alone (%d)", n);
    Editor_Hotkeys_SetFilter(&g_ui, NULL, EDITOR_HK_CTX_Q, 0, 0);
    n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    /* Workbench 6.6 added N (graph.new_node) beside Ctrl+L. */
    CHECK(n == 2 && rows[0] == EDITOR_HK_GRAPH_AUTO_LAYOUT && rows[1] == EDITOR_HK_GRAPH_NEW_NODE,
        "context Q: the graph's auto-layout and new node alone");
    Editor_Hotkeys_SetFilter(&g_ui, NULL, 0, 1, 0);
    CHECK(Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT) == 0, "changed: nothing is rebound yet");

    /* Two-slot capture: Redo's SECOND cell takes Ctrl+Alt+Y, the first kept. */
    Editor_Hotkeys_SetFilter(&g_ui, NULL, 0, 0, 0);
    {
        int const row = hotkeys_row_of(EDITOR_HK_EDIT_REDO);
        struct ToriRSChromeWidget* w = &g_chrome.widgets[list];
        ToriRSChrome_VListSetSelected(&g_chrome, list, row);
        w->vlist_cell = 2;
        g_chrome.activated = list;
        frame();
    }
    CHECK(Editor_Hotkeys_Capturing(&g_ui) == EDITOR_HK_EDIT_REDO && Editor_Hotkeys_CaptureSlot(&g_ui) == 1,
        "a click on chord cell 2 captures Redo's second slot");
    {
        const char* text = hotkeys_text(hotkeys_row_of(EDITOR_HK_EDIT_REDO));
        CHECK(text && strcmp(cell_field(text, 3, field, (int)sizeof(field)), "Ctrl+Shift+Z press...") == 0
                  && strcmp(cell_field(text, 5, field, (int)sizeof(field)), "*") == 0,
            "the captured cell says so, the row in the accent");
    }
    CHECK(hotkeys_feed("ctrl+alt+y") == 1 && Editor_Hotkeys_Capturing(&g_ui) < 0, "a chord ends the capture");
    {
        char a[32];
        char b[32];
        Editor_ChordFormat(g_ui.hotkeys.bind[EDITOR_HK_EDIT_REDO].chord[0], a, (int)sizeof(a));
        Editor_ChordFormat(g_ui.hotkeys.bind[EDITOR_HK_EDIT_REDO].chord[1], b, (int)sizeof(b));
        CHECK(strcmp(a, "ctrl+shift+z") == 0 && strcmp(b, "ctrl+alt+y") == 0,
            "only the clicked slot changed (%s, %s)", a, b);
    }
    Editor_Hotkeys_SetFilter(&g_ui, NULL, 0, 1, 0);
    n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    CHECK(n == 1 && rows[0] == EDITOR_HK_EDIT_REDO, "changed: Redo alone");
    Editor_Hotkeys_SetFilter(&g_ui, NULL, 0, 0, 0);

    /* Backspace unbinds the FIRST slot alone; the second moves up. */
    Editor_Hotkeys_BeginCaptureSlot(&g_ui, EDITOR_HK_EDIT_REDO, 0);
    (void)hotkeys_feed("backspace");
    {
        char a[32];
        Editor_ChordFormat(g_ui.hotkeys.bind[EDITOR_HK_EDIT_REDO].chord[0], a, (int)sizeof(a));
        CHECK(strcmp(a, "ctrl+alt+y") == 0 && g_ui.hotkeys.bind[EDITOR_HK_EDIT_REDO].chord[1].key == 0,
            "Backspace unbinds that slot; the other stays (%s)", a);
    }
    /* Esc cancels and keeps. */
    Editor_Hotkeys_BeginCaptureSlot(&g_ui, EDITOR_HK_EDIT_UNDO, 1);
    (void)hotkeys_feed("escape");
    CHECK(Editor_Hotkeys_Capturing(&g_ui) < 0 && Editor_HotkeysIsDefault(&g_ui.hotkeys, EDITOR_HK_EDIT_UNDO),
        "Esc cancels and keeps the chords");

    /* A clash in a live context marks both rows. */
    Editor_Hotkeys_BeginCaptureSlot(&g_ui, EDITOR_HK_FILE_BAKE, 0);
    (void)hotkeys_feed("ctrl+z");
    {
        const char* text = hotkeys_text(hotkeys_row_of(EDITOR_HK_FILE_BAKE));
        CHECK(text && strcmp(cell_field(text, 5, field, (int)sizeof(field)), "!") == 0, "Bake's row is marked");
        text = hotkeys_text(hotkeys_row_of(EDITOR_HK_EDIT_UNDO));
        CHECK(text && strcmp(cell_field(text, 5, field, (int)sizeof(field)), "!") == 0, "and so is Undo's");
        text = hotkeys_text(hotkeys_row_of(EDITOR_HK_EDIT_DUPLICATE));
        CHECK(text && strcmp(cell_field(text, 5, field, (int)sizeof(field)), "") == 0, "Duplicate's is not");
    }
    Editor_Hotkeys_SetFilter(&g_ui, NULL, 0, 0, 1);
    n = Editor_Hotkeys_Rows(&g_ui, rows, EDITOR_HK_COUNT);
    CHECK(n == 2, "conflicts: the two clashing rows (%d)", n);
    Editor_Hotkeys_SetFilter(&g_ui, NULL, 0, 0, 0);

    /* Reset all. */
    Editor_Hotkeys_ResetAll(&g_ui);
    {
        int all = 1;
        for( int a = 0; a < EDITOR_HK_COUNT; a++ )
            all &= Editor_HotkeysIsDefault(&g_ui.hotkeys, a);
        CHECK(all, "Reset all restores the table");
    }
    Editor_UiFree(&g_ui);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    setenv("TORIRS_EDITOR_LAYOUT", "", 1);

    test_workspace_defaults();
    test_workspace_edits();
    test_palette_model();
    test_palette_module();
    test_hotkeys_panel();

    printf("editor_workspaces_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
