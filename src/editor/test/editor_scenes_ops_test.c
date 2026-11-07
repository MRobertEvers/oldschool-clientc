/*
 * Workbench 6.5, part 1, headless: the Scenes panel (editor/panels/
 * edpanel_scenes.c), the Lanes panel's scratch lane and switch (edpanel_
 * publish.c), the status bar's lane chip, against the real chrome model and
 * an embedded ToriRSMapEd on lane "work" of a SYNTHETIC tree written under
 * the directory given -- two hand-written squares, no cache, no OSRS-Content:
 *
 *   ./editor_scenes_ops_test <tmp-dir>
 *
 *   <tmp-dir>/tree   the content tree (the lane's base)
 *   <tmp-dir>/repo   the repo root: build/lanes/work, the scratch lane,
 *                    build/lanes/lane.ini
 *
 *   save       Save scene from the window's state (its two squares, the
 *              World's camera, level 1 Above, the lane, the var preview) as
 *              one undo step, in editor/scenes.txt on disk at once;
 *   two        a second window of the Client sees it after a pump and
 *              enters it: its squares opened, the camera put once they are
 *              in, level 1 Above pinned, STATE_VAR_PREVIEW published (the
 *              first window hears it), an account's teleport refused while
 *              the live lane is down -- each said in the status sentence;
 *   verbs      Update rewrites in place, Rename, Delete, Ctrl+Shift+K's
 *              generated name: one undo step each;
 *   restart    the editor closed and opened again on the same lane: the
 *              scene is still there and entering it gives the same squares,
 *              camera, level and preview;
 *   lanes      the scratch lane's name rule; Ctrl+Shift+N making
 *              scratch-<today>a from main and staging the switch
 *              (build/lanes/lane.ini), the Lanes panel listing it with its
 *              change count, and the next boot serving it;
 *   chip       the status bar's lane chip ("work | 1 changed", then with the
 *              next boot) and its click opening the Lanes panel.
 */

#include "editor/editor.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_scenes.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_lanes.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int
palette_row(
    struct EditorUi* ui,
    int group,
    int kind,
    const char* name);

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

/* One window of the Client: its session, chrome, registry and fake App. */
struct Window
{
    struct Editor editor;
    struct ToriRSChrome chrome;
    struct EditorUi ui;
    int open;
    /* The App, faked: the World's camera as it reads and as it was put, the
     * squares its open hook was asked for, and a busy world. */
    struct EditorViewCamera camera;
    int camera_valid;
    struct EditorViewCamera put;
    int put_count;
    int busy;
    char opened[8][EDITOR_DOC_KEY_MAX];
    int opened_count;
    /* STATE_VAR_PREVIEW as this window heard it. */
    int32_t preview[TORIRSMAPED_STATE_VALUES_MAX];
    int preview_count;
    int preview_heard;
};

static struct Window g_a;
static struct Window g_b;
static struct RSCache g_profile;
static char g_tree[1024];
static char g_repo[1024];

static const char k_jm2[] = "==== MAP ====\n"
                            "0 10 10: h5 u2\n";
static const char k_jl2[] = "==== LOC ====\n";

static void
write_text(
    const char* path,
    const char* text)
{
    FILE* f = fopen(path, "wb");
    if( !f )
    {
        printf("FAIL: cannot write %s\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

static char*
read_text(const char* path)
{
    FILE* f;
    long size;
    char* text;

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
exists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void
make_dir(const char* path)
{
    mkdir(path, 0755);
}

static void
write_fixture(const char* root)
{
    char path[2048];

    make_dir(root);
    snprintf(g_tree, sizeof(g_tree), "%s/tree", root);
    snprintf(g_repo, sizeof(g_repo), "%s/repo", root);
    make_dir(g_tree);
    make_dir(g_repo);
    snprintf(path, sizeof(path), "%s/configs", g_tree);
    make_dir(path);
    snprintf(path, sizeof(path), "%s/maps", g_tree);
    make_dir(path);
    snprintf(path, sizeof(path), "%s/editor", g_tree);
    make_dir(path);
    snprintf(path, sizeof(path), "%s/configs/all.loc", g_tree);
    write_text(path, "[test_wall]\nname=Wall\n");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", g_tree);
    write_text(path, k_jm2);
    snprintf(path, sizeof(path), "%s/maps/m50_50.jl2", g_tree);
    write_text(path, k_jl2);
    snprintf(path, sizeof(path), "%s/maps/m50_51.jm2", g_tree);
    write_text(path, k_jm2);
    snprintf(path, sizeof(path), "%s/maps/m50_51.jl2", g_tree);
    write_text(path, k_jl2);
}

/* ---- the fake App ---------------------------------------------------------------- */

static int
fake_world_camera(
    void* user,
    int level,
    struct EditorViewCamera* out)
{
    struct Window* w = user;
    (void)level;
    if( !w->camera_valid )
        return 0;
    *out = w->camera;
    return 1;
}

static int
fake_world_camera_set(
    void* user,
    const struct EditorViewCamera* camera)
{
    struct Window* w = user;
    w->put = *camera;
    w->put_count++;
    w->camera = *camera;
    w->camera_valid = 1;
    return 1;
}

static int
fake_world_busy(void* user)
{
    struct Window* w = user;
    return w->busy;
}

static void
fake_open_square(
    void* user,
    int map_x,
    int map_z,
    const char* storage,
    int add)
{
    struct Window* w = user;
    char key[EDITOR_DOC_KEY_MAX];

    (void)add;
    if( storage[0] )
        snprintf(key, sizeof(key), "%s/m%d_%d", storage, map_x, map_z);
    else
        snprintf(key, sizeof(key), "m%d_%d", map_x, map_z);
    if( w->opened_count < 8 )
        snprintf(w->opened[w->opened_count++], sizeof(w->opened[0]), "%s", key);
    /* The world opens it: the mirror loads the square. */
    (void)Editor_OpenDoc(&w->editor, EDITOR_DOC_SQUARE, key);
}

static void
on_state(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct Window* w = user;
    if( key == TORIRSMAPED_STATE_VAR_PREVIEW && count <= TORIRSMAPED_STATE_VALUES_MAX )
    {
        memcpy(w->preview, values, sizeof(values[0]) * (size_t)count);
        w->preview_count = count;
        w->preview_heard++;
    }
    Editor_UiApplyState(&w->ui, key, values, count);
}

/* ---- windows -------------------------------------------------------------------- */

static void
window_ui(struct Window* w)
{
    ToriRSChrome_Init(&w->chrome);
    ToriRSChrome_SetSurface(&w->chrome, 1530, 1006);
    Editor_UiInit(&w->ui, &w->chrome, &w->editor);
    /* The App-bound modules register for a stand-in. */
    w->ui.app_services.user_data = w;
    w->ui.app_services.world_camera = fake_world_camera;
    w->ui.app_services.world_camera_set = fake_world_camera_set;
    w->ui.app_services.world_busy = fake_world_busy;
    w->ui.hooks.open_square = fake_open_square;
    w->ui.hooks_user = w;
    Editor_UiRegisterToolsPanels(&w->ui);
    Editor_UiSetVisible(&w->ui, 1);
    Editor_SetStateCallback(&w->editor, on_state, w);
    w->open = 1;
}

static int
window_open(
    struct Window* w,
    const char* lane)
{
    struct EditorHost host;

    memset(w, 0, sizeof(*w));
    if( !Editor_HostOpenMapEdEmbedLane(&host, g_tree, g_repo, lane) )
        return 0;
    Editor_OpenHost(&w->editor, &host, "scenes test", &g_profile);
    window_ui(w);
    return 1;
}

static int
window_peer(
    struct Window* w,
    struct Window* of)
{
    struct EditorHost host;

    memset(w, 0, sizeof(*w));
    if( !Editor_HostOpenMapEdEmbedPeer(&host, &of->editor.host, TORIRSMAPED_ROLE_GENERIC, 1) )
        return 0;
    Editor_OpenHost(&w->editor, &host, "second window", &g_profile);
    window_ui(w);
    return 1;
}

static void
window_close(struct Window* w)
{
    if( !w->open )
        return;
    Editor_SetStateCallback(&w->editor, NULL, NULL);
    Editor_UiFree(&w->ui);
    Editor_Close(&w->editor);
    w->open = 0;
}

static void
frame_of(struct Window* w)
{
    ToriRSChrome_Build(&w->chrome);
    Editor_UiTick(&w->ui);
    ToriRSChrome_Build(&w->chrome);
}

/** Pump every open window's facts and run a frame, `n` times. */
static void
pump(int n)
{
    for( int i = 0; i < n; i++ )
    {
        if( g_a.open )
            Editor_PumpFacts(&g_a.editor);
        if( g_b.open )
            Editor_PumpFacts(&g_b.editor);
        if( g_a.open )
            frame_of(&g_a);
        if( g_b.open )
            frame_of(&g_b);
    }
}

static const char*
doc_text(struct Window* w)
{
    struct Editor_DocHandle handle;
    static char text[16384];

    if( !Editor_DocFind(&w->editor.doc, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY, &handle) || !handle.text
        || !handle.text->loaded )
        return "";
    snprintf(text, sizeof(text), "%.*s", (int)handle.text->length, handle.text->text ? handle.text->text : "");
    return text;
}

static const char*
history_top(struct Window* w)
{
    static char row[EDITOR_HISTORY_LABEL_MAX];
    const struct Editor_History* history = Editor_GetHistory(&w->editor);
    if( history->applied <= 0 )
        return "";
    Editor_HistoryGroupName(&history->groups[history->applied - 1], row, (int)sizeof(row));
    return row;
}

static const char*
status(struct Window* w)
{
    return Editor_UiStatusText(&w->ui);
}

static int
find_panel(
    struct Window* w,
    const char* title)
{
    for( int i = 0; i < w->chrome.panel_count; i++ )
        if( strcmp(w->chrome.panels[i].title, title) == 0 )
            return i;
    return -1;
}

static int
find_widget(
    struct Window* w,
    int panel,
    int kind,
    const char* label)
{
    for( int i = 0; i < w->chrome.widget_count; i++ )
        if( w->chrome.widgets[i].panel == panel && (int)w->chrome.widgets[i].kind == kind
            && (!label || strcmp(w->chrome.widgets[i].label, label) == 0
                || (kind == TORIRS_CHROME_W_BUTTON && strcmp(w->chrome.widgets[i].text, label) == 0)) )
            return i;
    return -1;
}

static void
activate(
    struct Window* w,
    int widget)
{
    assert(widget >= 0);
    w->chrome.activated = widget;
    frame_of(w);
}

/** The status bar's label whose text starts with `prefix`, or -1. */
static int
statusbar_label(
    struct Window* w,
    const char* prefix)
{
    for( int i = 0; i < w->chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* widget = &w->chrome.widgets[i];
        if( widget->kind == TORIRS_CHROME_W_LABEL && w->chrome.panels[widget->panel].style == TORIRS_CHROME_PANEL_MENUBAR
            && !widget->hidden && strncmp(widget->text, prefix, strlen(prefix)) == 0 )
            return i;
    }
    return -1;
}

/** The text a VLIST row holds, "" when the chrome holds none for it. */
static const char*
row_text(
    struct Window* w,
    int list,
    int row)
{
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
        if( w->chrome.vrows[i].owner == list && w->chrome.vrows[i].index == row )
            return w->chrome.vrows[i].text;
    return "";
}

/** The lane's change count as the lane module counts it. */
static int
lane_changes(const char* name)
{
    struct ToriRSMapEdLaneDef lane;
    if( !ToriRSMapEdLanes_Lookup(g_repo, g_tree, name, &lane) )
        return -1;
    return ToriRSMapEdLanes_Changes(&lane, NULL, 0, NULL);
}

/* ---- the tests ----------------------------------------------------------------- */

static const struct EditorViewCamera k_camera = { 3222 * 128 + 64, 3218 * 128, 1, 512, 400, 1800 };

static void
test_save(void)
{
    int32_t preview[4] = { 1, 1, 29, 11 };
    char path[2048];
    char* on_disk;

    CHECK(Editor_OpenDoc(&g_a.editor, EDITOR_DOC_SQUARE, "m50_50"), "window A opens m50_50");
    CHECK(Editor_OpenDoc(&g_a.editor, EDITOR_DOC_SQUARE, "m50_51"), "and m50_51");
    g_a.camera = k_camera;
    g_a.camera_valid = 1;
    edp_level_set(&g_a.ui, 1);
    pump(1);
    CHECK(edp_level_show_above(&g_a.ui, 1), "level 1 above");
    CHECK(Editor_UiPublish(&g_a.ui, TORIRSMAPED_STATE_VAR_PREVIEW, preview, 4), "the var preview is published");
    pump(4);
    CHECK(g_a.ui.lane.known && strcmp(g_a.ui.lane.name, "work") == 0 && !g_a.ui.lane.is_main,
        "the window knows its lane from the first frames (%s)", g_a.ui.lane.name);

    CHECK(Editor_Scenes_Save(&g_a.ui, "smithy"), "Save scene: %s", status(&g_a));
    CHECK(strstr(doc_text(&g_a), "scene smithy | squares m50_50 m50_51 | camera 3222.5,3218,1,512,400,1800\n") != NULL,
        "where it looks: %s", doc_text(&g_a));
    CHECK(strstr(doc_text(&g_a), "scene smithy | level 1 above | lane work | preview varp29=11\n") != NULL,
        "what it pins: %s", doc_text(&g_a));
    CHECK(strcmp(history_top(&g_a), "Save scene smithy") == 0, "one History row: %s", history_top(&g_a));
    CHECK(strncmp(status(&g_a), "Saved scene smithy: m50_50 m50_51 | L1 above | work | preview varp29=11", 70) == 0,
        "the status bar confirms: %s", status(&g_a));
    snprintf(path, sizeof(path), "%s/build/lanes/work/content/editor/scenes.txt", g_repo);
    on_disk = read_text(path);
    CHECK(on_disk && strcmp(on_disk, doc_text(&g_a)) == 0, "saved at once, in the lane's tree");
    free(on_disk);
    snprintf(path, sizeof(path), "%s/editor/scenes.txt", g_tree);
    CHECK(!exists(path), "the base tree is untouched");

    CHECK(Editor_Undo(&g_a.editor) == 2 && strcmp(doc_text(&g_a), "") == 0, "one undo takes both lines back");
    CHECK(Editor_Redo(&g_a.editor) == 2 && strstr(doc_text(&g_a), "scene smithy | level 1") != NULL,
        "one redo brings them");
    CHECK(!Editor_Scenes_Save(&g_a.ui, "smithy") && strstr(status(&g_a), "exists; Update rewrites it"),
        "a second Save under the name is refused, saying why: %s", status(&g_a));
    CHECK(!Editor_Scenes_Save(&g_a.ui, "has space") && strstr(status(&g_a), "not a scene name"),
        "a name with a space is refused: %s", status(&g_a));
    (void)Editor_SaveDoc(&g_a.editor, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY);
}

static void
test_second_window(void)
{
    struct Editor_SceneList* list = calloc(1, sizeof(*list));
    struct Editor_ScenesFile file;
    struct Editor_Scene camp;
    struct Editor_Cmd commands[EDITOR_SCENE_PLAN_MAX];
    int n;

    assert(list);
    if( !window_peer(&g_b, &g_a) )
    {
        CHECK(0, "a second window joins the Client");
        free(list);
        return;
    }
    pump(4);
    CHECK(Editor_ScenesList(&g_b.ui, list) == 1 && strcmp(list->rows[0].name, "smithy") == 0
              && strcmp(list->rows[0].summary, "m50_50 m50_51 | L1 above | work | preview varp29=11") == 0,
        "the second window lists the scene (the command palette's hook): %d %s", list->count,
        list->count ? list->rows[0].summary : "");

    /* Enter from the second window: its world is busy until the squares
     * are in, so the camera waits for them. */
    g_b.busy = 1;
    edp_level_set(&g_b.ui, 0);
    /* Through the command palette's Scenes group (workbench 6.8's seam). */
    {
        int row;
        CHECK(Editor_CmdPalette_Open(&g_b.ui), "the second window has a command palette");
        Editor_CmdPalette_SetQuery(&g_b.ui, "smithy");
        row = palette_row(&g_b.ui, EDITOR_CMDPAL_SCENES, EDITOR_CMDPAL_KIND_SCENE, "smithy");
        CHECK(row >= 0, "the palette's Scenes group lists smithy");
        CHECK(row >= 0 && Editor_CmdPalette_RunRow(&g_b.ui, row), "Enter from the palette: %s", status(&g_b));
        CHECK(!Editor_CmdPalette_IsOpen(&g_b.ui), "the palette closes once the row runs");
    }
    CHECK(g_b.opened_count == 2 && strcmp(g_b.opened[0], "m50_50") == 0 && strcmp(g_b.opened[1], "m50_51") == 0,
        "its squares are opened (%d)", g_b.opened_count);
    pump(1);
    CHECK(g_b.ui.tools.edit_level == 1 && edp_level_showing_above(&g_b.ui), "level 1 is pinned, Above on");
    CHECK(strstr(status(&g_b), "Entered smithy: 2 squares, level 1 above, preview of 1 var, camera") != NULL,
        "the status sentence says what it did: %s", status(&g_b));
    pump(2);
    CHECK(g_b.put_count == 0, "no camera while the world is busy");
    g_b.busy = 0;
    pump(2);
    CHECK(g_b.put_count == 1 && memcmp(&g_b.put, &k_camera, sizeof(k_camera)) == 0,
        "the camera is put once the squares are in (%d: %d,%d yaw %d)", g_b.put_count, (int)g_b.put.x,
        (int)g_b.put.z, (int)g_b.put.yaw);
    CHECK(g_a.preview_heard > 0 && g_a.preview_count == 4 && g_a.preview[2] == 29 && g_a.preview[3] == 11,
        "STATE_VAR_PREVIEW reaches the first window (%d values)", g_a.preview_count);

    /* An account is teleported only through a live lane that is up. */
    Editor_ScenesInit(&file);
    Editor_ScenesParse(&file, doc_text(&g_a), strlen(doc_text(&g_a)));
    Editor_SceneInitRow(&camp, "camp");
    camp.segments = EDITOR_SCENE_SEG_ACCOUNT | EDITOR_SCENE_SEG_LANE;
    snprintf(camp.account, sizeof(camp.account), "bob");
    camp.account_x = 3224;
    camp.account_z = 3219;
    camp.account_level = 0;
    snprintf(camp.lane, sizeof(camp.lane), "work");
    n = Editor_ScenePlanSave(commands, EDITOR_SCENE_PLAN_MAX, &file, &camp);
    Editor_ScenesFree(&file);
    CHECK(n == 1 && edp_apply(&g_a.ui, commands, n, "Save scene camp") == 1, "a scene with an account");
    pump(3);
    CHECK(Editor_Scenes_Enter(&g_b.ui, "camp") && strstr(status(&g_b), "bob not moved (the live lane is down)"),
        "its teleport is refused while the live lane is down, and said: %s", status(&g_b));
    CHECK(!strstr(status(&g_b), "lane work"), "the lane it names is this one: no switch");
    CHECK(!Editor_Scenes_Enter(&g_b.ui, "nowhere") && strstr(status(&g_b), "there is no scene nowhere"),
        "an unknown scene is refused: %s", status(&g_b));
    CHECK(Editor_Undo(&g_a.editor) == 1, "camp undone");
    pump(2);
    free(list);
}

static void
test_verbs(void)
{
    const char* text;
    char before[16384];
    int save_button;
    int panel;

    /* Update: the first window at level 2 now. */
    snprintf(before, sizeof(before), "%s", doc_text(&g_a));
    edp_level_set(&g_a.ui, 2);
    pump(1);
    (void)edp_level_show_above(&g_a.ui, 1);
    CHECK(Editor_Scenes_Update(&g_a.ui, "smithy"), "Update: %s", status(&g_a));
    text = doc_text(&g_a);
    CHECK(strstr(text, "scene smithy | squares m50_50 m50_51 | camera 3222.5,3218,1,512,400,1800\n") == text
              + strlen("# ToriRSMapEd scenes (editor/scenes.txt). One record per line, |-separated segments.\n"
                       "# scene <name> | squares <key> ... | camera <x>,<z>,<level>,<yaw>,<pitch>,<zoom>\n"
                       "# scene <name> | level <0..3|auto> [above] | lane <name> | preview <var>=<value> ... | "
                       "account <name> at <x>,<z>,<level>\n"
                       "# scene <name> | note <text>\n"),
        "Update keeps the first line where it was: %s", text);
    CHECK(strstr(text, "scene smithy | level 2 above | lane work | preview varp29=11\n") != NULL, "level 2: %s", text);
    CHECK(strcmp(history_top(&g_a), "Update scene smithy") == 0, "one History row: %s", history_top(&g_a));
    CHECK(!Editor_Scenes_Update(&g_a.ui, "smithy") && strstr(status(&g_a), "already holds this state"),
        "an Update that changes nothing says so: %s", status(&g_a));
    CHECK(Editor_Undo(&g_a.editor) == 2 && strcmp(doc_text(&g_a), before) == 0, "one undo puts it back");
    CHECK(Editor_Redo(&g_a.editor) == 2, "and redo");

    /* Rename and Delete, one step each. */
    CHECK(Editor_Scenes_Rename(&g_a.ui, "smithy", "forge") && !strstr(doc_text(&g_a), "smithy")
              && strstr(doc_text(&g_a), "scene forge | level 2"),
        "Rename: %s", status(&g_a));
    CHECK(strcmp(history_top(&g_a), "Rename scene smithy to forge") == 0, "History: %s", history_top(&g_a));
    CHECK(!Editor_Scenes_Rename(&g_a.ui, "forge", "a b"), "Rename to a bad name is refused: %s", status(&g_a));
    CHECK(Editor_Scenes_Delete(&g_a.ui, "forge") && strcmp(doc_text(&g_a), "") == 0, "Delete empties the file");
    CHECK(strcmp(history_top(&g_a), "Delete scene forge") == 0, "History: %s", history_top(&g_a));
    CHECK(Editor_Undo(&g_a.editor) == 2 && strstr(doc_text(&g_a), "scene forge"), "one undo brings forge back");
    CHECK(Editor_Undo(&g_a.editor) == 2 && strstr(doc_text(&g_a), "scene smithy"), "and the rename");

    /* Ctrl+Shift+K with an empty Name box: a generated name. The panel's
     * Save scene button wears the chord. */
    (void)Editor_HotkeyRun(&g_a.ui, EDITOR_HK_FILE_SAVE_SCENE);
    CHECK(strstr(doc_text(&g_a), "scene scene-1 | squares m50_50 m50_51") != NULL, "Ctrl+Shift+K saves scene-1: %s",
        status(&g_a));
    panel = find_panel(&g_a, "Scenes");
    save_button = find_widget(&g_a, panel, TORIRS_CHROME_W_BUTTON, "Save scene");
    CHECK(save_button >= 0 && strcmp(g_a.chrome.widgets[save_button].chord, "Ctrl+Shift+K") == 0,
        "Save scene wears its keycap (%d %d %s)", panel, save_button,
        save_button >= 0 ? g_a.chrome.widgets[save_button].chord : "-");
    CHECK(Editor_Undo(&g_a.editor) == 2, "scene-1 undone");

    /* The panel lists both windows' view of the document. */
    CHECK(Editor_UiTogglePanel(&g_a.ui, "scenes") == 1, "View > Scenes opens the panel");
    pump(2);
    {
        int const list = find_widget(&g_a, panel, TORIRS_CHROME_W_VLIST, NULL);
        int const name_box = find_widget(&g_a, panel, TORIRS_CHROME_W_TEXTINPUT, "Name");
        CHECK(list >= 0 && g_a.chrome.widgets[list].imax == 1, "the list holds smithy (%d)",
            list >= 0 ? g_a.chrome.widgets[list].imax : -1);
        ToriRSChrome_VListSetSelected(&g_a.chrome, list, 0);
        activate(&g_a, list);
        CHECK(name_box >= 0 && strcmp(g_a.chrome.widgets[name_box].text, "smithy") == 0,
            "a click picks it: its name in Name (%s)", name_box >= 0 ? g_a.chrome.widgets[name_box].text : "-");
        /* The undo left the scenes document unsaved: Enter asks first. */
        g_a.opened_count = 0;
        activate(&g_a, list);
        {
            int const guard = find_panel(&g_a, "Unsaved changes");
            int const dont = find_widget(&g_a, guard, TORIRS_CHROME_W_BUTTON, "Don't save");
            int detail = -1;
            for( int i = 0; guard >= 0 && i < g_a.chrome.widget_count; i++ )
                if( g_a.chrome.widgets[i].panel == guard
                    && strcmp(g_a.chrome.widgets[i].text, "before entering scene smithy") == 0 )
                    detail = i;
            CHECK(guard >= 0 && g_a.chrome.panels[guard].visible && detail >= 0 && g_a.opened_count == 0,
                "a second activation enters it, through the save guard (%d %d)", guard, detail);
            if( dont >= 0 )
                activate(&g_a, dont);
        }
        CHECK(g_a.opened_count == 2 && strstr(status(&g_a), "Entered smithy"),
            "Don't save: entered: %s", status(&g_a));
    }
    (void)Editor_SaveDoc(&g_a.editor, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY);
}

static void
test_restart(void)
{
    int32_t none[1] = { 0 };

    window_close(&g_b);
    window_close(&g_a);
    if( !window_open(&g_a, "work") )
    {
        CHECK(0, "the editor opens again on lane work");
        return;
    }
    pump(4);
    CHECK(strstr(doc_text(&g_a), "scene smithy | level 2 above | lane work | preview varp29=11") != NULL,
        "after a restart the scene is there, as updated: %s", doc_text(&g_a));
    CHECK(g_a.ui.tools.edit_level == -1 && g_a.opened_count == 0, "a fresh window: Auto, nothing open");
    (void)Editor_UiPublish(&g_a.ui, TORIRSMAPED_STATE_VAR_PREVIEW, none, 1);
    pump(2);
    g_a.preview_heard = 0;
    CHECK(Editor_Scenes_Enter(&g_a.ui, "smithy"), "Enter after the restart: %s", status(&g_a));
    pump(3);
    CHECK(g_a.opened_count == 2 && strcmp(g_a.opened[0], "m50_50") == 0 && strcmp(g_a.opened[1], "m50_51") == 0,
        "the same squares");
    CHECK(g_a.put_count == 1 && memcmp(&g_a.put, &k_camera, sizeof(k_camera)) == 0, "the same camera");
    CHECK(g_a.ui.tools.edit_level == 2 && edp_level_showing_above(&g_a.ui), "the same level and Above");
    CHECK(g_a.preview_heard > 0 && g_a.preview_count == 4 && g_a.preview[1] == 1 && g_a.preview[2] == 29
              && g_a.preview[3] == 11,
        "the same preview (%d values)", g_a.preview_count);
}

static void
test_lanes_and_chip(void)
{
    const char* const taken_ab[] = { "main", "scratch-261105a", "scratch-261105b" };
    const char* taken_all[27];
    char names[26][24];
    char name[48];
    char today[48];
    char chip[96];
    char path[2048];
    char* ini;
    int label;
    int lanes_panel;
    int expected;

    /* The name rule. */
    CHECK(Editor_LanesScratchName("261105", NULL, 0, name, (int)sizeof(name)) && strcmp(name, "scratch-261105a") == 0,
        "the first scratch lane of a day is a (%s)", name);
    CHECK(Editor_LanesScratchName("261105", taken_ab, 3, name, (int)sizeof(name)) && strcmp(name, "scratch-261105c") == 0,
        "the first free letter (%s)", name);
    for( int i = 0; i < 26; i++ )
    {
        snprintf(names[i], sizeof(names[i]), "scratch-261105%c", 'a' + i);
        taken_all[i] = names[i];
    }
    taken_all[26] = "main";
    CHECK(!Editor_LanesScratchName("261105", taken_all, 27, name, (int)sizeof(name)) && name[0] == '\0',
        "a day's 26 taken: none");
    CHECK(Editor_LanesScratchName("261106", taken_all, 27, name, (int)sizeof(name)) && strcmp(name, "scratch-261106a") == 0,
        "the next day starts again at a");

    /* The chip: the lane and its change count (editor/scenes.txt is the
     * lane's one change). */
    expected = lane_changes("work");
    Editor_Lanes_ChipText(&g_a.ui, chip, (int)sizeof(chip));
    CHECK(expected == 1, "the lane has one change: its scenes.txt (%d)", expected);
    CHECK(strcmp(chip, "work | 1 changed") == 0, "the chip text: %s", chip);
    label = statusbar_label(&g_a, "work |");
    CHECK(label >= 0 && strcmp(g_a.chrome.widgets[label].text, "work | 1 changed") == 0,
        "the status bar shows it (%s)", label >= 0 ? g_a.chrome.widgets[label].text : "-");
    lanes_panel = find_panel(&g_a, "Lanes");
    CHECK(lanes_panel >= 0 && !g_a.chrome.panels[lanes_panel].visible, "the Lanes panel is closed");
    if( label >= 0 )
        activate(&g_a, label);
    CHECK(lanes_panel >= 0 && g_a.chrome.panels[lanes_panel].visible, "a click on the chip opens the Lanes panel");
    if( label >= 0 )
        activate(&g_a, label);
    CHECK(lanes_panel >= 0 && g_a.chrome.panels[lanes_panel].visible, "and a second click leaves it open");

    /* Ctrl+Shift+N: scratch-<today>a from main, and the switch staged. */
    CHECK(Editor_Lanes_ScratchNameFor(&g_a.ui, (long long)time(NULL), today, (int)sizeof(today)),
        "today's scratch name (%s)", today);
    (void)Editor_HotkeyRun(&g_a.ui, EDITOR_HK_FILE_SCRATCH_LANE);
    pump(10);
    snprintf(path, sizeof(path), "%s/build/lanes/%s/lane.ini", g_repo, today);
    CHECK(exists(path), "Ctrl+Shift+N made %s", today);
    snprintf(path, sizeof(path), "%s/build/lanes/lane.ini", g_repo);
    ini = read_text(path);
    snprintf(name, sizeof(name), "lane = %s\n", today);
    CHECK(ini && strstr(ini, name) != NULL, "and staged it for the next boot (%s)", ini ? ini : "no lane.ini");
    free(ini);
    CHECK(strcmp(g_a.ui.lane.next_boot, today) == 0 && strcmp(g_a.ui.lane.name, "work") == 0,
        "the session stays on work, the next boot on %s (%s)", today, g_a.ui.lane.next_boot);
    CHECK(strstr(status(&g_a), "for the next editor boot") && strstr(status(&g_a), "cannot re-point its lane"),
        "the status bar says the switch waits for a restart: %s", status(&g_a));
    Editor_Lanes_ChipText(&g_a.ui, chip, (int)sizeof(chip));
    snprintf(name, sizeof(name), "work | 1 changed | next: %s", today);
    CHECK(strcmp(chip, name) == 0, "the chip names the next boot's lane: %s", chip);

    /* The Lanes panel lists it with its count. */
    pump(4);
    {
        int const list = find_widget(&g_a, lanes_panel, TORIRS_CHROME_W_VLIST, "Lanes");
        int found = 0;
        if( list >= 0 )
        {
            for( int r = 0; r < g_a.chrome.widgets[list].imax && r < 8; r++ )
            {
                const char* row = row_text(&g_a, list, r);
                if( strstr(row, today) && strstr(row, "| clean") )
                    found |= 1;
                if( strncmp(row, "work (this) | 1 changed", 23) == 0 )
                    found |= 2;
            }
        }
        CHECK(list >= 0 && g_a.chrome.widgets[list].imax == 3, "three lanes: main, the scratch, work (%d)",
            list >= 0 ? g_a.chrome.widgets[list].imax : -1);
        CHECK(found == 3, "rows with their counts (%d: %s / %s / %s)", found, row_text(&g_a, list, 0),
            row_text(&g_a, list, 1), row_text(&g_a, list, 2));
    }

    /* The counts are the server's cache of each lane's last walk: the chip's
     * periodic ask walks nothing (a file written outside every editor does
     * not show), Refresh walks every lane again. */
    {
        int const list = find_widget(&g_a, lanes_panel, TORIRS_CHROME_W_VLIST, "Lanes");
        int const refresh = find_widget(&g_a, lanes_panel, TORIRS_CHROME_W_BUTTON, "Refresh");
        int work_row = 0;
        snprintf(path, sizeof(path), "%s/build/lanes/work/content/editor/outside.txt", g_repo);
        write_text(path, "written outside every editor\n");
        CHECK(lane_changes("work") == 2, "a file written outside the editor is a second change");
        g_a.chrome.now_ms += EDITOR_UI_LANE_COUNT_MS + 1;
        pump(4);
        Editor_Lanes_ChipText(&g_a.ui, chip, (int)sizeof(chip));
        CHECK(strncmp(chip, "work | 1 changed", 16) == 0, "the chip's periodic ask answers from the cache (%s)", chip);
        CHECK(refresh >= 0, "the Lanes panel has Refresh");
        if( refresh >= 0 )
            activate(&g_a, refresh);
        pump(4);
        Editor_Lanes_ChipText(&g_a.ui, chip, (int)sizeof(chip));
        CHECK(strncmp(chip, "work | 2 changed", 16) == 0, "Refresh walks the mirror again (%s)", chip);
        for( int r = 0; list >= 0 && r < g_a.chrome.widgets[list].imax && r < 8; r++ )
        {
            if( strncmp(row_text(&g_a, list, r), "work (this) | 2 changed", 23) == 0 )
                work_row = 1;
        }
        CHECK(work_row, "and the list's row for this lane says so");
        remove(path);
        if( refresh >= 0 )
            activate(&g_a, refresh);
        pump(4);
        Editor_Lanes_ChipText(&g_a.ui, chip, (int)sizeof(chip));
        CHECK(strncmp(chip, "work | 1 changed", 16) == 0, "and again once it is gone (%s)", chip);
    }

    /* The command palette's Lanes group lists the same three; Enter on one
     * stages a switch, as the panel's Switch... does. */
    {
        int row_work;
        int row_main;
        CHECK(Editor_CmdPalette_Open(&g_a.ui), "the window has a command palette");
        Editor_CmdPalette_SetQuery(&g_a.ui, "lane");
        row_work = palette_row(&g_a.ui, EDITOR_CMDPAL_LANES, EDITOR_CMDPAL_KIND_LANE, "work");
        row_main = palette_row(&g_a.ui, EDITOR_CMDPAL_LANES, EDITOR_CMDPAL_KIND_LANE, "main");
        CHECK(row_work >= 0 && row_main >= 0
                  && palette_row(&g_a.ui, EDITOR_CMDPAL_LANES, EDITOR_CMDPAL_KIND_LANE, today) >= 0,
            "the palette's Lanes group lists main, work and %s", today);
        Editor_CmdPalette_Close(&g_a.ui);
    }

    /* The next boot serves the scratch lane. */
    window_close(&g_a);
    if( !window_open(&g_a, "work") )
    {
        CHECK(0, "the editor boots again");
        return;
    }
    pump(4);
    CHECK(strcmp(g_a.ui.lane.name, today) == 0, "the next boot serves %s (%s)", today, g_a.ui.lane.name);
    Editor_Lanes_ChipText(&g_a.ui, chip, (int)sizeof(chip));
    snprintf(name, sizeof(name), "%s | clean", today);
    CHECK(strcmp(chip, name) == 0, "its chip: %s", chip);
    CHECK(strcmp(doc_text(&g_a), "") == 0, "a scratch lane is made from main: no scenes in it");
}

/** The palette's row for an item of `kind` named `name` in `group`, -1. */
static int
palette_row(
    struct EditorUi* ui,
    int group,
    int kind,
    const char* name)
{
    const struct Editor_CmdPalList* pal = Editor_CmdPalette_List(ui);
    if( !pal )
        return -1;
    for( int r = 0; r < pal->row_count; r++ )
    {
        const struct Editor_CmdPalItem* item;
        if( pal->row_kind[r] != EDITOR_CMDPAL_ROW_ITEM || pal->row_group[r] != group )
            continue;
        item = &pal->items[pal->row_item[r]];
        if( item->kind == kind && strcmp(item->name, name) == 0 )
            return r;
    }
    return -1;
}

int
main(
    int argc,
    char** argv)
{
    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <tmp-dir>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    if( !RSCache_ProfileByName("osrs242", &g_profile) )
    {
        printf("FAIL: no osrs242 profile\n");
        return 1;
    }
    write_fixture(argv[1]);
    if( !window_open(&g_a, "work") )
    {
        printf("FAIL: the embedded ToriRSMapEd did not open lane work of %s\n", g_tree);
        return 1;
    }
    CHECK(g_a.editor.writable, "the lane is writable");
    pump(2);

    test_save();
    test_second_window();
    test_verbs();
    test_restart();
    test_lanes_and_chip();

    window_close(&g_b);
    window_close(&g_a);
    printf("editor_scenes_ops_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
