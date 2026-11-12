/*
 * The Camera path panel (panels/edpanel_campath.c), headless: against the
 * real chrome model and an embedded ToriRSMapEd over a SYNTHETIC content
 * tree written under the directory given, with a World camera and a flat
 * ground standing in for the App's (three EditorUiAppServices members); no
 * cache, no OSRS-Content:
 *
 *   ./editor_campath_panel_test <tmp-content-dir>
 *
 *   paths      New path's refusals (a name no proc can have, a taken name);
 *              a new path is the document's `path` line under the header,
 *              one undo step; Rename; Delete path taking the header back
 *   keyframes  Add keyframe fits the World camera, writes the `key` line and
 *              puts the camera on the keyframe; the first cuts, the second
 *              glides and holds until it has arrived; Update keyframe; Go
 *              to; Move up / Move down; Delete keyframe; refusals with no
 *              keyframe selected
 *   the leg    Hold, Eye rate, Look rate by Enter; a typed field applied
 *              when it is left, not lost; Move and Speed; a refused value
 *              put back
 *   header     Coordinates and Template (typed, or the first keyframe's
 *              square), Wrap in a cutscene frame
 *   script     the proc the panel shows and copies
 *   preview    the camera flown on the chrome's clock -- cut to keyframe 1,
 *              glide to keyframe 2 -- and given back; Stop; no marks while
 *              it flies; the marks otherwise
 *   save       Save all writes editor/campaths.txt; Undo walks the edits
 *              back to no file text
 *   beside     Camera path and Triggers opened together, in either order:
 *              neither covers the other
 */

#include "editor/editor.h"
#include "editor/editor_campath.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
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
static struct Editor_CamPathWidgets g_w;
static int64_t g_now_ms = 1000;

/* ---- the World standing in for the App's ------------------------------------------- */

/** The camera, and the square of ground the fake world holds: m50_50. */
static struct Editor_CamEye g_eye;
static int g_eye_sets;

static int
fake_eye(
    void* user,
    struct Editor_CamEye* out)
{
    (void)user;
    *out = g_eye;
    return 1;
}

static int
fake_ground(
    void* user,
    int x,
    int z,
    int level,
    int* out_y)
{
    (void)user;
    (void)level;
    if( x < 3200 * 128 || z < 3200 * 128 || x >= 3264 * 128 || z >= 3264 * 128 )
        return 0;
    *out_y = 0;
    return 1;
}

static int
fake_eye_set(
    void* user,
    const struct Editor_CamEye* eye)
{
    int y;
    if( !fake_ground(user, eye->x, eye->z, 0, &y) )
        return 0;
    g_eye = *eye;
    g_eye_sets++;
    return 1;
}

/* ---- the harness ------------------------------------------------------------------ */

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

/** The file's text into `out`; 0 when there is no such file. */
static int
read_file(
    const char* root,
    const char* relative,
    char* out,
    size_t out_size)
{
    char path[2048];
    FILE* f;
    size_t n;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    f = fopen(path, "rb");
    if( !f )
        return 0;
    n = fread(out, 1, out_size - 1, f);
    out[n] = '\0';
    fclose(f);
    return 1;
}

static void
write_fixture(const char* root)
{
    static const char* const dirs[] = { "", "configs", "maps", "pack", "editor", "server", "server/scripts",
        "server/scripts/areas", "server/scripts/areas/edited", "server/scripts/areas/edited/configs" };
    for( size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++ )
        make_dir(root, dirs[i]);
    write_text(root, "configs/all.loc", "[test_wall]\nname=Wall\n");
    write_text(root, "configs/all.loc.compack", "1902=test_wall\n");
    write_text(root, "maps/m50_50.jm2", "==== MAP ====\n0 10 10: h5 u2\n");
    write_text(root, "maps/m50_50.jl2", "==== LOC ====\n0 10 10: 1902 0 1\n");
}

/** One frame, the chrome's clock `ms` further on. */
static void
frame_ms(int ms)
{
    g_now_ms += ms;
    ToriRSChrome_Tick(&g_chrome, g_now_ms);
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
frame(void)
{
    frame_ms(0);
}

static void
activate(int widget)
{
    assert(widget >= 0);
    g_chrome.activated = widget;
    frame();
}

/** Type `text` into a field and press Enter. */
static void
enter(
    int widget,
    const char* text)
{
    ToriRSChrome_SetText(&g_chrome, widget, text);
    activate(widget);
}

static void
choose(
    int dropdown,
    int option)
{
    ToriRSChrome_DropdownSetSelected(&g_chrome, dropdown, option);
    activate(dropdown);
}

/** Click row `row` of the keyframe list. */
static void
pick_row(int row)
{
    ToriRSChrome_VListSetSelected(&g_chrome, g_w.list_keys, row);
    activate(g_w.list_keys);
}

/** 1 when one of the console's newest lines holds `text`. */
static int
said(const char* text)
{
    for( int i = 0; i < 6; i++ )
    {
        const char* line = Editor_UiConsoleLine(&g_ui, i);
        if( line && strstr(line, text) )
            return 1;
    }
    return 0;
}

static int
groups(void)
{
    return Editor_GetHistory(&g_editor)->applied;
}

/** The document's text ("" when it is not open). */
static const char*
doc_text(void)
{
    struct Editor_DocHandle handle;
    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_TEXT, EDITOR_CAMPATHS_DOC_KEY, &handle) || !handle.text
        || !handle.text->text )
        return "";
    return handle.text->text;
}

static const char*
text_of(int widget)
{
    const char* text = ToriRSChrome_Text(&g_chrome, widget);
    return text ? text : "";
}

/** The camera ten tiles south of 3222,3218 and ten tiles up, looking north
 *  at 45 degrees: editor_campath_test.c's hand-computed case. */
static void
eye_north(void)
{
    g_eye.x = 3222 * 128 + 64;
    g_eye.y = -1280;
    g_eye.z = 3208 * 128 + 64;
    g_eye.yaw = 0;
    g_eye.pitch = 256;
}

/** Ten tiles east of the same tile, lower, looking west; off its tile's
 *  centre, as a camera flown by hand is. */
static void
eye_east(void)
{
    g_eye.x = 3232 * 128 + 64 + 30;
    g_eye.y = -900;
    g_eye.z = 3218 * 128 + 64 - 20;
    g_eye.yaw = 512;
    g_eye.pitch = 200;
}

/* ---- the tests -------------------------------------------------------------------- */

static void
test_paths(void)
{
    int const g0 = groups();

    CHECK(strstr(text_of(g_w.row_title), "no path yet"), "an empty document says so: %s", text_of(g_w.row_title));
    /* The camera is nowhere yet (0,0: off the terrain): nothing is made. */
    activate(g_w.button_add);
    CHECK(said("Add keyframe refused: the camera is outside the open squares") && doc_text()[0] == '\0' && groups() == g0,
        "Add keyframe from off the terrain makes no path");
    enter(g_w.input_name, "Bad Name");
    activate(g_w.button_new);
    CHECK(said("is not a path name") && groups() == g0, "a name no proc can have is refused");
    ToriRSChrome_SetText(&g_chrome, g_w.input_name, "intro");
    activate(g_w.button_new);
    CHECK(groups() == g0 + 1, "New path is one undo step (%d)", groups() - g0);
    CHECK(strstr(doc_text(), "# ToriRSMapEd camera paths") == doc_text() && strstr(doc_text(), "\npath intro\n"),
        "the document is the header and the path:\n%s", doc_text());
    CHECK(strcmp(ToriRSChrome_DropdownSelectedValue(&g_chrome, g_w.dropdown_path), "intro") == 0, "the dropdown shows it");
    CHECK(strstr(text_of(g_w.row_title), "0 keyframes") && strstr(text_of(g_w.row_title), "unsaved"),
        "the title counts its keyframes and says unsaved: %s", text_of(g_w.row_title));
    activate(g_w.button_new);
    CHECK(said("a path named intro exists") && groups() == g0 + 1, "a taken name is refused");
    activate(g_w.button_goto);
    CHECK(said("Go to refused: select a keyframe"), "Go to with no keyframe selected is refused");
    activate(g_w.button_preview);
    CHECK(said("Preview refused: the path has no keyframe yet"), "so is Preview of an empty path");
    activate(g_w.button_copy);
    CHECK(said("Copy script refused: the path has no keyframe yet"), "and Copy script");
}

static void
test_keyframes(void)
{
    struct Editor_CamPathMark marks[EDITOR_CAMPATH_KEYS_MAX];
    int const g0 = groups();
    int sets;

    eye_north();
    sets = g_eye_sets;
    activate(g_w.button_add);
    CHECK(groups() == g0 + 1, "Add keyframe is one undo step");
    CHECK(strstr(doc_text(), "path intro\nkey intro | eye 3222,3208,0,1280 | look 3222,3218,0 | cut | hold 4\n"),
        "the first keyframe cuts and holds 4:\n%s", doc_text());
    CHECK(g_eye_sets == sets + 1 && g_eye.x == 3222 * 128 + 64 && g_eye.z == 3208 * 128 + 64 && g_eye.y == -1280
              && g_eye.yaw == 0 && g_eye.pitch == 255,
        "and the camera stands on it: %d,%d,%d yaw %d pitch %d", g_eye.x, g_eye.y, g_eye.z, g_eye.yaw, g_eye.pitch);
    CHECK(said("Added keyframe 1 (cut, hold 4): eye 3222,3208 +1280 looks at 3222,3218 +0"), "the status says what it made");
    CHECK(ToriRSChrome_VListSelected(&g_chrome, g_w.list_keys) == 0, "the new keyframe is the selected one");
    frame();
    CHECK(strstr(text_of(g_w.row_key), "Key 1: pitch 255, yaw 0; a cut"), "the row under the list reads it: %s",
        text_of(g_w.row_key));

    eye_east();
    activate(g_w.button_add);
    {
        const char* line = strstr(doc_text(), "key intro | eye 3232,3218,0,900 | look ");
        int hold = 0;
        CHECK(line != NULL, "the second keyframe follows the first:\n%s", doc_text());
        CHECK(line && strstr(line, " | glide 5,5,5,5 | hold ") && sscanf(strstr(line, "hold ") + 5, "%d", &hold) == 1
                  && hold > 4 && hold < 40,
            "it glides at Medium and holds until it has arrived and rested (%d ticks)", hold);
    }
    CHECK(ToriRSChrome_VListSelected(&g_chrome, g_w.list_keys) == 1 && strcmp(text_of(g_w.input_eye_rate), "5,5") == 0,
        "selected, its rates in the fields (%s)", text_of(g_w.input_eye_rate));
    CHECK(g_eye.x == 3232 * 128 + 64 && g_eye.z == 3218 * 128 + 64 && g_eye.y == -900,
        "the camera, flown off-centre, now stands on the tile's centre as the game's will (%d,%d)", g_eye.x, g_eye.z);
    CHECK(strstr(text_of(g_w.row_key), "Key 2:") && strstr(text_of(g_w.row_key), "glides in"),
        "the row says how long the glide takes: %s", text_of(g_w.row_key));

    /* The marks, in order, the selected one flagged. */
    CHECK(Editor_CamPath_Marks(&g_ui, marks, EDITOR_CAMPATH_KEYS_MAX) == 2 && marks[0].eye_x == 3222 * 128 + 64
              && marks[0].eye_y == -1280 && marks[0].look_z == 3218 * 128 + 64 && marks[0].ground_y == 0
              && !marks[0].selected && !marks[0].glide && marks[1].selected && marks[1].glide && marks[1].index == 1,
        "the World's marks are the two keyframes");
    ToriRSChrome_SetChecked(&g_chrome, g_w.check_show, 0);
    CHECK(Editor_CamPath_Marks(&g_ui, marks, EDITOR_CAMPATH_KEYS_MAX) == 0, "and none with Show the path off");
    ToriRSChrome_SetChecked(&g_chrome, g_w.check_show, 1);

    /* Go to and Update. */
    pick_row(0);
    eye_east();
    activate(g_w.button_goto);
    CHECK(g_eye.x == 3222 * 128 + 64 && g_eye.z == 3208 * 128 + 64 && g_eye.pitch == 255 && said("is on keyframe 1"),
        "Go to puts the camera on keyframe 1");
    g_eye.y = -640;
    g_eye.pitch = 150;
    activate(g_w.button_update);
    CHECK(strstr(doc_text(), "key intro | eye 3222,3208,0,640 | look ") && strstr(doc_text(), " | cut | hold 4\nkey intro | eye 3232"),
        "Update keyframe re-fits it and keeps its timing:\n%s", doc_text());

    /* Order. */
    activate(g_w.button_up);
    CHECK(said("Move up refused: keyframe 1 is already the first"), "the first cannot move up");
    activate(g_w.button_down);
    CHECK(strstr(doc_text(), "path intro\nkey intro | eye 3232,3218,0,900") && ToriRSChrome_VListSelected(&g_chrome, g_w.list_keys) == 1,
        "Move down swaps it with the next and follows it");
    activate(g_w.button_up);
    CHECK(strstr(doc_text(), "path intro\nkey intro | eye 3222,3208,0,640"), "Move up swaps it back");
    /* The same verbs by action id, as a bound key or the agent runs them. */
    Editor_CamPath_Hotkey(&g_ui, EDITOR_HK_CAMPATH_MOVE_DOWN);
    CHECK(strstr(doc_text(), "path intro\nkey intro | eye 3232,3218,0,900"), "campath.move_down moves it down");
    frame();
    Editor_CamPath_Hotkey(&g_ui, EDITOR_HK_CAMPATH_MOVE_UP);
    CHECK(strstr(doc_text(), "path intro\nkey intro | eye 3222,3208,0,640"), "campath.move_up moves it back");
    frame();

    /* Off the terrain. */
    g_eye.x = 100 * 128;
    activate(g_w.button_add);
    CHECK(said("Add keyframe refused: the camera is outside the open squares"), "a camera off the terrain is refused");
    g_eye.x = 3222 * 128 + 64;
    g_eye.pitch = 1900;
    activate(g_w.button_add);
    CHECK(said("looks above the horizon"), "and one that looks up");
}

static void
test_leg(void)
{
    int g0;

    pick_row(1);
    g0 = groups();
    enter(g_w.input_hold, "9");
    CHECK(strstr(doc_text(), " | glide 5,5,5,5 | hold 9\n") && groups() == g0 + 1, "Hold by Enter, one step:\n%s", doc_text());
    enter(g_w.input_eye_rate, "3,7");
    enter(g_w.input_look_rate, "8 2");
    CHECK(strstr(doc_text(), " | glide 3,7,8,2 | hold 9\n"), "the rate pairs, by comma or space:\n%s", doc_text());
    CHECK(ToriRSChrome_DropdownSelected(&g_chrome, g_w.dropdown_speed) == EDITOR_CAM_SPEED_CUSTOM, "Speed reads Custom");

    /* Typed and left without Enter: applied on the next frame. */
    ToriRSChrome_SetText(&g_chrome, g_w.input_hold, "12");
    g_chrome.focus = -1;
    frame();
    CHECK(strstr(doc_text(), " | glide 3,7,8,2 | hold 12\n"), "a field typed and left is applied, not lost:\n%s", doc_text());

    /* A refusal puts the field back. */
    g0 = groups();
    enter(g_w.input_hold, "0");
    frame();
    CHECK(said("Hold refused") && groups() == g0 && strcmp(text_of(g_w.input_hold), "12") == 0,
        "a hold of 0 is refused and the field says 12 again (%s)", text_of(g_w.input_hold));
    enter(g_w.input_eye_rate, "5,100");
    frame();
    CHECK(said("Eye rate refused") && strcmp(text_of(g_w.input_eye_rate), "3,7") == 0, "a rate2 of 100 is a cut: refused");

    choose(g_w.dropdown_speed, EDITOR_CAM_SPEED_FAST);
    CHECK(strstr(doc_text(), " | glide 10,10,10,10 | hold 12\n"), "Speed Fast:\n%s", doc_text());
    choose(g_w.dropdown_move, 0);
    CHECK(strstr(doc_text(), "eye 3232,3218,0,900") && strstr(strstr(doc_text(), "eye 3232,3218,0,900"), " | cut | hold 12\n"),
        "Move Cut:\n%s", doc_text());
    /* A cut's line states no rates: made a glide again it is Medium. */
    choose(g_w.dropdown_move, 1);
    CHECK(strstr(doc_text(), " | glide 5,5,5,5 | hold 12\n"), "and Glide again is a Medium glide:\n%s", doc_text());
    choose(g_w.dropdown_speed, EDITOR_CAM_SPEED_MEDIUM);
    enter(g_w.input_hold, "5");

    /* A hold typed short is the map maker's; a Speed made slower than the
     * hold allows grows the hold so the glide still arrives. */
    enter(g_w.input_hold, "1");
    CHECK(strstr(doc_text(), " | glide 5,5,5,5 | hold 1\n"), "a typed hold of 1 stands:\n%s", doc_text());
    choose(g_w.dropdown_speed, EDITOR_CAM_SPEED_SLOW);
    {
        const char* line = strstr(doc_text(), " | glide 2,2,2,2 | hold ");
        int hold = 0;
        CHECK(line && sscanf(line + 24, "%d", &hold) == 1 && hold > 3 && said("hold raised so the glide arrives"),
            "Slow raises the hold to fit the glide (%d):\n%s", hold, doc_text());
    }
    choose(g_w.dropdown_speed, EDITOR_CAM_SPEED_MEDIUM);
    enter(g_w.input_hold, "5");
    CHECK(strstr(doc_text(), " | glide 5,5,5,5 | hold 5\n"), "and back to Medium, hold 5:\n%s", doc_text());
}

static void
test_header_and_script(void)
{
    const char* script;

    script = Editor_CamPath_Script(&g_ui);
    CHECK(strncmp(script, "[proc,intro_camera]\n", 20) == 0 && strstr(script, "cam_moveto(0_50_50_22_8, 640, 232, 232);\n")
              && strstr(script, "p_delay(3);\ncam_moveto(0_50_50_32_18, 900, 5, 5);\n") && strstr(script, "p_delay(4);\ncam_reset;\n")
              && !strstr(script, "cam_unlock"),
        "the proc the panel shows:\n%s", script);
    activate(g_w.button_copy);
    CHECK(said("Copied [proc,intro_camera]") || said("the system clipboard did not take it"), "Copy script says what it did");
    /* No lane attached: refused before the first cheat, with the reason. */
    activate(g_w.button_send);
    CHECK(said("Send to play client refused: no lane is live"), "Send to play client with no live lane says why, up front");
    Editor_CamPath_Hotkey(&g_ui, EDITOR_HK_CAMPATH_SEND);
    CHECK(said("Send to play client refused: no lane is live"), "and so does campath.send_to_play");

    ToriRSChrome_SetChecked(&g_chrome, g_w.check_frame, 1);
    activate(g_w.check_frame);
    CHECK(strstr(doc_text(), "path intro | frame\n"), "Wrap in a cutscene frame is the path's:\n%s", doc_text());
    CHECK(strstr(Editor_CamPath_Script(&g_ui), "~cutscene_lock(3, 0);") && strstr(Editor_CamPath_Script(&g_ui), "~cutscene_hud_unlock;"),
        "and the proc wears the frame");
    ToriRSChrome_SetChecked(&g_chrome, g_w.check_frame, 0);
    activate(g_w.check_frame);

    /* Instance-relative: the first keyframe's square when none is typed. */
    choose(g_w.dropdown_coords, 1);
    CHECK(strstr(doc_text(), "path intro | coords instance m50_50\n") && strcmp(text_of(g_w.input_template), "m50_50") == 0,
        "Instance-relative takes the first keyframe's square:\n%s", doc_text());
    script = Editor_CamPath_Script(&g_ui);
    CHECK(strncmp(script, "[proc,intro_camera](int $handle)\n", 33) == 0
              && strstr(script, "cam_moveto(map_instance_coord($handle, 22, 8, 0), 640, 232, 232);\n"),
        "the proc takes the handle and states offsets:\n%s", script);
    enter(g_w.input_template, "m49_50");
    CHECK(strstr(doc_text(), "path intro | coords instance m49_50\n")
              && strstr(Editor_CamPath_Script(&g_ui), "map_instance_coord($handle, 86, 8, 0)"),
        "a typed Template moves the origin:\n%s", doc_text());
    enter(g_w.input_template, "fifty");
    frame();
    CHECK(said("Template refused") && strcmp(text_of(g_w.input_template), "m49_50") == 0, "a Template that is no square is refused");
    choose(g_w.dropdown_coords, 0);
    CHECK(strstr(doc_text(), "path intro\nkey intro"), "World tiles again:\n%s", doc_text());

    /* A framing the locked camera cannot hold. */
    pick_row(0);
    g_eye.x = 3222 * 128 + 64;
    g_eye.z = 3215 * 128 + 64;
    g_eye.y = -2000;
    g_eye.yaw = 0;
    g_eye.pitch = 440;
    activate(g_w.button_update);
    script = Editor_CamPath_Script(&g_ui);
    CHECK(strstr(script, "in\ncam_unlock(true);\ncam_moveto(") && strstr(script, "cam_unlock(false);\ncam_reset;\n"),
        "a steep keyframe unlocks the pitch in the proc:\n%s", script);
    CHECK(g_eye.pitch > 383, "and the camera shows it unclamped (%d)", g_eye.pitch);
    eye_north();
    activate(g_w.button_update);
    CHECK(!strstr(Editor_CamPath_Script(&g_ui), "cam_unlock"), "re-framed inside the range, the unlock goes");
}

static void
test_preview(void)
{
    struct Editor_CamPathMark marks[EDITOR_CAMPATH_KEYS_MAX];
    struct Editor_CamEye before;
    int moved = 0;
    int frames = 0;

    /* From somewhere else entirely. */
    g_eye.x = 3240 * 128;
    g_eye.z = 3240 * 128;
    g_eye.y = -500;
    g_eye.yaw = 1000;
    g_eye.pitch = 300;
    before = g_eye;
    CHECK(ToriRSChrome_Disabled(&g_chrome, g_w.button_stop), "Stop is disabled with nothing flying");
    activate(g_w.button_preview);
    CHECK(said("Preview: intro, 2 keyframes") && !ToriRSChrome_Disabled(&g_chrome, g_w.button_stop), "Preview starts");
    CHECK(Editor_CamPath_Marks(&g_ui, marks, EDITOR_CAMPATH_KEYS_MAX) == 0, "no marks stand in the shot while it flies");
    frame_ms(20);
    CHECK(g_eye.x == 3222 * 128 + 64 && g_eye.z == 3208 * 128 + 64 && g_eye.y == -1280 && g_eye.pitch == 255,
        "one cycle in, the camera has cut to keyframe 1 (%d,%d,%d)", g_eye.x, g_eye.y, g_eye.z);
    /* Keyframe 1 holds 4 ticks: 120 cycles. Just before, still there. */
    for( int i = 0; i < 118; i++ )
        frame_ms(20);
    CHECK(g_eye.x == 3222 * 128 + 64 && g_eye.z == 3208 * 128 + 64, "it holds keyframe 1 for its 4 ticks");
    for( int i = 0; i < 10; i++ )
    {
        struct Editor_CamEye const was = g_eye;
        frame_ms(20);
        moved += g_eye.x > was.x;
    }
    CHECK(moved >= 8 && g_eye.x > 3222 * 128 + 64 && g_eye.x < 3232 * 128 + 64, "then glides east towards keyframe 2 (x %d)", g_eye.x);
    /* Keyframe 2 holds 5 ticks: the whole flight is 9 ticks, 5.4 s. */
    while( !ToriRSChrome_Disabled(&g_chrome, g_w.button_stop) && frames++ < 400 )
        frame_ms(20);
    CHECK(frames > 120 && frames < 160, "the flight ends when the last hold does (%d more frames)", frames);
    CHECK(said("Preview finished: 2 keyframes in 5.4 s") && memcmp(&g_eye, &before, sizeof(before)) == 0,
        "and the camera is back where it was");

    /* Stop, mid-flight; a hitch does not skip the flight. */
    activate(g_w.button_preview);
    frame_ms(20);
    frame_ms(5000);
    CHECK(!ToriRSChrome_Disabled(&g_chrome, g_w.button_stop), "a five second hitch steps one second of flight, not all of it");
    activate(g_w.button_stop);
    CHECK(said("Preview stopped") && memcmp(&g_eye, &before, sizeof(before)) == 0 && ToriRSChrome_Disabled(&g_chrome, g_w.button_stop),
        "Stop gives the camera back");

    /* The action toggles it. */
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_CAMPATH_PREVIEW);
    frame();
    CHECK(!ToriRSChrome_Disabled(&g_chrome, g_w.button_stop), "campath.preview starts a flight");
    (void)Editor_HotkeyRun(&g_ui, EDITOR_HK_CAMPATH_PREVIEW);
    frame();
    CHECK(ToriRSChrome_Disabled(&g_chrome, g_w.button_stop), "and stops it");
    CHECK(Editor_HotkeyFind("view.campath") == EDITOR_HK_VIEW_CAMPATH && Editor_HotkeyForPanel("campath") == EDITOR_HK_VIEW_CAMPATH,
        "view.campath names the module");
}

static void
test_save_and_undo(const char* root)
{
    static char file[8192];
    int steps = 0;

    CHECK(Editor_SaveAll(&g_editor) >= 1, "Save all saves the document");
    CHECK(read_file(root, EDITOR_CAMPATHS_FILE_PATH, file, sizeof(file)) && strcmp(file, doc_text()) == 0,
        "%s on disk is the document:\n%s", EDITOR_CAMPATHS_FILE_PATH, file);
    frame();
    CHECK(!strstr(text_of(g_w.row_title), "unsaved"), "the title no longer says unsaved: %s", text_of(g_w.row_title));

    /* Rename and delete. */
    enter(g_w.input_name, "opening");
    activate(g_w.button_rename);
    CHECK(strstr(doc_text(), "path opening\nkey opening | ") && !strstr(doc_text(), "intro")
              && strcmp(ToriRSChrome_DropdownSelectedValue(&g_chrome, g_w.dropdown_path), "opening") == 0,
        "Rename renames every line:\n%s", doc_text());
    pick_row(1);
    activate(g_w.button_delete_key);
    CHECK(!strstr(doc_text(), "eye 3232,3218") && said("Deleted keyframe 2 of opening"), "Delete keyframe:\n%s", doc_text());
    activate(g_w.button_delete);
    CHECK(doc_text()[0] == '\0' && said("Deleted camera path opening"), "Delete path takes the header back: \"%s\"", doc_text());
    frame();
    CHECK(strstr(text_of(g_w.row_title), "no path yet"), "the panel is empty again: %s", text_of(g_w.row_title));

    /* With no path, Add keyframe makes one: named by Name when that is a
     * free path name, else camera_path. One undo step, path and keyframe. */
    {
        int const g0 = groups();
        eye_north();
        activate(g_w.button_add);
        CHECK(groups() == g0 + 1
                  && strstr(doc_text(), "\npath opening\nkey opening | eye 3222,3208,0,1280 | look 3222,3218,0 | cut | hold 4\n")
                  && said("made path opening"),
            "Add keyframe with no path makes one named by Name, in one step:\n%s", doc_text());
        CHECK(strcmp(ToriRSChrome_DropdownSelectedValue(&g_chrome, g_w.dropdown_path), "opening") == 0
                  && ToriRSChrome_VListSelected(&g_chrome, g_w.list_keys) == 0,
            "and shows it, its keyframe selected");
        activate(g_w.button_delete);
        ToriRSChrome_SetText(&g_chrome, g_w.input_name, "Not A Name");
        eye_north();
        activate(g_w.button_add);
        CHECK(strstr(doc_text(), "\npath cutscene\nkey cutscene | eye 3222,3208,0,1280"),
            "with no usable Name it is cutscene:\n%s", doc_text());
        activate(g_w.button_delete);
        CHECK(doc_text()[0] == '\0', "deleted again");
    }

    /* Every edit back. */
    while( Editor_GetHistory(&g_editor)->applied > 0 && steps++ < 200 )
        Editor_Undo(&g_editor);
    frame();
    CHECK(doc_text()[0] == '\0', "Undo walks every edit back to no text: \"%s\"", doc_text());
    Editor_Redo(&g_editor);
    frame();
    CHECK(strstr(doc_text(), "\npath intro\n"), "Redo brings the path back");
}

/** Whether the two windows' boxes (where they stand, at their measured
 *  size) overlap. */
static int
windows_overlap(
    int a,
    int b)
{
    struct ToriRSChromeRect ra = ToriRSChrome_PanelRect(&g_chrome, a);
    struct ToriRSChromeRect rb = ToriRSChrome_PanelRect(&g_chrome, b);
    ra.x = g_chrome.panels[a].x;
    ra.y = g_chrome.panels[a].y;
    rb.x = g_chrome.panels[b].x;
    rb.y = g_chrome.panels[b].y;
    return ra.x < rb.x + rb.w && rb.x < ra.x + ra.w && ra.y < rb.y + rb.h && rb.y < ra.y + ra.h;
}

/** Camera path and Triggers open together, in either order: neither covers
 *  the other (both fly out against the column at full height, and the
 *  second one used to cover the first whole). */
static void
test_beside_triggers(void)
{
    struct EditorUiPanel* triggers = Editor_UiFindPanel(&g_ui, "triggers");
    int t;

    CHECK(triggers && triggers->chrome_panel >= 0, "the tools panels bring Triggers");
    if( !triggers || triggers->chrome_panel < 0 )
        return;
    t = triggers->chrome_panel;
    for( int order = 0; order < 2; order++ )
    {
        if( g_chrome.panels[t].visible )
            Editor_UiTogglePanel(&g_ui, "triggers");
        if( g_chrome.panels[g_w.panel].visible )
            Editor_UiTogglePanel(&g_ui, "campath");
        for( int i = 0; i < 3; i++ )
            frame();
        Editor_UiTogglePanel(&g_ui, order == 0 ? "campath" : "triggers");
        for( int i = 0; i < 3; i++ )
            frame();
        Editor_UiTogglePanel(&g_ui, order == 0 ? "triggers" : "campath");
        for( int i = 0; i < 4; i++ )
            frame();
        CHECK(g_chrome.panels[t].visible && g_chrome.panels[g_w.panel].visible, "both open (%s first)",
            order == 0 ? "Camera path" : "Triggers");
        CHECK(!windows_overlap(t, g_w.panel), "%s opened first: Camera path (%d,%d) stands beside Triggers (%d,%d)",
            order == 0 ? "Camera path" : "Triggers", g_chrome.panels[g_w.panel].x, g_chrome.panels[g_w.panel].y,
            g_chrome.panels[t].x, g_chrome.panels[t].y);
    }
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

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1530, 1006);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    Editor_UiRegisterToolsPanels(&g_ui);
    /* After the modules registered: these three stand in for the App's
     * World without bringing the App-bound modules (the Catalog) with them. */
    g_ui.app_services.world_eye = fake_eye;
    g_ui.app_services.world_eye_set = fake_eye_set;
    g_ui.app_services.world_ground = fake_ground;
    Editor_UiSetVisible(&g_ui, 1);
    for( int i = 0; i < 4; i++ )
        frame();
    CHECK(Editor_CamPath_Widgets(&g_ui, &g_w), "the panel is registered");
    CHECK(!g_chrome.panels[g_w.panel].visible, "and closed until asked for");
    CHECK(Editor_UiTogglePanel(&g_ui, "campath") == 1, "View > Camera path opens it");
    frame();
    frame();

    test_paths();
    test_keyframes();
    test_leg();
    test_header_and_script();
    test_preview();
    test_save_and_undo(root);
    test_beside_triggers();

    Editor_UiFree(&g_ui);
    Editor_Close(&g_editor);
    printf("editor_campath_panel_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
