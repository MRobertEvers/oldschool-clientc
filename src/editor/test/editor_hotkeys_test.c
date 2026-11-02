/*
 * The hotkey table (editor/editor_hotkeys.h): defaults, chords, the ini
 * codec, conflicts and the per-frame resolver, plus the doc check -- every
 * action's id and its row are in docs/MAPED_HOTKEYS.md.
 *
 *   ./editor_hotkeys_test <path to docs/MAPED_HOTKEYS.md>
 */

#include "editor/editor_hotkeys.h"
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
                       "tool.raise =\n"
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
    CHECK(g_hk.bind[EDITOR_HK_TOOL_RAISE].chord[0].key == TORIRSK_UNKNOWN, "tool.raise = is unbound");
    CHECK(g_hk.bind[EDITOR_HK_EDIT_REDO].chord[0].key == TORIRSK_Y, "redo's chords keep the file's order");
    CHECK(Editor_HotkeysIsDefault(&g_hk, EDITOR_HK_EDIT_UNDO) && Editor_HotkeysIsDefault(&g_hk, EDITOR_HK_FILE_NEW)
              && Editor_HotkeysIsDefault(&g_hk, EDITOR_HK_EDIT_COPY),
        "ignored lines leave their actions at the default");

    Editor_HotkeysEncode(&g_hk, &data, &size);
    CHECK(strstr(data, "file.bake = ctrl+b\n") && strstr(data, "tool.raise =\n")
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
    unsigned const LOCS = EDITOR_HK_CTX_G | EDITOR_HK_CTX_L;
    unsigned const SPAWNS = EDITOR_HK_CTX_G | EDITOR_HK_CTX_S | EDITOR_HK_CTX_N;

    Editor_HotkeysInit(&g_hk);
    CHECK(fire("ctrl+z", G) == EDITOR_HK_EDIT_UNDO, "Ctrl+Z is Undo");
    CHECK(fire("z", G) == -1, "bare Z is nothing");
    CHECK(fire("ctrl+shift+z", G) == EDITOR_HK_EDIT_REDO && fire("ctrl+y", G) == EDITOR_HK_EDIT_REDO,
        "both Redo chords");
    CHECK(fire("3", G) == EDITOR_HK_TOOL_RAISE, "3 arms Raise");
    CHECK(fire("ctrl+3", G) == EDITOR_HK_LEVEL_PIN3, "Ctrl+3 pins level 3");
    CHECK(fire("delete", LOCS) == EDITOR_HK_EDIT_DELETE, "Delete with locs selected");
    CHECK(fire("delete", SPAWNS) == EDITOR_HK_EDIT_DELETE, "Delete with a spawn selection");
    CHECK(fire("delete", G) == -1, "Delete with neither does nothing");
    CHECK(fire("shift+lbracket", G) == EDITOR_HK_BRUSH_STRENGTH_DOWN, "Shift+[ is strength -");
    CHECK(fire("lbracket", G) == EDITOR_HK_BRUSH_RADIUS_DOWN && fire("ctrl+rbracket", G) == EDITOR_HK_BRUSH_STEP_UP,
        "[ radius, Ctrl+] step");
    CHECK(fire("ctrl+c", G) == -1 && fire("ctrl+c", G | EDITOR_HK_CTX_R) == EDITOR_HK_EDIT_COPY,
        "Ctrl+C copies only with a rectangle");
    CHECK(fire("up", LOCS) == EDITOR_HK_EDIT_NUDGE_N, "Up nudges a selection");
    CHECK(fire("f1", G) == EDITOR_HK_VIEW_HOTKEYS, "F1 opens the Hotkeys panel");

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
    press("ctrl+w");
    g_in.contexts = G;
    Editor_HotkeysResolve(&g_hk, &g_in, &frame);
    CHECK(!frame.active[EDITOR_HK_CAM_FORWARD] && !frame.claimed[TORIRSK_W],
        "Ctrl+W is not claimed: the manifest's debug flight keeps it");
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
        (void)Editor_ChordParse("left", &chord);
        Editor_HotkeysSet(&g_hk, EDITOR_HK_EDIT_NUDGE_N, &chord, 1);
        CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_EDIT_NUDGE_N) == EDITOR_HK_EDIT_NUDGE_W,
            "nudge N on Left clashes with nudge W");
        CHECK(Editor_HotkeysClash(&g_hk, EDITOR_HK_CAM_TURN_LEFT) < 0,
            "but not with the camera's turn (no loc vs locs: exclusive)");
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
    test_doc(argc > 1 ? argv[1] : "../docs/MAPED_HOTKEYS.md");
    printf("editor_hotkeys_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
