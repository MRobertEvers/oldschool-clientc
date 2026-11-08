/*
 * The hotkey table (editor/editor_hotkeys.h): defaults, chords, the ini
 * codec, conflicts and the per-frame resolver, plus the doc check -- every
 * action's id and its row are in docs/MAPED_HOTKEYS.md.
 *
 *   ./editor_hotkeys_test <path to docs/MAPED_HOTKEYS.md>
 */

#include "editor/editor_hotkeys.h"
#include "editor/editor_recent.h"
#include "mem/torirs_mem.h"

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

static struct Editor_Hotkeys g_hk;

/* ---- helpers --------------------------------------------------------------------- */

static struct Editor_HotkeyInput g_in;

static void
input_clear(uint64_t now_ms)
{
    memset(&g_in, 0, sizeof(g_in));
    g_in.now_ms = now_ms;
}

/** Press `chord` this frame: modifiers held, the key down and held. */
static void
press(const char* chord_text)
{
    struct Editor_Chord chord;
    int const ok = Editor_ChordParse(chord_text, &chord);
    assert(ok);
    (void)ok;
    if( chord.mods & EDITOR_HK_MOD_CTRL )
        g_in.held[TORIRSK_CTRL] = 1;
    if( chord.mods & EDITOR_HK_MOD_SHIFT )
        g_in.held[TORIRSK_SHIFT] = 1;
    if( chord.mods & EDITOR_HK_MOD_ALT )
        g_in.held[TORIRSK_ALT] = 1;
    g_in.down[chord.key] = 1;
    g_in.held[chord.key] = 1;
}

/** Resolve one frame; the single action fired, -1 none, -2 several. */
static int
fire(
    const char* chord_text,
    unsigned contexts)
{
    struct Editor_HotkeyFrame frame;
    input_clear(1000);
    press(chord_text);
    g_in.contexts = contexts;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    if( frame.count == 0 )
        return -1;
    return frame.count == 1 ? frame.fired[0] : -2;
}

/* ---- the table ------------------------------------------------------------------- */

static void
test_defaults(void)
{
    int pairs[64];
    int conflicts;

    Editor_HotkeysInit(&g_hk);
    conflicts = Editor_HotkeysConflicts(&g_hk, pairs, 32);
    for( int i = 0; i < conflicts && i < 32; i++ )
        printf("  clash: %s / %s\n", Editor_HotkeyInfo(pairs[2 * i])->id, Editor_HotkeyInfo(pairs[2 * i + 1])->id);
    CHECK(conflicts == 0, "the defaults are conflict-free per context (%d clashes)", conflicts);

    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        CHECK(info->id && info->group && info->name && info->doc && info->contexts, "action %d is described", a);
        if( !info->id )
            continue;
        CHECK(Editor_HotkeyFind(info->id) == a, "%s finds itself", info->id);
        CHECK(Editor_HotkeysIsDefault(&g_hk, a), "%s starts at its default", info->id);
        for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
        {
            struct Editor_Chord chord;
            struct Editor_Chord again;
            char text[48];
            if( !info->defaults[c] )
                continue;
            CHECK(Editor_ChordParse(info->defaults[c], &chord), "%s's default '%s' parses", info->id,
                info->defaults[c]);
            Editor_ChordFormat(chord, text, (int)sizeof(text));
            CHECK(strcmp(text, info->defaults[c]) == 0, "%s: '%s' formats back as '%s'", info->id,
                info->defaults[c], text);
            CHECK(Editor_ChordParse(text, &again) && again.key == chord.key && again.mods == chord.mods,
                "%s: the formatted chord parses to the same chord", info->id);
        }
    }
    CHECK(Editor_HotkeyFind("no.such") == -1, "an unknown id is -1");
    /* The Preview panel (the Catalog pick's model): its own View action, on
     * Ctrl+Shift+P (every F-key is taken). */
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(EDITOR_HK_VIEW_PREVIEW);
        struct Editor_Chord chord;
        CHECK(Editor_HotkeyFind("view.preview") == EDITOR_HK_VIEW_PREVIEW
                  && Editor_HotkeyForPanel("preview") == EDITOR_HK_VIEW_PREVIEW,
            "view.preview names the preview module");
        CHECK(info->defaults[0] && Editor_ChordParse(info->defaults[0], &chord) && chord.key == TORIRSK_P
                  && chord.mods == (EDITOR_HK_MOD_CTRL | EDITOR_HK_MOD_SHIFT) && !info->defaults[1],
            "view.preview defaults to Ctrl+Shift+P alone");
    }
    CHECK(Editor_HotkeyForPanel("terrain") == EDITOR_HK_VIEW_TERRAIN && Editor_HotkeyForPanel("hotkeys")
              == EDITOR_HK_VIEW_HOTKEYS,
        "the View actions name their modules");
}

static void
test_chords(void)
{
    struct Editor_Chord chord;
    char text[48];

    CHECK(Editor_ChordParse("Ctrl+Shift+Z", &chord) && chord.key == TORIRSK_Z
              && chord.mods == (EDITOR_HK_MOD_CTRL | EDITOR_HK_MOD_SHIFT),
        "parse is case-insensitive");
    CHECK(Editor_ChordParse("cmd+s", &chord) && chord.key == TORIRSK_S && chord.mods == EDITOR_HK_MOD_CTRL,
        "cmd is ctrl");
    CHECK(Editor_ChordParse("shift", &chord) && chord.key == TORIRSK_SHIFT && chord.mods == 0,
        "a modifier alone names that key");
    CHECK(!Editor_ChordParse("ctrl+", &chord) && !Editor_ChordParse("hyper+z", &chord)
              && !Editor_ChordParse("zz", &chord) && !Editor_ChordParse("", &chord),
        "malformed chords are refused");
    CHECK(Editor_ChordParse("ctrl+lbracket", &chord), "ctrl+lbracket parses");
    Editor_ChordDisplay(chord, text, (int)sizeof(text));
#if defined(__APPLE__)
    CHECK(strcmp(text, "Cmd+[") == 0, "display reads Cmd+[ on Apple (%s)", text);
#else
    CHECK(strcmp(text, "Ctrl+[") == 0, "display reads Ctrl+[ (%s)", text);
#endif
    (void)Editor_ChordParse("shift+delete", &chord);
    Editor_ChordDisplay(chord, text, (int)sizeof(text));
    CHECK(strcmp(text, "Shift+Del") == 0, "Shift+Del (%s)", text);
    (void)Editor_ChordParse("f12", &chord);
    Editor_ChordDisplay(chord, text, (int)sizeof(text));
    CHECK(strcmp(text, "F12") == 0, "F12 (%s)", text);
    Editor_HotkeysInit(&g_hk);
    Editor_HotkeyDisplay(&g_hk, EDITOR_HK_EDIT_REDO, text, (int)sizeof(text));
#if defined(__APPLE__)
    CHECK(strcmp(text, "Cmd+Shift+Z, Cmd+Y") == 0, "Redo shows both chords (%s)", text);
#else
    CHECK(strcmp(text, "Ctrl+Shift+Z, Ctrl+Y") == 0, "Redo shows both chords (%s)", text);
#endif
    Editor_HotkeyDisplay(&g_hk, EDITOR_HK_FILE_BAKE, text, (int)sizeof(text));
    CHECK(text[0] == '\0', "an unbound action shows nothing");
}

static void
test_ini(void)
{
    struct Editor_Hotkeys back;
    struct Editor_HotkeysReport report;
    struct Editor_Chord chord;
    char* data = NULL;
    int size = 0;
    const char* hand = "; hand edited\n"
                       "[hotkeys]\r\n"
                       "version = 1\n"
                       "file.bake = ctrl+b\n"
                       "edit.redo = ctrl+y, ctrl+shift+z   ; swapped\n"
                       "tool.height =\n"
                       "no.such.action = ctrl+q\n"
                       "edit.undo = ctrl+hyper\n"
                       "edit.copy = ctrl+c, ctrl+x, ctrl+v\n"
                       "[other]\n"
                       "file.new = ctrl+o\n";

    Editor_HotkeysInit(&g_hk);
    Editor_HotkeysEncode(&g_hk, &data, &size);
    CHECK(data && strstr(data, "[hotkeys]") && strstr(data, "version = 1\n")
              && strcmp(strstr(data, "version = 1\n"), "version = 1\n") == 0,
        "the defaults encode as a bare header");
    ToriMem_Free(ToriMem_Process(), data);

    CHECK(Editor_HotkeysDecode(&g_hk, hand, (int)strlen(hand), &report) == 3, "three lines apply (%d)",
        report.applied);
    CHECK(report.ignored == 5, "five lines are ignored and explained (%d)", report.ignored);
    for( int i = 0; i < report.line_count; i++ )
        printf("  ignored: %s\n", report.lines[i]);
    CHECK(report.line_count == 5 && strstr(report.lines[0], "unknown action") && strstr(report.lines[1], "bad chord")
              && strstr(report.lines[2], "bad chord"),
        "the report names each line's fault");
    (void)Editor_ChordParse("ctrl+b", &chord);
    CHECK(g_hk.bind[EDITOR_HK_FILE_BAKE].chord[0].key == chord.key
              && g_hk.bind[EDITOR_HK_FILE_BAKE].chord[0].mods == chord.mods,
        "file.bake bound to ctrl+b");
    CHECK(g_hk.bind[EDITOR_HK_TOOL_HEIGHT].chord[0].key == TORIRSK_UNKNOWN, "tool.height = is unbound");
    CHECK(g_hk.bind[EDITOR_HK_EDIT_REDO].chord[0].key == TORIRSK_Y, "redo's chords keep the file's order");
    CHECK(Editor_HotkeysIsDefault(&g_hk, EDITOR_HK_EDIT_UNDO) && Editor_HotkeysIsDefault(&g_hk, EDITOR_HK_FILE_NEW)
              && Editor_HotkeysIsDefault(&g_hk, EDITOR_HK_EDIT_COPY),
        "ignored lines leave their actions at the default");

    Editor_HotkeysEncode(&g_hk, &data, &size);
    CHECK(strstr(data, "file.bake = ctrl+b\n") && strstr(data, "tool.height =\n")
              && strstr(data, "edit.redo = ctrl+y, ctrl+shift+z\n") && !strstr(data, "edit.undo"),
        "encode writes exactly the non-default bindings:\n%s", data);
    Editor_HotkeysInit(&back);
    CHECK(Editor_HotkeysDecode(&back, data, size, &report) == 3 && report.ignored == 0, "its own output decodes clean");
    {
        int same = 1;
        for( int a = 0; a < EDITOR_HK_COUNT; a++ )
            if( memcmp(back.bind[a].chord, g_hk.bind[a].chord, sizeof(back.bind[a].chord)) != 0 )
                same = 0;
        CHECK(same, "the roundtrip holds every binding");
    }
    ToriMem_Free(ToriMem_Process(), data);
    CHECK(Editor_HotkeysDecode(&back, "", 0, NULL) == 0, "an empty file is the defaults");
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
        if( !Editor_HotkeysIsDefault(&back, a) )
        {
            CHECK(0, "%s left at a non-default after an empty decode", Editor_HotkeyInfo(a)->id);
            break;
        }
}

static void
test_resolve(void)
{
    struct Editor_HotkeyFrame frame;
    unsigned const G = EDITOR_HK_CTX_G | EDITOR_HK_CTX_N;
    /* Locs selected under Select / Move: what the App sets (contexts M). */
    unsigned const LOCS = EDITOR_HK_CTX_G | EDITOR_HK_CTX_L | EDITOR_HK_CTX_M;
    /* Locs selected with another tool armed (Place, a brush): N, not M. */
    unsigned const LOCS_PLACE = EDITOR_HK_CTX_G | EDITOR_HK_CTX_L | EDITOR_HK_CTX_N;
    /* The Place tool with a loc pick and nothing selected. */
    unsigned const PLACING = EDITOR_HK_CTX_G | EDITOR_HK_CTX_N | EDITOR_HK_CTX_P;
    unsigned const SPAWNS = EDITOR_HK_CTX_G | EDITOR_HK_CTX_S | EDITOR_HK_CTX_N;

    Editor_HotkeysInit(&g_hk);
    CHECK(fire("ctrl+z", G) == EDITOR_HK_EDIT_UNDO, "Ctrl+Z is Undo");
    CHECK(fire("z", G) == -1, "bare Z is nothing");
    CHECK(fire("ctrl+shift+z", G) == EDITOR_HK_EDIT_REDO && fire("ctrl+y", G) == EDITOR_HK_EDIT_REDO,
        "both Redo chords");
    CHECK(fire("3", G) == EDITOR_HK_TOOL_HEIGHT, "3 arms Height");
    CHECK(fire("alt+3", G) == EDITOR_HK_LEVEL_PIN3, "Alt+3 pins level 3 (workbench 6.8)");
    CHECK(fire("delete", LOCS) == EDITOR_HK_EDIT_DELETE, "Delete with locs selected");
    CHECK(fire("delete", SPAWNS) == EDITOR_HK_EDIT_DELETE, "Delete with a spawn selection");
    CHECK(fire("delete", G) == -1, "Delete with neither does nothing");
    CHECK(fire("shift+lbracket", G) == EDITOR_HK_BRUSH_STRENGTH_DOWN, "Shift+[ is strength -");
    CHECK(fire("lbracket", G) == EDITOR_HK_BRUSH_RADIUS_DOWN && fire("ctrl+rbracket", G) == EDITOR_HK_BRUSH_STEP_UP,
        "[ radius, Ctrl+] step");
    CHECK(fire("ctrl+c", G) == -1 && fire("ctrl+c", G | EDITOR_HK_CTX_R) == EDITOR_HK_EDIT_COPY,
        "Ctrl+C copies only with a rectangle");
    /* 2026-11-07 (owner: "the arrow keys ALWAYS tilt/roll etc the camera
     * when focused on the world view"): the nudges are Ctrl+arrows (Cmd on
     * a Mac); the bare arrows are the camera's under every tool and
     * selection. */
    CHECK(fire("ctrl+up", LOCS) == EDITOR_HK_EDIT_NUDGE_N && fire("ctrl+shift+left", LOCS) == EDITOR_HK_EDIT_NUDGE_FAR_W,
        "Ctrl+Up nudges a selection under Select / Move, Ctrl+Shift+Left 8 tiles");
    CHECK(fire("up", LOCS) == -1 && fire("shift+left", LOCS) == -1, "the bare arrows nudge nothing, Shift or not");
    CHECK(fire("ctrl+up", LOCS_PLACE) == -1 && fire("ctrl+left", LOCS_PLACE) == -1,
        "with locs selected under the Place tool Ctrl+arrows fire no nudge");
    {
        struct Editor_HotkeyFrame held;
        unsigned const k_ctx[2] = { LOCS, LOCS_PLACE };
        for( int i = 0; i < 2; i++ )
        {
            input_clear(1000);
            press("left");
            g_in.contexts = k_ctx[i];
            Editor_HotkeysResolve(&g_hk, &g_in, &held);
            CHECK(held.active[EDITOR_HK_CAM_TURN_LEFT] && held.count == 0,
                "Left held turns the camera with locs selected (%s)", i ? "Place" : "Select");
        }
        input_clear(1000);
        press("ctrl+left");
        g_in.contexts = LOCS;
        Editor_HotkeysResolve(&g_hk, &g_in, &held);
        CHECK(!held.active[EDITOR_HK_CAM_TURN_LEFT] && held.count == 1 && held.fired[0] == EDITOR_HK_EDIT_NUDGE_W,
            "Ctrl+Left nudges and does not turn");
    }
    CHECK(fire("return", PLACING) == EDITOR_HK_PLACE_HERE && fire("space", PLACING) == -1,
        "Return places at the hovered tile with the Place tool armed and nothing selected; Space no longer does");
    CHECK(fire("return", LOCS) == EDITOR_HK_EDIT_APPLY && fire("space", G) == -1,
        "Return still applies the tool to a selection; Space fires nothing");
    /* Space is the pan key now (2026-11-07): held over the World, with the
     * Place tool armed too, and its key claimed from the game; off the
     * World it is nothing. It clashes with no action. */
    {
        struct Editor_HotkeyFrame held;
        input_clear(1000);
        press("space");
        g_in.contexts = PLACING | EDITOR_HK_CTX_W;
        Editor_HotkeysResolve(&g_hk, &g_in, &held);
        CHECK(held.active[EDITOR_HK_CAM_PAN] && held.count == 0 && held.claimed[TORIRSK_SPACE],
            "Space held over the World is the pan, while placing too, and fires nothing");
        input_clear(1000);
        press("space");
        g_in.contexts = PLACING;
        Editor_HotkeysResolve(&g_hk, &g_in, &held);
        CHECK(!held.active[EDITOR_HK_CAM_PAN], "Space off the World is no pan");
        CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_CAM_PAN) == -1, "the pan's Space clashes with nothing");
    }
    CHECK(fire("q", PLACING) == EDITOR_HK_EDIT_ROTATE && fire("shift+q", PLACING) == EDITOR_HK_EDIT_RESHAPE,
        "Q / Shift+Q turn and reshape the ghost (the Edit actions, which act on the pose while placing)");
    CHECK(fire("g", G) == EDITOR_HK_PLACE_FIT, "G toggles Fit walls to neighbours");
    CHECK(fire("e", LOCS) == EDITOR_HK_PLACE_SWAP && fire("e", LOCS_PLACE) == EDITOR_HK_PLACE_SWAP,
        "E swaps the selected locs to the catalog's pick");
    CHECK(fire("escape", PLACING) == EDITOR_HK_EDIT_DESELECT, "Escape is Deselect / cancel (drops the ghost)");
    CHECK(fire("f1", G) == EDITOR_HK_VIEW_HOTKEYS, "F1 opens the Hotkeys panel");
    /* 2026-11-02: R and F fly, so Rotate moved to Q and Reshape to Shift+Q. */
    CHECK(fire("q", G) == EDITOR_HK_EDIT_ROTATE && fire("shift+q", G) == EDITOR_HK_EDIT_RESHAPE,
        "Q rotates, Shift+Q reshapes");
    CHECK(fire("r", G) == -1 && fire("e", G) == -1, "bare R and E fire no edge action with nothing selected");
    CHECK(fire("ctrl+g", G) == EDITOR_HK_VIEW_SNAP, "Ctrl+G toggles Snap to grid");
    CHECK(fire("ctrl+shift+u", G) == EDITOR_HK_FILE_PUBLISH && Editor_HotkeyForPanel("publish") == EDITOR_HK_FILE_PUBLISH,
        "Ctrl+Shift+U is File > Publish..., the Publish panel's row");
    CHECK(fire("shift+3", G) == EDITOR_HK_PALETTE_SLOT_3 && fire("shift+0", G) == EDITOR_HK_PALETTE_SLOT_10,
        "Shift+3 and Shift+0 are palette slots 3 and 10");
    CHECK(fire("ctrl+shift+a", G) == EDITOR_HK_PALETTE_ADD_PICK && fire("alt+rbracket", G) == EDITOR_HK_PALETTE_NEXT
              && fire("alt+lbracket", G) == EDITOR_HK_PALETTE_PREVIOUS,
        "Ctrl+Shift+A adds the pick, Alt+] / Alt+[ step palettes");
    CHECK(Editor_HotkeyForPanel("catalog_palette") == EDITOR_HK_VIEW_CATALOG_PALETTE,
        "the Catalog palette panel has its View action");
    CHECK(fire("shift+f12", G) == EDITOR_HK_VIEW_POPOUT && fire("ctrl+w", G) == EDITOR_HK_VIEW_CLOSE_PANEL,
        "Shift+F12 pops the panel out, Ctrl+W closes it");

    /* Held: the camera turns with no loc selected, W flies with Shift on top. */
    input_clear(1000);
    press("up");
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.count == 0 && frame.active[EDITOR_HK_CAM_TILT_UP] && frame.claimed[TORIRSK_UP],
        "Up tilts the camera with nothing selected");
    input_clear(1000);
    press("shift+w");
    g_in.down[TORIRSK_W] = 0;
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.active[EDITOR_HK_CAM_FORWARD] && frame.active[EDITOR_HK_CAM_FAST], "Shift+W held flies forward, fast");
    input_clear(1000);
    press("r");
    g_in.down[TORIRSK_R] = 0;
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.active[EDITOR_HK_CAM_UP] && !frame.active[EDITOR_HK_CAM_DOWN], "R held flies up");
    input_clear(1000);
    press("f");
    g_in.down[TORIRSK_F] = 0;
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.active[EDITOR_HK_CAM_DOWN] && !frame.active[EDITOR_HK_CAM_UP], "F held flies down");
    /* Ctrl alone is the snap hold: active while down, and it fires nothing. */
    input_clear(1000);
    press("ctrl");
    g_in.down[TORIRSK_CTRL] = 0;
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.active[EDITOR_HK_VIEW_SNAP_HOLD] && frame.count == 0, "Ctrl held forces the dock snap");
    input_clear(1000);
    press("ctrl+r");
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(!frame.active[EDITOR_HK_CAM_UP] && !frame.claimed[TORIRSK_R],
        "Ctrl+R is not claimed: the manifest's debug flight keeps it (Ctrl+W is Close panel now)");
    input_clear(1000);
    press("ctrl+s");
    g_in.down[TORIRSK_S] = 0;
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.claimed[TORIRSK_S] && frame.count == 0, "held Ctrl+S stays claimed after its edge");

    /* Repeat: the edge, nothing until 300 ms, then every 50 ms. */
    {
        int fires = 0;
        input_clear(0);
        press("rbracket");
        g_in.contexts = G;
        Editor_HotkeysResolve(&g_hk, &g_in, &frame);
        fires += frame.count;
        g_in.down[TORIRSK_RBRACKET] = 0;
        for( uint64_t t = 16; t <= 400; t += 16 )
        {
            g_in.now_ms = t;
            Editor_HotkeysResolve(&g_hk, &g_in, &frame);
            fires += frame.count;
            if( t < 300 )
                CHECK(frame.count == 0, "no repeat before 300 ms (t=%d)", (int)t);
        }
        CHECK(fires >= 2 && fires <= 4, "held 400 ms: the edge plus 1..3 repeats (%d)", fires);
        g_in.held[TORIRSK_RBRACKET] = 0;
        g_in.now_ms = 1000;
        Editor_HotkeysResolve(&g_hk, &g_in, &frame);
        CHECK(frame.count == 0, "released: no more");
    }

    /* A clash: the later binding wins until one is changed. */
    {
        struct Editor_Chord chord;
        (void)Editor_ChordParse("ctrl+z", &chord);
        Editor_HotkeysSet(&g_hk, EDITOR_HK_FILE_BAKE, &chord, 1);
        CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_FILE_BAKE) == EDITOR_HK_EDIT_UNDO
                  && Editor_HotkeysClash(&g_hk, EDITOR_HK_EDIT_UNDO) == EDITOR_HK_FILE_BAKE,
            "a clash shows on both sides");
        CHECK(fire("ctrl+z", G) == EDITOR_HK_FILE_BAKE, "the later binding wins the chord");
        Editor_HotkeysSet(&g_hk, EDITOR_HK_EDIT_UNDO, &chord, 1);
        CHECK(fire("ctrl+z", G) == EDITOR_HK_EDIT_UNDO, "...until the other side is rebound");
        (void)Editor_ChordParse("ctrl+left", &chord);
        Editor_HotkeysSet(&g_hk, EDITOR_HK_EDIT_NUDGE_N, &chord, 1);
        CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_EDIT_NUDGE_N) == EDITOR_HK_EDIT_NUDGE_W,
            "nudge N on Ctrl+Left clashes with nudge W");
        CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_CAM_TURN_LEFT) < 0, "but not with the camera's bare Left");
        (void)Editor_ChordParse("left", &chord);
        Editor_HotkeysSet(&g_hk, EDITOR_HK_EDIT_NUDGE_N, &chord, 1);
        CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_EDIT_NUDGE_N) == EDITOR_HK_CAM_TURN_LEFT,
            "nudge N on bare Left clashes with the camera's turn (the arrows are live everywhere)");
    }

    /* Capture. */
    {
        struct Editor_Chord chord;
        input_clear(0);
        g_in.down[TORIRSK_SHIFT] = 1;
        g_in.held[TORIRSK_SHIFT] = 1;
        CHECK(!Editor_HotkeysCapture(&g_in, 0, &chord), "a modifier alone never binds");
        CHECK(Editor_HotkeysCapture(&g_in, 1, &chord) && chord.key == TORIRSK_SHIFT && chord.mods == 0,
            "...except for the fly-faster action");
        g_in.down[TORIRSK_SHIFT] = 0;
        g_in.down[TORIRSK_F] = 1;
        g_in.held[TORIRSK_F] = 1;
        g_in.held[TORIRSK_CTRL] = 1;
        CHECK(Editor_HotkeysCapture(&g_in, 0, &chord) && chord.key == TORIRSK_F
                  && chord.mods == (EDITOR_HK_MOD_CTRL | EDITOR_HK_MOD_SHIFT),
            "Ctrl+Shift+F captures with its modifiers");
    }
}

/* ---- the hint strip ----------------------------------------------------------------- */

#define HINT_TOOL(action) ((action) - EDITOR_HK_TOOL_SELECT)

#if defined(__APPLE__)
#define HINT_CTRL "Cmd"
#else
#define HINT_CTRL "Ctrl"
#endif

/** The hint row whose verb is `verb`, or NULL. */
static const struct Editor_HotkeyHint*
hint_row(
    const struct Editor_HotkeyHint* rows,
    int count,
    const char* verb)
{
    for( int i = 0; i < count; i++ )
        if( strcmp(rows[i].verb, verb) == 0 )
            return &rows[i];
    return NULL;
}

/* The status bar's hint rows (Editor_HotkeysHint): every row of every tool
 * reads a real action, the contexts filter them, and the chords are the live
 * table's, so a rebind relabels the strip. */
static void
test_hint(void)
{
    /* With every context live, each tool shows all its rows. */
    static const struct
    {
        int tool;
        int rows;
    } k_counts[] = {
        { HINT_TOOL(EDITOR_HK_TOOL_SELECT), 10 }, { HINT_TOOL(EDITOR_HK_TOOL_PLACE), 8 },
        { HINT_TOOL(EDITOR_HK_TOOL_HEIGHT), 7 },  { HINT_TOOL(EDITOR_HK_TOOL_PAINT_FLAGS), 4 },
        { HINT_TOOL(EDITOR_HK_TOOL_MOVE), 2 },    { HINT_TOOL(EDITOR_HK_TOOL_ROTATE), 1 },
        { HINT_TOOL(EDITOR_HK_TOOL_DELETE), 1 },  { HINT_TOOL(EDITOR_HK_TOOL_RECT), 5 },
        { HINT_TOOL(EDITOR_HK_TOOL_PLACE_PLAYER), 1 },
    };
    struct Editor_HotkeyHint rows[EDITOR_HK_HINT_MAX];
    const struct Editor_HotkeyHint* row;
    struct Editor_Chord chords[2];
    int count;

    Editor_HotkeysInit(&g_hk);
    for( int tool = 0; tool < EDITOR_HK_HINT_TOOLS; tool++ )
    {
        count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, tool, 0, rows, EDITOR_HK_HINT_MAX);
        CHECK(count > 0 && count <= EDITOR_HK_HINT_MAX, "hint: tool %d has rows (%d)", tool, count);
        for( int i = 0; i < count; i++ )
        {
            CHECK(rows[i].verb && rows[i].verb[0] && strlen(rows[i].verb) <= 10,
                "hint: tool %d row %d has a short verb", tool, i);
            CHECK(rows[i].chord[0], "hint: tool %d row %d (%s) has a chord", tool, i, rows[i].verb);
            CHECK(rows[i].action == -1 || (rows[i].action >= 0 && rows[i].action < EDITOR_HK_COUNT
                                              && Editor_HotkeyInfo(rows[i].action)->id),
                "hint: tool %d row %d (%s) reads a real action (%d)", tool, i, rows[i].verb, rows[i].action);
        }
    }
    for( size_t i = 0; i < sizeof(k_counts) / sizeof(k_counts[0]); i++ )
    {
        count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, k_counts[i].tool, 0, rows, EDITOR_HK_HINT_MAX);
        CHECK(count == k_counts[i].rows, "hint: tool %d shows its %d rows with every context live (%d)",
            k_counts[i].tool, k_counts[i].rows, count);
    }
    /* `max` bounds the rows. */
    CHECK(Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 0, rows, 3) == 3,
        "hint: max bounds the rows");

    /* The fixed order and the default spellings. */
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 10 && strcmp(rows[0].chord, "click") == 0 && strcmp(rows[0].verb, "tile") == 0
              && strcmp(rows[1].chord, "Shift+click") == 0 && strcmp(rows[2].verb, "marquee") == 0
              && strcmp(rows[3].chord, HINT_CTRL "+A") == 0 && strcmp(rows[4].chord, "Alt+A") == 0
              && strcmp(rows[4].verb, "same") == 0 && strcmp(rows[5].chord, HINT_CTRL "+arrows") == 0
              && strcmp(rows[5].verb, "nudge") == 0 && strcmp(rows[6].chord, "Q") == 0
              && strcmp(rows[7].chord, "Shift+Q") == 0 && strcmp(rows[8].chord, "E") == 0
              && strcmp(rows[9].chord, "Del, Backspace") == 0 && strcmp(rows[9].verb, "delete") == 0,
        "hint: Select's rows in order (row 3 '%s', 4 '%s', 9 '%s')", count > 3 ? rows[3].chord : "",
        count > 4 ? rows[4].chord : "", count > 9 ? rows[9].chord : "");
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_PLACE), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 8 && strcmp(rows[0].chord, "click/Enter") == 0 && strcmp(rows[0].verb, "place") == 0
              && rows[0].action == EDITOR_HK_PLACE_HERE && strcmp(rows[1].chord, "drag") == 0
              && strcmp(rows[1].verb, "wall run") == 0 && strcmp(rows[2].chord, "Shift") == 0
              && rows[2].action == EDITOR_HK_PLACE_CONSTRAIN && strcmp(rows[2].verb, "one axis") == 0
              && strcmp(rows[3].verb, "turn") == 0 && strcmp(rows[5].chord, "G") == 0
              && strcmp(rows[7].chord, "Esc") == 0 && strcmp(rows[7].verb, "drop") == 0,
        "hint: Place's rows in order (row 0 '%s', 1 '%s', 2 '%s')", count > 0 ? rows[0].chord : "",
        count > 1 ? rows[1].chord : "", count > 2 ? rows[2].chord : "");
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_SMOOTH), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 4 && strcmp(rows[0].chord, "drag") == 0 && strcmp(rows[1].chord, "[ ]") == 0
              && strcmp(rows[2].chord, "Shift+[ ]") == 0 && strcmp(rows[3].chord, HINT_CTRL "+[ ]") == 0,
        "hint: a brush's bracket pairs say their modifier once ('%s', '%s', '%s')", count > 1 ? rows[1].chord : "",
        count > 2 ? rows[2].chord : "", count > 3 ? rows[3].chord : "");
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_MOVE), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 2 && strcmp(rows[0].chord, "drag/" HINT_CTRL "+arrows") == 0 && strcmp(rows[1].chord, HINT_CTRL "+D") == 0,
        "hint: Move drags or nudges, and duplicates ('%s')", count > 0 ? rows[0].chord : "");
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_RECT), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 5 && strcmp(rows[1].chord, HINT_CTRL "+C") == 0 && strcmp(rows[2].chord, HINT_CTRL "+V") == 0
              && strcmp(rows[3].chord, HINT_CTRL "+Shift+F") == 0 && strcmp(rows[4].chord, "Shift+Del") == 0,
        "hint: Rect's clipboard rows ('%s', '%s')", count > 3 ? rows[3].chord : "", count > 4 ? rows[4].chord : "");

    /* The live contexts filter: Select with nothing selected and the pointer
     * off the world keeps its mouse rows and Q / Shift+Q, nothing else. */
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_N, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 5 && !hint_row(rows, count, "nudge") && !hint_row(rows, count, "delete")
              && !hint_row(rows, count, "swap") && !hint_row(rows, count, "square") && hint_row(rows, count, "rotate"),
        "hint: Select with no selection drops nudge, delete, swap, square (%d rows)", count);
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_W | EDITOR_HK_CTX_L | EDITOR_HK_CTX_M,
        HINT_TOOL(EDITOR_HK_TOOL_SELECT), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 10, "hint: a selection under the pointer brings them back (%d rows)", count);
    /* Place with no pick (no P): the click row keeps its mouse word. */
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_N, HINT_TOOL(EDITOR_HK_TOOL_PLACE), 0, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "place");
    CHECK(row && strcmp(row->chord, "click") == 0 && !hint_row(rows, count, "swap"),
        "hint: Place without a pick says click alone, and no swap ('%s')", row ? row->chord : "");
    /* Move with nothing selected: drag, no arrows, no dup. */
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_N, HINT_TOOL(EDITOR_HK_TOOL_MOVE), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 1 && strcmp(rows[0].chord, "drag") == 0, "hint: Move with no selection says drag (%d rows)",
        count);
    /* Rect without a rectangle: drag, and paste only over the world. */
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_N, HINT_TOOL(EDITOR_HK_TOOL_RECT), 0, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 1, "hint: Rect with no rectangle and the pointer off the world says drag (%d rows)", count);

    /* A rebind relabels the strip: the table is the one source. */
    CHECK(Editor_ChordParse("r", &chords[0]), "hint: r parses");
    Editor_HotkeysSet(&g_hk, EDITOR_HK_EDIT_ROTATE, chords, 1);
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_N, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 0, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "rotate");
    CHECK(row && strcmp(row->chord, "R") == 0, "hint: Rotate rebound to R shows R ('%s')", row ? row->chord : "");
    CHECK(Editor_ChordParse("minus", &chords[0]) && Editor_ChordParse("equals", &chords[1]), "hint: - and = parse");
    Editor_HotkeysSet(&g_hk, EDITOR_HK_BRUSH_RADIUS_DOWN, &chords[0], 1);
    Editor_HotkeysSet(&g_hk, EDITOR_HK_BRUSH_RADIUS_UP, &chords[1], 1);
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_N, HINT_TOOL(EDITOR_HK_TOOL_HEIGHT), 0, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "radius");
    CHECK(row && strcmp(row->chord, "- =") == 0, "hint: radius rebound to - and = shows '- =' ('%s')",
        row ? row->chord : "");
    CHECK(Editor_ChordParse("ctrl+w", &chords[0]), "hint: ctrl+w parses");
    Editor_HotkeysSet(&g_hk, EDITOR_HK_EDIT_NUDGE_N, chords, 1);
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_M, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 0, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "nudge");
    CHECK(row && strcmp(row->chord, HINT_CTRL "+W Down Left Right") == 0,
        "hint: a nudge off the arrows spells the four keys ('%s')", row ? row->chord : "");
    /* Unbinding an action drops its row; unbinding the click's chord leaves
     * the mouse word. */
    Editor_HotkeysSet(&g_hk, EDITOR_HK_EDIT_RESHAPE, NULL, 0);
    Editor_HotkeysSet(&g_hk, EDITOR_HK_PLACE_HERE, NULL, 0);
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_PLACE), 0, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "place");
    CHECK(count == 7 && !hint_row(rows, count, "reshape") && row && strcmp(row->chord, "click") == 0,
        "hint: unbound rows drop, the click stays (%d rows, '%s')", count, row ? row->chord : "");
    Editor_HotkeysResetDefaults(&g_hk);
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 0, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "rotate");
    CHECK(row && strcmp(row->chord, "Q") == 0, "hint: Reset all puts Q back ('%s')", row ? row->chord : "");
}

/* The contexts the App's key snapshot and the status bar derive from the
 * shared tools (Editor_HotkeyContextsFrom): the exclusions at the top of
 * editor_hotkeys.h. */
/* The gesture spelling (workbench 6.1, design sections 14 and 15): the table
 * says the gesture beside the chords where one does the same, and the hint
 * strip, told the last input was a finger, says the gesture words instead of
 * the mouse words -- "2 fingers: pan, pinch: zoom, hold: menu". */
static void
test_gesture_hint(void)
{
    static const struct
    {
        int action;
        const char* gesture;
    } k_spelled[] = {
        { EDITOR_HK_CAM_FORWARD, "2 fingers" }, { EDITOR_HK_CAM_BACK, "2 fingers" },
        { EDITOR_HK_CAM_LEFT, "2 fingers" },    { EDITOR_HK_CAM_RIGHT, "2 fingers" },
        { EDITOR_HK_CAM_DOWN, "pinch" },        { EDITOR_HK_CAM_UP, "pinch" },
        { EDITOR_HK_CAM_TURN_LEFT, "twist" },   { EDITOR_HK_CAM_TURN_RIGHT, "twist" },
    };
    struct Editor_HotkeyHint rows[EDITOR_HK_HINT_MAX];
    const struct Editor_HotkeyHint* row;
    int spelled = 0;
    int count;

    Editor_HotkeysInit(&g_hk);
    for( size_t i = 0; i < sizeof(k_spelled) / sizeof(k_spelled[0]); i++ )
    {
        const char* gesture = Editor_HotkeyInfo((enum Editor_HotkeyAction)k_spelled[i].action)->gesture;
        CHECK(gesture && strcmp(gesture, k_spelled[i].gesture) == 0, "gesture: %s is '%s' ('%s')",
            Editor_HotkeyInfo((enum Editor_HotkeyAction)k_spelled[i].action)->id, k_spelled[i].gesture,
            gesture ? gesture : "(none)");
    }
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const char* gesture = Editor_HotkeyInfo((enum Editor_HotkeyAction)a)->gesture;
        if( !gesture )
            continue;
        spelled++;
        CHECK(gesture[0] && strlen(gesture) <= 12, "gesture: %s's spelling is a short word ('%s')",
            Editor_HotkeyInfo((enum Editor_HotkeyAction)a)->id, gesture);
    }
    CHECK(spelled == (int)(sizeof(k_spelled) / sizeof(k_spelled[0])),
        "gesture: only the camera's pan, zoom and turn are spelled (%d)", spelled);

    /* Select under a finger: tap, drag, the camera's three, the long press.
     * No Shift+click (a finger has no shift) and no keyboard row. */
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 1, rows, EDITOR_HK_HINT_MAX);
    CHECK(count == 6, "touch hint: Select has six rows (%d)", count);
    row = hint_row(rows, count, "tile");
    CHECK(row && strcmp(row->chord, "tap") == 0, "touch hint: a click is a tap ('%s')", row ? row->chord : "");
    row = hint_row(rows, count, "marquee");
    CHECK(row && strcmp(row->chord, "drag") == 0, "touch hint: one finger drags the marquee");
    CHECK(!hint_row(rows, count, "add loc"), "touch hint: Shift+click has no finger");
    CHECK(!hint_row(rows, count, "rotate") && !hint_row(rows, count, "nudge"),
        "touch hint: the keyboard's rows step aside");
    row = hint_row(rows, count, "pan");
    CHECK(row && strcmp(row->chord, "2 fingers") == 0 && row->action == EDITOR_HK_CAM_FORWARD,
        "touch hint: 2 fingers pan, read off the table ('%s')", row ? row->chord : "");
    row = hint_row(rows, count, "zoom");
    CHECK(row && strcmp(row->chord, "pinch") == 0, "touch hint: pinch zooms");
    row = hint_row(rows, count, "turn");
    CHECK(row && strcmp(row->chord, "twist") == 0, "touch hint: twist turns");
    row = hint_row(rows, count, "menu");
    CHECK(row && strcmp(row->chord, "hold") == 0 && row->action == -1 && row == &rows[count - 1],
        "touch hint: the long press opens the menu, last");
    for( int i = 0; i < count; i++ )
        CHECK(!strchr(rows[i].chord, '+') && !strstr(rows[i].chord, "click"),
            "touch hint: row %d (%s) names no key and no mouse ('%s')", i, rows[i].verb, rows[i].chord);

    /* Place, a brush, Rotate: their pointer word, then the same four. */
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_PLACE), 1, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "place");
    CHECK(count == 6 && row && strcmp(row->chord, "tap") == 0 && hint_row(rows, count, "wall run"),
        "touch hint: Place taps, and drags a run (%d rows)", count);
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_N, HINT_TOOL(EDITOR_HK_TOOL_FLATTEN), 1, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "paint");
    CHECK(count == 5 && row && strcmp(row->chord, "drag") == 0 && !hint_row(rows, count, "radius"),
        "touch hint: a brush strokes under one finger, its keys aside (%d rows)", count);
    CHECK(Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 1, rows, 2) == 2,
        "touch hint: max is honoured");

    /* A rebind relabels the mouse strip; the gesture is the gesture. */
    {
        struct Editor_Chord chord;
        CHECK(Editor_ChordParse("i", &chord), "parse i");
        Editor_HotkeysSet(&g_hk, EDITOR_HK_CAM_FORWARD, &chord, 1);
        count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 1, rows,
            EDITOR_HK_HINT_MAX);
        row = hint_row(rows, count, "pan");
        CHECK(row && strcmp(row->chord, "2 fingers") == 0, "touch hint: a rebound fly key still pans by 2 fingers");
        Editor_HotkeysResetDefaults(&g_hk);
    }

    /* And the mouse strip is the one it was. */
    count = Editor_HotkeysHint(&g_hk, EDITOR_HK_CTX_ALL, HINT_TOOL(EDITOR_HK_TOOL_SELECT), 0, rows, EDITOR_HK_HINT_MAX);
    row = hint_row(rows, count, "tile");
    CHECK(count == 10 && row && strcmp(row->chord, "click") == 0 && !hint_row(rows, count, "pan"),
        "hint: with a mouse the strip keeps the mouse words (%d rows)", count);
}

/* Editor_HotkeysJoin: the keycap one control wears (workbench 6.0), and the
 * compact pair a slider's label ends with. */
static void
test_join(void)
{
    int const rotate[1] = { EDITOR_HK_EDIT_ROTATE };
    int const radius[2] = { EDITOR_HK_BRUSH_RADIUS_DOWN, EDITOR_HK_BRUSH_RADIUS_UP };
    int const strength[2] = { EDITOR_HK_BRUSH_STRENGTH_DOWN, EDITOR_HK_BRUSH_STRENGTH_UP };
    int const redo[1] = { EDITOR_HK_EDIT_REDO };
    int const nudges[4] = { EDITOR_HK_EDIT_NUDGE_N, EDITOR_HK_EDIT_NUDGE_S, EDITOR_HK_EDIT_NUDGE_W,
                            EDITOR_HK_EDIT_NUDGE_E };
    struct Editor_Chord chord;
    char out[EDITOR_HK_HINT_CHORD_MAX];

    Editor_HotkeysInit(&g_hk);
    Editor_HotkeysJoin(&g_hk, rotate, 1, out, (int)sizeof(out));
    CHECK(strcmp(out, "Q") == 0, "join: one action is its first chord ('%s')", out);
    Editor_HotkeysJoin(&g_hk, redo, 1, out, (int)sizeof(out));
    CHECK(strcmp(out, "Ctrl+Shift+Z") == 0 || strcmp(out, "Cmd+Shift+Z") == 0,
        "join: Redo's keycap is its first chord only ('%s')", out);
    Editor_HotkeysJoin(&g_hk, radius, 2, out, (int)sizeof(out));
    CHECK(strcmp(out, "[ ]") == 0, "join: the radius pair is '[ ]' ('%s')", out);
    Editor_HotkeysJoin(&g_hk, strength, 2, out, (int)sizeof(out));
    CHECK(strcmp(out, "Shift+[ ]") == 0, "join: the shared modifier said once ('%s')", out);
    Editor_HotkeysJoin(&g_hk, nudges, 4, out, (int)sizeof(out));
    CHECK(strcmp(out, "Ctrl+arrows") == 0 || strcmp(out, "Cmd+arrows") == 0,
        "join: the four nudges are 'Ctrl+arrows' ('%s')", out);
    CHECK(Editor_ChordParse("minus", &chord), "join: minus parses");
    Editor_HotkeysSet(&g_hk, EDITOR_HK_BRUSH_RADIUS_DOWN, &chord, 1);
    Editor_HotkeysJoin(&g_hk, radius, 2, out, (int)sizeof(out));
    CHECK(strcmp(out, "- ]") == 0, "join: a rebind reads from the live table ('%s')", out);
    /* Modifier sets that differ are never compacted under the part they
     * share: "Ctrl+Shift+[ ]" would say Step + is Ctrl+Shift+]. */
    {
        int const step[2] = { EDITOR_HK_BRUSH_STEP_DOWN, EDITOR_HK_BRUSH_STEP_UP };
        char want[EDITOR_HK_HINT_CHORD_MAX];
        char down[32];
        char up[32];
        CHECK(Editor_ChordParse("ctrl+shift+lbracket", &chord), "join: ctrl+shift+lbracket parses");
        Editor_HotkeysSet(&g_hk, EDITOR_HK_BRUSH_STEP_DOWN, &chord, 1);
        Editor_ChordDisplay(g_hk.bind[EDITOR_HK_BRUSH_STEP_DOWN].chord[0], down, (int)sizeof(down));
        Editor_ChordDisplay(g_hk.bind[EDITOR_HK_BRUSH_STEP_UP].chord[0], up, (int)sizeof(up));
        snprintf(want, sizeof(want), "%s %s", down, up);
        Editor_HotkeysJoin(&g_hk, step, 2, out, (int)sizeof(out));
        CHECK(strcmp(out, want) == 0, "join: Ctrl+Shift+[ beside Ctrl+] is said whole ('%s', want '%s')", out, want);
        CHECK(strstr(out, "Shift+[ ]") == NULL, "join: never the unbound Ctrl+Shift+] ('%s')", out);
        CHECK(Editor_ChordParse("shift+rbracket", &chord), "join: shift+rbracket parses");
        Editor_HotkeysSet(&g_hk, EDITOR_HK_BRUSH_STEP_DOWN, &chord, 1);
        CHECK(Editor_ChordParse("rbracket", &chord), "join: rbracket parses");
        Editor_HotkeysSet(&g_hk, EDITOR_HK_BRUSH_STEP_UP, &chord, 1);
        Editor_HotkeysJoin(&g_hk, step, 2, out, (int)sizeof(out));
        CHECK(strcmp(out, "Shift+] ]") == 0, "join: Shift+] beside bare ] is said whole ('%s')", out);
        Editor_HotkeysResetDefaults(&g_hk);
        Editor_HotkeysJoin(&g_hk, step, 2, out, (int)sizeof(out));
        CHECK(strcmp(out, "Ctrl+[ ]") == 0 || strcmp(out, "Cmd+[ ]") == 0,
            "join: the default step pair is still compact ('%s')", out);
    }
    Editor_HotkeysSet(&g_hk, EDITOR_HK_BRUSH_RADIUS_DOWN, NULL, 0);
    Editor_HotkeysJoin(&g_hk, radius, 2, out, (int)sizeof(out));
    CHECK(strcmp(out, "]") == 0, "join: an unbound action is skipped ('%s')", out);
    Editor_HotkeysSet(&g_hk, EDITOR_HK_BRUSH_RADIUS_UP, NULL, 0);
    Editor_HotkeysJoin(&g_hk, radius, 2, out, (int)sizeof(out));
    CHECK(out[0] == '\0', "join: none bound is '' ('%s')", out);
}

static void
test_contexts_from(void)
{
    struct Editor_HotkeyContextFacts facts;
    unsigned contexts;

    memset(&facts, 0, sizeof(facts));
    facts.tool = HINT_TOOL(EDITOR_HK_TOOL_SELECT);
    contexts = Editor_HotkeyContextsFrom(&facts);
    CHECK(contexts == (EDITOR_HK_CTX_G | EDITOR_HK_CTX_N | EDITOR_HK_CTX_D),
        "contexts: Select, nothing selected is G and N, on the default camera scheme D (%#x)", contexts);
    facts.pointer_in_world = 1;
    facts.loc_count = 2;
    contexts = Editor_HotkeyContextsFrom(&facts);
    CHECK(contexts == (EDITOR_HK_CTX_G | EDITOR_HK_CTX_W | EDITOR_HK_CTX_L | EDITOR_HK_CTX_M | EDITOR_HK_CTX_D),
        "contexts: Select with locs over the world is G W L M D (%#x)", contexts);
    facts.camera_unity = 1;
    contexts = Editor_HotkeyContextsFrom(&facts);
    CHECK((contexts & EDITOR_HK_CTX_U) && !(contexts & EDITOR_HK_CTX_D), "contexts: the Unity scheme is U, not D (%#x)",
        contexts);
    facts.camera_unity = 0;
    facts.tool = HINT_TOOL(EDITOR_HK_TOOL_MOVE);
    CHECK(Editor_HotkeyContextsFrom(&facts) & EDITOR_HK_CTX_M, "contexts: Move nudges too");
    facts.tool = HINT_TOOL(EDITOR_HK_TOOL_PLACE);
    facts.loc_pick = 1;
    contexts = Editor_HotkeyContextsFrom(&facts);
    CHECK((contexts & EDITOR_HK_CTX_N) && !(contexts & EDITOR_HK_CTX_M) && !(contexts & EDITOR_HK_CTX_P),
        "contexts: Place with locs selected has no nudges (M) and no P (%#x)", contexts);
    facts.loc_count = 0;
    contexts = Editor_HotkeyContextsFrom(&facts);
    CHECK((contexts & EDITOR_HK_CTX_P) && !(contexts & EDITOR_HK_CTX_L), "contexts: Place with a loc pick is P (%#x)",
        contexts);
    facts.loc_pick = 0;
    CHECK(!(Editor_HotkeyContextsFrom(&facts) & EDITOR_HK_CTX_P), "contexts: no pick, no P");
    facts.spawn_count = 1;
    facts.rect_valid = 1;
    contexts = Editor_HotkeyContextsFrom(&facts);
    CHECK((contexts & EDITOR_HK_CTX_S) && (contexts & EDITOR_HK_CTX_R), "contexts: spawns are S, a rectangle R (%#x)",
        contexts);
    facts.loc_count = 1;
    CHECK(!(Editor_HotkeyContextsFrom(&facts) & EDITOR_HK_CTX_S), "contexts: a loc selection excludes S");
    /* Every mask the helper makes is one the table's exclusions allow. */
    for( int tool = 0; tool < EDITOR_HK_HINT_TOOLS; tool++ )
        for( int bits = 0; bits < 32; bits++ )
        {
            facts.tool = tool;
            facts.pointer_in_world = bits & 1;
            facts.loc_count = (bits >> 1) & 1;
            facts.spawn_count = (bits >> 2) & 1;
            facts.loc_pick = (bits >> 3) & 1;
            facts.rect_valid = (bits >> 4) & 1;
            contexts = Editor_HotkeyContextsFrom(&facts);
            CHECK(!((contexts & EDITOR_HK_CTX_M) && (contexts & EDITOR_HK_CTX_N))
                      && !((contexts & EDITOR_HK_CTX_L) && (contexts & (EDITOR_HK_CTX_S | EDITOR_HK_CTX_P))),
                "contexts: tool %d facts %#x make an allowed mask (%#x)", tool, bits, contexts);
        }
}

#undef HINT_TOOL

/* Workbench 6.3's two chords (design section 7): Alt+A selects every loc of
 * the hovered loc's id (the double-click's chord), and Shift held keeps a
 * Place drag to one axis -- a qualifier, beside Fly faster on the same Shift
 * without a clash, while an EDGE action on a bare Shift still clashes. */
static void
test_placement_chords(void)
{
    unsigned const OVER_WORLD = EDITOR_HK_CTX_G | EDITOR_HK_CTX_W | EDITOR_HK_CTX_N;
    unsigned const LOCS = EDITOR_HK_CTX_G | EDITOR_HK_CTX_L | EDITOR_HK_CTX_M;
    struct Editor_HotkeyFrame frame;
    struct Editor_Chord chord;
    int pairs[8];

    Editor_HotkeysInit(&g_hk);
    CHECK(Editor_HotkeyFind("edit.select_same") == EDITOR_HK_EDIT_SELECT_SAME
              && Editor_HotkeyFind("place.constrain") == EDITOR_HK_PLACE_CONSTRAIN,
        "placement chords: the two ids are in the table");
    CHECK(fire("alt+a", OVER_WORLD) == EDITOR_HK_EDIT_SELECT_SAME,
        "placement chords: Alt+A over the World is select same");
    CHECK(fire("alt+a", LOCS) == EDITOR_HK_EDIT_SELECT_SAME,
        "placement chords: Alt+A with a selection (the primary's id) is select same");
    CHECK(fire("alt+a", EDITOR_HK_CTX_G | EDITOR_HK_CTX_N) == -1,
        "placement chords: with no loc to point at or selected, Alt+A is nothing");
    CHECK(fire("a", OVER_WORLD) == -1 && fire("ctrl+a", OVER_WORLD) == EDITOR_HK_EDIT_SELECT_SQUARE,
        "placement chords: A alone stays the camera's, Ctrl+A the square's");

    /* Held Shift over the World: both qualifiers active, nothing fired. */
    input_clear(1000);
    g_in.held[TORIRSK_SHIFT] = 1;
    g_in.contexts = OVER_WORLD;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.active[EDITOR_HK_PLACE_CONSTRAIN] && frame.active[EDITOR_HK_CAM_FAST] && frame.count == 0,
        "placement chords: Shift held over the World constrains a drag (and is Fly faster), firing nothing");
    g_in.contexts = EDITOR_HK_CTX_G | EDITOR_HK_CTX_N;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(!frame.active[EDITOR_HK_PLACE_CONSTRAIN], "placement chords: off the World nothing is constrained");
    g_in.held[TORIRSK_CTRL] = 1;
    g_in.contexts = OVER_WORLD;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(!frame.active[EDITOR_HK_PLACE_CONSTRAIN], "placement chords: Ctrl+Shift is not the constrain chord");

    CHECK(Editor_HotkeyIsQualifier(EDITOR_HK_PLACE_CONSTRAIN) && Editor_HotkeyIsQualifier(EDITOR_HK_CAM_FAST)
              && Editor_HotkeyIsQualifier(EDITOR_HK_VIEW_SNAP_HOLD) && !Editor_HotkeyIsQualifier(EDITOR_HK_HOLD_PLACE)
              && !Editor_HotkeyIsQualifier(EDITOR_HK_EDIT_SELECT_SAME),
        "placement chords: the qualifiers are Fly faster, Snap while dragging and Constrain");
    CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_PLACE_CONSTRAIN) == -1
              && Editor_HotkeysClash(&g_hk, EDITOR_HK_CAM_FAST) == -1,
        "placement chords: two qualifiers on Shift do not clash");
    /* A non-qualifier on bare Shift in a live context still does. */
    CHECK(Editor_ChordParse("shift", &chord), "placement chords: shift parses");
    Editor_HotkeysSet(&g_hk, EDITOR_HK_EDIT_SELECT_SAME, &chord, 1);
    CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_EDIT_SELECT_SAME) >= 0
              && Editor_HotkeysConflicts(&g_hk, pairs, 4) > 0,
        "placement chords: an edge action rebound to bare Shift clashes with them");
    /* And a qualifier rebound onto an ordinary chord clashes like any action. */
    Editor_HotkeysResetDefaults(&g_hk);
    CHECK(Editor_ChordParse("alt+a", &chord), "placement chords: alt+a parses");
    Editor_HotkeysSet(&g_hk, EDITOR_HK_PLACE_CONSTRAIN, &chord, 1);
    CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_PLACE_CONSTRAIN) == EDITOR_HK_EDIT_SELECT_SAME,
        "placement chords: Constrain on Alt+A clashes with select same");
    Editor_HotkeysResetDefaults(&g_hk);
    CHECK(Editor_HotkeysConflicts(&g_hk, pairs, 4) == 0, "placement chords: the reset table is conflict-free");
}

/* The picker context (workbench 6.4): K is "the picker has focus", and
 * while it is the well's keys are the K rows alone -- the arrows move the
 * focus, Return opens, Shift+Return adds, Backspace goes back, Escape clears
 * Find -- with no clash against the nudges, the camera's arrows, Apply,
 * Place, Delete or Deselect, which share those chords in the World. */
static void
test_picker_context(void)
{
    static const struct
    {
        const char* chord;
        int action;
    } k_picker[] = {
        { "up", EDITOR_HK_PICKER_NORTH },     { "down", EDITOR_HK_PICKER_SOUTH },
        { "left", EDITOR_HK_PICKER_WEST },    { "right", EDITOR_HK_PICKER_EAST },
        { "return", EDITOR_HK_PICKER_OPEN },  { "shift+return", EDITOR_HK_PICKER_ADD },
        { "backspace", EDITOR_HK_PICKER_BACK }, { "escape", EDITOR_HK_PICKER_CLEAR_FIND },
    };
    struct Editor_Chord chord;
    char text[64];
    int pairs[64];

    Editor_HotkeysInit(&g_hk);
    CHECK(Editor_HotkeysConflicts(&g_hk, pairs, 32) == 0, "picker: the K rows clash with nothing");
    for( unsigned bit = 0; bit < EDITOR_HK_CTX_COUNT; bit++ )
        CHECK(!Editor_HotkeyContextsOverlap(EDITOR_HK_CTX_K, 1u << bit) == ((1u << bit) != EDITOR_HK_CTX_K),
            "picker: K overlaps context bit %u only when it is K", bit);
    CHECK(EDITOR_HK_CTX_ALL & EDITOR_HK_CTX_K, "picker: ALL holds K");
    Editor_HotkeyContextText(EDITOR_HK_CTX_K, text, (int)sizeof(text));
    CHECK(strcmp(text, "K") == 0, "picker: the panel prints K ('%s')", text);
    for( size_t i = 0; i < sizeof(k_picker) / sizeof(k_picker[0]); i++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo((enum Editor_HotkeyAction)k_picker[i].action);
        CHECK(info->contexts == EDITOR_HK_CTX_K, "picker: %s lives in K alone", info->id);
        CHECK(strcmp(info->group, "Picker") == 0, "picker: %s is in the Picker table", info->id);
        CHECK(Editor_ChordParse(k_picker[i].chord, &chord), "picker: %s parses", k_picker[i].chord);
        CHECK(Editor_HotkeysMatch(&g_hk, chord, EDITOR_HK_CTX_K) == k_picker[i].action,
            "picker: %s is %s in K (%d)", k_picker[i].chord, info->id, Editor_HotkeysMatch(&g_hk, chord, EDITOR_HK_CTX_K));
        CHECK(Editor_HotkeysMatch(&g_hk, chord, EDITOR_HK_CTX_ALL & ~EDITOR_HK_CTX_K) != k_picker[i].action,
            "picker: %s is not the picker's outside K", k_picker[i].chord);
        /* The App's resolver never sees K: the World keeps its chords. */
        CHECK(fire(k_picker[i].chord, EDITOR_HK_CTX_G | EDITOR_HK_CTX_N) != k_picker[i].action,
            "picker: %s resolves to the World's action in G N", k_picker[i].chord);
    }
    CHECK(Editor_ChordParse("ctrl+up", &chord), "picker: ctrl+up parses");
    CHECK(Editor_HotkeysMatch(&g_hk, chord, EDITOR_HK_CTX_G | EDITOR_HK_CTX_M) == EDITOR_HK_EDIT_NUDGE_N,
        "picker: Ctrl+Up still nudges in M");
    CHECK(Editor_ChordParse("return", &chord), "picker: return parses");
    CHECK(Editor_HotkeysMatch(&g_hk, chord, EDITOR_HK_CTX_G | EDITOR_HK_CTX_L) == EDITOR_HK_EDIT_APPLY,
        "picker: Return still applies the tool in L");
    /* A rebind re-routes the well: Enter moves to O. */
    CHECK(Editor_ChordParse("o", &chord), "picker: o parses");
    Editor_HotkeysSet(&g_hk, EDITOR_HK_PICKER_OPEN, &chord, 1);
    CHECK(Editor_HotkeysMatch(&g_hk, chord, EDITOR_HK_CTX_K) == EDITOR_HK_PICKER_OPEN, "picker: rebound to O");
    CHECK(Editor_ChordParse("return", &chord), "picker: return parses again");
    CHECK(Editor_HotkeysMatch(&g_hk, chord, EDITOR_HK_CTX_K) == -1, "picker: Return no longer opens");
    Editor_HotkeysResetDefaults(&g_hk);
    {
        unsigned contexts;
        struct Editor_HotkeyContextFacts facts;
        memset(&facts, 0, sizeof(facts));
        contexts = Editor_HotkeyContextsFrom(&facts);
        CHECK(!(contexts & EDITOR_HK_CTX_K), "picker: the App's facts never make K (%#x)", contexts);
    }
}

/* Workbench 6.8's chord moves (design section 15 "Resolved collisions"): the
 * level pins on Alt+0..5 with Ctrl+0 and Ctrl+5 kept one release and a note
 * when they fire, the workspaces on Ctrl+1..4, Ctrl+K, Ctrl+Shift+K,
 * Ctrl+Shift+N, Ctrl+L in the graph's context Q, the reserved Alt layer keys,
 * the keycap spelling, and the one-slot rebind -- the defaults still free of
 * any clash with every new id and both aliases. */
static void
test_workbench68(void)
{
    static const struct
    {
        int action;
        const char* id;
        const char* first;
        const char* second;
        unsigned contexts;
    } k_new[] = {
        { EDITOR_HK_LEVEL_PIN0, "level.pin0", "alt+0", "ctrl+0", EDITOR_HK_CTX_G },
        { EDITOR_HK_LEVEL_PIN1, "level.pin1", "alt+1", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_LEVEL_PIN2, "level.pin2", "alt+2", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_LEVEL_PIN3, "level.pin3", "alt+3", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_LEVEL_AUTO, "level.auto", "alt+4", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_LEVEL_ABOVE, "level.above", "alt+5", "ctrl+5", EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_WORKSPACE_BUILD, "view.workspace_build", "ctrl+1", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_WORKSPACE_TERRAIN, "view.workspace_terrain", "ctrl+2", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_WORKSPACE_SCRIPT, "view.workspace_script", "ctrl+3", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_WORKSPACE_PLAYTEST, "view.workspace_playtest", "ctrl+4", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_COMMAND_PALETTE, "view.command_palette", "ctrl+k", "alt+space", EDITOR_HK_CTX_G },
        { EDITOR_HK_FILE_SAVE_SCENE, "file.save_scene", "ctrl+shift+k", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_FILE_SCRATCH_LANE, "file.scratch_lane", "ctrl+shift+n", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_GRAPH_AUTO_LAYOUT, "graph.auto_layout", "ctrl+l", NULL, EDITOR_HK_CTX_Q },
        { EDITOR_HK_VIEW_LAYER_LOCS, "view.layer_locs", "alt+l", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_LAYER_NPCS, "view.layer_npcs", "alt+n", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_LAYER_OBJS, "view.layer_objs", "alt+o", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_LAYER_COLLISION, "view.layer_collision", "alt+c", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_VIEW_LAYER_GRID, "view.layer_grid", "alt+g", NULL, EDITOR_HK_CTX_G },
        { EDITOR_HK_EDIT_LEVEL_UP, "edit.level_up", "ctrl+pageup", NULL, EDITOR_HK_CTX_L },
        { EDITOR_HK_EDIT_LEVEL_DOWN, "edit.level_down", "ctrl+pagedown", NULL, EDITOR_HK_CTX_L },
    };
    unsigned const G = EDITOR_HK_CTX_G;
    unsigned const W = EDITOR_HK_CTX_W;
    unsigned const L = EDITOR_HK_CTX_L;
    unsigned const S = EDITOR_HK_CTX_S;
    unsigned const N = EDITOR_HK_CTX_N;
    int pairs[64];
    char note[160];
    char text[64];
    struct Editor_Chord chord;

    Editor_HotkeysInit(&g_hk);
    for( size_t i = 0; i < sizeof(k_new) / sizeof(k_new[0]); i++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(k_new[i].action);
        CHECK(Editor_HotkeyFind(k_new[i].id) == k_new[i].action, "6.8: %s is in the table", k_new[i].id);
        CHECK(info->defaults[0] && strcmp(info->defaults[0], k_new[i].first) == 0, "6.8: %s defaults to %s (%s)",
            k_new[i].id, k_new[i].first, info->defaults[0] ? info->defaults[0] : "none");
        CHECK(k_new[i].second ? info->defaults[1] && strcmp(info->defaults[1], k_new[i].second) == 0
                              : info->defaults[1] == NULL,
            "6.8: %s's second chord is %s", k_new[i].id, k_new[i].second ? k_new[i].second : "none");
        CHECK(info->contexts == k_new[i].contexts, "6.8: %s lives in its context", k_new[i].id);
        CHECK(Editor_HotkeysClash(&g_hk, k_new[i].action) < 0, "6.8: %s clashes with nothing", k_new[i].id);
    }
    CHECK(Editor_HotkeysConflicts(&g_hk, pairs, 32) == 0, "6.8: the defaults, new ids and aliases included, are clash-free");

    /* What fires where. */
    CHECK(fire("ctrl+1", G) == EDITOR_HK_VIEW_WORKSPACE_BUILD, "6.8: Ctrl+1 is the Build workspace, not a pin");
    CHECK(fire("ctrl+4", G) == EDITOR_HK_VIEW_WORKSPACE_PLAYTEST, "6.8: Ctrl+4 is Playtest, not Level Auto");
    CHECK(fire("alt+1", G) == EDITOR_HK_LEVEL_PIN1, "6.8: Alt+1 pins level 1");
    CHECK(fire("alt+4", G) == EDITOR_HK_LEVEL_AUTO, "6.8: Alt+4 is Level Auto");
    CHECK(fire("ctrl+0", G) == EDITOR_HK_LEVEL_PIN0, "6.8: the old Ctrl+0 still pins level 0");
    CHECK(fire("ctrl+5", G) == EDITOR_HK_LEVEL_ABOVE, "6.8: the old Ctrl+5 still shows above");
    CHECK(fire("ctrl+k", G) == EDITOR_HK_VIEW_COMMAND_PALETTE, "6.8: Ctrl+K opens the palette");
    CHECK(fire("ctrl+shift+k", G) == EDITOR_HK_FILE_SAVE_SCENE && fire("ctrl+shift+n", G) == EDITOR_HK_FILE_SCRATCH_LANE,
        "6.8: Ctrl+Shift+K saves the scene, Ctrl+Shift+N a scratch lane");
    CHECK(fire("ctrl+n", G) == EDITOR_HK_FILE_NEW, "6.8: Ctrl+N is still New square");
    CHECK(fire("ctrl+l", G | W | N) == -1, "6.8: Ctrl+L does nothing outside the graph");
    CHECK(fire("ctrl+l", G | EDITOR_HK_CTX_Q) == EDITOR_HK_GRAPH_AUTO_LAYOUT, "6.8: Ctrl+L lays the graph out in Q");
    CHECK(fire("l", G | W) == EDITOR_HK_HOLD_PLACE || fire("l", G | W) == -1, "6.8: a bare L is still the hold key's");
    CHECK(fire("alt+g", G) == EDITOR_HK_VIEW_LAYER_GRID && fire("g", G) == EDITOR_HK_PLACE_FIT,
        "6.8: Alt+G is the Grid layer, G still Fit");
    CHECK(fire("ctrl+pageup", G | L) == EDITOR_HK_EDIT_LEVEL_UP, "6.8: Ctrl+PgUp moves the selection up a level");

    /* Q from the facts: a GRAPH holds the focus. */
    {
        struct Editor_HotkeyContextFacts facts;
        memset(&facts, 0, sizeof(facts));
        CHECK(!(Editor_HotkeyContextsFrom(&facts) & EDITOR_HK_CTX_Q), "6.8: no graph focus, no Q");
        facts.graph_focus = 1;
        CHECK(Editor_HotkeyContextsFrom(&facts) & EDITOR_HK_CTX_Q, "6.8: a focused graph is Q");
        CHECK(!Editor_HotkeyContextsOverlap(EDITOR_HK_CTX_Q, EDITOR_HK_CTX_K)
                  && Editor_HotkeyContextsOverlap(EDITOR_HK_CTX_Q, G),
            "6.8: Q overlaps G but not the picker's K");
    }

    /* The console note of a retired chord. */
    (void)Editor_ChordParse("ctrl+0", &chord);
    CHECK(Editor_HotkeyRetiredNote(&g_hk, EDITOR_HK_LEVEL_PIN0, chord, note, (int)sizeof(note)) == 1
              && strstr(note, "Ctrl+0") && strstr(note, "Alt+0") && strstr(note, "next release"),
        "6.8: Ctrl+0 firing says it is the old chord and names Alt+0 (%s)", note);
    (void)Editor_ChordParse("alt+0", &chord);
    CHECK(Editor_HotkeyRetiredNote(&g_hk, EDITOR_HK_LEVEL_PIN0, chord, note, (int)sizeof(note)) == 0 && !note[0],
        "6.8: the new chord says nothing");
    (void)Editor_ChordParse("ctrl+5", &chord);
    CHECK(Editor_HotkeyRetiredNote(&g_hk, EDITOR_HK_LEVEL_ABOVE, chord, note, (int)sizeof(note)) == 1
              && strstr(note, "Alt+5"),
        "6.8: so does Ctrl+5 for Show above");
    CHECK(Editor_HotkeyRetiredNote(&g_hk, EDITOR_HK_LEVEL_PIN0, chord, note, (int)sizeof(note)) == 0,
        "6.8: another action's retired chord is not this one's");
    {
        struct Editor_Chord rebound;
        (void)Editor_ChordParse("alt+9", &rebound);
        Editor_HotkeysSetSlot(&g_hk, EDITOR_HK_LEVEL_PIN0, 0, rebound);
        (void)Editor_ChordParse("ctrl+0", &chord);
        CHECK(Editor_HotkeyRetiredNote(&g_hk, EDITOR_HK_LEVEL_PIN0, chord, note, (int)sizeof(note)) == 1
                  && strstr(note, "Alt+9"),
            "6.8: the note names the chord the first slot holds now (%s)", note);
        Editor_HotkeysResetDefaults(&g_hk);
    }

    /* One slot at a time. */
    {
        struct Editor_Chord y;
        struct Editor_Chord none = { 0, 0 };
        (void)Editor_ChordParse("ctrl+alt+y", &y);
        Editor_HotkeysSetSlot(&g_hk, EDITOR_HK_EDIT_REDO, 1, y);
        Editor_ChordFormat(g_hk.bind[EDITOR_HK_EDIT_REDO].chord[0], text, (int)sizeof(text));
        CHECK(strcmp(text, "ctrl+shift+z") == 0, "6.8: binding slot 2 keeps slot 1");
        Editor_ChordFormat(g_hk.bind[EDITOR_HK_EDIT_REDO].chord[1], text, (int)sizeof(text));
        CHECK(strcmp(text, "ctrl+alt+y") == 0, "6.8: and slot 2 is the new chord");
        Editor_HotkeysSetSlot(&g_hk, EDITOR_HK_EDIT_REDO, 0, y);
        CHECK(g_hk.bind[EDITOR_HK_EDIT_REDO].chord[0].key == y.key && g_hk.bind[EDITOR_HK_EDIT_REDO].chord[1].key == 0,
            "6.8: the same chord in the other slot is one copy");
        Editor_HotkeysSetSlot(&g_hk, EDITOR_HK_EDIT_DELETE, 0, none);
        Editor_ChordFormat(g_hk.bind[EDITOR_HK_EDIT_DELETE].chord[0], text, (int)sizeof(text));
        CHECK(strcmp(text, "backspace") == 0 && g_hk.bind[EDITOR_HK_EDIT_DELETE].chord[1].key == 0,
            "6.8: unbinding slot 1 moves slot 2 up (%s)", text);
        Editor_HotkeysResetDefaults(&g_hk);
    }

    /* The keycap spelling. */
    (void)Editor_ChordParse("ctrl+shift+z", &chord);
    Editor_ChordSpell(chord, 0, 0, text, (int)sizeof(text));
    CHECK(strcmp(text, "Ctrl+Shift+Z") == 0, "6.8: words elsewhere (%s)", text);
    Editor_ChordSpell(chord, 1, 0, text, (int)sizeof(text));
    CHECK(strcmp(text, "Cmd+Shift+Z") == 0, "6.8: Apple words without the glyphs (%s)", text);
    Editor_ChordSpell(chord, 1, 1, text, (int)sizeof(text));
    CHECK(strcmp(text, "\xe2\x87\xa7\xe2\x8c\x98Z") == 0, "6.8: Apple symbols run together (%s)", text);
    (void)Editor_ChordParse("alt+up", &chord);
    Editor_ChordSpell(chord, 0, 1, text, (int)sizeof(text));
    CHECK(strcmp(text, "Alt+\xe2\x86\x91") == 0, "6.8: an arrow as its glyph (%s)", text);
    Editor_ChordSpell(chord, 1, 1, text, (int)sizeof(text));
    CHECK(strcmp(text, "\xe2\x8c\xa5\xe2\x86\x91") == 0, "6.8: Option, then the arrow (%s)", text);
    (void)Editor_ChordParse("backspace", &chord);
    Editor_ChordSpell(chord, 0, 1, text, (int)sizeof(text));
    CHECK(strcmp(text, "\xe2\x8c\xab") == 0, "6.8: Backspace as its glyph");
    Editor_ChordSpell(chord, 0, 0, text, (int)sizeof(text));
    CHECK(strcmp(text, "Backspace") == 0, "6.8: and its word without");
    (void)Editor_ChordParse("ctrl+shift+z", &chord);
    Editor_ChordDisplay(chord, text, (int)sizeof(text));
    CHECK(EDITOR_HK_SMALL_FACE_GLYPHS == 0 && strstr(text, "Shift+Z") != NULL,
        "6.8: the build's keycaps keep the words (the Small face has no symbols) (%s)", text);

    /* The doc's letters. */
    Editor_HotkeyContextLetters(L | S, text, (int)sizeof(text));
    CHECK(strcmp(text, "L, S") == 0, "6.8: the context letters (%s)", text);
    Editor_HotkeyContextLetters(EDITOR_HK_CTX_Q, text, (int)sizeof(text));
    CHECK(strcmp(text, "Q") == 0, "6.8: Q's letter");
}

/* Every action's ini id and its row are in the user's reference. */
static void
test_doc(const char* path)
{
    FILE* f = fopen(path, "rb");
    char* text;
    long size;

    CHECK(f != NULL, "the doc %s opens", path);
    if( !f )
        return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)size + 1);
    assert(text);
    CHECK(fread(text, 1, (size_t)size, f) == (size_t)size, "the doc reads");
    fclose(f);
    text[size] = '\0';
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        char quoted[64];
        snprintf(quoted, sizeof(quoted), "`%s`", info->id);
        CHECK(strstr(text, quoted) != NULL, "the doc lists %s", quoted);
        CHECK(strstr(text, info->doc) != NULL, "the doc has %s's row \"%s\"", info->id, info->doc);
    }
    free(text);
}

/* The ergonomics additions (2026-11-03): the palette's (workbench 6.8's
 * Ctrl+K since the merge) and Go to tile's chords, the camera schemes as contexts, and the recent-squares file. */
static void
test_ergonomics(void)
{
    struct Editor_Chord chord;
    struct Editor_HotkeyFrame frame;
    struct Editor_Recent recent;
    struct Editor_Recent again;
    char* data = NULL;
    int size = 0;
    int mx;
    int mz;
    char storage[EDITOR_DOC_STORAGE_MAX];
    unsigned const G = EDITOR_HK_CTX_G | EDITOR_HK_CTX_N;

    Editor_HotkeysInit(&g_hk);
    CHECK(Editor_HotkeyFind("view.command_palette") == EDITOR_HK_VIEW_COMMAND_PALETTE
              && Editor_HotkeyForPanel("cmdpalette") == EDITOR_HK_VIEW_COMMAND_PALETTE,
        "view.command_palette names the cmdpalette module");
    CHECK(Editor_ChordParse(Editor_HotkeyInfo(EDITOR_HK_VIEW_COMMAND_PALETTE)->defaults[0], &chord)
              && chord.key == TORIRSK_K && chord.mods == EDITOR_HK_MOD_CTRL,
        "the palette is Ctrl+K");
    CHECK(Editor_ChordParse(Editor_HotkeyInfo(EDITOR_HK_VIEW_GOTO)->defaults[0], &chord) && chord.key == TORIRSK_G
              && chord.mods == (EDITOR_HK_MOD_CTRL | EDITOR_HK_MOD_SHIFT),
        "Go to tile... is Ctrl+Shift+G (Ctrl+G stays Snap)");
    CHECK(fire("ctrl+k", G) == EDITOR_HK_VIEW_COMMAND_PALETTE, "Ctrl+K fires the palette");
    CHECK(fire("alt+space", G) == EDITOR_HK_VIEW_COMMAND_PALETTE, "Alt+Space (Option+Space) fires the palette");
    CHECK(fire("ctrl+shift+g", G) == EDITOR_HK_VIEW_GOTO && fire("ctrl+g", G) == EDITOR_HK_VIEW_SNAP,
        "Ctrl+Shift+G is Go to tile, Ctrl+G still Snap");
    CHECK(fire("ctrl+shift+l", G) == EDITOR_HK_EDIT_SELECT_KIND && fire("ctrl+i", G) == EDITOR_HK_EDIT_INVERT,
        "Ctrl+Shift+L selects the kind, Ctrl+I inverts");

    /* F: fly down under the default scheme (held, no edge), frame under
     * Unity (an edge, nothing held). A caller stating no scheme is on the
     * default one. */
    input_clear(1000);
    press("f");
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.count == 0 && frame.active[EDITOR_HK_CAM_DOWN] && !frame.active[EDITOR_HK_CAM_FRAME_KEY],
        "F with no scheme stated flies down (the default scheme is implied)");
    input_clear(1000);
    press("f");
    g_in.contexts = G | EDITOR_HK_CTX_D;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.count == 0 && frame.active[EDITOR_HK_CAM_DOWN], "F under D flies down");
    input_clear(1000);
    press("f");
    g_in.contexts = G | EDITOR_HK_CTX_U;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(frame.count == 1 && frame.fired[0] == EDITOR_HK_CAM_FRAME_KEY && !frame.active[EDITOR_HK_CAM_DOWN],
        "F under U frames the selection and does not fly");
    CHECK(fire("r", G | EDITOR_HK_CTX_U) == -1 && fire("home", G | EDITOR_HK_CTX_U) == EDITOR_HK_CAM_FRAME,
        "R still flies up (held) and Home still frames under U");
    CHECK(!Editor_HotkeyContextsOverlap(EDITOR_HK_CTX_D, EDITOR_HK_CTX_U), "D and U never overlap");
    CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_CAM_FRAME_KEY) < 0, "F under U clashes with nothing");

    /* editor_recent.ini: the ring (newest first, once each, ten at most)
     * and the scheme roundtrip. */
    Editor_RecentInit(&recent);
    Editor_RecentNote(&recent, 50, 50, "");
    Editor_RecentNote(&recent, 50, 51, "maps");
    Editor_RecentNote(&recent, 50, 50, "ported/testlane");
    Editor_RecentNote(&recent, 50, 50, "");
    CHECK(recent.count == 3 && strcmp(recent.keys[0], "m50_50") == 0 && strcmp(recent.keys[1], "ported/testlane/m50_50") == 0
              && strcmp(recent.keys[2], "m50_51") == 0,
        "the ring is newest first, a square once (%d: %s, %s, %s)", recent.count, recent.keys[0], recent.keys[1],
        recent.keys[2]);
    for( int i = 0; i < 20; i++ )
        Editor_RecentNote(&recent, 60 + i, 60, "");
    CHECK(recent.count == EDITOR_RECENT_MAX && strcmp(recent.keys[0], "m79_60") == 0, "the ring holds %d",
        EDITOR_RECENT_MAX);
    Editor_RecentSetScheme(&recent, EDITOR_CAMERA_SCHEME_UNITY);
    Editor_RecentEncode(&recent, &data, &size);
    CHECK(data && strstr(data, "[recent]\nversion = 1\nsquare = m79_60\n") && strstr(data, "[camera]\nscheme = unity\n"),
        "the file lists the ring and the scheme");
    CHECK(Editor_RecentDecode(&again, data, size) == EDITOR_RECENT_MAX && again.count == recent.count
              && again.camera_scheme == EDITOR_CAMERA_SCHEME_UNITY && strcmp(again.keys[0], recent.keys[0]) == 0
              && strcmp(again.keys[9], recent.keys[9]) == 0,
        "the file decodes to the same ring and scheme");
    ToriMem_Free(ToriMem_Process(), data);
    CHECK(Editor_RecentDecode(&again, "[recent]\nsquare = nonsense\nsquare = ported/x/m1_2\n[camera]\nscheme = zork\n", 62)
                  == 1
              && again.count == 1 && again.camera_scheme == EDITOR_CAMERA_SCHEME_DEFAULT
              && Editor_RecentEntry(&again, 0, &mx, &mz, storage, (int)sizeof(storage)) && mx == 1 && mz == 2
              && strcmp(storage, "ported/x") == 0,
        "a bad square or scheme word is skipped, a lane key parses (%d, %s)", again.count, again.keys[0]);
    CHECK(!Editor_RecentParseKey("m50", &mx, &mz, storage, (int)sizeof(storage))
              && !Editor_RecentParseKey("x50_50", &mx, &mz, storage, (int)sizeof(storage)),
        "a key is m<x>_<z>");
    CHECK(Editor_RecentPath() && strcmp(Editor_RecentPath(), EDITOR_RECENT_DEFAULT_PATH) == 0,
        "the file is editor_recent.ini beside the layout");
}

int
main(
    int argc,
    char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    test_defaults();
    test_chords();
    test_ini();
    test_resolve();
    test_ergonomics();
    test_hint();
    test_gesture_hint();
    test_join();
    test_contexts_from();
    test_placement_chords();
    test_picker_context();
    test_workbench68();
    test_doc(argc > 1 ? argv[1] : "../docs/MAPED_HOTKEYS.md");
    printf("editor_hotkeys_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
