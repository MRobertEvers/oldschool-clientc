/*
 * The Accounts panel (editor/accounts/accounts_panel.c), headless, against
 * the real chrome model and an embedded ToriRSMapEd with a lane, over a small
 * content tree and a live dir written under the directory given:
 *
 *   ./editor_panels_accounts_test <tmp-dir> <repo-root>
 *
 * The panel lists the lane's accounts; a stat edit from the Stats tab writes
 * the save (offline) and the list rows follow; Move and the Place player tool
 * write the position; Find resolves a varp through the catalog and Set writes
 * it; the Quests tab lists the rows with their states and a mark is queued;
 * the Items tab sets a slot (by name and from the Catalog's pick) and removes
 * it; New account / Use for RUN / Delete; a second window of the Client sees
 * the list after a pump; the tool's name and hotkey. Links the client's
 * objects minus main.o, as test-editor-panels does.
 */

#include "editor/accounts/accounts_panel.h"
#include "editor/editor.h"
#include "editor/editor_host.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_ui.h"
#include "editor/live/live_panel.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_accounts.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/uitree_debug_overlay.h"
#include "platform/launch_sessions.h"
#include "torirsmaped/torirs_maped_build.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
static char g_live[1024];

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

static void
make_dir(
    const char* root,
    const char* relative)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    mkdir(path, 0755);
}

static int
file_has(
    const char* path,
    const char* needle)
{
    FILE* f = fopen(path, "rb");
    static char text[65536];
    size_t n;
    if( !f )
        return 0;
    n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return strstr(text, needle) != NULL;
}

static void
write_fixture(const char* root)
{
    char content[1200];
    snprintf(content, sizeof(content), "%s/content", root);
    make_dir(root, "");
    make_dir(root, "content");
    make_dir(root, "content/configs");
    make_dir(root, "content/maps");
    make_dir(root, "content/pack");
    make_dir(root, "content/server");
    make_dir(root, "content/server/scripts");
    make_dir(root, "content/server/scripts/quests");
    make_dir(root, "content/server/scripts/quests/scripts");
    make_dir(root, "content/server/scripts/quests/configs");
    make_dir(root, "live");
    make_dir(root, "live/saves");
    write_text(content, "configs/all.varp", "[varp29_cookquest]\nscope=perm\n\n[varp281_tutorial]\nscope=perm\n");
    write_text(content, "configs/all.varp.compack", "29=varp29_cookquest\n281=varp281_tutorial\n");
    write_text(content, "configs/all.varbit", "[varb5078_soulbearer]\nbasevar=varp281_tutorial\nstartbit=30\nendbit=31\n");
    write_text(content, "configs/all.varbit.compack", "5078=varb5078_soulbearer\n");
    write_text(content, "configs/all.obj", "[coins]\nname=Coins\n\n[bronze_sword]\nname=Bronze sword\n");
    write_text(content, "configs/all.obj.compack", "995=coins\n1277=bronze_sword\n");
    write_text(content, "configs/all.dbrow",
        "[quest_cooksassistant]\ntable=quest\ndata=displayname,Cook's Assistant\n\n[miniquest_bearyoursoul]\ntable=quest\n"
        "data=displayname,Bear Your Soul\n");
    write_text(content, "server/scripts/quests/scripts/quest_cheat.rs2",
        "if ($row = quest_cooksassistant) {\n    if (%varp29_cookquest >= ^cook_complete) {\n        return(^questcheat_already);\n    }\n}\n"
        "if ($row = miniquest_bearyoursoul) {\n    if (%varb5078_soulbearer >= ^bys_complete) {\n        return(^questcheat_already);\n    }\n}\n");
    write_text(content, "server/scripts/quests/configs/q.constant", "^cook_complete = 2\n^bys_complete = 3\n");
    write_text(root, "live/saves/tester.ini",
        "; a save\n[player]\nversion = 1\nname = tester\nx = 3206\nz = 3233\nlevel = 0\n\n[varps]\n281 = 1000\n\n[inv]\n0 = 1277 1\n");
    write_text(root, "fixture.ini", "[player]\nversion = 1\nname = fixture\nx = 3100\nz = 3100\nlevel = 0\n");
    write_text(root, "live/accounts.ini", "; test registry\n[passwords]\ntester = Secret123x\n");
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
frame(void)
{
    frame_of(&g_chrome, &g_ui);
}

/** Pump the embedded server and the UI until `done` says so or `frames`
 *  passed. */
static int
settle(
    int (*done)(void),
    int frames)
{
    for( int i = 0; i < frames; i++ )
    {
        Editor_PumpFacts(&g_editor);
        frame();
        if( done && done() )
            return 1;
    }
    return done ? done() : 1;
}

static void
activate(int widget)
{
    assert(widget >= 0);
    g_chrome.activated = widget;
    frame();
    Editor_PumpFacts(&g_editor);
    frame();
}

static int
find_panel(
    struct ToriRSChrome* chrome,
    const char* title)
{
    for( int i = 0; i < chrome->panel_count; i++ )
        if( strcmp(chrome->panels[i].title, title) == 0 )
            return i;
    return -1;
}

/** The nth (0 first) widget of `panel` and `kind` whose label or text is
 *  `caption` (NULL: any). */
static int
find_widget(
    struct ToriRSChrome* chrome,
    int panel,
    int kind,
    const char* caption,
    int nth)
{
    for( int i = 0; i < chrome->widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( !caption || strcmp(w->label, caption) == 0 || strcmp(w->text, caption) == 0 )
            if( nth-- == 0 )
                return i;
    }
    return -1;
}

/** 1 when a row on screen in VLIST `list` contains `needle`. */
static int
vlist_has(
    int list,
    const char* needle)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[list];
    for( int i = 0; i < w->rows; i++ )
    {
        const struct ToriRSChromeVRow* row = &g_chrome.vrows[w->vrow_first + i];
        if( strstr(row->text, needle) )
            return 1;
    }
    return 0;
}

static int
console_has(
    struct EditorUi* ui,
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
on_state(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count)
{
    Editor_UiApplyState((struct EditorUi*)user, key, values, count);
}

static const struct Editor_AccountsModel* g_model;

static int
list_arrived(void)
{
    return g_model && g_model->list_status == EDITOR_HOST_OK && g_model->row_count >= 1;
}

static int
view_tester(void)
{
    return g_model && g_model->view.status == EDITOR_HOST_OK && strcmp(g_model->view.name, "tester") == 0;
}

static int g_want_serial;

static int
view_moved(void)
{
    return g_model && g_model->view_serial > g_want_serial;
}

static int
status_moved(void)
{
    return g_model && g_model->status_serial > g_want_serial;
}

static void
test_panel(const char* root)
{
    struct Editor peer;
    struct EditorHost peer_host;
    struct ToriRSChrome pv_chrome;
    struct EditorUi pv_ui;
    char save[1200];
    int panel;
    int dropdown;
    int status;
    int list_stats;
    int input_level;
    int button_set_level;
    int input_x;
    int input_z;
    int button_move;
    int input_find;
    int list_find;
    int input_value;
    int button_var_set;
    int list_quests;
    int button_mark;
    int list_items;
    int input_slot;
    int input_obj;
    int input_count;
    int button_item_set;
    int button_item_remove;
    int button_item_pick;
    int input_name;
    int input_password;
    int button_new;
    int button_use;
    int button_delete;
    int list_log;
    int label_queue;

    snprintf(save, sizeof(save), "%s/live/saves/tester.ini", root);
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1280, 900);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.app_services.user_data = &g_app_stand_in;
    Editor_Catalog_Register(&g_ui);
    Editor_Live_Register(&g_ui);
    Editor_Accounts_Register(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    Editor_SetStateCallback(&g_editor, on_state, &g_ui);
    g_model = Editor_AccountsPanelModel(&g_ui);
    panel = find_panel(&g_chrome, "Accounts");
    CHECK(panel >= 0 && Editor_UiFindPanel(&g_ui, "accounts") && g_model, "the panel registers as accounts");
    if( panel < 0 )
        return;
    CHECK(!g_chrome.panels[panel].visible, "it starts closed");
    CHECK(Editor_UiTogglePanel(&g_ui, "accounts") == 1, "View toggles it open");
    CHECK(strcmp(Editor_ToolName(EDITOR_TOOL_PLACE_PLAYER), "Place player") == 0, "the tool is named Place player");
    CHECK(Editor_HotkeyFind("tool.place_player") == EDITOR_HK_TOOL_PLACE_PLAYER
              && EDITOR_HK_TOOL_PLACE_PLAYER - EDITOR_HK_TOOL_SELECT == (int)EDITOR_TOOL_PLACE_PLAYER
              && Editor_HotkeyForPanel("accounts") == EDITOR_HK_VIEW_ACCOUNTS,
        "the hotkey table has the tool in enum order and the View action");

    dropdown = find_widget(&g_chrome, panel, TORIRS_CHROME_W_DROPDOWN, "Account", 0);
    status = find_widget(&g_chrome, panel, TORIRS_CHROME_W_LABEL, NULL, 1);
    list_stats = find_widget(&g_chrome, panel, TORIRS_CHROME_W_VLIST, "Stats", 0);
    input_level = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Level", 1);
    button_set_level = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Set level", 0);
    input_x = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "X", 0);
    input_z = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Z", 0);
    button_move = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Move", 0);
    input_find = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Find", 0);
    list_find = find_widget(&g_chrome, panel, TORIRS_CHROME_W_VLIST, "Matches", 0);
    input_value = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Value", 0);
    button_var_set = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Set", 0);
    list_quests = find_widget(&g_chrome, panel, TORIRS_CHROME_W_VLIST, "Quests", 0);
    button_mark = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Mark complete", 0);
    label_queue = find_widget(&g_chrome, panel, TORIRS_CHROME_W_LABEL, "0 queued cheat(s) for the next login", 0);
    list_items = find_widget(&g_chrome, panel, TORIRS_CHROME_W_VLIST, "Slots", 0);
    input_slot = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Slot", 0);
    input_obj = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Obj", 0);
    input_count = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Count", 0);
    button_item_set = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Set", 1);
    button_item_remove = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Remove", 0);
    button_item_pick = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "From Catalog pick", 0);
    input_name = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Name", 0);
    input_password = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Password", 0);
    button_new = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "New account", 0);
    button_use = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Use for RUN / Play", 0);
    button_delete = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Delete", 0);
    list_log = find_widget(&g_chrome, panel, TORIRS_CHROME_W_VLIST, "Log", 0);
    CHECK(dropdown >= 0 && status >= 0 && list_stats >= 0 && input_level >= 0 && button_set_level >= 0 && input_x >= 0
              && input_z >= 0 && button_move >= 0 && input_find >= 0 && list_find >= 0 && input_value >= 0
              && button_var_set >= 0 && list_quests >= 0 && button_mark >= 0 && label_queue >= 0 && list_items >= 0
              && input_slot >= 0 && input_obj >= 0 && input_count >= 0 && button_item_set >= 0 && button_item_remove >= 0
              && button_item_pick >= 0 && input_name >= 0 && input_password >= 0 && button_new >= 0 && button_use >= 0
              && button_delete >= 0 && list_log >= 0,
        "its widgets are there");
    if( dropdown < 0 || list_stats < 0 || button_item_set < 0 || list_log < 0 )
        return;

    /* The list arrives and the first (only) account is shown. */
    CHECK(settle(list_arrived, 200), "the lane's accounts arrive (%d row(s), status %u)", g_model->row_count,
        g_model->list_status);
    CHECK(g_chrome.widgets[dropdown].option_count == 1 && strcmp(g_chrome.widgets[dropdown].options[0], "tester") == 0,
        "the dropdown lists tester (%d)", g_chrome.widgets[dropdown].option_count);
    CHECK(settle(view_tester, 200), "tester's view arrives");
    CHECK(strcmp(Editor_Accounts_Current(&g_ui), "tester") == 0, "tester is the shown account");
    CHECK(strstr(g_chrome.widgets[status].text, "tester: offline: edits write the save") != NULL, "the status row says offline (%s)",
        g_chrome.widgets[status].text);
    CHECK(vlist_has(list_stats, "hitpoints     lvl 10  xp 1154"), "the Stats list shows the fresh character's hitpoints");

    /* Stats: cooking 50 writes the save, the row follows. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 1);
    frame();
    ToriRSChrome_VListSetSelected(&g_chrome, list_stats, 7);
    ToriRSChrome_SetText(&g_chrome, input_level, "50");
    g_want_serial = g_model->view_serial;
    activate(button_set_level);
    CHECK(settle(view_moved, 200), "the view comes back after Set level");
    CHECK(file_has(save, "7 = 50 1013330"), "cooking 50 is in the save");
    CHECK(g_model->view.boosted[7] == 50 && vlist_has(list_stats, "cooking       lvl 50  xp 101333"), "the Stats row reads lvl 50");
    CHECK(console_has(&g_ui, "offline: wrote saves/tester.ini (cooking level 50)"), "the console says which path it took");
    ToriRSChrome_SetText(&g_chrome, input_level, "0");
    g_want_serial = g_model->status_serial;
    activate(button_set_level);
    CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_REFUSED && strstr(g_model->last_message, "1..99"),
        "a level of 0 is refused and the Log says so (%s)", g_model->last_message);
    CHECK(vlist_has(list_log, "(refused)"), "the Log lists the refusal");

    /* Position: Move, then the Place player tool. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 0);
    frame();
    ToriRSChrome_SetText(&g_chrome, input_x, "3222");
    ToriRSChrome_SetText(&g_chrome, input_z, "3218");
    g_want_serial = g_model->view_serial;
    activate(button_move);
    CHECK(settle(view_moved, 200) && g_model->view.x == 3222 && g_model->view.z == 3218 && file_has(save, "x = 3222\nz = 3218\n"),
        "Move writes the position (%d,%d)", g_model->view.x, g_model->view.z);
    CHECK(!Editor_Accounts_PlacePlayer(&g_ui, 3300, 3300, 0) || settle(NULL, 20),
        "Place player for the lane's default play account (no save: the status says ABSENT)");
    g_want_serial = g_model->status_serial;
    (void)Editor_Accounts_PlacePlayer(&g_ui, 3300, 3300, 0);
    CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_ABSENT && console_has(&g_ui, "no save"),
        "the default play account has no save: ABSENT in the console (%s)", g_model->last_message);
    g_want_serial = g_model->status_serial;
    activate(button_use);
    CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_OK, "Use for RUN / Play: %s", g_model->last_message);
    CHECK(settle(NULL, 20) && strcmp(g_model->play_account, "tester") == 0, "the list says tester is the play account");
    g_want_serial = g_model->view_serial;
    CHECK(Editor_Accounts_PlacePlayer(&g_ui, 3300, 3301, 1), "Place player sends for tester");
    CHECK(settle(view_moved, 200) && g_model->view.x == 3300 && g_model->view.z == 3301 && g_model->view.level == 1,
        "the tool's click moved the save (%d,%d,%d)", g_model->view.x, g_model->view.z, g_model->view.level);
    CHECK(console_has(&g_ui, "offline: wrote saves/tester.ini (position 3300,3301 level 1)"),
        "the console says the offline path");

    /* Vars: Find through the catalog, Set. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 2);
    frame();
    ToriRSChrome_SetText(&g_chrome, input_find, "cookquest");
    settle(NULL, 30);
    CHECK(vlist_has(list_find, "varp 29 varp29_cookquest"), "Find lists varp 29 once the typing settled");
    ToriRSChrome_VListSetSelected(&g_chrome, list_find, 0);
    activate(list_find);
    ToriRSChrome_SetText(&g_chrome, input_value, "2");
    g_want_serial = g_model->view_serial;
    activate(button_var_set);
    CHECK(settle(view_moved, 200) && file_has(save, "29 = 2\n"), "Set writes varp 29 = 2 into the save");
    {
        int32_t value = 0;
        CHECK(Editor_AccountsVarp(&g_model->view, 29, &value) && value == 2, "the view carries varp 29 = 2");
    }

    /* Quests: the rows, the state, a queued mark. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 3);
    settle(NULL, 60);
    CHECK(g_model->quest_count == 2 && vlist_has(list_quests, "[x] Cook's Assistant") && vlist_has(list_quests, "[ ] Bear Your Soul"),
        "the Quests list: Cook's Assistant complete (varp 29 = 2), Bear Your Soul not (%d rows)", g_model->quest_count);
    ToriRSChrome_VListSetSelected(&g_chrome, list_quests, 1);
    g_want_serial = g_model->view_serial;
    activate(button_mark);
    CHECK(settle(view_moved, 200) && g_model->view.queue_count == 1 && strcmp(g_model->view.queue[0], "questmark miniquest_bearyoursoul") == 0,
        "Mark complete offline is queued (%d)", g_model->view.queue_count);
    CHECK(strcmp(g_chrome.widgets[label_queue].text, "1 queued cheat(s) for the next login") == 0, "the queue label (%s)",
        g_chrome.widgets[label_queue].text);
    {
        char registry[1200];
        snprintf(registry, sizeof(registry), "%s/live/accounts.ini", root);
        CHECK(file_has(registry, "tester.0 = questmark miniquest_bearyoursoul"), "the registry holds the queued line");
    }

    /* Items: a slot by name, from the Catalog's pick, removed. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 4);
    frame();
    CHECK(vlist_has(list_items, "0: obj 1277 x1") && vlist_has(list_items, "1: (empty)"), "the backpack rows");
    ToriRSChrome_SetText(&g_chrome, input_slot, "3");
    ToriRSChrome_SetText(&g_chrome, input_obj, "coins");
    ToriRSChrome_SetText(&g_chrome, input_count, "100");
    g_want_serial = g_model->view_serial;
    activate(button_item_set);
    CHECK(settle(view_moved, 200) && file_has(save, "3 = 995 100\n") && vlist_has(list_items, "3: obj 995 x100"),
        "Set puts coins x100 in slot 3 by name");
    CHECK(Editor_Catalog_PickRecord(&g_ui, TORIRSMAPED_CATALOG_OBJ, "bronze_sword"), "pick bronze_sword in the Catalog");
    activate(button_item_pick);
    CHECK(strcmp(g_chrome.widgets[input_obj].text, "1277") == 0, "From Catalog pick fills Obj with 1277 (%s)",
        g_chrome.widgets[input_obj].text);
    ToriRSChrome_SetText(&g_chrome, input_slot, "3");
    g_want_serial = g_model->view_serial;
    activate(button_item_remove);
    CHECK(settle(view_moved, 200) && !file_has(save, "3 = 995 100\n") && vlist_has(list_items, "3: (empty)"), "Remove empties slot 3");

    /* New account, then Delete. */
    ToriRSChrome_SetText(&g_chrome, input_name, "second");
    ToriRSChrome_SetText(&g_chrome, input_password, "Other9pass");
    g_want_serial = g_model->status_serial;
    activate(button_new);
    CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_OK, "New account: %s", g_model->last_message);
    settle(NULL, 40);
    CHECK(g_model->row_count == 2 && strcmp(Editor_Accounts_Current(&g_ui), "second") == 0
              && g_chrome.widgets[dropdown].option_count == 2,
        "the list has two accounts and second is shown (%s)", Editor_Accounts_Current(&g_ui));
    ToriRSChrome_SetText(&g_chrome, input_name, "x");
    ToriRSChrome_SetText(&g_chrome, input_password, "short");
    g_want_serial = g_model->status_serial;
    activate(button_new);
    CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_REFUSED, "a bad password is refused: %s",
        g_model->last_message);

    /* A second window of the Client sees the list after a pump. */
    if( Editor_HostOpenMapEdEmbedPeer(&peer_host, &g_editor.host, TORIRSMAPED_ROLE_GENERIC, 1) )
    {
        const struct Editor_AccountsModel* pv_model;
        Editor_OpenHost(&peer, &peer_host, "popped-out accounts", &g_profile);
        ToriRSChrome_Init(&pv_chrome);
        ToriRSChrome_SetSurface(&pv_chrome, 800, 600);
        Editor_UiInit(&pv_ui, &pv_chrome, &peer);
        pv_ui.app_services.user_data = &g_app_stand_in;
        Editor_Catalog_Register(&pv_ui);
        Editor_Live_Register(&pv_ui);
        Editor_Accounts_Register(&pv_ui);
        CHECK(Editor_UiSetSolo(&pv_ui, "accounts"), "a window can be the Accounts panel alone");
        Editor_UiSetVisible(&pv_ui, 1);
        Editor_SetStateCallback(&peer, on_state, &pv_ui);
        pv_model = Editor_AccountsPanelModel(&pv_ui);
        for( int i = 0; i < 200 && !(pv_model && pv_model->row_count == 2); i++ )
        {
            Editor_PumpFacts(&peer);
            frame_of(&pv_chrome, &pv_ui);
        }
        CHECK(pv_model && pv_model->row_count == 2 && strcmp(pv_model->play_account, "tester") == 0,
            "the second window lists both accounts and the play account");
        /* The first window's Delete reaches it. */
        g_want_serial = g_model->status_serial;
        activate(button_delete);
        CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_OK, "Delete second: %s", g_model->last_message);
        for( int i = 0; i < 200 && pv_model->row_count != 1; i++ )
        {
            Editor_PumpFacts(&peer);
            frame_of(&pv_chrome, &pv_ui);
        }
        CHECK(pv_model->row_count == 1, "the second window's list follows the delete (%d)", pv_model->row_count);
        settle(NULL, 40);
        CHECK(strcmp(Editor_Accounts_Current(&g_ui), "tester") == 0, "the main window falls back to tester");
        Editor_Close(&peer);
    }
    else
        CHECK(0, "a peer connection to the embedded server opens");
}

int
main(
    int argc,
    char** argv)
{
    struct EditorHost host;
    const char* root;
    char content[1200];

    if( argc < 3 )
    {
        fprintf(stderr, "usage: %s <tmp-dir> <repo-root>\n", argv[0]);
        return 2;
    }
    root = argv[1];
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    /* The editor server starts its children through the host's spawner, as
     * the client and torirsmaped hand it theirs (torirs_maped_build.h). */
    ToriRSMapEd_SetSpawner(LaunchSessions_SystemSpawner());
    if( !RSCache_ProfileByName("osrs241", &g_profile) )
    {
        printf("FAIL: no osrs241 profile\n");
        return 1;
    }
    write_fixture(root);
    snprintf(content, sizeof(content), "%s/content", root);
    snprintf(g_live, sizeof(g_live), "%s/live", root);
    /* The embedded server binds the lane from the environment (nothing in
     * it is launched: the binaries are never run by this test). */
    setenv("TORIRSMAPED_LIVE_DIR", g_live, 1);
    setenv("TORIRSMAPED_LIVE_SERVER_BIN", "/nonexistent/torirsserver", 1);
    setenv("TORIRSMAPED_LIVE_PLAY_BIN", "/nonexistent/torirs", 1);
    if( !Editor_HostOpenMapEdEmbed(&host, content, argv[2]) )
    {
        printf("FAIL: the embedded ToriRSMapEd did not open %s\n", content);
        return 1;
    }
    Editor_OpenHost(&g_editor, &host, content, &g_profile);
    CHECK(g_editor.writable, "the tmp tree is writable");
    CHECK((g_editor.capabilities & TORIRSMAPED_CAP_LIVE) != 0, "the embedded server bound the lane (live capability)");
    /* The catalog over the small tree, so names resolve at once. */
    ToriRSMapEdCatalog_Refresh(Editor_HostMapEdEmbedServer(&host)->catalog, 1, NULL);
    test_panel(root);
    Editor_Close(&g_editor);
    printf("editor_panels_accounts_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
