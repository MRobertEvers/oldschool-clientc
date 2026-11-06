/*
 * Workbench step 6.0b, the editor's half (design section 2 "R1 in full",
 * section 3 "Every panel pops out", section 16 step 6.0b):
 *
 *   - THE GOLDEN: the chrome's draw list (every primitive Build emits) for a
 *     fixed scene of chrome widgets and for the whole editor registry laid
 *     out on a 1530x1006 surface, at the integer scales 1, 2 and 3, digested
 *     and compared with the digests recorded before step 6.0b touched the
 *     draw path -- so the integer scales stay pixel-identical (the rasteriser
 *     draws only what the list says);
 *   - the chrome scale snap table (Auto from the display's ratio, the touch
 *     floor, a decimal pin);
 *   - View > Chrome scale and View > Text size: the rows, their hotkey
 *     actions (unbound), the relayout, the theme's body face and row height;
 *   - the device-local prefs file (editor_prefs.ini) round trip;
 *   - the Dock button on a popped-out window's title bar and
 *     TORIRSMAPED_STATE_DOCK_PANEL, across two sessions of one embedded
 *     ToriRSMapEd: the popped-out window asks, the leader shows the panel
 *     again exactly as View > panel does;
 *   - which modules a panel-only window skips the world for.
 *
 *   ./editor_workbench_test <scratch-dir>
 *
 * Headless and content-free (no square is opened), so it runs where the
 * content tree is not checked out. UPDATE_GOLDEN=1 prints the digests
 * instead of comparing them.
 */

#include "editor/editor.h"
#include "editor/editor_host.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_layout.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/torirs_chrome_scale_q.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>

#include <assert.h>
#include <stdint.h>
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

static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static int g_app_stand_in;

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
frame_of(
    struct ToriRSChrome* chrome,
    struct EditorUi* ui)
{
    ToriRSChrome_Build(chrome);
    Editor_UiTick(ui);
    ToriRSChrome_Build(chrome);
}

static void
boot_registry(
    struct ToriRSChrome* chrome,
    struct EditorUi* ui,
    struct Editor* editor,
    int w,
    int h,
    int scale)
{
    ToriRSChrome_Init(chrome);
    ToriRSChrome_SetScale(chrome, scale);
    ToriRSChrome_SetSurface(chrome, w, h);
    Editor_UiInit(ui, chrome, editor);
    ui->app_services.user_data = &g_app_stand_in;
    ui->app_services.spawn_panel = fake_spawn_panel;
    Editor_UiRegisterToolsPanels(ui);
    Editor_UiSetVisible(ui, 1);
    for( int i = 0; i < 5; i++ )
        frame_of(chrome, ui);
}

/* ---- the golden ----------------------------------------------------------- */

static uint32_t
fnv(
    uint32_t h,
    const void* data,
    size_t size)
{
    const unsigned char* p = data;
    for( size_t i = 0; i < size; i++ )
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint32_t
fnv_int(
    uint32_t h,
    int v)
{
    return fnv(h, &v, sizeof(v));
}

/** Every field of every primitive the last Build emitted, text by content. */
static uint32_t
prims_digest(
    const struct ToriRSChrome* chrome,
    int* out_count)
{
    int count = 0;
    const struct ToriRSChromePrim* prims = ToriRSChrome_Prims(chrome, &count);
    uint32_t h = 2166136261u;

    for( int i = 0; i < count; i++ )
    {
        const struct ToriRSChromePrim* p = &prims[i];
        h = fnv_int(h, p->kind);
        h = fnv_int(h, p->x);
        h = fnv_int(h, p->y);
        h = fnv_int(h, p->w);
        h = fnv_int(h, p->h);
        h = fnv_int(h, (int)p->color);
        h = fnv_int(h, p->filled);
        h = fnv_int(h, p->line_direction);
        h = fnv_int(h, p->line_width);
        h = fnv_int(h, p->font_slot);
        h = fnv_int(h, p->baseline);
        h = fnv_int(h, p->shadowed);
        if( p->text )
            h = fnv(h, p->text, strlen(p->text));
        h = fnv_int(h, p->sprite_slot);
        h = fnv_int(h, p->sprite_scene_id);
        h = fnv_int(h, p->trans);
        h = fnv_int(h, p->clip.x);
        h = fnv_int(h, p->clip.y);
        h = fnv_int(h, p->clip.w);
        h = fnv_int(h, p->clip.h);
    }
    *out_count = count;
    return h;
}

/** A fixed scene of the widget kinds every panel is made of, on a titled,
 *  closable, poppable, resizable window in `theme`, the pointer over the
 *  Close box so its hover art is in the list too. */
static uint32_t
scene_digest(
    int scale,
    const struct ToriRSChromeTheme* theme,
    int* out_count)
{
    static const char* const k_options[] = { "Linear", "Smooth", "Flat" };
    int panel;
    struct ToriRSChromeRect close;

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetTheme(&g_chrome, theme);
    ToriRSChrome_SetScale(&g_chrome, scale);
    ToriRSChrome_SetSurface(&g_chrome, 800 * scale, 600 * scale);
    panel = ToriRSChrome_PanelAdd(&g_chrome, TORIRS_CHROME_PANEL_WINDOW, 20 * scale, 30 * scale, 300 * scale, "Terrain");
    ToriRSChrome_PanelSetClosable(&g_chrome, panel, 1);
    ToriRSChrome_PanelSetPoppable(&g_chrome, panel, 1);
    ToriRSChrome_PanelSetResizable(&g_chrome, panel, 1);
    (void)ToriRSChrome_Label(&g_chrome, panel, "Brush 3222,3218 level 0");
    (void)ToriRSChrome_Button(&g_chrome, panel, "Apply to selection");
    (void)ToriRSChrome_Checkbox(&g_chrome, panel, "Snap to grid", 1);
    (void)ToriRSChrome_Dropdown(&g_chrome, panel, "Falloff", k_options, 3, 1);
    (void)ToriRSChrome_Slider(&g_chrome, panel, "Radius", 0, 16, 5, 1);
    (void)ToriRSChrome_TextInput(&g_chrome, panel, "Search", "oak");
    ToriRSChrome_Build(&g_chrome);
    close = ToriRSChrome_PanelCloseRect(&g_chrome, panel);
    ToriRSChrome_MouseMove(&g_chrome, close.x + close.w / 2, close.y + close.h / 2);
    ToriRSChrome_Build(&g_chrome);
    return prims_digest(&g_chrome, out_count);
}

static uint32_t
registry_digest(
    int scale,
    int* out_count)
{
    boot_registry(&g_chrome, &g_ui, NULL, 1530, 1006, scale);
    return prims_digest(&g_chrome, out_count);
}

/* Recorded at v3-workbench 64310269, before step 6.0b changed the draw path:
 * scene in the default theme, scene in the osrs theme, the registry; at
 * scales 1, 2, 3. The 1x registry was recorded again at workbench 6.4 (the
 * picker's panes): the Squares panel, docked in that layout, gained its
 * open-set strip and the detail pane's heading, so the dock column's share
 * moved (was 1127817167u; the scenes, and 2x and 3x where Squares is not
 * docked, are untouched). All three registries were recorded again at the
 * 6.2-6.4 integration (was 1751176799u, 2175812620u, 3489523192u): the
 * Catalog's finder rows (6.2: chip strips, Filter and Sort, the cells list)
 * move 1x and 2x on its own, the Locs inspector and the Catalog's Stamps...
 * row (6.3 part 2) move all three (3x on its own is exactly 3420398390u at
 * that slice's tip), and the Squares panes (6.4) move 1x. The scenes -- the
 * chrome's own draw path at each scale -- are untouched. Workbench 6.8
 * moved all three registries once more (was 2131301703u, 3539872907u,
 * 3420398390u): the menu bar gained its workspace chips ("Workspace", Build,
 * Terrain, Script, Playtest after the Level row); with them alone added the
 * rest of 6.8 (the Hotkeys panel's list, the hidden palette) drew the old
 * digests. The scenes are untouched. The 6.5-6.8 integration moved all three
 * again (was 1167870673u, 444441760u, 129691160u), with the slider row's
 * caption column alone (the merged tree drew the old digests; with that one
 * file changed it draws these): the Terrain panel's "Strength  Shift+[ ]"
 * was longer than the fixed column at every scale and ran on under its
 * track; the column now widens to hold it (dbg_slider_label_w). Workbench
 * 6.6 part 1 moved the 1x registry (was 2691440990u): the bottom strip's
 * group gained its third tab, Problems, which the 1x surface draws; the 2x
 * and 3x registries and the scenes did not move. The Windows run (2026-11-06)
 * moved all three registries (was 2593871087u, 4031613255u, 2905863413u):
 * since 6.3 part 2 the Stamps panel opened with the editor and the digests
 * drew it; it now opens on demand (edpanel_stamps.c, stamps_set_visible).
 * The same digests on Linux and Windows; the scenes did not move. The same
 * run moved the 1x registry again (was 584349543u) with two layout fixes:
 * the default column at 1280x800 hid the square picker (its least height
 * counted the detail pane that folds, and the well's least left it 31 px
 * short; edpanel_squares.c, squares_stretch), and Problems opened floating
 * with every editor show (it opens on demand now, problems_set_visible).
 * The path painter moved all three registries again (was 2335556997u,
 * 2229059899u, 2715795737u): the tool rail's Paint path button, four
 * primitives at each scale; Linux and Windows agree. */
static const uint32_t k_golden[3][3] = {
    { 1970238540u, 3840548130u, 1199327579u },
    { 1606657634u, 3552625563u, 1252539268u },
    { 1621493878u, 3838641767u, 2734818940u },
};

static void
test_golden(void)
{
    int const update = getenv("UPDATE_GOLDEN") && getenv("UPDATE_GOLDEN")[0] == '1';

    for( int scale = 1; scale <= 3; scale++ )
    {
        int counts[3];
        uint32_t got[3];
        got[0] = scene_digest(scale, &torirs_chrome_theme_default, &counts[0]);
        got[1] = scene_digest(scale, &torirs_chrome_theme_osrs, &counts[1]);
        got[2] = registry_digest(scale, &counts[2]);
        if( update )
        {
            printf("golden scale %d: { %uu, %uu, %uu }, (prims %d %d %d)\n", scale, got[0], got[1], got[2],
                counts[0], counts[1], counts[2]);
            continue;
        }
        for( int k = 0; k < 3; k++ )
        {
            static const char* const k_what[3] = { "scene (default theme)", "scene (osrs theme)", "registry" };
            CHECK(counts[k] > 0, "scale %d %s draws something", scale, k_what[k]);
            CHECK(got[k] == k_golden[scale - 1][k], "scale %d %s draws exactly what it drew before 6.0b (%u, want %u, %d prims)",
                scale, k_what[k], got[k], k_golden[scale - 1][k], counts[k]);
        }
    }
}

/* ---- the scale snap table --------------------------------------------------- */

static void
test_scale_snap(void)
{
    static const int k_snap[][2] = {
        { 50, 4 },   { 100, 4 },  { 110, 4 },  { 112, 4 },  { 113, 5 },  { 125, 5 },  { 137, 5 },  { 138, 6 },
        { 150, 6 },  { 160, 6 },  { 175, 7 },  { 187, 7 },  { 188, 8 },  { 200, 8 },  { 224, 8 },  { 225, 10 },
        { 240, 10 }, { 250, 10 }, { 274, 10 }, { 275, 12 }, { 300, 12 }, { 400, 12 },
    };
    static const struct
    {
        const char* text;
        int q;
    } k_parse[] = {
        { "1", 4 },    { "2", 8 },     { "3", 12 },  { "4", 12 }, { "1.5", 6 }, { "1.25", 5 }, { "1.75", 7 },
        { "2.5", 10 }, { "1.125", 5 }, { "0.5", 4 }, { "0", 0 },  { "", 0 },    { "abc", 0 },  { "1.5x", 0 },
        { "-1", 0 },   { "2.", 8 },
    };

    for( size_t i = 0; i < sizeof(k_snap) / sizeof(k_snap[0]); i++ )
        CHECK(ToriRSChromeScaleQ_Snap(k_snap[i][0]) == k_snap[i][1], "a %d %% display snaps to %d quarters (got %d)",
            k_snap[i][0], k_snap[i][1], ToriRSChromeScaleQ_Snap(k_snap[i][0]));
    /* The design's steps, in the View menu's order. */
    {
        static const int k_steps[TORIRS_CHROME_SCALEQ_STEP_COUNT] = { 4, 5, 6, 7, 8, 10, 12 };
        for( int i = 0; i < TORIRS_CHROME_SCALEQ_STEP_COUNT; i++ )
        {
            CHECK(ToriRSChromeScaleQ_Step(i) == k_steps[i], "step %d is %d quarters", i, k_steps[i]);
            CHECK(ToriRSChromeScaleQ_StepIndex(k_steps[i]) == i, "%d quarters is step %d", k_steps[i], i);
            CHECK(ToriRSChromeScaleQ_Snap(ToriRSChromeScaleQ_Percent(k_steps[i])) == k_steps[i],
                "every step is its own snap");
        }
        CHECK(ToriRSChromeScaleQ_StepIndex(9) == -1, "2.25x is no step");
    }
    /* Auto on a touch device never goes under 2x. */
    CHECK(ToriRSChromeScaleQ_FromDisplay(100, 1) == 8, "a 1x touch device gets 2x");
    CHECK(ToriRSChromeScaleQ_FromDisplay(150, 1) == 8, "a 1.5x touch device gets 2x");
    CHECK(ToriRSChromeScaleQ_FromDisplay(250, 1) == 10, "a 2.5x touch device keeps 2.5x");
    CHECK(ToriRSChromeScaleQ_FromDisplay(150, 0) == 6, "a 1.5x laptop gets 1.5x");
    CHECK(ToriRSChromeScaleQ_FromDisplay(300, 0) == 12, "a 3x phone without touch reported gets 3x");
    /* TORIRS_CHROME_SCALE and [ui:boot] chrome_scale: integer or decimal. */
    for( size_t i = 0; i < sizeof(k_parse) / sizeof(k_parse[0]); i++ )
        CHECK(ToriRSChromeScaleQ_Parse(k_parse[i].text) == k_parse[i].q, "the pin \"%s\" reads %d quarters (got %d)",
            k_parse[i].text, k_parse[i].q, ToriRSChromeScaleQ_Parse(k_parse[i].text));
    /* The whole scale the native draw path draws each step at while its
     * bridge has only the integer tiers (main.c main_chrome_scale_apply). */
    CHECK(ToriRSChromeScaleQ_Whole(4) == 4 && ToriRSChromeScaleQ_Whole(5) == 4 && ToriRSChromeScaleQ_Whole(6) == 8
              && ToriRSChromeScaleQ_Whole(7) == 8 && ToriRSChromeScaleQ_Whole(8) == 8
              && ToriRSChromeScaleQ_Whole(10) == 12 && ToriRSChromeScaleQ_Whole(12) == 12,
        "the native chrome draws 1.25 at 1x, 1.5 and 1.75 at 2x, 2.5 at 3x");
    /* The shim, both ways round on one chrome. */
    {
        ToriRSChrome_Init(&g_chrome);
        CHECK(Editor_ChromeSetScaleQ(&g_chrome, 8) == 1 && ToriRSChrome_Scale(&g_chrome) == 2, "2x through the shim");
        CHECK(Editor_ChromeSetScaleQ(&g_chrome, 8) == 0, "the same scale again moves nothing");
        CHECK(Editor_ChromeSetScaleQ(&g_chrome, 6) == 1 && Editor_ChromeScaleQ(&g_chrome) == 6,
            "1.5x is 1.5x on a chrome that takes quarters");
        for( int q = TORIRS_CHROME_SCALEQ_MIN; q <= TORIRS_CHROME_SCALEQ_MAX; q++ )
        {
            /* The integer setter beside the quarter one never undoes it. */
            ToriRSChrome_SetScale(&g_chrome, Editor_ChromeScaleInteger(q));
            (void)Editor_ChromeSetScaleQ(&g_chrome, q);
            CHECK(ToriRSChrome_Scale(&g_chrome) == Editor_ChromeScaleInteger(q),
                "q %d: the integer scale handed to App_SetChromeScale is the one the chrome then answers", q);
        }
    }
}

/* ---- View > Chrome scale and View > Text size ------------------------------------ */

static int
view_menu(void)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
        if( g_chrome.widgets[i].kind == TORIRS_CHROME_W_DROPDOWN && g_chrome.widgets[i].menu_mode
            && strcmp(g_chrome.widgets[i].text, "View") == 0 )
            return i;
    return -1;
}

/** The View row whose label starts with `head`, or -1. */
static int
view_row(const char* head)
{
    int const view = view_menu();
    if( view < 0 )
        return -1;
    for( int i = 0; i < g_chrome.widgets[view].option_count; i++ )
        if( strncmp(g_chrome.widgets[view].options[i], head, strlen(head)) == 0 )
            return i;
    return -1;
}

static const char*
view_label(int row)
{
    int const view = view_menu();
    return view >= 0 && row >= 0 ? g_chrome.widgets[view].options[row] : "";
}

/** A row's right-hand column says "on". */
static int
row_on(int row)
{
    const char* tab = strchr(view_label(row), '\t');
    return tab && strncmp(tab + 1, "on", 2) == 0;
}

static void
choose(int row)
{
    int const view = view_menu();
    assert(view >= 0);
    ToriRSChrome_DropdownSetSelected(&g_chrome, view, row);
    g_chrome.activated = view;
    frame_of(&g_chrome, &g_ui);
}

/** What the host does every frame (main.c main_chrome_scale_q / _apply), the
 *  display being `auto_q`. */
static void
host_frame(int auto_q)
{
    (void)Editor_ChromeSetScaleQ(&g_chrome, Editor_UiChromeScaleFor(&g_ui, auto_q, 0));
    frame_of(&g_chrome, &g_ui);
}

/** The commonest height of a plain label row in a shown window: one row's
 *  (a plain label is DBG_ROW_H per wrapped line; the odd one cut at a
 *  panel's foot or wrapped to two is outvoted). */
static int
some_row_height(void)
{
    int heights[64];
    int votes[64];
    int kinds = 0;
    int best = 0;
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        struct ToriRSChromeWidget const* w = &g_chrome.widgets[i];
        int k;
        if( w->kind != TORIRS_CHROME_W_LABEL || w->label_style != TORIRS_CHROME_LABEL_PLAIN || w->hidden
            || w->panel < 0 || !g_chrome.panels[w->panel].visible
            || g_chrome.panels[w->panel].style != TORIRS_CHROME_PANEL_WINDOW || w->h <= 0 )
            continue;
        for( k = 0; k < kinds && heights[k] != w->h; k++ )
            ;
        if( k == kinds )
        {
            if( kinds == 64 )
                continue;
            heights[kinds] = w->h;
            votes[kinds++] = 0;
        }
        votes[k]++;
    }
    for( int k = 0; k < kinds; k++ )
        if( best == 0 || votes[k] > votes[best - 1] )
            best = k + 1;
    return best ? heights[best - 1] : 0;
}

static void
test_view_rows(void)
{
    static const char* const k_scale_rows[] = { "Chrome scale: 100 %", "Chrome scale: 125 %", "Chrome scale: 150 %",
        "Chrome scale: 175 %", "Chrome scale: 200 %", "Chrome scale: 250 %", "Chrome scale: 300 %" };
    int auto_row;
    int large_row;
    int normal_row;
    int row_h;
    uint32_t serial;

    boot_registry(&g_chrome, &g_ui, NULL, 1530, 1006, 1);
    CHECK(view_menu() >= 0, "the menu bar has a View menu");
    auto_row = view_row("Chrome scale: Auto (100 %)");
    CHECK(auto_row >= 0, "View > Chrome scale: Auto names what the display gives (%s)", view_label(auto_row));
    CHECK(row_on(auto_row), "Auto is the default and says so");
    for( int i = 0; i < 7; i++ )
    {
        int const row = view_row(k_scale_rows[i]);
        CHECK(row == auto_row + 1 + i, "%s is the row after the one before it (%d)", k_scale_rows[i], row);
        CHECK(!row_on(row), "%s is not chosen", k_scale_rows[i]);
    }
    normal_row = view_row("Text size: Normal");
    large_row = view_row("Text size: Large");
    CHECK(normal_row == auto_row + 8 && large_row == auto_row + 9, "Text size: Normal / Large follow");
    CHECK(row_on(normal_row) && !row_on(large_row), "Normal is the default");
    {
        int const view = view_menu();
        int const rows = g_chrome.widgets[view].option_count;
        CHECK(strncmp(view_label(rows - 1), "Snap to grid", 12) == 0 && strncmp(view_label(rows - 2), "Reset layout", 12) == 0,
            "Reset layout and Snap to grid stay the last two rows");
        CHECK(strncmp(view_label(auto_row - 1), "Hide / show panels", 18) != 0, "the view's rows sit before the layout's");
    }
    /* Every one of them a hotkey action, unbound by default. */
    for( int a = EDITOR_HK_VIEW_SCALE_AUTO; a <= EDITOR_HK_VIEW_TEXT_LARGE; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        CHECK(info->defaults[0] == NULL && info->defaults[1] == NULL, "%s is unbound by default", info->id);
        CHECK(strcmp(info->group, "View") == 0 && info->panel == NULL, "%s is a View action that toggles no panel",
            info->id);
    }

    /* 200 %: the choice is the device's, the host applies it, the layout
     * re-runs at it. */
    row_h = some_row_height();
    CHECK(row_h == 18, "a row is 18 px at 1x (%d)", row_h);
    serial = g_ui.prefs_serial;
    choose(view_row("Chrome scale: 200 %"));
    CHECK(g_ui.prefs.chrome_scale_q == 8, "View > Chrome scale: 200 %% chooses 8 quarters (%d)", g_ui.prefs.chrome_scale_q);
    CHECK(g_ui.prefs_serial != serial, "a choice moves the serial the host saves on");
    CHECK(row_on(view_row("Chrome scale: 200 %")) && !row_on(view_row("Chrome scale: Auto")), "the row says on");
    CHECK(Editor_UiChromeScaleFor(&g_ui, 4, 0) == 8, "the host is asked for 200 %% whatever Auto says");
    for( int i = 0; i < 3; i++ )
        host_frame(4);
    CHECK(ToriRSChrome_Scale(&g_chrome) == 2 && g_ui.layout_scale == 8 && g_ui.laid_out,
        "the layout re-ran at 2x (scale %d, layout_scale %d)", ToriRSChrome_Scale(&g_chrome), g_ui.layout_scale);
    CHECK(some_row_height() == 36, "a row is 36 px at 2x (%d)", some_row_height());
    /* Through the hotkey action too, and Auto back. */
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_SCALE_150) == 1 && g_ui.prefs.chrome_scale_q == 6,
        "view.scale_150 chooses 1.5x");
    /* A touch device never goes under 2x, whatever the row chosen: the
     * choice is kept (the prefs file is the device's), the host is asked for
     * 2x, and the rows under it say so. */
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_SCALE_100) == 1 && g_ui.prefs.chrome_scale_q == 4,
        "view.scale_100 chooses 1x");
    CHECK(Editor_UiChromeScaleFor(&g_ui, 8, 1) == 8 && g_ui.prefs.chrome_scale_q == 4,
        "on a touch device 100 %% is held at 200 %%, the choice kept");
    CHECK(Editor_UiChromeScaleFor(&g_ui, 8, 1) == 8 && g_ui.scale_touch == 1, "the window knows it is touch");
    frame_of(&g_chrome, &g_ui);
    CHECK(view_row("Chrome scale: 125 % (touch: 200 %)") >= 0, "the 125 %% row says what it draws on touch (%s)",
        view_label(view_row("Chrome scale: 125 %")));
    CHECK(view_row("Chrome scale: 250 %") >= 0 && view_row("Chrome scale: 250 % (touch") < 0,
        "a row at or over 2x is unmarked");
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_SCALE_250) == 1 && Editor_UiChromeScaleFor(&g_ui, 8, 1) == 10,
        "250 %% on touch is 250 %%");
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_SCALE_100) == 1 && Editor_UiChromeScaleFor(&g_ui, 4, 0) == 4,
        "and 100 %% off touch is 100 %%");
    frame_of(&g_chrome, &g_ui);
    CHECK(view_row("Chrome scale: 125 % (touch") < 0, "off touch the rows are unmarked");
    choose(view_row("Chrome scale: Auto"));
    CHECK(g_ui.prefs.chrome_scale_q == EDITOR_PREFS_SCALE_AUTO, "Auto again");
    for( int i = 0; i < 3; i++ )
        host_frame(4);
    CHECK(ToriRSChrome_Scale(&g_chrome) == 1 && g_ui.layout_scale == 4 && g_ui.laid_out, "back at 1x and laid out");
    host_frame(6);
    CHECK(view_row("Chrome scale: Auto (150 %)") >= 0, "the Auto row names the display's 150 %% (%s)",
        view_label(view_row("Chrome scale: Auto")));
    for( int i = 0; i < 3; i++ )
        host_frame(4);

    /* Text size Large: the Menu face as the body face, rows of 22. */
    serial = g_ui.prefs_serial;
    choose(view_row("Text size: Large"));
    CHECK(g_ui.prefs.text_large == 1 && g_ui.prefs_serial != serial, "View > Text size: Large chooses it");
    CHECK(g_chrome.theme.font_row == TORIRS_CHROME_FONT_MENU && g_chrome.theme.row_h == EDITOR_UI_LARGE_ROW_H,
        "the theme rows in the Menu face, %d px", g_chrome.theme.row_h);
    for( int i = 0; i < 3; i++ )
        frame_of(&g_chrome, &g_ui);
    CHECK(g_ui.laid_out && some_row_height() == 22, "the layout re-ran with rows of 22 (%d)", some_row_height());
    CHECK(row_on(view_row("Text size: Large")) && !row_on(view_row("Text size: Normal")), "Large says on");
    /* A theme installed under it later wears it too. */
    ToriRSChrome_SetTheme(&g_chrome, &torirs_chrome_theme_default);
    frame_of(&g_chrome, &g_ui);
    CHECK(g_chrome.theme.font_row == TORIRS_CHROME_FONT_MENU && g_chrome.theme.row_h == EDITOR_UI_LARGE_ROW_H,
        "a theme installed later is worn at the window's text size");
    choose(view_row("Text size: Normal"));
    for( int i = 0; i < 3; i++ )
        frame_of(&g_chrome, &g_ui);
    CHECK(g_chrome.theme.font_row == torirs_chrome_theme_default.font_row && g_chrome.theme.row_h == 0,
        "Normal gives the theme its own face back");
    CHECK(some_row_height() == 18, "rows of 18 again (%d)", some_row_height());

    /* Text rendering: bitmap at whole scales (the default), or the vector
     * face at every scale. Device-local like the scale; nothing reflows. */
    CHECK(row_on(view_row("Text rendering: bitmap at whole scales")) &&
              !row_on(view_row("Text rendering: smooth always")),
        "bitmap at whole scales is the default and says on");
    /* The native canvas draws it too now (the scene bridge registers the
     * vector face at a whole q), so the row carries no "web pages only". */
    CHECK(strncmp(view_label(view_row("Text rendering: smooth always")),
              "Text rendering: smooth always", 29) == 0 &&
              strstr(view_label(view_row("Text rendering: smooth always")), "web pages") == NULL,
        "the smooth row names no web-pages-only caveat (%s)",
        view_label(view_row("Text rendering: smooth always")));
    serial = g_ui.prefs_serial;
    choose(view_row("Text rendering: smooth always"));
    CHECK(g_ui.prefs.text_smooth == 1 && Editor_UiTextSmooth(&g_ui) == 1 && g_ui.prefs_serial != serial,
        "View > Text rendering: smooth always chooses it");
    for( int i = 0; i < 3; i++ )
        frame_of(&g_chrome, &g_ui);
    CHECK(row_on(view_row("Text rendering: smooth always")) &&
              !row_on(view_row("Text rendering: bitmap at whole scales")),
        "smooth always says on");
    CHECK(some_row_height() == 18, "and nothing reflowed (%d)", some_row_height());
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_TEXT_BITMAP) == 1 && g_ui.prefs.text_smooth == 0,
        "the hotkey action takes it back to bitmap at whole scales");
    frame_of(&g_chrome, &g_ui);
    {
        int fresh = 0;
        CHECK(registry_digest(1, &fresh) == k_golden[0][2], "a fresh 1x boot after all that is still the golden");
    }
}

/* ---- editor_prefs.ini -------------------------------------------------------------- */

static void
test_prefs_file(void)
{
    struct Editor_Prefs prefs;
    struct Editor_Prefs back;

    Editor_PrefsInit(&prefs);
    CHECK(prefs.chrome_scale_q == EDITOR_PREFS_SCALE_AUTO && prefs.text_large == 0, "the defaults are Auto, Normal");
    for( int s = -1; s < TORIRS_CHROME_SCALEQ_STEP_COUNT; s++ )
        for( int large = 0; large <= 1; large++ )
        {
            char* text = NULL;
            int size = 0;
            prefs.chrome_scale_q = s < 0 ? EDITOR_PREFS_SCALE_AUTO : ToriRSChromeScaleQ_Step(s);
            prefs.text_large = large;
            Editor_PrefsEncode(&prefs, &text, &size);
            CHECK(text && size == (int)strlen(text), "the file encodes");
            Editor_PrefsInit(&back);
            back.text_large = !large;
            CHECK(Editor_PrefsDecode(&back, text, size) == 0, "and decodes with nothing skipped");
            CHECK(back.chrome_scale_q == prefs.chrome_scale_q && back.text_large == large,
                "round trip: scale %d text %d (got %d %d)", prefs.chrome_scale_q, large, back.chrome_scale_q,
                back.text_large);
            ToriMem_Free(ToriMem_Process(), text);
        }
    {
        static const char k_hand[] = "; edited by hand\n"
                                     "[other]\n"
                                     "chrome_scale = 300\n"
                                     "[view]\n"
                                     "  chrome_scale = 125   ; a laptop\r\n"
                                     "text_size = huge\n"
                                     "no equals sign\n"
                                     "future_key = 7\n";
        Editor_PrefsInit(&back);
        CHECK(Editor_PrefsDecode(&back, k_hand, (int)strlen(k_hand)) == 2, "two malformed lines are skipped");
        CHECK(back.chrome_scale_q == 5, "125 is read from [view] only, comment and CR cut (%d)", back.chrome_scale_q);
        CHECK(back.text_large == 0, "a bad text size keeps the default");
    }
    {
        static const char k_smooth[] = "[view]\ntext_rendering = smooth\n";
        static const char k_bad[] = "[view]\ntext_rendering = blurry\n";
        char* text = NULL;
        int size = 0;
        Editor_PrefsInit(&back);
        CHECK(back.text_smooth == 0, "text rendering defaults to bitmap at whole scales");
        CHECK(Editor_PrefsDecode(&back, k_smooth, (int)strlen(k_smooth)) == 0 && back.text_smooth == 1,
            "text_rendering = smooth reads");
        Editor_PrefsEncode(&back, &text, &size);
        CHECK(text && strstr(text, "text_rendering = smooth\n") != NULL, "and is written back");
        Editor_PrefsInit(&back);
        CHECK(Editor_PrefsDecode(&back, text, size) == 0 && back.text_smooth == 1, "round trip: smooth");
        ToriMem_Free(ToriMem_Process(), text);
        Editor_PrefsInit(&back);
        CHECK(Editor_PrefsDecode(&back, k_bad, (int)strlen(k_bad)) == 1 && back.text_smooth == 0,
            "a bad text rendering is skipped and keeps the default");
    }
    {
        static const char k_odd[] = "[view]\nchrome_scale = 133\n";
        Editor_PrefsInit(&back);
        CHECK(Editor_PrefsDecode(&back, k_odd, (int)strlen(k_odd)) == 1 && back.chrome_scale_q == EDITOR_PREFS_SCALE_AUTO,
            "133 is not a step: skipped, Auto kept");
    }
    CHECK(Editor_PrefsDecode(&back, NULL, 0) == 0, "an empty file is the defaults");
    /* The path: beside the layout file, TORIRS_EDITOR_PREFS overriding it. */
    unsetenv("TORIRS_EDITOR_PREFS");
    CHECK(strcmp(Editor_PrefsPath(), EDITOR_PREFS_DEFAULT_PATH) == 0, "editor_prefs.ini by default");
#ifndef _WIN32
    /* Windows' C runtime holds no empty variable: setting one to "" removes
     * it (_putenv("NAME=")), so there the convention cannot be spelled and
     * the variable reads as unset. */
    setenv("TORIRS_EDITOR_PREFS", "", 1);
    CHECK(Editor_PrefsPath() == NULL, "an empty TORIRS_EDITOR_PREFS turns the file off");
#endif
    setenv("TORIRS_EDITOR_PREFS", "/tmp/x.ini", 1);
    CHECK(strcmp(Editor_PrefsPath(), "/tmp/x.ini") == 0, "TORIRS_EDITOR_PREFS names it");
    unsetenv("TORIRS_EDITOR_PREFS");

    /* Applied from the file: no serial (nothing to write back). */
    boot_registry(&g_chrome, &g_ui, NULL, 1530, 1006, 1);
    prefs.chrome_scale_q = 10;
    prefs.text_large = 1;
    Editor_UiApplyPrefs(&g_ui, &prefs);
    CHECK(g_ui.prefs_serial == 0, "the file's values move no serial");
    CHECK(Editor_UiChromeScaleFor(&g_ui, 4, 0) == 10 && g_chrome.theme.row_h == EDITOR_UI_LARGE_ROW_H,
        "and are what the window asks for");
}

/* ---- the Dock button, across two sessions ------------------------------------------ */

static struct ToriRSChrome g_chrome_b;
static struct EditorUi g_ui_b;
static struct Editor g_ed_a;
static struct Editor g_ed_b;

/* The last DOCK_PANEL either session heard, as the relay carried it. */
static int32_t g_dock_heard[TORIRSMAPED_STATE_VALUES_MAX];
static int g_dock_heard_count;

static void
on_state_ui(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    if( key == TORIRSMAPED_STATE_DOCK_PANEL && count <= TORIRSMAPED_STATE_VALUES_MAX )
    {
        memcpy(g_dock_heard, values, sizeof(values[0]) * (size_t)count);
        g_dock_heard_count = count;
    }
    Editor_UiApplyState(user_data, key, values, count);
}

/** The first VLIST on `chrome`'s panel `panel`, or -1. */
static int
panel_list(
    const struct ToriRSChrome* chrome,
    int panel)
{
    for( int i = 0; i < chrome->widget_count; i++ )
        if( chrome->widgets[i].panel == panel && chrome->widgets[i].kind == TORIRS_CHROME_W_VLIST )
            return i;
    return -1;
}

static int
console_has(
    const struct EditorUi* ui,
    const char* needle)
{
    for( int i = 0; i < 24; i++ )
    {
        const char* line = Editor_UiConsoleLine(ui, i);
        if( line && strstr(line, needle) )
            return 1;
    }
    return 0;
}

static void
press_at(
    struct ToriRSChrome* chrome,
    int x,
    int y)
{
    ToriRSChrome_MouseMove(chrome, x, y);
    (void)ToriRSChrome_MouseDown(chrome, x, y);
    (void)ToriRSChrome_MouseUp(chrome, x, y);
}

static void
test_dock(
    const char* scratch,
    const struct RSCache* profile)
{
    struct EditorHost host_a;
    struct EditorHost host_b;
    struct EditorUiPanel* catalog_a;
    struct EditorUiPanel* catalog_b;
    struct ToriRSChromeRect dock;
    int catalog_index;

    CHECK(Editor_HostOpenMapEdEmbed(&host_a, scratch, NULL), "the embedded server starts");
    CHECK(Editor_HostOpenMapEdEmbedPeer(&host_b, &host_a, TORIRSMAPED_ROLE_CONTROLLER, 1),
        "a second session joins the first one's Client");
    Editor_OpenHost(&g_ed_a, &host_a, "leader", profile);
    Editor_OpenHost(&g_ed_b, &host_b, "popped", profile);

    /* The leader: the panel set. The popped-out window: Catalog alone. */
    boot_registry(&g_chrome, &g_ui, &g_ed_a, 1530, 1006, 1);
    boot_registry(&g_chrome_b, &g_ui_b, &g_ed_b, 640, 480, 1);
    Editor_SetStateCallback(&g_ed_a, on_state_ui, &g_ui);
    Editor_SetStateCallback(&g_ed_b, on_state_ui, &g_ui_b);
    CHECK(Editor_UiSetSolo(&g_ui_b, "catalog"), "the second window shows Catalog alone");
    for( int i = 0; i < 3; i++ )
        frame_of(&g_chrome_b, &g_ui_b);

    catalog_a = Editor_UiFindPanel(&g_ui, "catalog");
    catalog_b = Editor_UiFindPanel(&g_ui_b, "catalog");
    CHECK(catalog_a && catalog_b && catalog_a->chrome_panel >= 0 && catalog_b->chrome_panel >= 0,
        "both windows register Catalog");
    if( !catalog_a || !catalog_b )
        return;
    catalog_index = (int)(catalog_a - g_ui.panels);
    CHECK(catalog_index == (int)(catalog_b - g_ui_b.panels), "at the same registry index in both (%d)", catalog_index);

    /* The popped-out window wears Dock on its title bar, at the right end
     * where the main window's Close and Pop out sit; the main window wears
     * none. */
    dock = ToriRSChrome_PanelDockRect(&g_chrome_b, catalog_b->chrome_panel);
    {
        struct ToriRSChromeRect const r = g_chrome_b.panels[catalog_b->chrome_panel].last_rect;
        CHECK(dock.w > 0 && dock.h > 0, "the popped-out Catalog has a Dock button (%dx%d)", dock.w, dock.h);
        CHECK(dock.x + dock.w <= r.x + r.w && dock.x > r.x + r.w / 2 && dock.y >= r.y && dock.y + dock.h <= r.y + 30,
            "at the right end of its title bar (%d,%d in %d,%d %dx%d)", dock.x, dock.y, r.x, r.y, r.w, r.h);
        CHECK(ToriRSChrome_PanelCloseRect(&g_chrome_b, catalog_b->chrome_panel).w == 0
                  && ToriRSChrome_PanelPopOutRect(&g_chrome_b, catalog_b->chrome_panel).w == 0,
            "and no Close or Pop out (the OS window closes it)");
    }
    CHECK(ToriRSChrome_PanelDockRect(&g_chrome, catalog_a->chrome_panel).w == 0, "the main window's Catalog has none");
    /* Its art: the baked DockButton slot. */
    {
        int count = 0;
        int dock_sprites = 0;
        const struct ToriRSChromePrim* prims = ToriRSChrome_Prims(&g_chrome_b, &count);
        for( int i = 0; i < count; i++ )
            if( prims[i].kind == TORIRS_CHROME_PRIM_SPRITE
                && (prims[i].sprite_slot == TORIRS_CHROME_SKIN_DOCK || prims[i].sprite_slot == TORIRS_CHROME_SKIN_DOCK_OVER) )
                dock_sprites++;
        CHECK(dock_sprites == 0 || dock_sprites == 1, "at most one Dock drawn (%d)", dock_sprites);
    }

    /* The leader pops Catalog out (its window is the one above), scrolled. */
    ToriRSChrome_PanelSetVisible(&g_chrome, catalog_a->chrome_panel, 1);
    frame_of(&g_chrome, &g_ui);
    g_chrome.panels[catalog_a->chrome_panel].scroll_y = 0;
    CHECK(Editor_UiPanelPopOut(&g_ui, "catalog") == 1, "the leader pops Catalog out");
    frame_of(&g_chrome, &g_ui);
    CHECK(!g_chrome.panels[catalog_a->chrome_panel].visible && Editor_UiPanelPoppedOut(&g_ui, "catalog"),
        "and hides it here");

    /* Dock, pressed. */
    press_at(&g_chrome_b, dock.x + dock.w / 2, dock.y + dock.h / 2);
    frame_of(&g_chrome_b, &g_ui_b);
    CHECK(g_ui_b.dock_exit == 1, "the popped-out window is told to close");
    CHECK(console_has(&g_ui_b, "docks back into the main window"), "and says so");
    /* The relay: the leader hears it, as any Client fact. */
    for( int i = 0; i < 4; i++ )
    {
        Editor_PumpFacts(&g_ed_a);
        Editor_PumpFacts(&g_ed_b);
    }
    frame_of(&g_chrome, &g_ui);
    CHECK(g_chrome.panels[catalog_a->chrome_panel].visible, "the leader shows Catalog again");
    CHECK(!Editor_UiPanelPoppedOut(&g_ui, "catalog"), "it is no longer popped out there");
    CHECK(console_has(&g_ui, "Catalog docked back from its own window"), "the leader's console says so");
    CHECK(Editor_UiPanelInStrip(&g_ui, "catalog") || g_chrome.panels[catalog_a->chrome_panel].visible,
        "in its group's strip, as View > Catalog puts it");
    CHECK(Editor_UiSetSolo(&g_ui_b, "catalog") && g_ui_b.solo_panel >= 0, "the popped window stayed solo");
    /* Where the reader was rides the relay: the panel's scroll and its
     * list's first row, as the popped window had them. */
    {
        int const list_b = panel_list(&g_chrome_b, catalog_b->chrome_panel);
        CHECK(g_dock_heard_count == 4, "the dock carries four values (%d)", g_dock_heard_count);
        CHECK(g_dock_heard_count == 4 && g_dock_heard[0] == catalog_index
                  && g_dock_heard[2] == ToriRSChrome_PanelScroll(&g_chrome_b, catalog_b->chrome_panel)
                  && g_dock_heard[3] == (list_b >= 0 ? g_chrome_b.widgets[list_b].imin : -1),
            "the panel, then the popped window's scroll and first row (%d, %d)", g_dock_heard[2], g_dock_heard[3]);
    }
    /* And the leader takes them: a Catalog popped out scrolled 40 px down
     * its panel and 37 rows down its list docks back there. (The rows this
     * content-free Catalog lists are none; the list is given some.) */
    {
        int const list_a = panel_list(&g_chrome, catalog_a->chrome_panel);
        int32_t const values[4] = { catalog_index, (int32_t)g_ui_b.dock_serial + 100, 40, 37 };
        CHECK(list_a >= 0, "the leader's Catalog has its list");
        if( list_a >= 0 )
        {
            ToriRSChrome_PanelSetVisible(&g_chrome, catalog_a->chrome_panel, 0);
            ToriRSChrome_VListSetCount(&g_chrome, list_a, 300);
            ToriRSChrome_VListSetScroll(&g_chrome, list_a, 0);
            ToriRSChrome_PanelSetScroll(&g_chrome, catalog_a->chrome_panel, 0);
            Editor_UiApplyState(&g_ui, TORIRSMAPED_STATE_DOCK_PANEL, values, 4);
            CHECK(g_chrome.panels[catalog_a->chrome_panel].visible, "the scrolled dock shows it");
            CHECK(ToriRSChrome_PanelScroll(&g_chrome, catalog_a->chrome_panel) == 40,
                "the docked Catalog keeps the panel's scroll (%d)",
                ToriRSChrome_PanelScroll(&g_chrome, catalog_a->chrome_panel));
            CHECK(g_chrome.widgets[list_a].imin == 37, "and its list's first row (%d)", g_chrome.widgets[list_a].imin);
            /* A two-value dock (an older window) docks with no place. */
            ToriRSChrome_PanelSetVisible(&g_chrome, catalog_a->chrome_panel, 0);
            ToriRSChrome_VListSetScroll(&g_chrome, list_a, 5);
            Editor_UiApplyState(&g_ui, TORIRSMAPED_STATE_DOCK_PANEL, values, 2);
            CHECK(g_chrome.panels[catalog_a->chrome_panel].visible && g_chrome.widgets[list_a].imin == 5,
                "a dock without a place shows the panel and moves no scroll");
            frame_of(&g_chrome, &g_ui);
        }
    }

    /* The same panel docked twice is two requests (the serial). */
    ToriRSChrome_PanelSetVisible(&g_chrome, catalog_a->chrome_panel, 0);
    frame_of(&g_chrome, &g_ui);
    g_ui_b.dock_exit = 0;
    CHECK(Editor_UiDockBack(&g_ui_b) == 1, "a second Dock goes out");
    for( int i = 0; i < 4; i++ )
    {
        Editor_PumpFacts(&g_ed_a);
        Editor_PumpFacts(&g_ed_b);
    }
    frame_of(&g_chrome, &g_ui);
    CHECK(g_chrome.panels[catalog_a->chrome_panel].visible, "and shows it again");
    /* A panel already open stays open (a View toggle would have closed it). */
    g_ui_b.dock_exit = 0;
    CHECK(Editor_UiDockBack(&g_ui_b) == 1, "a third Dock goes out");
    for( int i = 0; i < 4; i++ )
    {
        Editor_PumpFacts(&g_ed_a);
        Editor_PumpFacts(&g_ed_b);
    }
    frame_of(&g_chrome, &g_ui);
    CHECK(g_chrome.panels[catalog_a->chrome_panel].visible, "an open panel stays open");

    Editor_UiFree(&g_ui_b);
    Editor_UiFree(&g_ui);
    Editor_Close(&g_ed_b);
    Editor_Close(&g_ed_a);

    /* No session: nobody to tell, the window stays. */
    boot_registry(&g_chrome_b, &g_ui_b, NULL, 640, 480, 1);
    CHECK(Editor_UiSetSolo(&g_ui_b, "terrain"), "a session-less window shows Terrain alone");
    frame_of(&g_chrome_b, &g_ui_b);
    CHECK(Editor_UiDockBack(&g_ui_b) == 0 && g_ui_b.dock_exit == 0, "Dock with no session leaves the window open");
    CHECK(console_has(&g_ui_b, "no session to tell the main window through"), "and says why");
    Editor_UiClearSolo(&g_ui_b);
    frame_of(&g_chrome_b, &g_ui_b);
    {
        struct EditorUiPanel* terrain = Editor_UiFindPanel(&g_ui_b, "terrain");
        CHECK(terrain && ToriRSChrome_PanelDockRect(&g_chrome_b, terrain->chrome_panel).w == 0,
            "leaving the solo layout takes the Dock off");
    }
    Editor_UiFree(&g_ui_b);
}

/* ---- panel-only windows -------------------------------------------------------------- */

static void
test_panel_only(void)
{
    static const char* const k_light[] = { "catalog", "squares", "terrain", "locs", "console", "hotkeys" };
    CHECK(Editor_UiModuleNeedsWorld("world"), "a popped-out World holds a world");
    CHECK(Editor_UiModuleNeedsWorld("preview"), "a popped-out Preview holds one (its renderer)");
    for( size_t i = 0; i < sizeof(k_light) / sizeof(k_light[0]); i++ )
        CHECK(!Editor_UiModuleNeedsWorld(k_light[i]), "a popped-out %s loads no world", k_light[i]);
}

int
main(
    int argc,
    char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    /* No layout or prefs another run left behind. */
    setenv("TORIRS_EDITOR_LAYOUT", "", 1);
    unsetenv("TORIRS_EDITOR_PREFS");

    test_golden();
    test_scale_snap();
    test_view_rows();
    test_prefs_file();
    test_panel_only();
    if( argc > 1 )
    {
        static struct RSCache profile;
        char maps[512];
        CHECK(RSCache_ProfileByName("osrs241", &profile), "the osrs241 cache profile is built in");
        snprintf(maps, sizeof(maps), "%s/maps", argv[1]);
        (void)mkdir(maps, 0755);
        test_dock(argv[1], &profile);
    }
    else
        printf("editor_workbench_test: no scratch dir given, the two-session Dock test is skipped\n");
    /* The golden again, after everything above ran in this process. */
    test_golden();

    printf("editor_workbench_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
