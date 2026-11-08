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
 * the Items tab draws the worn layout and the backpack's grid with a key per
 * slot, a pressed slot fills the inputs, Set puts an item in a slot (by name
 * and from the Catalog's pick) and Remove takes it out, an inv opened by name
 * gets its section on the first Set; a loadout is saved, listed, drawn,
 * applied and deleted; the quest filter, counts, detail, reset and "Complete
 * shown"; the play account's look for Place player's ghost; New account /
 * Use for RUN / Delete; a second window of the Client sees the list after a
 * pump; the tool's name and hotkey. Links the client's
 * objects minus main.o, as test-editor-panels does.
 */

#include "editor/accounts/accounts_panel.h"
#include "editor/panels/edpanel_chips.h"
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
    write_text(content, "configs/all.obj",
        "[coins]\nname=Coins\n\n[bronze_sword]\nname=Bronze sword\nwearpos=3\n\n[bronze_med_helm]\nname=Bronze med helm\nwearpos=0\n");
    write_text(content, "configs/all.obj.compack", "995=coins\n1277=bronze_sword\n1139=bronze_med_helm\n");
    write_text(content, "configs/all.dbrow",
        "[quest_cooksassistant]\ntable=quest\ndata=displayname,Cook's Assistant\n\n[miniquest_bearyoursoul]\ntable=quest\n"
        "data=displayname,Bear Your Soul\n");
    write_text(content, "server/scripts/quests/scripts/quest_cheat.rs2",
        "if ($row = quest_cooksassistant) {\n    if (%varp29_cookquest >= ^cook_complete) {\n        return(^questcheat_already);\n    }\n}\n"
        "if ($row = miniquest_bearyoursoul) {\n    if (%varb5078_soulbearer >= ^bys_complete) {\n        return(^questcheat_already);\n    }\n}\n");
    write_text(content, "server/scripts/quests/configs/q.constant", "^cook_complete = 2\n^bys_complete = 3\n");
    write_text(content, "configs/all.inv", "[inv]\nsize=28\n\n[worn]\nsize=14\n\n[bank]\nsize=1410\n\n[tob_chests]\nsize=12\n");
    write_text(content, "configs/all.inv.compack", "93=inv\n94=worn\n95=bank\n612=tob_chests\n");
    make_dir(root, "content/server/scripts/player");
    make_dir(root, "content/server/scripts/player/configs");
    write_text(content, "server/scripts/player/configs/appearance.enum",
        "[default_appearance]\ninputtype=int\noutputtype=int\ndefault=-1\nval=4,18\nval=8,0\n");
    write_text(root, "live/saves/tester.ini",
        "; a save\n[player]\nversion = 1\nname = tester\nx = 3206\nz = 3233\nlevel = 0\n\n[appearance]\ngender = 1\n"
        "kit8 = 7\ncolours = 1 2 3 4 5\n\n[varps]\n281 = 1000\n\n[inv]\n0 = 1277 1\n\n[worn]\n3 = 1277 1\n");
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

static void
press_graph(
    int graph,
    int x,
    int y)
{
    struct ToriRSChromeEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = TORIRS_CHROME_EVENT_GRAPH_PRESS;
    event.widget = graph;
    event.serial = g_chrome.widgets[graph].serial;
    event.x = x;
    event.y = y;
    event.key = ToriRSChrome_GraphKeyAt(&g_chrome, graph, x, y);
    (void)ToriRSChrome_PushEvent(&g_chrome, &event);
    frame();
}

static void
choose(
    int dropdown,
    int option)
{
    ToriRSChrome_DropdownSetSelected(&g_chrome, dropdown, option);
    activate(dropdown);
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

/** Where GRAPH `graph` answers `key` (well-local), scanning its top rows;
 *  1 when found. */
static int
graph_key_find(
    int graph,
    int key,
    int* out_x,
    int* out_y)
{
    for( int y = 1; y < 40; y += 2 )
        for( int x = 1; x < 360; x += 2 )
            if( ToriRSChrome_GraphKeyAt(&g_chrome, graph, x, y) == key )
            {
                if( out_x )
                    *out_x = x;
                if( out_y )
                    *out_y = y;
                return 1;
            }
    return 0;
}

/** The absolute row of VLIST `list` on screen whose text holds `needle`, -1. */
static int
vlist_row_of(
    int list,
    const char* needle)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[list];
    int first = 0;
    int shown = 0;
    (void)ToriRSChrome_VListWindow(&g_chrome, list, &first, &shown);
    for( int i = 0; i < w->rows; i++ )
    {
        const struct ToriRSChromeVRow* row = &g_chrome.vrows[w->vrow_first + i];
        if( strstr(row->text, needle) )
            return first + i;
    }
    return -1;
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
loadouts_moved(void)
{
    return g_model && g_model->loadouts_serial > g_want_serial;
}

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
    int dropdown_container;
    int graph_items;
    int input_inv;
    int button_inv_open;
    int button_clear;
    int list_loadouts;
    int graph_loadout;
    int input_loadout_name;
    int button_loadout_save;
    int button_loadout_apply;
    int button_loadout_delete;
    int dropdown_quest_filter;
    int label_quest_summary;
    int label_quest_detail;
    int button_quest_reset;
    int button_quest_complete_shown;
    int label_slot;
    int label_verify;
    int input_put;
    int input_count;
    int list_put;
    int graph_put;
    int button_item_remove;
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
    status = find_widget(&g_chrome, panel, TORIRS_CHROME_W_LABEL, NULL, 0);
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
    dropdown_container = find_widget(&g_chrome, panel, TORIRS_CHROME_W_DROPDOWN, "Container", 0);
    graph_items = find_widget(&g_chrome, panel, TORIRS_CHROME_W_GRAPH, NULL, 0);
    graph_put = find_widget(&g_chrome, panel, TORIRS_CHROME_W_GRAPH, NULL, 1);
    graph_loadout = find_widget(&g_chrome, panel, TORIRS_CHROME_W_GRAPH, NULL, 2);
    input_inv = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Inv", 0);
    button_inv_open = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Open inv", 0);
    button_clear = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Clear container", 0);
    list_loadouts = find_widget(&g_chrome, panel, TORIRS_CHROME_W_VLIST, "Loadouts", 0);
    input_loadout_name = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Name", 1);
    button_loadout_save = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Save current gear", 0);
    button_loadout_apply = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Apply to account", 0);
    button_loadout_delete = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Delete loadout", 0);
    dropdown_quest_filter = find_widget(&g_chrome, panel, TORIRS_CHROME_W_DROPDOWN, "Show", 0);
    label_quest_summary = find_widget(&g_chrome, panel, TORIRS_CHROME_W_LABEL, "asking for the quest rows...", 0);
    label_quest_detail = find_widget(&g_chrome, panel, TORIRS_CHROME_W_LABEL, "pick a quest for its var and value", 0);
    button_quest_reset = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Reset", 0);
    button_quest_complete_shown = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Complete shown", 0);
    label_slot = find_widget(&g_chrome, panel, TORIRS_CHROME_W_LABEL, "click a slot", 0);
    label_verify = label_slot >= 0 ? label_slot + 1 : -1;
    input_put = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Put here", 0);
    input_count = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Count", 0);
    list_put = find_widget(&g_chrome, panel, TORIRS_CHROME_W_VLIST, "\x1f" "name" "\x1f" "\x1f" "wear" "\x1f" "id", 0);
    button_item_remove = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Remove", 0);
    input_name = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Name", 0);
    input_password = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Password", 0);
    button_new = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "New account", 0);
    button_use = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Use for RUN / Play", 0);
    button_delete = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Delete", 0);
    list_log = find_widget(&g_chrome, panel, TORIRS_CHROME_W_VLIST, "Log", 0);
    CHECK(dropdown >= 0 && status >= 0 && list_stats >= 0 && input_level >= 0 && button_set_level >= 0 && input_x >= 0
              && input_z >= 0 && button_move >= 0 && input_find >= 0 && list_find >= 0 && input_value >= 0
              && button_var_set >= 0 && list_quests >= 0 && button_mark >= 0 && label_queue >= 0 && dropdown_container >= 0
              && graph_items >= 0 && graph_loadout >= 0 && input_inv >= 0 && button_inv_open >= 0 && button_clear >= 0
              && list_loadouts >= 0 && input_loadout_name >= 0 && button_loadout_save >= 0 && button_loadout_apply >= 0
              && button_loadout_delete >= 0 && dropdown_quest_filter >= 0 && label_quest_summary >= 0
              && label_quest_detail >= 0 && button_quest_reset >= 0 && button_quest_complete_shown >= 0
              && label_slot >= 0 && input_put >= 0 && input_count >= 0 && list_put >= 0 && graph_put >= 0
              && button_item_remove >= 0 && input_name >= 0 && input_password >= 0 && button_new >= 0 && button_use >= 0
              && button_delete >= 0 && list_log >= 0,
        "its widgets are there");
    if( dropdown < 0 || list_stats < 0 || list_put < 0 || list_log < 0 )
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

    /* Only the picker and its status line sit above the tabs. */
    CHECK(g_chrome.widgets[dropdown].tab == -1 && g_chrome.widgets[status].tab == -1
              && g_chrome.widgets[button_use].tab == 0 && g_chrome.widgets[input_name].tab == 0
              && g_chrome.widgets[list_log].tab == 6,
        "the account picker and status are above the tabs; the account's buttons and the log have tabs of their own");

    /* Stats: cooking 50 writes the save, the row follows. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 3);
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
    /* The lane's default play account has never logged in: Place player
     * makes its first save (workbench 7.1) instead of refusing. */
    g_want_serial = g_model->status_serial;
    CHECK(Editor_Accounts_PlacePlayer(&g_ui, 3300, 3300, 0), "Place player for the lane's default play account");
    CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_OK && console_has(&g_ui, "first save"),
        "the default play account's first save is made, and the console says so (%s)", g_model->last_message);
    CHECK(settle(NULL, 20) && g_model->row_count == 2, "the list now has its save too (%d)", g_model->row_count);
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

    /* Place player's ghost: the play account's look (tester since Use). */
    {
        struct Editor_AccountLook look;
        int serial = -1;
        int known = 0;
        for( int i = 0; i < 200 && !known; i++ )
        {
            known = Editor_Accounts_PlayLook(&g_ui, &look, &serial);
            Editor_PumpFacts(&g_editor);
            frame();
        }
        CHECK(known && strcmp(look.name, "tester") == 0 && look.gender == 1 && look.kits[8] == 7 && look.kits[4] == 18
                  && look.kits[0] == -1 && look.colours[4] == 5 && look.worn[3] == 1277 && look.worn[0] == -1,
            "the play account's look: its gender, own hair kit, the content's default torso, colours and the worn sword");
    }

    /* Vars: Find through the catalog, Set. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 4);
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

    /* Quests: the rows, the state, the counts, the filter, a queued mark. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 5);
    settle(NULL, 60);
    CHECK(g_model->quest_count == 2 && vlist_has(list_quests, "[x] Cook's Assistant") && vlist_has(list_quests, "[ ] Bear Your Soul"),
        "the Quests list: Cook's Assistant complete (varp 29 = 2), Bear Your Soul not (%d rows)", g_model->quest_count);
    CHECK(strstr(g_chrome.widgets[label_quest_summary].text, "1 complete, 1 not, 0 unknown of 2") != NULL,
        "the counts (%s)", g_chrome.widgets[label_quest_summary].text);
    choose(dropdown_quest_filter, 2);
    CHECK(g_chrome.widgets[list_quests].imax == 1 && vlist_has(list_quests, "Cook's Assistant"),
        "Show: Complete keeps only Cook's Assistant (%d rows)", g_chrome.widgets[list_quests].imax);
    choose(dropdown_quest_filter, 0);
    ToriRSChrome_VListSetSelected(&g_chrome, list_quests, 0);
    activate(list_quests);
    CHECK(strstr(g_chrome.widgets[label_quest_detail].text, "quest_cooksassistant: varp 29 = 2, complete at 2") != NULL,
        "the detail names the var and value (%s)", g_chrome.widgets[label_quest_detail].text);
    g_want_serial = g_model->view_serial;
    activate(button_quest_reset);
    CHECK(settle(view_moved, 200) && !file_has(save, "29 = 2\n") && vlist_has(list_quests, "[ ] Cook's Assistant"),
        "Reset puts Cook's Assistant's varp back to 0 offline");
    ToriRSChrome_VListSetSelected(&g_chrome, list_quests, 1);
    g_want_serial = g_model->view_serial;
    activate(button_mark);
    CHECK(settle(view_moved, 200) && g_model->view.queue_count == 1 && strcmp(g_model->view.queue[0], "questmark miniquest_bearyoursoul") == 0,
        "Mark complete offline is queued (%d)", g_model->view.queue_count);
    CHECK(strcmp(g_chrome.widgets[label_queue].text, "1 queued cheat(s) for the next login") == 0, "the queue label (%s)",
        g_chrome.widgets[label_queue].text);
    g_want_serial = g_model->view_serial;
    activate(button_quest_complete_shown);
    CHECK(settle(view_moved, 200) && g_model->view.queue_count == 3
              && strcmp(g_model->view.queue[1], "complete quest_cooksassistant") == 0,
        "Complete shown queues every shown quest not complete (%d)", g_model->view.queue_count);
    {
        char registry[1200];
        snprintf(registry, sizeof(registry), "%s/live/accounts.ini", root);
        CHECK(file_has(registry, "tester.0 = questmark miniquest_bearyoursoul"), "the registry holds the queued line");
    }

    /* Items: the worn layout, the backpack's grid, the slot line, Put here
     * (a row click sets at once, #id on the wire; the wear chip narrows to
     * the slot and its x widens; a wrong slot is refused here), the Catalog
     * pick chip, the check against the view, Remove; an inv opened by name. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 1);
    settle(NULL, 10);
    CHECK(g_chrome.widgets[dropdown_container].option_count == 3
              && strcmp(g_chrome.widgets[dropdown_container].options[0], "Equipment") == 0
              && strcmp(g_chrome.widgets[dropdown_container].options[2], "Bank") == 0,
        "the container picker: Equipment, Backpack, Bank (%d)", g_chrome.widgets[dropdown_container].option_count);
    CHECK(ToriRSChrome_GraphKeyAt(&g_chrome, graph_items, 8 + 77 + 10, 2 + 4 + 10) == 1
              && ToriRSChrome_GraphKeyAt(&g_chrome, graph_items, 8 + 21 + 10, 2 + 82 + 10) == 4
              && ToriRSChrome_GraphKeyAt(&g_chrome, graph_items, 8 + 133 + 10, 2 + 162 + 10) == 13,
        "the worn layout has head, weapon and ring where the game draws them");
    CHECK(g_chrome.widgets[graph_put].hidden, "no chips before a slot is selected");
    press_graph(graph_items, 8 + 21 + 10, 2 + 82 + 10);
    CHECK(strcmp(g_chrome.widgets[label_slot].text, "weapon (slot 3): bronze_sword (1277) x1") == 0,
        "pressing the weapon slot names what it holds by its pack name (%s)", g_chrome.widgets[label_slot].text);
    settle(NULL, 3);
    CHECK(!g_chrome.widgets[graph_put].hidden && graph_key_find(graph_put, EDP_CHIP_KEY_BASE, NULL, NULL)
              && graph_key_find(graph_put, EDP_CHIP_X_BASE, NULL, NULL),
        "the wear chip is up, with its x");
    ToriRSChrome_SetText(&g_chrome, input_put, "bronze");
    settle(NULL, 20);
    CHECK(vlist_has(list_put, "bronze_sword") && !vlist_has(list_put, "bronze_med_helm"),
        "Put here narrows to what the weapon slot wears");
    {
        int x = 0;
        int y = 0;
        CHECK(graph_key_find(graph_put, EDP_CHIP_X_BASE, &x, &y), "the wear chip's x is where a press finds it");
        press_graph(graph_put, x, y);
    }
    settle(NULL, 20);
    CHECK(vlist_has(list_put, "bronze_med_helm") && vlist_has(list_put, "weapon") && vlist_has(list_put, "head"),
        "the x widens the search to every obj, each row with its wear slot");
    {
        int const row = vlist_row_of(list_put, "bronze_med_helm");
        CHECK(row >= 0, "the helm has a row");
        ToriRSChrome_VListSetSelected(&g_chrome, list_put, row);
        activate(list_put);
        CHECK(console_has(&g_ui, "bronze_med_helm (1139) is worn in the head slot; this is weapon")
                  && file_has(save, "3 = 1277 1\n") && !file_has(save, "3 = 1139 1\n"),
            "a helm into the weapon slot is refused in the panel, nothing sent");
    }
    /* A scrolled panel: a real press at the weapon slot's screen place must
     * still select the weapon slot (the tab grew taller than the dock). */
    {
        struct ToriRSChromeWidget const* w = &g_chrome.widgets[graph_items];
        int const scale = ToriRSChrome_Scale(&g_chrome) > 0 ? ToriRSChrome_Scale(&g_chrome) : 1;
        int before;
        int x;
        int y;
        press_graph(graph_items, 8 + 77 + 10, 2 + 4 + 10); /* head */
        CHECK(strncmp(g_chrome.widgets[label_slot].text, "head", 4) == 0, "the head slot is selected (%s)",
            g_chrome.widgets[label_slot].text);
        before = w->y;
        /* A dock height shorter than the tab's rows: the panel scrolls. */
        ToriRSChrome_PanelSetFixedHeight(&g_chrome, panel, 360);
        frame();
        ToriRSChrome_PanelSetScroll(&g_chrome, panel, 60);
        frame();
        frame();
        CHECK(w->y == before - 60, "scrolling the panel 60 px moves the well up 60 (%d -> %d)", before, w->y);
        x = w->x + 1 + (8 + 21 + 10) * scale;
        y = w->y + 1 + (2 + 82 + 10) * scale;
        ToriRSChrome_MouseDown(&g_chrome, x, y);
        ToriRSChrome_MouseUp(&g_chrome, x, y);
        frame();
        CHECK(strncmp(g_chrome.widgets[label_slot].text, "weapon", 6) == 0,
            "a press at the weapon slot's screen place on a scrolled panel selects weapon (%s)", g_chrome.widgets[label_slot].text);
        /* The same at chrome scale 2 (a Retina Mac's Auto): the well's
         * items map through the drawn scale. */
        ToriRSChrome_SetScale(&g_chrome, 2);
        ToriRSChrome_PanelSetFixedHeight(&g_chrome, panel, 760);
        frame();
        frame();
        press_graph(graph_items, 8 + 77 + 10, 2 + 4 + 10); /* head, well-local */
        ToriRSChrome_PanelSetScroll(&g_chrome, panel, 120);
        frame();
        frame();
        x = w->x + ToriRSChrome_Px(&g_chrome, 1) + (8 + 21 + 10) * 2;
        y = w->y + ToriRSChrome_Px(&g_chrome, 1) + (2 + 82 + 10) * 2;
        ToriRSChrome_MouseDown(&g_chrome, x, y);
        ToriRSChrome_MouseUp(&g_chrome, x, y);
        frame();
        CHECK(strncmp(g_chrome.widgets[label_slot].text, "weapon", 6) == 0,
            "at scale 2, a press at the weapon slot's screen place on a scrolled panel selects weapon (%s; hit %d vs well %d; "
            "well %d,%d %dx%d view_h %d clip %d,%d %dx%d; press %d,%d; key at well-local %d)",
            g_chrome.widgets[label_slot].text, ToriRSChrome_HitTest(&g_chrome, x, y), graph_items, w->x, w->y, w->w, w->h,
            w->view_h, w->custom_clip_x, w->custom_clip_y, w->custom_clip_w, w->custom_clip_h, x, y,
            ToriRSChrome_GraphKeyAt(&g_chrome, graph_items, 8 + 21 + 10, 2 + 82 + 10));
        ToriRSChrome_SetScale(&g_chrome, 1);
        ToriRSChrome_PanelSetScroll(&g_chrome, panel, 0);
        ToriRSChrome_PanelSetFixedHeight(&g_chrome, panel, 0);
        frame();
        frame();
    }

    /* The backpack: coins x100 into slot 3 by a row click, #id on the wire. */
    choose(dropdown_container, 1);
    CHECK(ToriRSChrome_GraphKeyAt(&g_chrome, graph_items, 8 + 3 * 42 + 5, 4 + 5) == 4
              && ToriRSChrome_GraphKeyAt(&g_chrome, graph_items, 8 + 3 * 42 + 5, 4 + 6 * 36 + 5) == 28,
        "the backpack is the gameframe's 4 x 7");
    press_graph(graph_items, 8 + 3 * 42 + 5, 4 + 5);
    CHECK(strcmp(g_chrome.widgets[label_slot].text, "slot 3: empty") == 0, "pressing backpack slot 3 selects it (%s)",
        g_chrome.widgets[label_slot].text);
    ToriRSChrome_SetText(&g_chrome, input_put, "coins");
    ToriRSChrome_SetText(&g_chrome, input_count, "100");
    settle(NULL, 20);
    CHECK(vlist_has(list_put, "coins") && vlist_has(list_put, "995"), "Put here lists coins with its id");
    g_want_serial = g_model->view_serial;
    ToriRSChrome_VListSetSelected(&g_chrome, list_put, 0);
    activate(list_put);
    {
        const struct Editor_AccountItem* item;
        settle(view_moved, 200);
        item = Editor_AccountsItem(&g_model->view, EDITOR_ACCOUNTS_SECTION_INV, 3);
        CHECK(file_has(save, "3 = 995 100\n") && item && item->obj == 995 && item->count == 100,
            "a row click puts coins x100 in backpack slot 3 at once");
        CHECK(g_model->last_obj == 995 && g_model->last_count == 100, "the status fact carries the resolved item (%d x%d)",
            g_model->last_obj, g_model->last_count);
        settle(NULL, 5);
        CHECK(strcmp(g_chrome.widgets[label_verify].text, "view agrees with the last Set") == 0,
            "the view is checked against the request (%s)", g_chrome.widgets[label_verify].text);
        CHECK(strcmp(g_chrome.widgets[label_slot].text, "slot 3: coins (995) x100") == 0, "the slot line follows (%s)",
            g_chrome.widgets[label_slot].text);
    }
    /* The Catalog's pick as a chip: bronze_sword into slot 4. */
    CHECK(Editor_Catalog_PickRecord(&g_ui, TORIRSMAPED_CATALOG_OBJ, "bronze_sword"), "pick bronze_sword in the Catalog");
    press_graph(graph_items, 8 + 5, 4 + 36 + 5);
    ToriRSChrome_SetText(&g_chrome, input_count, "1");
    settle(NULL, 3);
    {
        int x = 0;
        int y = 0;
        CHECK(graph_key_find(graph_put, EDP_CHIP_KEY_BASE, &x, &y), "the Catalog pick chip is up in the backpack");
        g_want_serial = g_model->view_serial;
        press_graph(graph_put, x, y);
        CHECK(settle(view_moved, 200) && file_has(save, "4 = 1277 1\n"), "the pick chip puts the pick in slot 4");
    }
    g_want_serial = g_model->view_serial;
    activate(button_item_remove);
    CHECK(settle(view_moved, 200) && !file_has(save, "4 = 1277 1\n"), "Remove empties slot 4 again");
    press_graph(graph_items, 8 + 3 * 42 + 5, 4 + 5);
    g_want_serial = g_model->view_serial;
    activate(button_item_remove);
    CHECK(settle(view_moved, 200) && !file_has(save, "3 = 995 100\n")
              && !Editor_AccountsItem(&g_model->view, EDITOR_ACCOUNTS_SECTION_INV, 3),
        "Remove empties slot 3");
    /* Another inv, by name: the save gets its section on the first Set;
     * Return in Put here sets the first match (#995 is the id). */
    settle(NULL, 20);
    ToriRSChrome_SetText(&g_chrome, input_inv, "tob_chests");
    activate(button_inv_open);
    CHECK(g_chrome.widgets[dropdown_container].option_count == 4
              && strstr(g_chrome.widgets[dropdown_container].options[3], "tob_chests") != NULL
              && ToriRSChrome_DropdownSelected(&g_chrome, dropdown_container) == 3,
        "Open inv adds tob_chests to the picker and shows it (%d)", g_chrome.widgets[dropdown_container].option_count);
    CHECK(ToriRSChrome_GraphKeyAt(&g_chrome, graph_items, 4 + 3 * 40 + 5, 4 + 36 + 5) == 12
              && ToriRSChrome_GraphKeyAt(&g_chrome, graph_items, 4 + 4 * 40 + 5, 4 + 36 + 5) == 0,
        "its grid is its 12 slots");
    press_graph(graph_items, 4 + 2 * 40 + 5, 4 + 5);
    ToriRSChrome_SetText(&g_chrome, input_put, "#995");
    ToriRSChrome_SetText(&g_chrome, input_count, "50");
    g_want_serial = g_model->view_serial;
    activate(input_put);
    CHECK(settle(view_moved, 200) && file_has(save, "[container.612]\n2 = 995 50\n") && g_model->view.container_count == 1,
        "Return puts #995 x50 in slot 2: the inv's section is written and the view lists the container");
    settle(NULL, 5);
    CHECK(g_chrome.widgets[dropdown_container].option_count == 4 && ToriRSChrome_DropdownSelected(&g_chrome, dropdown_container) == 3,
        "the picker keeps it, now one of the save's (%d)", g_chrome.widgets[dropdown_container].option_count);
    g_want_serial = g_model->view_serial;
    activate(button_clear);
    CHECK(settle(view_moved, 200) && !file_has(save, "2 = 995 50\n"), "Clear container empties it");

    /* Loadouts: save the gear, see it, change the account, apply, delete. */
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 2);
    ToriRSChrome_SetText(&g_chrome, input_loadout_name, "Sword Kit");
    g_want_serial = g_model->loadouts_serial;
    activate(button_loadout_save);
    CHECK(settle(loadouts_moved, 200) && g_model->loadout_count == 1 && strcmp(g_model->loadouts[0].name, "sword_kit") == 0
              && vlist_has(list_loadouts, "sword_kit  (1 worn, 1 in the backpack)"),
        "Save current gear lists sword_kit (1 worn, 1 in the backpack)");
    ToriRSChrome_VListSetSelected(&g_chrome, list_loadouts, 0);
    activate(list_loadouts);
    settle(NULL, 3);
    CHECK(g_chrome.widgets[graph_loadout].graph_count > 0, "the loadout's gear is drawn (%d items)",
        g_chrome.widgets[graph_loadout].graph_count);
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 1);
    choose(dropdown_container, 0);
    press_graph(graph_items, 8 + 21 + 10, 2 + 82 + 10);
    g_want_serial = g_model->view_serial;
    activate(button_item_remove);
    CHECK(settle(view_moved, 200) && !file_has(save, "[worn]\n3 = 1277 1\n"), "the sword comes off");
    ToriRSChrome_PanelSetActiveTab(&g_chrome, panel, 2);
    g_want_serial = g_model->view_serial;
    activate(button_loadout_apply);
    CHECK(settle(view_moved, 200) && file_has(save, "3 = 1277 1\n") && file_has(save, "0 = 1277 1\n"),
        "Apply to account puts the sword back on and the backpack back");
    g_want_serial = g_model->loadouts_serial;
    activate(button_loadout_delete);
    CHECK(settle(loadouts_moved, 200) && g_model->loadout_count == 0, "Delete loadout removes it");

    /* New account, then Delete. */
    ToriRSChrome_SetText(&g_chrome, input_name, "second");
    ToriRSChrome_SetText(&g_chrome, input_password, "Other9pass");
    g_want_serial = g_model->status_serial;
    activate(button_new);
    CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_OK, "New account: %s", g_model->last_message);
    settle(NULL, 40);
    CHECK(g_model->row_count == 3 && strcmp(Editor_Accounts_Current(&g_ui), "second") == 0
              && g_chrome.widgets[dropdown].option_count == 3,
        "the list has three accounts and second is shown (%s)", Editor_Accounts_Current(&g_ui));
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
        for( int i = 0; i < 200 && !(pv_model && pv_model->row_count == 3); i++ )
        {
            Editor_PumpFacts(&peer);
            frame_of(&pv_chrome, &pv_ui);
        }
        CHECK(pv_model && pv_model->row_count == 3 && strcmp(pv_model->play_account, "tester") == 0,
            "the second window lists every account and the play account");
        /* The first window's Delete reaches it. */
        g_want_serial = g_model->status_serial;
        activate(button_delete);
        CHECK(settle(status_moved, 200) && g_model->last_status == EDITOR_HOST_OK, "Delete second: %s", g_model->last_message);
        for( int i = 0; i < 200 && pv_model->row_count != 2; i++ )
        {
            Editor_PumpFacts(&peer);
            frame_of(&pv_chrome, &pv_ui);
        }
        CHECK(pv_model->row_count == 2, "the second window's list follows the delete (%d)", pv_model->row_count);
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
    if( !RSCache_ProfileByName("osrs242", &g_profile) )
    {
        printf("FAIL: no osrs242 profile\n");
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
