/*
 * Chords at the action (workbench 6.0, design section 15 surfaces 2 and 3,
 * and the contract's R2): every registered panel button whose action has a
 * hotkey wears its chord as a keycap and Build draws it; the tool rail's
 * icons wear theirs as a badge; the Terrain sliders' labels end with their
 * steppers' chords; a rebind relabels all of them on the next frame; and a
 * string the chrome cuts reaches the console once, one line per record.
 *
 *   ./editor_chords_test
 *
 * Headless and content-free, like editor_groups_test.c: the real chrome
 * model and every tools-panels module registered with no session and an App
 * stand-in, so it runs where the content tree is not checked out.
 */

#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/live/live_panel.h"
#include "editor/panels/edpanel_common.h"
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

/* The Live panel registers only over an editor host (live_host.c); the
 * test gives it a transport that sends into nothing and receives nothing,
 * so its RUN button is there to wear its chord. */
static int
fake_live_send(
    void* user,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    (void)user;
    (void)type;
    (void)payload;
    (void)length;
    return 1;
}

static int
fake_live_take(
    void* user,
    uint32_t* out_type,
    uint8_t** out_body,
    uint32_t* out_length)
{
    (void)user;
    (void)out_type;
    (void)out_body;
    (void)out_length;
    return 0;
}

static void
fake_live_release(
    void* user,
    uint8_t* body)
{
    (void)user;
    (void)body;
}

static void
frame(void)
{
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
boot(
    int w,
    int h,
    int scale)
{
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetScale(&g_chrome, scale);
    ToriRSChrome_SetSurface(&g_chrome, w, h);
    Editor_UiInit(&g_ui, &g_chrome, NULL);
    g_ui.app_services.user_data = &g_app_stand_in;
    g_ui.app_services.spawn_panel = fake_spawn_panel;
    Editor_UiRegisterToolsPanels(&g_ui);
    {
        struct Editor_LiveTransport transport;
        memset(&transport, 0, sizeof(transport));
        transport.send = fake_live_send;
        transport.take = fake_live_take;
        transport.release = fake_live_release;
        Editor_Live_RegisterWith(&g_ui, &transport);
    }
    Editor_UiSetVisible(&g_ui, 1);
    for( int i = 0; i < 5; i++ )
        frame();
}

static void
shutdown_ui(void)
{
    Editor_UiFree(&g_ui);
}

static int
window_of(const char* module)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(&g_ui, module);
    return panel ? panel->chrome_panel : -1;
}

static int
widget_in(
    const char* module,
    int kind,
    const char* caption)
{
    int const panel = window_of(module);

    if( panel < 0 )
        return -1;
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( strcmp(w->text, caption) == 0 || strcmp(w->label, caption) == 0 )
            return i;
    }
    return -1;
}

/** The keycap spelling of `action`: its first chord, "" unbound. */
static void
first_chord(
    enum Editor_HotkeyAction action,
    char* out,
    int out_size)
{
    int const one = (int)action;
    Editor_HotkeysJoin(&g_ui.hotkeys, &one, 1, out, out_size);
}

/** Build drew `text` (the widget's own buffer) in the Small face. */
static int
drawn_small(const char* text)
{
    int n = 0;
    struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_chrome, &n);

    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text
            && (prims[i].text == text || strcmp(prims[i].text, text) == 0)
            && prims[i].font_slot == TORIRS_CHROME_FONT_SMALL )
            return 1;
    return 0;
}

/** The caption text Build drew inside BUTTON `widget`'s plate (its row
 *  face, not the keycap's Small one), or NULL. */
static const char*
drawn_caption(int widget)
{
    int n = 0;
    struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_chrome, &n);
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[widget];

    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text
            && prims[i].font_slot != TORIRS_CHROME_FONT_SMALL && prims[i].x >= w->x && prims[i].x < w->x + w->w
            && prims[i].y > w->y && prims[i].y <= w->y + w->h )
            return prims[i].text;
    return NULL;
}

/** The Small-face text Build drew inside ICONBUTTON `widget`'s box (its
 *  badge), or NULL. */
static const char*
drawn_badge(int widget)
{
    int n = 0;
    struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_chrome, &n);
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[widget];

    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text
            && prims[i].font_slot == TORIRS_CHROME_FONT_SMALL && prims[i].x >= w->x && prims[i].x < w->x + w->w
            && prims[i].y > w->y && prims[i].y <= w->y + w->h )
            return prims[i].text;
    return NULL;
}

/** Console lines logged since `serial` that start with "chrome:". */
static int
chrome_lines_since(
    int serial,
    const char** newest)
{
    int const added = g_ui.console_serial - serial;
    int count = 0;

    *newest = NULL;
    for( int i = 0; i < added; i++ )
    {
        const char* line = Editor_UiConsoleLine(&g_ui, i);
        if( line && strncmp(line, "chrome:", 7) == 0 )
        {
            if( !*newest )
                *newest = line;
            count++;
        }
    }
    return count;
}

/* Every panel button whose action has a hotkey: section 15 surface 3. */
static const struct
{
    const char* module;
    const char* caption;
    enum Editor_HotkeyAction action;
} k_buttons[] = {
    { "locs", "Nudge north", EDITOR_HK_EDIT_NUDGE_N },
    { "locs", "Nudge south", EDITOR_HK_EDIT_NUDGE_S },
    { "locs", "Nudge west", EDITOR_HK_EDIT_NUDGE_W },
    { "locs", "Nudge east", EDITOR_HK_EDIT_NUDGE_E },
    { "locs", "Rotate", EDITOR_HK_EDIT_ROTATE },
    { "locs", "Reshape", EDITOR_HK_EDIT_RESHAPE },
    { "locs", "Duplicate", EDITOR_HK_EDIT_DUPLICATE },
    { "locs", "Delete", EDITOR_HK_EDIT_DELETE },
    { "locs", "Swap to catalog pick", EDITOR_HK_PLACE_SWAP },
    { "locs", "Up a level", EDITOR_HK_EDIT_LEVEL_UP },
    { "locs", "Down a level", EDITOR_HK_EDIT_LEVEL_DOWN },
    { "spawns", "Duplicate", EDITOR_HK_EDIT_DUPLICATE },
    { "spawns", "Delete / suppress", EDITOR_HK_EDIT_DELETE },
    { "rect", "Copy", EDITOR_HK_EDIT_COPY },
    { "rect", "Paste", EDITOR_HK_EDIT_PASTE },
    { "rect", "Fill with palette", EDITOR_HK_EDIT_FILL },
    { "rect", "Clear", EDITOR_HK_EDIT_CLEAR },
    { "rect", "Mark area from rect", EDITOR_HK_EDIT_MARK_AREA },
    { "history", "Undo", EDITOR_HK_EDIT_UNDO },
    { "history", "Redo", EDITOR_HK_EDIT_REDO },
    { "catalog", "Add to palette", EDITOR_HK_PALETTE_ADD_PICK },
    { "catalog_palette", "Add current pick", EDITOR_HK_PALETTE_ADD_PICK },
    { "live", "RUN", EDITOR_HK_FILE_RUN },
    { "triggers", "Check", EDITOR_HK_FILE_CHECK_SCRIPTS },
    { "triggers", "Run", EDITOR_HK_FILE_RUN },
    { "squares", "Open", EDITOR_HK_PICKER_OPEN },
    { "squares", "Add to open set", EDITOR_HK_PICKER_ADD },
    { "squares", "New square...", EDITOR_HK_FILE_NEW },
};
#define BUTTON_COUNT ((int)(sizeof(k_buttons) / sizeof(k_buttons[0])))

static void
check_button_chords(const char* when)
{
    for( int i = 0; i < BUTTON_COUNT; i++ )
    {
        int const widget = widget_in(k_buttons[i].module, TORIRS_CHROME_W_BUTTON, k_buttons[i].caption);
        char key[EDITOR_HK_HINT_CHORD_MAX];
        char all[EDITOR_HK_HINT_CHORD_MAX];

        first_chord(k_buttons[i].action, key, (int)sizeof(key));
        Editor_HotkeyDisplay(&g_ui.hotkeys, k_buttons[i].action, all, (int)sizeof(all));
        if( strcmp(all, key) == 0 )
            all[0] = '\0';
        CHECK(widget >= 0, "%s: %s has its '%s' button", when, k_buttons[i].module, k_buttons[i].caption);
        if( widget < 0 )
            continue;
        CHECK(strcmp(g_chrome.widgets[widget].chord, key) == 0, "%s: %s '%s' wears '%s' (has '%s')", when,
            k_buttons[i].module, k_buttons[i].caption, key, g_chrome.widgets[widget].chord);
        CHECK(strcmp(g_chrome.widgets[widget].tooltip_chord, all) == 0,
            "%s: %s '%s' tooltip ends with '%s' (has '%s')", when, k_buttons[i].module, k_buttons[i].caption, all,
            g_chrome.widgets[widget].tooltip_chord);
        /* The tooltip text is the action alone: the chord is the keycap. */
        CHECK(strchr(g_chrome.widgets[widget].tooltip, '[') == NULL, "%s: %s '%s' tip has no '[chord]' text ('%s')",
            when, k_buttons[i].module, k_buttons[i].caption, g_chrome.widgets[widget].tooltip);
    }
}

static void
test_buttons(void)
{
    int cut;

    boot(1530, 1006, 1);

    /* Every default is bound, so every button wears a keycap. */
    for( int i = 0; i < BUTTON_COUNT; i++ )
    {
        char key[EDITOR_HK_HINT_CHORD_MAX];
        first_chord(k_buttons[i].action, key, (int)sizeof(key));
        CHECK(key[0], "%s's action %s has a default chord", k_buttons[i].caption,
            Editor_HotkeyInfo(k_buttons[i].action)->id);
    }
    check_button_chords("boot");
    {
        int const undo = widget_in("history", TORIRS_CHROME_W_BUTTON, "Undo");
        int const redo = widget_in("history", TORIRS_CHROME_W_BUTTON, "Redo");
        int const del = widget_in("locs", TORIRS_CHROME_W_BUTTON, "Delete");
        CHECK(undo >= 0 && (strcmp(g_chrome.widgets[undo].chord, "Ctrl+Z") == 0
                               || strcmp(g_chrome.widgets[undo].chord, "Cmd+Z") == 0),
            "Undo wears Ctrl+Z");
        CHECK(redo >= 0 && strstr(g_chrome.widgets[redo].tooltip_chord, "+Y") != NULL,
            "Redo's tooltip names both chords ('%s')", redo >= 0 ? g_chrome.widgets[redo].tooltip_chord : "-");
        CHECK(del >= 0 && strcmp(g_chrome.widgets[del].chord, "Del") == 0
                  && strcmp(g_chrome.widgets[del].tooltip_chord, "Del, Backspace") == 0,
            "Delete wears Del; its tooltip says Backspace too ('%s' / '%s')",
            del >= 0 ? g_chrome.widgets[del].chord : "-", del >= 0 ? g_chrome.widgets[del].tooltip_chord : "-");
    }

    /* Build draws every keycap: each module's window opened content-sized,
     * one at a time, so none is clipped by a group's box or another window. */
    cut = 0;
    for( int i = 0; i < BUTTON_COUNT; i++ )
    {
        int const panel = window_of(k_buttons[i].module);
        int const widget = widget_in(k_buttons[i].module, TORIRS_CHROME_W_BUTTON, k_buttons[i].caption);
        if( panel < 0 || widget < 0 )
            continue;
        for( int p = 0; p < g_chrome.panel_count; p++ )
            if( p != panel )
                ToriRSChrome_PanelSetVisible(&g_chrome, p, 0);
        ToriRSChrome_PanelSetFixedHeight(&g_chrome, panel, 0);
        ToriRSChrome_PanelSetScrollable(&g_chrome, panel, 0);
        ToriRSChrome_PanelMove(&g_chrome, panel, 40, 30);
        ToriRSChrome_PanelSetVisible(&g_chrome, panel, 1);
        /* A button on a tab page draws when its page shows. */
        if( g_chrome.widgets[widget].tab >= 0 )
            ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, g_chrome.widgets[widget].tab);
        ToriRSChrome_Build(&g_chrome);
        CHECK(!g_chrome.widgets[widget].hidden && drawn_small(g_chrome.widgets[widget].chord),
            "%s '%s': Build draws its keycap '%s' in the Small face", k_buttons[i].module, k_buttons[i].caption,
            g_chrome.widgets[widget].chord);
        /* Design section 2: what the plate cuts ends in "..." and the full
         * text is the tooltip; section 15: the tooltip ends with the chord. */
        {
            const char* drawn = drawn_caption(widget);
            char tip[TORIRS_CHROME_INPUT_MAX + TORIRS_CHROME_LABEL_MAX + 4];
            int const has_tip = ToriRSChrome_TooltipText(&g_chrome, widget, tip, (int)sizeof(tip));
            CHECK(drawn != NULL, "%s '%s': Build draws a caption", k_buttons[i].module, k_buttons[i].caption);
            CHECK(!drawn || strcmp(drawn, k_buttons[i].caption) == 0 || (has_tip && strstr(tip, k_buttons[i].caption)),
                "%s '%s': drawn '%s', so the tooltip ('%s') says it whole", k_buttons[i].module,
                k_buttons[i].caption, drawn ? drawn : "-", tip);
            CHECK(!drawn || strcmp(drawn, k_buttons[i].caption) == 0
                      || (strlen(drawn) >= 3 && strcmp(drawn + strlen(drawn) - 3, "...") == 0),
                "%s '%s': a cut caption ends in an ellipsis ('%s')", k_buttons[i].module, k_buttons[i].caption,
                drawn ? drawn : "-");
            CHECK(has_tip, "%s '%s': hovering it shows a tooltip, ending with its chord", k_buttons[i].module,
                k_buttons[i].caption);
            if( drawn && strcmp(drawn, k_buttons[i].caption) != 0 )
                cut++;
            if( strcmp(k_buttons[i].caption, "Redo") == 0 && drawn && strcmp(drawn, "Redo") != 0 )
                CHECK(strncmp(tip, "Redo: ", 6) == 0, "Redo cut to '%s': its tooltip leads with it ('%s')", drawn,
                    tip);
        }
    }
    /* The check above is live: beside Ctrl+Shift+Z at 1x, Redo is cut. */
    CHECK(cut > 0, "some keycapped caption is cut at 1x (%d)", cut);
    shutdown_ui();
}

static void
test_rail(void)
{
    struct Editor_Chord chord;

    boot(1530, 1006, 1);
    for( int t = 0; t < EDITOR_TOOL_COUNT; t++ )
    {
        int const widget = widget_in("toolrail", TORIRS_CHROME_W_ICONBUTTON, Editor_ToolName((enum Editor_Tool)t));
        char key[EDITOR_HK_HINT_CHORD_MAX];
        first_chord((enum Editor_HotkeyAction)(EDITOR_HK_TOOL_SELECT + t), key, (int)sizeof(key));
        CHECK(widget >= 0, "rail: %s's icon", Editor_ToolName((enum Editor_Tool)t));
        if( widget < 0 )
            continue;
        CHECK(strcmp(g_chrome.widgets[widget].chord, key) == 0, "rail: %s's badge is '%s' (has '%s')",
            Editor_ToolName((enum Editor_Tool)t), key, g_chrome.widgets[widget].chord);
        CHECK(g_chrome.widgets[widget].tooltip[0] && strchr(g_chrome.widgets[widget].tooltip, '[') == NULL,
            "rail: %s's tip is what the click does ('%s')", Editor_ToolName((enum Editor_Tool)t),
            g_chrome.widgets[widget].tooltip);
        CHECK(!key[0] || drawn_small(g_chrome.widgets[widget].chord), "rail: %s's badge '%s' is drawn",
            Editor_ToolName((enum Editor_Tool)t), g_chrome.widgets[widget].chord);
    }
    {
        int const save = widget_in("toolrail", TORIRS_CHROME_W_ICONBUTTON, "Save");
        char all[EDITOR_HK_HINT_CHORD_MAX];
        Editor_HotkeyDisplay(&g_ui.hotkeys, EDITOR_HK_FILE_SAVE_ALL, all, (int)sizeof(all));
        CHECK(save >= 0 && g_chrome.widgets[save].chord[0] == '\0'
                  && strcmp(g_chrome.widgets[save].tooltip_chord, all) == 0,
            "rail: Save says its chord in the tooltip only ('%s')",
            save >= 0 ? g_chrome.widgets[save].tooltip_chord : "-");
    }

    /* A rebind relabels the badge, the panel buttons and the sliders on the
     * next frame; an unbind clears them; Reset brings the defaults back. */
    CHECK(Editor_ChordParse("r", &chord), "r parses");
    Editor_HotkeysSet(&g_ui.hotkeys, EDITOR_HK_EDIT_ROTATE, &chord, 1);
    CHECK(Editor_ChordParse("ctrl+shift+s", &chord), "ctrl+shift+s parses");
    Editor_HotkeysSet(&g_ui.hotkeys, EDITOR_HK_TOOL_SELECT, &chord, 1);
    CHECK(Editor_ChordParse("minus", &chord), "minus parses");
    Editor_HotkeysSet(&g_ui.hotkeys, EDITOR_HK_BRUSH_RADIUS_DOWN, &chord, 1);
    Editor_HotkeysSet(&g_ui.hotkeys, EDITOR_HK_EDIT_UNDO, NULL, 0);
    frame();
    {
        int const rotate = widget_in("locs", TORIRS_CHROME_W_BUTTON, "Rotate");
        int const select = widget_in("toolrail", TORIRS_CHROME_W_ICONBUTTON, "Select");
        int const undo = widget_in("history", TORIRS_CHROME_W_BUTTON, "Undo");
        int const radius = widget_in("terrain", TORIRS_CHROME_W_SLIDER, "Radius  - ]");
        CHECK(rotate >= 0 && strcmp(g_chrome.widgets[rotate].chord, "R") == 0, "rebind: Rotate wears R ('%s')",
            rotate >= 0 ? g_chrome.widgets[rotate].chord : "-");
        CHECK(select >= 0
                  && (strcmp(g_chrome.widgets[select].chord, "Ctrl+Shift+S") == 0
                      || strcmp(g_chrome.widgets[select].chord, "Cmd+Shift+S") == 0),
            "rebind: the Select badge follows ('%s')", select >= 0 ? g_chrome.widgets[select].chord : "-");
        CHECK(undo >= 0 && g_chrome.widgets[undo].chord[0] == '\0', "unbind: Undo wears no keycap");
        CHECK(radius >= 0, "rebind: the Radius slider reads 'Radius  - ]'");
        /* A badge wider than the icon box is fitted, never cut into a chord
         * that reads as another ("Ctrl+S"); the tooltip has it whole. */
        if( select >= 0 )
        {
            const char* badge;
            int const panel = g_chrome.widgets[select].panel;
            ToriRSChrome_PanelSetVisible(&g_chrome, panel, 1);
            ToriRSChrome_Build(&g_chrome);
            badge = drawn_badge(select);
            CHECK(badge && strcmp(badge, "...+S") == 0, "rebind: the over-wide Select badge reads '...+S' ('%s')",
                badge ? badge : "-");
            CHECK(strstr(g_chrome.widgets[select].chord, "Shift+S") != NULL,
                "rebind: the badge's chord (the tooltip's) is whole ('%s')", g_chrome.widgets[select].chord);
        }
    }
    check_button_chords("rebound");
    Editor_HotkeysResetDefaults(&g_ui.hotkeys);
    frame();
    check_button_chords("reset");
    shutdown_ui();
}

static void
test_sliders(void)
{
    static const struct
    {
        const char* label;
        enum Editor_HotkeyAction down;
        enum Editor_HotkeyAction up;
    } k_sliders[] = {
        { "Radius", EDITOR_HK_BRUSH_RADIUS_DOWN, EDITOR_HK_BRUSH_RADIUS_UP },
        { "Step", EDITOR_HK_BRUSH_STEP_DOWN, EDITOR_HK_BRUSH_STEP_UP },
        { "Strength", EDITOR_HK_BRUSH_STRENGTH_DOWN, EDITOR_HK_BRUSH_STRENGTH_UP },
    };

    boot(1530, 1006, 1);
    CHECK(widget_in("terrain", TORIRS_CHROME_W_SLIDER, "Radius  [ ]") >= 0,
        "Terrain: the design's 'Radius  [ ]'");
    for( size_t i = 0; i < sizeof(k_sliders) / sizeof(k_sliders[0]); i++ )
    {
        int const actions[2] = { (int)k_sliders[i].down, (int)k_sliders[i].up };
        char chords[EDITOR_HK_HINT_CHORD_MAX];
        char want[TORIRS_CHROME_LABEL_MAX];
        int widget;
        Editor_HotkeysJoin(&g_ui.hotkeys, actions, 2, chords, (int)sizeof(chords));
        snprintf(want, sizeof(want), "%s  %s", k_sliders[i].label, chords);
        widget = widget_in("terrain", TORIRS_CHROME_W_SLIDER, want);
        CHECK(chords[0] && widget >= 0, "Terrain: the slider reads '%s'", want);
        CHECK(widget < 0 || strcmp(g_chrome.widgets[widget].tooltip_chord, chords) == 0,
            "Terrain: %s's tooltip ends with '%s'", k_sliders[i].label, chords);
    }
    /* Both steppers unbound: the label alone. */
    Editor_HotkeysSet(&g_ui.hotkeys, EDITOR_HK_BRUSH_STEP_DOWN, NULL, 0);
    Editor_HotkeysSet(&g_ui.hotkeys, EDITOR_HK_BRUSH_STEP_UP, NULL, 0);
    frame();
    CHECK(widget_in("terrain", TORIRS_CHROME_W_SLIDER, "Step") >= 0, "Terrain: unbound steppers leave 'Step'");
    shutdown_ui();
}

static void
test_truncation(void)
{
    char long_label[301];
    const char* line = NULL;
    int serial;
    int panel;
    int count;

    boot(1530, 1006, 1);
    serial = 0;
    count = chrome_lines_since(serial, &line);
    CHECK(count == 0, "boot: no module hands the chrome a string it cuts (%d, newest '%s')", count,
        line ? line : "");

    memset(long_label, 'x', 300);
    long_label[300] = '\0';
    memcpy(long_label, "LONG", 4);
    panel = window_of("history");
    CHECK(panel >= 0, "History's window");
    {
        int const widget = ToriRSChrome_Label(&g_chrome, panel, long_label);
        serial = g_ui.console_serial;
        frame();
        count = chrome_lines_since(serial, &line);
        CHECK(count == 1, "a 300-byte label is logged once (%d)", count);
        /* A LABEL row keeps its words in `text` (TORIRS_CHROME_INPUT_MAX). */
        CHECK(line && strstr(line, "chrome: History text cut to 191 of 300 bytes") && strstr(line, "\"LONGxxx"),
            "the line names the window, the field and the sizes ('%s')", line ? line : "");
        /* Set again, frame after frame: still once for this widget. */
        serial = g_ui.console_serial;
        for( int i = 0; i < 3; i++ )
        {
            ToriRSChrome_SetLabel(&g_chrome, widget, long_label);
            frame();
        }
        count = chrome_lines_since(serial, &line);
        CHECK(count == 0, "the same widget cut again says nothing more (%d)", count);
    }

    /* More records in one frame than the ring holds: one line per kept
     * record, and one saying how many were dropped. */
    serial = g_ui.console_serial;
    for( int i = 0; i < TORIRS_CHROME_TRUNCATION_RING + 4; i++ )
        (void)ToriRSChrome_Label(&g_chrome, panel, long_label);
    frame();
    count = chrome_lines_since(serial, &line);
    CHECK(count == TORIRS_CHROME_TRUNCATION_RING + 1, "a full ring: %d lines and the dropped count (%d)",
        TORIRS_CHROME_TRUNCATION_RING, count);
    CHECK(line && strstr(line, "4 more cut string(s)"), "the newest line says 4 were dropped ('%s')",
        line ? line : "");
    serial = g_ui.console_serial;
    frame();
    count = chrome_lines_since(serial, &line);
    CHECK(count == 0, "drained: a quiet frame logs nothing (%d)", count);
    shutdown_ui();
}

/* An on-demand panel is not opened by showing the editor (Windows run,
 * 2026-11-06: Stamps opened at every boot, floating over the World, because
 * Editor_UiSetVisible shows every titled panel and Stamps had no set_visible
 * to keep itself closed, as Record and Triggers do). Opened by the user, a
 * hide and show of the editor (Tab) brings it back as it was. */
static void
test_on_demand_closed(void)
{
    int panel;

    boot(1558, 1000, 1);
    panel = window_of("stamps");
    CHECK(panel >= 0, "the Stamps panel is registered");
    if( panel >= 0 )
    {
        CHECK(!g_chrome.panels[panel].visible, "boot: Stamps is closed (it opens from the Catalog's Stamps...)");
        (void)Editor_UiTogglePanel(&g_ui, "stamps");
        frame();
        CHECK(g_chrome.panels[panel].visible, "View > Stamps opens it");
        Editor_UiSetVisible(&g_ui, 0);
        frame();
        Editor_UiSetVisible(&g_ui, 1);
        frame();
        CHECK(g_chrome.panels[panel].visible, "hiding and showing the editor keeps it open");
        (void)Editor_UiTogglePanel(&g_ui, "stamps");
        frame();
        Editor_UiSetVisible(&g_ui, 0);
        frame();
        Editor_UiSetVisible(&g_ui, 1);
        frame();
        CHECK(!g_chrome.panels[panel].visible, "closed by the user, it stays closed after a hide and show");
    }
    shutdown_ui();
}

int
main(void)
{
    test_buttons();
    test_rail();
    test_sliders();
    test_truncation();
    test_on_demand_closed();
    printf("editor_chords_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
