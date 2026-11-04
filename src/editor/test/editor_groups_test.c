/*
 * Panel groups (workbench 6.0, design #workbench "Panel groups"): the
 * registry's default groups, their tab strips, the layout's one place per
 * group, Close / Pop out / maximise on a group's title bar, View > Reset
 * groups, and the layout file's [group] sections -- round trip, and a file
 * written before groups existed.
 *
 *   ./editor_groups_test
 *
 * Headless and content-free: the real chrome model and every tools-panels
 * module registered with no session (Editor_UiInit's `editor` NULL, which
 * every module tolerates) and App services standing in for an App, so the
 * App-bound members (Catalog, Inspector, Catalog palette) are there too.
 * editor_panels_test.c drives the modules themselves over a content tree.
 */

#include "editor/editor_layout.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "ui/uitree_debug_overlay.h"

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

static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static int g_app_stand_in;

/* What the fake App was asked to pop out. */
static struct
{
    int calls;
    char module[32];
} g_spawned;

static int
fake_spawn_panel(
    void* user_data,
    const char* module,
    const struct EditorViewCamera* camera,
    char* why,
    int why_size)
{
    (void)user_data;
    (void)camera;
    (void)why;
    (void)why_size;
    g_spawned.calls++;
    snprintf(g_spawned.module, sizeof(g_spawned.module), "%s", module);
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
boot_with(
    int w,
    int h,
    int scale,
    const struct EditorLayoutFile* file)
{
    memset(&g_spawned, 0, sizeof(g_spawned));
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetScale(&g_chrome, scale);
    ToriRSChrome_SetSurface(&g_chrome, w, h);
    Editor_UiInit(&g_ui, &g_chrome, NULL);
    g_ui.app_services.user_data = &g_app_stand_in;
    g_ui.app_services.spawn_panel = fake_spawn_panel;
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

static void
boot(
    int w,
    int h,
    int scale)
{
    boot_with(w, h, scale, NULL);
}

static int
window_of(const char* module)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(&g_ui, module);
    return panel ? panel->chrome_panel : -1;
}

static struct EditorUiGroup*
group(const char* name)
{
    return Editor_UiFindGroup(&g_ui, name);
}

static const char*
shown_of(const char* name)
{
    struct EditorUiGroup* g = group(name);
    return g ? Editor_UiGroupShown(&g_ui, (int)(g - g_ui.groups)) : NULL;
}

static int
visible(int panel)
{
    return panel >= 0 && g_chrome.panels[panel].visible;
}

/** The strip's titles, joined with '|'. */
static const char*
strip_text(struct EditorUiGroup* g)
{
    static char text[512];
    struct ToriRSChromeWidget const* w = &g_chrome.widgets[g->tabs];
    int used = 0;

    text[0] = '\0';
    for( int i = 0; i < w->option_count; i++ )
        used += snprintf(text + used, sizeof(text) - (size_t)used, "%s%s", i ? "|" : "", w->options[i]);
    return text;
}

static int
rects_overlap(
    struct ToriRSChromeRect a,
    struct ToriRSChromeRect b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static int
layout_panel(int i)
{
    const struct ToriRSChromePanel* p = &g_chrome.panels[i];
    return p->visible && p->last_rect.w > 0 && p->last_rect.h > 0 && p->style != TORIRS_CHROME_PANEL_MENU
           && !p->modal && i != g_ui.world_panel;
}

/** A group's frame or member window, the group docked by the layout (in
 *  the column or the bottom strip): what this test holds to the dock. A
 *  standalone panel is editor_panels_test.c's (a flyout opens over the world
 *  on purpose, and a same-surface restore puts one back at its saved box). */
static int
layout_docked(int i)
{
    if( i < g_ui.panel_first || i >= g_ui.panel_end )
        return 0;
    for( int gi = 0; gi < g_ui.group_count; gi++ )
    {
        struct EditorUiGroup const* g = &g_ui.groups[gi];
        int const docked = g_ui.docked[g->chrome_panel - g_ui.panel_first];
        if( g->chrome_panel == i )
            return docked;
        for( int slot = 0; slot < g->member_count; slot++ )
            if( g_ui.panels[g->members[slot]].chrome_panel == i )
                return docked;
    }
    return 0;
}

/** Every group in its frame: one window shown under the strip, the frame's
 *  width, titleless; nothing overlaps; all on the surface. */
static void
layout_check(const char* what)
{
    struct EditorUiRect const world = Editor_UiWorldRect(&g_ui);
    struct ToriRSChromeRect world_r;

    world_r.x = world.x;
    world_r.y = world.y;
    world_r.w = world.w;
    world_r.h = world.h;
    CHECK(g_ui.laid_out && world.w > 0 && world.h > 0, "%s: laid out, world %dx%d", what, world.w, world.h);
    for( int i = 0; i < g_chrome.panel_count; i++ )
    {
        struct ToriRSChromeRect const r = g_chrome.panels[i].last_rect;
        if( !layout_panel(i) )
            continue;
        /* The groups the layout docked keep clear of the world and of every
         * other window, on the surface. */
        if( !layout_docked(i) )
            continue;
        CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= g_chrome.surface_w && r.y + r.h <= g_chrome.surface_h,
            "%s: panel %d '%s' %d,%d %dx%d is on the surface", what, i, g_chrome.panels[i].title, r.x, r.y, r.w,
            r.h);
        CHECK(!rects_overlap(r, world_r), "%s: panel %d '%s' does not cover the world", what, i,
            g_chrome.panels[i].title);
        for( int j = 0; j < g_chrome.panel_count; j++ )
            if( j != i && layout_panel(j) )
                CHECK(!rects_overlap(r, g_chrome.panels[j].last_rect), "%s: panels %d '%s' and %d '%s' do not overlap",
                    what, i, g_chrome.panels[i].title, j, g_chrome.panels[j].title);
    }
    for( int gi = 0; gi < g_ui.group_count; gi++ )
    {
        struct EditorUiGroup* g = &g_ui.groups[gi];
        struct ToriRSChromeRect const f = g_chrome.panels[g->chrome_panel].last_rect;
        int shown_windows = 0;
        if( g->shown < 0 || g->parked )
        {
            CHECK(!visible(g->chrome_panel), "%s: group %s with nothing shown hides its frame", what, g->name);
            continue;
        }
        CHECK(visible(g->chrome_panel), "%s: group %s shows its frame", what, g->name);
        for( int slot = 0; slot < g->member_count; slot++ )
            shown_windows += visible(g_ui.panels[g->members[slot]].chrome_panel);
        CHECK(shown_windows == 1, "%s: group %s shows one window (%d)", what, g->name, shown_windows);
        {
            int const panel = g_ui.panels[g->members[g->shown]].chrome_panel;
            struct ToriRSChromeRect const m = g_chrome.panels[panel].last_rect;
            CHECK(visible(panel) && m.x == f.x && m.y == f.y + f.h && m.w == f.w,
                "%s: group %s's shown window %d,%d %dx%d sits under its strip %d,%d %dx%d", what, g->name, m.x, m.y,
                m.w, m.h, f.x, f.y, f.w, f.h);
            CHECK(g_chrome.panels[panel].title[0] == '\0', "%s: group %s's shown window has no title bar of its own",
                what, g->name);
            CHECK(strcmp(g_chrome.panels[g->chrome_panel].title, g->title) == 0, "%s: the frame wears '%s'", what,
                g->title);
        }
    }
}

static void
press(
    int x,
    int y)
{
    ToriRSChrome_MouseMove(&g_chrome, x, y);
    (void)ToriRSChrome_MouseDown(&g_chrome, x, y);
    (void)ToriRSChrome_MouseUp(&g_chrome, x, y);
    frame();
}

static void
drag(
    int from_x,
    int from_y,
    int to_x,
    int to_y)
{
    ToriRSChrome_MouseMove(&g_chrome, from_x, from_y);
    (void)ToriRSChrome_MouseDown(&g_chrome, from_x, from_y);
    ToriRSChrome_MouseMove(&g_chrome, (from_x + to_x) / 2, (from_y + to_y) / 2);
    frame();
    ToriRSChrome_MouseMove(&g_chrome, to_x, to_y);
    (void)ToriRSChrome_MouseUp(&g_chrome, to_x, to_y);
    frame();
}

/** A click on tab `index` of a group's strip, the way the chrome reports one. */
static void
tab_click(
    struct EditorUiGroup* g,
    int index)
{
    ToriRSChrome_PanelSetActiveTab(&g_chrome, g->chrome_panel, index);
    g_chrome.activated = g->tabs;
    frame();
}

static int
view_menu(void)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
        if( g_chrome.widgets[i].kind == TORIRS_CHROME_W_DROPDOWN && g_chrome.widgets[i].menu_mode
            && strcmp(g_chrome.widgets[i].text, "View") == 0 )
            return i;
    return -1;
}

/** The View row that starts with `label` ("Catalog\t..."), or NULL. */
static const char*
view_row(
    const char* label,
    int* out_index)
{
    int const view = view_menu();
    size_t const n = strlen(label);
    if( view < 0 )
        return NULL;
    for( int r = 0; r < g_chrome.widgets[view].option_count; r++ )
    {
        const char* row = g_chrome.widgets[view].options[r];
        if( strncmp(row, label, n) == 0 && (row[n] == '\0' || row[n] == '\t') )
        {
            if( out_index )
                *out_index = r;
            return row;
        }
    }
    return NULL;
}

static void
view_press(const char* label)
{
    int row = -1;
    CHECK(view_row(label, &row) != NULL, "View lists '%s'", label);
    if( row < 0 )
        return;
    ToriRSChrome_DropdownSetSelected(&g_chrome, view_menu(), row);
    g_chrome.activated = view_menu();
    frame();
    frame();
}

/* ---- the registry builds the default groups ------------------------------------- */

static void
test_default_groups(void)
{
    static const struct
    {
        const char* name;
        const char* members[4];
        int count;
    } k_want[] = {
        { "right-top", { "catalog", "locs", "spawns", "inspector" }, 4 },
        { "right-mid", { "terrain", "palette", "catalog_palette" }, 3 },
        { "bottom", { "console", "history" }, 2 },
    };

    boot(1530, 1006, 1);
    CHECK(g_ui.group_count == 3, "three default groups (%d)", g_ui.group_count);
    CHECK(g_chrome.panel_count <= TORIRS_CHROME_MAX_PANELS, "the frames fit the chrome (%d panels)",
        g_chrome.panel_count);
    for( int i = 0; i < 3; i++ )
    {
        struct EditorUiGroup* g = group(k_want[i].name);
        CHECK(g != NULL, "group %s is registered", k_want[i].name);
        if( !g )
            continue;
        CHECK(g->member_count == k_want[i].count, "%s holds %d members (%d)", g->name, k_want[i].count,
            g->member_count);
        for( int m = 0; m < k_want[i].count && m < g->member_count; m++ )
            CHECK(strcmp(g_ui.panels[g->members[m]].name, k_want[i].members[m]) == 0, "%s member %d is %s (%s)",
                g->name, m, k_want[i].members[m], g_ui.panels[g->members[m]].name);
        for( int m = 0; m < k_want[i].count; m++ )
            CHECK(Editor_UiGroupOf(&g_ui, k_want[i].members[m]) == (int)(g - g_ui.groups), "%s is in %s",
                k_want[i].members[m], g->name);
        /* One WINDOW, its first row the strip. */
        CHECK(g_chrome.panels[g->chrome_panel].style == TORIRS_CHROME_PANEL_WINDOW
                  && g_chrome.panels[g->chrome_panel].first_widget == g->tabs
                  && g_chrome.widgets[g->tabs].kind == TORIRS_CHROME_W_TABSTRIP,
            "%s is a window whose first row is a tab strip", g->name);
        CHECK(g->chrome_panel >= g_ui.panel_first && g->chrome_panel < g_ui.panel_end,
            "%s's frame is one of the registry's windows", g->name);
        CHECK(g_chrome.panels[g->chrome_panel].closable && g_chrome.panels[g->chrome_panel].poppable,
            "%s's title bar carries Close and Pop out", g->name);
    }
    /* The standalone modules stay standalone. */
    {
        static const char* const k_alone[] = { "world", "preview", "squares", "areas", "hotkeys", "triggers", "live",
            "accounts", "locbuilder", "npcbuilder", "rect", "record" };
        for( int i = 0; i < (int)(sizeof(k_alone) / sizeof(k_alone[0])); i++ )
            if( Editor_UiFindPanel(&g_ui, k_alone[i]) )
                CHECK(Editor_UiGroupOf(&g_ui, k_alone[i]) < 0, "%s is not in a group", k_alone[i]);
    }

    /* The strips: the open members, titled with their chords. */
    {
        struct EditorUiGroup* top = group("right-top");
        struct EditorUiGroup* mid = group("right-mid");
        struct EditorUiGroup* bottom = group("bottom");
        char chords[48];
        char want[96];

        CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "catalog") == 0,
            "right-top shows the Catalog (%s)", shown_of("right-top") ? shown_of("right-top") : "-");
        CHECK(Editor_UiPanelInStrip(&g_ui, "catalog") && Editor_UiPanelInStrip(&g_ui, "inspector")
                  && !Editor_UiPanelInStrip(&g_ui, "locs") && !Editor_UiPanelInStrip(&g_ui, "spawns"),
            "right-top's strip: Catalog and Inspector (Locs waits for a selection, Spawns for View)");
        Editor_HotkeyDisplay(&g_ui.hotkeys, (enum Editor_HotkeyAction)Editor_HotkeyForPanel("catalog"), chords,
            (int)sizeof(chords));
        snprintf(want, sizeof(want), "Catalog (%s)|", chords);
        CHECK(chords[0] && strncmp(strip_text(top), want, strlen(want)) == 0,
            "the Catalog tab wears its chord: '%s' (strip '%s')", want, strip_text(top));
        CHECK(g_chrome.widgets[top->tabs].option_count == 2, "right-top's strip has two tabs (%d)",
            g_chrome.widgets[top->tabs].option_count);
        CHECK(shown_of("right-mid") && strcmp(shown_of("right-mid"), "terrain") == 0, "right-mid shows Terrain");
        CHECK(strstr(strip_text(mid), "Terrain (") == strip_text(mid) && strstr(strip_text(mid), "|Palette ("),
            "right-mid's strip: Terrain and Palette with chords ('%s')", strip_text(mid));
        CHECK(shown_of("bottom") && strcmp(shown_of("bottom"), "console") == 0, "bottom shows the console");
        CHECK(strstr(strip_text(bottom), "Output") && strstr(strip_text(bottom), "History"),
            "bottom's strip: Output and History ('%s')", strip_text(bottom));
        CHECK(g_chrome.widgets[top->tabs].selected == 0 && g_chrome.panels[top->chrome_panel].active_tab == 0,
            "the strip selects the shown tab");
    }

    /* The layout: one place per group. */
    layout_check("1530x1006 boot");
    {
        struct EditorUiGroup* top = group("right-top");
        struct EditorUiGroup* mid = group("right-mid");
        struct EditorUiGroup* bottom = group("bottom");
        struct ToriRSChromeRect const fm = g_chrome.panels[mid->chrome_panel].last_rect;
        struct ToriRSChromeRect const ft = g_chrome.panels[top->chrome_panel].last_rect;
        struct ToriRSChromeRect const fb = g_chrome.panels[bottom->chrome_panel].last_rect;
        struct ToriRSChromeRect const console = g_chrome.panels[window_of("console")].last_rect;
        struct ToriRSChromeRect const status = g_chrome.panels[window_of("statusbar")].last_rect;

        CHECK(fm.x == g_ui.dock_x && fm.y == g_ui.dock_top, "right-mid heads the column (Terrain's place) %d,%d",
            fm.x, fm.y);
        CHECK(ft.x == g_ui.dock_x && ft.y > fm.y, "right-top docks in the column below it (%d,%d)", ft.x, ft.y);
        /* The status bar takes the foot (workbench 6.0): the strip ends on it. */
        CHECK(status.h > 0 && status.y + status.h == g_chrome.surface_h,
            "the status bar spans the foot (%d,%d %dx%d)", status.x, status.y, status.w, status.h);
        CHECK(fb.x == g_ui.world.x && console.y + console.h == status.y,
            "bottom is the strip under the world, above the status bar (frame %d,%d, console to %d)", fb.x, fb.y,
            console.y + console.h);
        CHECK(g_ui.world.y + g_ui.world.h == fb.y, "the world ends where the bottom strip starts");
        /* Its size is the largest member's: every member has the box. */
        for( int slot = 0; slot < top->member_count; slot++ )
        {
            struct ToriRSChromePanel const* p = &g_chrome.panels[g_ui.panels[top->members[slot]].chrome_panel];
            CHECK(p->x == ft.x && p->fixed_w == ft.w && p->fixed_h == top->body_h,
                "right-top member %s has the group's box", g_ui.panels[top->members[slot]].name);
        }
    }
    printf("groups at 1530x1006: dock x %d w %d, world %d,%d %dx%d\n", g_ui.dock_x, g_ui.dock_w, g_ui.world.x,
        g_ui.world.y, g_ui.world.w, g_ui.world.h);
    Editor_UiFree(&g_ui);

    /* Every surface the panels test lays out, at 1x and 2x. */
    {
        static const int sizes[][2] = { { 1280, 800 }, { 1530, 1006 }, { 1920, 1080 }, { 2560, 1440 } };
        for( int scale = 1; scale <= 2; scale++ )
            for( int s = 0; s < 4; s++ )
            {
                char what[48];
                boot(sizes[s][0], sizes[s][1], scale);
                snprintf(what, sizeof(what), "%dx%d at %dx", sizes[s][0], sizes[s][1], scale);
                layout_check(what);
                Editor_UiFree(&g_ui);
            }
    }
}

/* ---- showing and hiding members --------------------------------------------------- */

static void
test_strip_follows(void)
{
    struct EditorUiGroup* top;
    int const before_tabs = 2;

    boot(1530, 1006, 1);
    top = group("right-top");

    /* View > Spawns: it joins the strip, in front, at its own place in it. */
    view_press("Spawns");
    CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "spawns") == 0, "View > Spawns brings Spawns up");
    CHECK(g_chrome.widgets[top->tabs].option_count == before_tabs + 1 && strstr(strip_text(top), "|Spawns ("),
        "Spawns joins the strip ('%s')", strip_text(top));
    CHECK(!visible(window_of("catalog")) && Editor_UiPanelInStrip(&g_ui, "catalog"),
        "the Catalog waits behind its tab, still open");
    CHECK(g_chrome.widgets[top->tabs].selected == 1, "the strip selects Spawns (%d)",
        g_chrome.widgets[top->tabs].selected);
    {
        const char* row = view_row("Catalog", NULL);
        CHECK(row && !strstr(row, "hidden"), "View does not call a tab behind another hidden ('%s')", row ? row : "-");
        row = view_row("Locs", NULL);
        CHECK(row && strstr(row, "hidden"), "View calls Locs (in no strip) hidden ('%s')", row ? row : "-");
    }
    layout_check("Spawns in front");

    /* A tab click brings the Inspector up. */
    tab_click(top, 2);
    CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "inspector") == 0, "a click on its tab shows the Inspector");
    CHECK(!visible(window_of("spawns")) && visible(window_of("inspector")), "the windows follow the tab");
    layout_check("Inspector by its tab");

    /* The module showing itself (Locs on a selection) and hiding itself. */
    {
        struct Editor_LocRef ref;
        memset(&ref, 0, sizeof(ref));
        ref.map_x = 50;
        ref.map_z = 50;
        Editor_UiSelectLoc(&g_ui, &ref);
        frame();
        frame();
        CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "locs") == 0,
            "a selection opens Locs in front of its group (%s)", shown_of("right-top") ? shown_of("right-top") : "-");
        CHECK(strstr(strip_text(top), "|Locs ("), "Locs is in the strip ('%s')", strip_text(top));
        layout_check("Locs on a selection");
        Editor_UiSelectLoc(&g_ui, NULL);
        frame();
        frame();
        CHECK(!Editor_UiPanelInStrip(&g_ui, "locs") && !strstr(strip_text(top), "Locs"),
            "an empty selection closes Locs: out of the strip ('%s')", strip_text(top));
        CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "inspector") == 0,
            "and the tab in front before it comes back (%s)", shown_of("right-top") ? shown_of("right-top") : "-");
    }

    /* View on the front tab closes it; the hotkey's path is the same. */
    CHECK(Editor_UiTogglePanel(&g_ui, "inspector") == 0, "toggling the shown Inspector closes it");
    frame();
    CHECK(!Editor_UiPanelInStrip(&g_ui, "inspector") && shown_of("right-top")
              && strcmp(shown_of("right-top"), "spawns") == 0,
        "the Inspector leaves the strip and the tab before it (Spawns) is in front (%s)",
        shown_of("right-top") ? shown_of("right-top") : "-");
    /* View on a tab behind brings it to the front rather than closing it. */
    CHECK(Editor_UiTogglePanel(&g_ui, "catalog") == 1, "toggling the Catalog behind a tab opens it");
    frame();
    CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "catalog") == 0 && Editor_UiPanelInStrip(&g_ui, "spawns"),
        "the Catalog is in front, Spawns still in the strip");

    /* Close on the group's title bar closes the shown tab. */
    {
        struct ToriRSChromeRect const close = ToriRSChrome_PanelCloseRect(&g_chrome, top->chrome_panel);
        CHECK(close.w > 0, "the group's title bar has a Close box");
        press(close.x + close.w / 2, close.y + close.h / 2);
        frame();
        CHECK(!Editor_UiPanelInStrip(&g_ui, "catalog") && visible(top->chrome_panel)
                  && shown_of("right-top") && strcmp(shown_of("right-top"), "spawns") == 0,
            "Close on the group closed the Catalog tab; Spawns is in front, the group stays (%s)",
            shown_of("right-top") ? shown_of("right-top") : "-");
        layout_check("after Close on the group");
        press(close.x + close.w / 2, close.y + close.h / 2);
        frame();
        frame();
        CHECK(!visible(top->chrome_panel) && shown_of("right-top") == NULL,
            "closing the last tab hides the group");
        CHECK(!visible(window_of("spawns")) && !visible(window_of("catalog")), "and every member window");
        layout_check("right-top closed");
        /* View brings a member back, and with it the group. */
        view_press("Catalog");
        frame();
        CHECK(visible(top->chrome_panel) && shown_of("right-top") && strcmp(shown_of("right-top"), "catalog") == 0,
            "View > Catalog brings the group back with the Catalog in front");
        CHECK(g_chrome.panels[top->chrome_panel].x == g_ui.dock_x, "back in the column (%d, dock %d)",
            g_chrome.panels[top->chrome_panel].x, g_ui.dock_x);
        layout_check("Catalog back");
    }

    /* Pop out on the group's title bar pops the shown tab. */
    {
        struct ToriRSChromeRect const pop = ToriRSChrome_PanelPopOutRect(&g_chrome, top->chrome_panel);
        CHECK(pop.w > 0, "the group's title bar has a Pop out button");
        view_press("Inspector");
        press(pop.x + pop.w / 2, pop.y + pop.h / 2);
        frame();
        CHECK(g_spawned.calls == 1 && strcmp(g_spawned.module, "inspector") == 0,
            "Pop out on the group pops its shown tab (asked for '%s')", g_spawned.module);
        CHECK(Editor_UiPanelPoppedOut(&g_ui, "inspector") && !Editor_UiPanelInStrip(&g_ui, "inspector"),
            "the Inspector is popped out and out of its strip");
        CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "catalog") == 0,
            "the Catalog is in front again");
        {
            const char* row = view_row("Inspector", NULL);
            CHECK(row && strstr(row, "popped out"), "View says the Inspector popped out ('%s')", row ? row : "-");
        }
        /* The per-panel path keeps working for a member. */
        CHECK(Editor_UiPanelPopOut(&g_ui, "catalog") == 1 && g_spawned.calls == 2
                  && strcmp(g_spawned.module, "catalog") == 0,
            "Editor_UiPanelPopOut pops a member by name");
        frame();
        CHECK(!visible(top->chrome_panel) && !Editor_UiPanelInStrip(&g_ui, "catalog"),
            "with both popped out the group hides");
        /* View brings each back into its group (pop in). */
        view_press("Inspector");
        frame();
        CHECK(!Editor_UiPanelPoppedOut(&g_ui, "inspector") && Editor_UiPanelInStrip(&g_ui, "inspector")
                  && visible(top->chrome_panel),
            "View > Inspector pops it back in, into its group");
        view_press("Catalog");
        frame();
        CHECK(Editor_UiPanelInStrip(&g_ui, "catalog") && shown_of("right-top")
                  && strcmp(shown_of("right-top"), "catalog") == 0,
            "and the Catalog, in front");
        layout_check("both popped back in");
    }

    /* The pointer over the group's strip is its shown tab (the pop-out
     * hotkey's "panel under the pointer"). */
    {
        struct ToriRSChromeRect const f = g_chrome.panels[top->chrome_panel].last_rect;
        const char* at = Editor_UiPanelAt(&g_ui, f.x + f.w / 2, f.y + f.h / 2);
        CHECK(at && strcmp(at, "catalog") == 0, "the panel under the group's strip is its shown tab (%s)", at ? at : "-");
    }

    /* Hide / show panels takes every strip away and brings it back. */
    {
        char before[512];
        snprintf(before, sizeof(before), "%s", strip_text(top));
        CHECK(Editor_UiToggleAllPanels(&g_ui) == 1, "Hide panels");
        frame();
        CHECK(!visible(top->chrome_panel) && !visible(group("right-mid")->chrome_panel),
            "the groups hide with the panels");
        CHECK(Editor_UiToggleAllPanels(&g_ui) == 0, "Show panels");
        frame();
        frame();
        CHECK(strcmp(strip_text(top), before) == 0 && shown_of("right-top")
                  && strcmp(shown_of("right-top"), "catalog") == 0,
            "every tab back, the same one in front ('%s' was '%s')", strip_text(top), before);
        layout_check("panels shown again");
    }
    Editor_UiFree(&g_ui);
}

/* ---- leaving, Reset groups, solo, maximise, drag and resize ------------------- */

/** The status bar's shown LABEL whose text ends with `tail`, or NULL. */
static const char*
statusbar_hint_ending(const char* tail)
{
    int const panel = window_of("statusbar");
    size_t const tail_len = strlen(tail);

    if( panel < 0 )
        return NULL;
    for( int w = g_chrome.panels[panel].first_widget; w >= 0; w = g_chrome.widgets[w].next )
    {
        struct ToriRSChromeWidget const* widget = &g_chrome.widgets[w];
        size_t const len = strlen(widget->text);
        if( widget->kind == TORIRS_CHROME_W_LABEL && !widget->hidden && len >= tail_len
            && strcmp(widget->text + len - tail_len, tail) == 0 )
            return widget->text;
    }
    return NULL;
}

/** The status bar's shown LABELs, joined with '|'. */
static const char*
statusbar_text(void)
{
    static char text[1024];
    int const panel = window_of("statusbar");
    int used = 0;

    text[0] = '\0';
    if( panel < 0 )
        return text;
    for( int w = g_chrome.panels[panel].first_widget; w >= 0 && used < (int)sizeof(text); w = g_chrome.widgets[w].next )
        if( g_chrome.widgets[w].kind == TORIRS_CHROME_W_LABEL && !g_chrome.widgets[w].hidden )
            used += snprintf(text + used, sizeof(text) - (size_t)used, "%s%s", used ? "|" : "", g_chrome.widgets[w].text);
    return text;
}

/** Del's path: the newest console line after Editor_HotkeyRun(Delete). */
static const char*
delete_says(void)
{
    const char* line;

    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_EDIT_DELETE) == 1, "Delete runs");
    line = Editor_UiConsoleLine(&g_ui, 0);
    return line ? line : "";
}

/* A Spawns selection behind a tab of its strip is still open (review 6.0):
 * the S context, Del's route to the spawns and the hint strip's "delete"
 * all keep acting on it while another tab of right-top is in front. */
static void
test_spawns_behind_tab(void)
{
    struct EditorUiGroup* top;
    struct Editor_SpawnRow row;
    struct Editor_HotkeyContextFacts facts;

    /* Wide enough that the hint strip's share reaches Select's last row
     * ("delete"); at 1530 the rows before it fill the share. */
    boot(2560, 1006, 1);
    top = group("right-top");
    view_press("Spawns");
    CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "spawns") == 0, "spawns tab: Spawns in front");

    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "%s", "goblin");
    row.abs_x = 3222;
    row.abs_z = 3218;
    CHECK(Editor_Spawns_Select(&g_ui, &row, 1) == 1, "spawns tab: one spawn selected");
    frame();
    CHECK(Editor_Spawns_SelectionCount(&g_ui) == 1, "spawns tab: the selection counts with Spawns in front");
    CHECK(statusbar_hint_ending("] delete") != NULL, "spawns tab: the strip offers delete in front ('%s')",
        statusbar_text());
    CHECK(strstr(delete_says(), "pick a spawn") != NULL, "spawns tab: Del goes to the spawns in front ('%s')",
        Editor_UiConsoleLine(&g_ui, 0));

    /* The Catalog's tab to the front: Spawns waits behind it, still open. */
    tab_click(top, 0);
    CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "catalog") == 0, "spawns tab: the Catalog in front");
    CHECK(!visible(window_of("spawns")) && Editor_UiPanelInStrip(&g_ui, "spawns"),
        "spawns tab: Spawns waits behind its tab");
    CHECK(Editor_Spawns_SelectionCount(&g_ui) == 1, "spawns tab: the selection still counts behind the tab (%d)",
        Editor_Spawns_SelectionCount(&g_ui));
    memset(&facts, 0, sizeof(facts));
    facts.tool = (int)g_ui.tools.tool;
    facts.spawn_count = Editor_Spawns_SelectionCount(&g_ui);
    CHECK(Editor_HotkeyContextsFrom(&facts) & EDITOR_HK_CTX_S, "spawns tab: the S context is live behind the tab");
    CHECK(statusbar_hint_ending("] delete") != NULL, "spawns tab: the strip still offers delete behind the tab ('%s')",
        statusbar_text());
    CHECK(strstr(delete_says(), "pick a spawn") != NULL, "spawns tab: Del still goes to the spawns ('%s')",
        Editor_UiConsoleLine(&g_ui, 0));

    /* Closed out of the strip, the selection is no longer the keys'. */
    tab_click(top, 1);
    CHECK(Editor_UiTogglePanel(&g_ui, "spawns") == 0, "spawns tab: toggling the shown Spawns closes it");
    frame();
    CHECK(!Editor_UiPanelInStrip(&g_ui, "spawns") && Editor_Spawns_SelectionCount(&g_ui) == 0,
        "spawns tab: closed, the selection counts no more");
    CHECK(statusbar_hint_ending("] delete") == NULL, "spawns tab: closed, the strip drops delete ('%s')",
        statusbar_text());
}

static void
test_reset_groups(void)
{
    struct EditorUiGroup* mid;
    int palette;

    boot(1530, 1006, 1);
    mid = group("right-mid");
    palette = window_of("palette");

    Editor_UiLeaveGroup(&g_ui, "palette");
    frame();
    frame();
    CHECK(Editor_UiGroupOf(&g_ui, "palette") < 0 && mid->member_count == 2, "Palette left its group");
    CHECK(strcmp(g_chrome.panels[palette].title, "Palette") == 0, "with its own title bar back ('%s')",
        g_chrome.panels[palette].title);
    CHECK(visible(palette) && !strstr(strip_text(mid), "Palette"),
        "open as a panel of its own, out of the strip ('%s')", strip_text(mid));
    layout_check("Palette on its own");

    /* View > Reset groups: back at its place in the strip. */
    view_press("Reset groups");
    CHECK(Editor_UiGroupOf(&g_ui, "palette") == (int)(mid - g_ui.groups) && mid->member_count == 3
              && strcmp(g_ui.panels[mid->members[1]].name, "palette") == 0,
        "Reset groups puts Palette back as right-mid's second member");
    CHECK(Editor_UiPanelInStrip(&g_ui, "palette") && strstr(strip_text(mid), "|Palette ("),
        "open in the strip ('%s')", strip_text(mid));
    CHECK(g_chrome.panels[palette].title[0] == '\0' || !visible(palette), "inside the frame it wears no title");
    layout_check("after Reset groups");

    /* A module in another group: Reset moves it home. */
    Editor_UiLeaveGroup(&g_ui, "history");
    Editor_UiJoinGroup(&g_ui, "history", "right-mid");
    frame();
    CHECK(Editor_UiGroupOf(&g_ui, "history") == (int)(mid - g_ui.groups), "History joined right-mid");
    Editor_UiResetGroups(&g_ui);
    frame();
    frame();
    CHECK(Editor_UiGroupOf(&g_ui, "history") == (int)(group("bottom") - g_ui.groups) && mid->member_count == 3,
        "Reset groups sends History home to the bottom strip");
    layout_check("History home");

    /* Drag the group by its title: the shown window follows, the group is
     * the user's and keeps its place through a relayout. */
    {
        struct ToriRSChromeRect f = g_chrome.panels[mid->chrome_panel].last_rect;
        int const to_x = g_ui.world.x + 200;
        int const to_y = g_ui.world.y + 120;
        int shown_panel;
        drag(f.x + 30, f.y + 5, to_x + 30, to_y + 5);
        frame();
        f = g_chrome.panels[mid->chrome_panel].last_rect;
        shown_panel = g_ui.panels[mid->members[mid->shown]].chrome_panel;
        CHECK(g_chrome.panels[mid->chrome_panel].user_placed && abs(f.x - to_x) <= EDP_GRID && abs(f.y - to_y) <= EDP_GRID,
            "the group moved with its title (%d,%d for %d,%d)", f.x, f.y, to_x, to_y);
        CHECK(g_chrome.panels[shown_panel].last_rect.x == f.x && g_chrome.panels[shown_panel].last_rect.y == f.y + f.h,
            "its shown window came along");
        ToriRSChrome_SetSurface(&g_chrome, 1920, 1080);
        frame();
        frame();
        CHECK(g_chrome.panels[mid->chrome_panel].x == f.x && g_chrome.panels[mid->chrome_panel].y == f.y,
            "a placed group keeps its place through a relayout");

        /* Its shown window's corner resizes the group; the frame follows. */
        {
            struct ToriRSChromeRect const m = g_chrome.panels[shown_panel].last_rect;
            drag(m.x + m.w - 3, m.y + m.h - 3, m.x + m.w - 3 + 64, m.y + m.h - 3 + 40);
            frame();
            CHECK(g_chrome.panels[shown_panel].fixed_w > m.w && g_chrome.panels[mid->chrome_panel].fixed_w
                                                                  == g_chrome.panels[shown_panel].fixed_w,
                "a resize of the shown window widens the group (%d -> %d, frame %d)", m.w,
                g_chrome.panels[shown_panel].fixed_w, g_chrome.panels[mid->chrome_panel].fixed_w);
            CHECK(mid->body_h == g_chrome.panels[shown_panel].fixed_h && mid->body_h > m.h,
                "and makes it taller (%d)", mid->body_h);
            /* Every tab has the box. */
            tab_click(mid, 1);
            CHECK(g_chrome.panels[palette].fixed_w == mid->w && g_chrome.panels[palette].fixed_h == mid->body_h
                      && g_chrome.panels[palette].x == mid->x,
                "the next tab opens in the group's box");
        }
    }

    /* View > Reset layout: the group back in the column. */
    Editor_UiResetLayout(&g_ui);
    frame();
    frame();
    CHECK(!g_chrome.panels[mid->chrome_panel].user_placed && g_chrome.panels[mid->chrome_panel].x == g_ui.dock_x,
        "Reset layout docks the group again");
    layout_check("after Reset layout");

    /* Double-click the group's title: maximised over the dock, strip on top. */
    {
        struct ToriRSChromeRect const f = g_chrome.panels[mid->chrome_panel].last_rect;
        int const shown_panel = g_ui.panels[mid->members[mid->shown]].chrome_panel;
        int64_t ms = 100000;
        ToriRSChrome_Tick(&g_chrome, ms);
        press(f.x + 40, f.y + 5);
        ToriRSChrome_Tick(&g_chrome, ms + 100);
        press(f.x + 40, f.y + 5);
        frame();
        CHECK(ToriRSChrome_PanelIsMaximised(&g_chrome, shown_panel), "a double-click on the group's title maximises it");
        CHECK(g_chrome.panels[mid->chrome_panel].y == g_ui.dock_top
                  && g_chrome.panels[shown_panel].y == g_ui.dock_top + mid->frame_h
                  && g_chrome.panels[shown_panel].fixed_w == g_chrome.surface_w,
            "the strip on top of the dock, the window under it, full width (%d,%d)",
            g_chrome.panels[mid->chrome_panel].y, g_chrome.panels[shown_panel].y);
        {
            struct EditorLayoutFile file;
            struct EditorLayoutGroup const* gs;
            Editor_UiLayoutCapture(&g_ui, &file);
            gs = EditorLayout_FindGroup(&file, "right-mid");
            CHECK(gs && gs->maximised && gs->x == f.x && gs->y == f.y,
                "the capture says maximised, with the box it restores to (%d,%d)", gs ? gs->x : -1, gs ? gs->y : -1);
        }
        ToriRSChrome_Tick(&g_chrome, ms + 2000);
        press(f.x + 40, g_ui.dock_top + 5);
        ToriRSChrome_Tick(&g_chrome, ms + 2100);
        press(f.x + 40, g_ui.dock_top + 5);
        frame();
        frame();
        CHECK(!ToriRSChrome_PanelIsMaximised(&g_chrome, shown_panel)
                  && g_chrome.panels[mid->chrome_panel].x == f.x && g_chrome.panels[mid->chrome_panel].y == f.y,
            "and again restores it to the column");
        layout_check("restored");
    }

    /* A popped-out window of a member: its own title, no frames. */
    Editor_UiFree(&g_ui);
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 640, 480);
    Editor_UiInit(&g_ui, &g_chrome, NULL);
    g_ui.app_services.user_data = &g_app_stand_in;
    Editor_UiRegisterToolsPanels(&g_ui);
    CHECK(Editor_UiSetSolo(&g_ui, "catalog"), "a popped-out Catalog window");
    Editor_UiSetVisible(&g_ui, 1);
    for( int i = 0; i < 4; i++ )
        frame();
    CHECK(visible(window_of("catalog")) && strcmp(g_chrome.panels[window_of("catalog")].title, "Catalog") == 0,
        "shows the Catalog alone under its own title ('%s')", g_chrome.panels[window_of("catalog")].title);
    for( int i = 0; i < g_ui.group_count; i++ )
        CHECK(!visible(g_ui.groups[i].chrome_panel), "no group frame in it (%s)", g_ui.groups[i].name);
    Editor_UiFree(&g_ui);
}

/* ---- the layout file ------------------------------------------------------------- */

static int
layout_files_equal(
    const struct EditorLayoutFile* a,
    const struct EditorLayoutFile* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void
test_layout_file(void)
{
    static struct EditorLayoutFile saved;
    static struct EditorLayoutFile reread;
    struct EditorUiGroup* top;
    char* text = NULL;
    int size = 0;
    struct ToriRSChromeRect frame_before;
    struct ToriRSChromeRect window_before;

    /* The codec: a [group] section, its `shown` a name, comments and
     * unknown keys skipped. */
    {
        const char* hand = "[layout]\nsurface_w = 800\n"
                           "[group right-top]\nvisible = 1\nshown = spawns ; hand-edited\nbogus = 2\nx = 40\n"
                           "w = abc\n[group]\nx = 1\n[groupies x]\ny = 3\n";
        struct EditorLayoutGroup const* g;
        CHECK(EditorLayout_Decode(&reread, hand, (int)strlen(hand)), "a file with a group section decodes");
        g = EditorLayout_FindGroup(&reread, "right-top");
        CHECK(reread.group_count == 1 && g && g->has_visible && g->visible && g->has_shown
                  && strcmp(g->shown, "spawns") == 0 && g->has_x && g->x == 40 && !g->has_w && !g->has_y,
            "the group states visible, shown (comment cut off) and x; the rest is skipped");
    }

    /* A session: Spawns open behind the Catalog, Inspector closed, right-mid
     * dragged out, Palette in front of it. */
    boot(1530, 1006, 1);
    top = group("right-top");
    view_press("Spawns");
    view_press("Catalog");
    CHECK(Editor_UiTogglePanel(&g_ui, "inspector") == 1, "the Inspector to the front");
    frame();
    CHECK(Editor_UiTogglePanel(&g_ui, "inspector") == 0, "and closed");
    frame();
    {
        struct EditorUiGroup* mid = group("right-mid");
        struct ToriRSChromeRect const f = g_chrome.panels[mid->chrome_panel].last_rect;
        drag(f.x + 30, f.y + 5, g_ui.world.x + 230, g_ui.world.y + 150);
        tab_click(mid, 1);
    }
    frame();
    frame_before = g_chrome.panels[top->chrome_panel].last_rect;
    window_before = g_chrome.panels[window_of("catalog")].last_rect;
    Editor_UiLayoutCapture(&g_ui, &saved);
    {
        struct EditorLayoutGroup const* g = EditorLayout_FindGroup(&saved, "right-top");
        struct EditorLayoutGroup const* mid = EditorLayout_FindGroup(&saved, "right-mid");
        struct EditorLayoutPanel const* cat = EditorLayout_Find(&saved, "catalog");
        struct EditorLayoutPanel const* ins = EditorLayout_Find(&saved, "inspector");
        CHECK(saved.group_count == 3, "the capture holds the three groups (%d)", saved.group_count);
        CHECK(g && g->visible && g->has_shown && strcmp(g->shown, "catalog") == 0 && !g->user_placed
                  && g->x == frame_before.x && g->y == frame_before.y && g->w == frame_before.w
                  && g->h == frame_before.h + window_before.h,
            "[group right-top]: shown, its whole box (%d,%d %dx%d)", g ? g->x : 0, g ? g->y : 0, g ? g->w : 0,
            g ? g->h : 0);
        CHECK(mid && mid->user_placed && strcmp(mid->shown, "palette") == 0, "[group right-mid]: placed, Palette in front");
        CHECK(cat && cat->has_visible && cat->visible && !cat->has_x && !cat->has_w && !cat->has_user_placed,
            "a member's [panel] says only that it is in its strip");
        CHECK(ins && ins->has_visible && !ins->visible, "the closed Inspector is out of its strip");
    }
    EditorLayout_Encode(&saved, &text, &size);
    CHECK(text && strstr(text, "\n[group right-top]\n") && strstr(text, "shown = catalog\n"),
        "the text has the group section");
    CHECK(EditorLayout_Decode(&reread, text, size) && layout_files_equal(&saved, &reread),
        "the captured layout survives encode -> decode");
    printf("group section:\n%.*s\n", (int)(strstr(text, "[group right-mid]") ? strstr(text, "[group right-mid]") - strstr(text, "[group right-top]") : 0),
        strstr(text, "[group right-top]") ? strstr(text, "[group right-top]") : "");
    ToriMem_Free(ToriMem_Process(), text);
    Editor_UiFree(&g_ui);

    /* Boot with it on the same surface: every group as it was. */
    boot_with(1530, 1006, 1, &reread);
    top = group("right-top");
    {
        struct EditorUiGroup* mid = group("right-mid");
        struct EditorLayoutGroup const* sm = EditorLayout_FindGroup(&reread, "right-mid");
        struct ToriRSChromeRect const f = g_chrome.panels[top->chrome_panel].last_rect;
        struct ToriRSChromeRect const w = g_chrome.panels[window_of("catalog")].last_rect;
        CHECK(shown_of("right-top") && strcmp(shown_of("right-top"), "catalog") == 0
                  && Editor_UiPanelInStrip(&g_ui, "spawns") && !Editor_UiPanelInStrip(&g_ui, "inspector"),
            "right-top comes back: Catalog in front, Spawns behind, Inspector closed");
        CHECK(f.x == frame_before.x && f.y == frame_before.y && f.w == frame_before.w && w.h == window_before.h,
            "at its box (%d,%d %dx%d+%d, was %d,%d %dx%d+%d)", f.x, f.y, f.w, f.h, w.h, frame_before.x,
            frame_before.y, frame_before.w, frame_before.h, window_before.h);
        CHECK(g_chrome.panels[mid->chrome_panel].user_placed && g_chrome.panels[mid->chrome_panel].x == sm->x
                  && g_chrome.panels[mid->chrome_panel].y == sm->y && shown_of("right-mid")
                  && strcmp(shown_of("right-mid"), "palette") == 0,
            "right-mid where it was dragged, Palette in front");
        layout_check("restored from the file");
    }
    Editor_UiFree(&g_ui);

    /* Another surface: the placed group keeps its box, the rest is laid
     * out for the new one. */
    boot_with(1920, 1080, 1, &reread);
    {
        struct EditorUiGroup* mid = group("right-mid");
        struct EditorLayoutGroup const* sm = EditorLayout_FindGroup(&reread, "right-mid");
        CHECK(g_chrome.panels[mid->chrome_panel].x == sm->x && g_chrome.panels[mid->chrome_panel].y == sm->y,
            "stale surface: the placed group keeps its box");
        CHECK(g_chrome.panels[group("right-top")->chrome_panel].x == g_ui.dock_x && g_ui.dock_x + g_ui.dock_w == 1920,
            "and right-top docks in the column for 1920");
        layout_check("1920x1080 from a 1530x1006 file");
    }
    Editor_UiFree(&g_ui);

    /* A file from before groups: [panel] sections with boxes, no [group]. */
    {
        static struct EditorLayoutFile old;
        struct EditorLayoutPanel* p;
        EditorLayout_Clear(&old);
        old.version = 1;
        old.surface_w = 1530;
        old.surface_h = 1006;
        old.scale = 1;
        old.has_snap = 1;
        old.snap = 1;
        p = EditorLayout_Add(&old, "catalog");
        p->has_visible = p->has_user_placed = p->has_x = p->has_y = p->has_w = p->has_h = 1;
        p->visible = 0;
        p->user_placed = 1;
        p->x = 100;
        p->y = 100;
        p->w = 300;
        p->h = 300;
        p = EditorLayout_Add(&old, "inspector");
        p->has_visible = p->has_user_placed = p->has_x = p->has_y = p->has_w = p->has_h = 1;
        p->visible = 1;
        p->user_placed = 0;
        p->x = 1224;
        p->y = 400;
        p->w = 306;
        p->h = 200;
        p = EditorLayout_Add(&old, "squares");
        p->has_visible = 1;
        p->visible = 0;
        EditorLayout_Encode(&old, &text, &size);
        CHECK(EditorLayout_Decode(&reread, text, size) && reread.group_count == 0, "an old file has no groups");
        ToriMem_Free(ToriMem_Process(), text);
        boot_with(1530, 1006, 1, &reread);
        top = group("right-top");
        CHECK(!Editor_UiPanelInStrip(&g_ui, "catalog") && Editor_UiPanelInStrip(&g_ui, "inspector")
                  && shown_of("right-top") && strcmp(shown_of("right-top"), "inspector") == 0,
            "an old file's `visible` says who is in the strip: the Inspector, not the Catalog");
        CHECK(!g_chrome.panels[top->chrome_panel].user_placed && g_chrome.panels[top->chrome_panel].x == g_ui.dock_x
                  && !g_chrome.panels[window_of("catalog")].user_placed,
            "its boxes are ignored: the group docks in the column (placed %d, x %d, dock %d, catalog placed %d)",
            g_chrome.panels[top->chrome_panel].user_placed, g_chrome.panels[top->chrome_panel].x, g_ui.dock_x,
            g_chrome.panels[window_of("catalog")].user_placed);
        CHECK(!visible(window_of("squares")), "a standalone panel's `visible` still applies");
        layout_check("an old-format file");
        Editor_UiFree(&g_ui);
    }
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    /* No layout another run left behind. */
    setenv("TORIRS_EDITOR_LAYOUT", "", 1);

    test_default_groups();
    test_strip_follows();
    test_spawns_behind_tab();
    test_reset_groups();
    test_layout_file();

    printf("editor_groups_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
