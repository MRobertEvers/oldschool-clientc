/*
 * Workbench 6.7, part 2, headless: the Shop builder and the Quest board
 * (editor/panels/edpanel_shop.c, edpanel_quest.c) against the real chrome
 * model and an embedded ToriRSMapEd -- its catalog over a SYNTHETIC content
 * tree and a synthetic repository the test writes, its BUILD runner the test
 * runner (no make, no python, no OSRS-Content):
 *
 *   ./editor_shop_quest_panels_test <tmp-dir>
 *
 *   shop      a new shop typed into the panel: the price columns from the
 *             catalog's obj costs, Check clean, Save as ONE undo step whose
 *             two files are the emitter's bytes, undo / redo followed by the
 *             panel; Check's catalog refusals (an inv too small, another
 *             file's op trigger, an obj the tree lacks); the generator's pair
 *             opened read-only and Save refused with the reason; From wiki
 *             catalogue; Test refused with no spawn, then RUN sent and its
 *             outcome said once it lands; the three buttons' keycaps
 *   quest     the board over the catalog: the dbrow's columns, the trigger
 *             files the catalog names; Open part into Triggers and into the
 *             CODE view; Run gate refused without the quest client, then the
 *             BUILD job's make line, its end read back as gate.py's verdict;
 *             Scaffold refused over a test file; the ledger rows and the
 *             screenshot through png_publish, or its path without it
 *   views     both panels are View actions, unbound by default
 */

#include "editor/editor.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_quest.h"
#include "editor/editor_shop.h"
#include "editor/editor_ui.h"
#include "editor/live/live_panel.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_live.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

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
static struct ToriRSMapEdBuildMock* g_mock;
static char g_content[1024];
static char g_repo[1024];
static char g_png_path[2048];
static int g_png_calls;

/* ---- files ---------------------------------------------------------------------- */

static void
make_dirs(const char* path)
{
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "%s", path);
    for( char* p = buffer + 1; *p; p++ )
    {
        if( *p == '/' )
        {
            *p = '\0';
            mkdir(buffer, 0755);
            *p = '/';
        }
    }
    mkdir(buffer, 0755);
}

static void
write_at(
    const char* root,
    const char* relative,
    const char* text)
{
    char path[2048];
    char dir[2048];
    FILE* f;
    char* slash;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    assert(slash);
    *slash = '\0';
    make_dirs(dir);
    f = fopen(path, "wb");
    if( !f )
    {
        printf("FAIL: cannot write %s\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

static char*
read_at(
    const char* root,
    const char* relative)
{
    char path[2048];
    FILE* f;
    long size;
    char* text;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
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

static void
write_fixture(void)
{
    static char text[8192];
    struct Editor_Shop shop;

    make_dirs(g_content);
    write_at(g_content, "configs/all.npc",
        "[shop_keeper_lumbridge]\nname=Shop keeper\nop1=Talk-to\nop3=Trade\n\n[other_keeper]\nname=Other\nop3=Trade\n\n"
        "[cook]\nname=Cook\nop1=Talk-to\n");
    write_at(g_content, "configs/all.npc.compack", "2813=shop_keeper_lumbridge\n2814=other_keeper\n4626=cook\n");
    write_at(g_content, "configs/all.obj",
        "[pot]\nname=Pot\ncost=1\n\n[bread]\nname=Bread\ncost=12\n\n[feather]\nname=Feather\ncost=2\n");
    write_at(g_content, "configs/all.obj.compack", "1931=pot\n2309=bread\n314=feather\n");
    write_at(g_content, "configs/all.inv", "[lumbridge_general]\nsize=2\n\n[generalshop1]\nsize=40\n");
    write_at(g_content, "configs/all.inv.compack", "900=lumbridge_general\n3=generalshop1\n");
    write_at(g_content, "configs/all.dbrow", "[quest_cook]\ntable=quest\ndata=points,1\ndata=difficulty,novice\n");
    write_at(g_content, "configs/all.dbrow.compack", "1=quest_cook\n");
    write_at(g_content, "server/scripts/areas/lumbridge/scripts/keepers.rs2",
        "[opnpc3,other_keeper]\nmes(\"I've nothing to trade just now.\");\n");
    write_at(g_content, "wiki/shop_stock.csv",
        "shop_key,item_name,obj_gameval,stock,restock,match_rule,review\n"
        "lumbridge_general,Pot,pot,5,10,exact,\n"
        "lumbridge_general,Skirt (blue),dwarf_skirt1,2,100,strip-qualifier,variant\n"
        "lumbridge_general,Feather,feather,1000,10,exact,\n");

    /* The generator's own pair for a generated shop (editor_shop_test proves
     * these are gen_shop_scripts.py's bytes for the same rows). */
    Editor_ShopInit(&shop, "lumbridge_general_store");
    snprintf(shop.name, sizeof(shop.name), "%s", "Lumbridge General Store");
    snprintf(shop.page_title, sizeof(shop.page_title), "%s", "Lumbridge General Store");
    snprintf(shop.revid, sizeof(shop.revid), "%s", "15231736");
    snprintf(shop.fetch_date, sizeof(shop.fetch_date), "%s", "2026-08-13");
    snprintf(shop.special, sizeof(shop.special), "%s", "General store");
    snprintf(shop.inv, sizeof(shop.inv), "%s", "generalshop1");
    Editor_ShopOwnerAdd(&shop, "generalshopkeeper1");
    Editor_ShopStockAdd(&shop, "pot", 5, 10);
    Editor_ShopEmitRs2(&shop, EDITOR_SHOP_GENERATOR_MARK, text, sizeof(text));
    write_at(g_content, "server/scripts/shop/misthalin/scripts/lumbridge_general_store.rs2", text);
    Editor_ShopEmitInv(&shop, EDITOR_SHOP_GENERATOR_MARK, text, sizeof(text));
    write_at(g_content, "server/scripts/shop/misthalin/configs/lumbridge_general_store.inv", text);

    /* A quest: the reader's own fixture, smaller. */
    write_at(g_content, "server/scripts/quests/quest_cook/configs/quest_cook.constant",
        "^cook_not_started = 0\n^cook_started = 1\n^cook_complete = 2\n^cook_questpoints = 1\n");
    write_at(g_content, "server/scripts/quests/quest_cook/scripts/quest_cook.rs2",
        "[opnpc1,cook]\n"
        "if (%varp29_cookquest = ^cook_not_started) {\n"
        "    @cook_start;\n"
        "}\n"
        "if (%varp29_cookquest = ^cook_started) {\n"
        "    ~chatnpc(^chat_neutral, \"Got them yet?\");\n"
        "}\n"
        "\n"
        "[label,cook_start]\n"
        "~chatnpc(^chat_sad, \"What am I to do?\");\n"
        "%varp29_cookquest = ^cook_started;\n"
        "\n"
        "[proc,cook_complete]\n"
        "%varp29_cookquest = ^cook_complete;\n");
    write_at(g_content, "server/scripts/quests/quest_cook/journal/quest_cook_journal.rs2",
        "[questjournal,quest_cook]\n~send_quest_journal(\"I spoke to the cook.\");\n");

    make_dirs(g_repo);
    write_at(g_repo, "test/quests/QUEUE.tsv",
        "quest_dir\ttest_id\thelper_dir\thelper_file\ttier\tstatus\towner\tlast_failure\tclaimed_at\tclaim_prev\n"
        "quest_cook\tcooks_assistant\tcooksassistant\t\t1\tgreen\t\t\t\t\n");
    write_at(g_repo, "tools/quest_gate/quest_inventory.tsv",
        "dir\thuman_name\tvarp\tconst_not_started\tconst_complete\n"
        "quest_cook\tCook's Assistant\t%varp29_cookquest\t^cook_not_started\t^cook_complete\n");
    write_at(g_repo, "tools/quest_gate/new_quest.py", "# stand-in\n");
    write_at(g_repo, "tools/quest_gate/run.py", "# stand-in\n");
    write_at(g_repo, "tools/quest_gate/gate.py", "# stand-in\n");
    write_at(g_repo, "test/quests/cooks_assistant.lua",
        "return {\n    id = \"cooks_assistant\",\n    run = function(t)\n"
        "        t.exec(\"cook.greet\", t.player.talk_to, \"cook\")\n"
        "        t.expect(\"quest.stage.started\", t.quest.stage())\n"
        "    end,\n}\n");
    write_at(g_repo, "build/quest_gate/cooks_assistant/ledger.tsv",
        "quest-ledger-v1\nindex\tstep\tverdict\tticks\tshots\tdetail\n"
        "1\tcook.reset\tPASS\t3\t\tstage not_started (0)\n"
        "2\tcook.greet\tPASS\t10\t01-cook-greet\tok talked to Cook\n"
        "SUMMARY\t3\tPASS\t13\texit=0\tpass=2 fail=0\n");
}

/* ---- the harness ------------------------------------------------------------------ */

static void
frame(void)
{
    /* The App pumps the session every frame; the embedded server's builds
     * advance on each pump. */
    Editor_PumpFacts(&g_editor);
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
frames(int n)
{
    for( int i = 0; i < n; i++ )
        frame();
}

static int
module_panel(const char* name)
{
    struct EditorUiPanel* p = Editor_UiFindPanel(&g_ui, name);
    return p ? p->chrome_panel : -1;
}

static int
widget_in(
    const char* module,
    int kind,
    const char* label)
{
    int const panel = module_panel(module);
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel == panel && w->kind == kind && (strcmp(w->label, label) == 0 || strcmp(w->text, label) == 0) )
            return i;
    }
    return -1;
}

static void
type_into(
    const char* module,
    const char* label,
    const char* text)
{
    int const widget = widget_in(module, TORIRS_CHROME_W_TEXTINPUT, label);
    assert(widget >= 0);
    ToriRSChrome_SetText(&g_chrome, widget, text);
}

static void
press(
    const char* module,
    const char* caption)
{
    int const widget = widget_in(module, TORIRS_CHROME_W_BUTTON, caption);
    if( widget < 0 )
    {
        printf("FAIL: no %s button '%s'\n", module, caption);
        exit(1);
    }
    g_chrome.activated = widget;
    frame();
}

static const char*
vrow(
    int widget,
    int row)
{
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
        if( g_chrome.vrows[i].owner == widget && g_chrome.vrows[i].index == row )
            return g_chrome.vrows[i].text;
    return NULL;
}

static const char*
status(void)
{
    return Editor_UiStatusText(&g_ui);
}

static int
history_applied(void)
{
    return Editor_GetHistory(&g_editor)->applied;
}

static int
fake_png_publish(
    void* user,
    int slot,
    const char* path,
    int max_w,
    int max_h,
    int* out_w,
    int* out_h)
{
    (void)user;
    (void)slot;
    snprintf(g_png_path, sizeof(g_png_path), "%s", path);
    g_png_calls++;
    *out_w = max_w < 120 ? max_w : 120;
    *out_h = max_h < 80 ? max_h : 80;
    return 0x4D000000 + slot;
}

/* ---- shop ------------------------------------------------------------------------------ */

static void
test_shop(void)
{
    const struct Editor_Shop* shop;
    int list;
    int groups;
    char* rs2;
    char* inv;
    static char want[8192];

    CHECK(Editor_Shop_Open(&g_ui, "lumbridge_general"), "shop: a new key opens a new shop");
    CHECK(strstr(status(), "New shop lumbridge_general") != NULL, "shop: and says so (%s)", status());
    shop = Editor_Shop_Model(&g_ui);
    CHECK(shop && shop->op == 3 && shop->sell == 1300 && shop->buy == 400 && shop->delta == 30,
        "shop: op3 and the general-store multipliers by default");
    type_into("shop", "Owner npc", "shop_keeper_lumbridge");
    type_into("shop", "Title", "Lumbridge General");
    ToriRSChrome_SetChecked(&g_chrome, widget_in("shop", TORIRS_CHROME_W_CHECKBOX, "general store"), 1);
    type_into("shop", "obj", "pot");
    type_into("shop", "count", "5");
    type_into("shop", "rate", "100");
    press("shop", "+ stock");
    type_into("shop", "obj", "bread");
    type_into("shop", "count", "10");
    type_into("shop", "rate", "50");
    press("shop", "+ stock");
    frames(4);
    shop = Editor_Shop_Model(&g_ui);
    CHECK(shop->stock_count == 2 && strcmp(shop->owners[0].npc, "shop_keeper_lumbridge") == 0
              && Editor_ShopIsGeneral(shop) && strcmp(shop->inv, "lumbridge_general") == 0,
        "shop: the typed fields are the model");
    list = widget_in("shop", TORIRS_CHROME_W_VLIST, "stockN = obj, count, rate   sells at, buys at");
    CHECK(list >= 0 && vrow(list, 0) && strstr(vrow(list, 0), "pot") && strstr(vrow(list, 0), "sells at 1 gp")
              && strstr(vrow(list, 0), "buys at 0 gp"),
        "shop: a pot (cost 1) sells at 1 gp and buys at 0 gp (%s)", list >= 0 && vrow(list, 0) ? vrow(list, 0) : "-");
    CHECK(list >= 0 && vrow(list, 1) && strstr(vrow(list, 1), "sells at 15 gp") && strstr(vrow(list, 1), "buys at 4 gp"),
        "shop: bread (cost 12) sells at 15 gp and buys at 4 gp (%s)", list >= 0 && vrow(list, 1) ? vrow(list, 1) : "-");
    type_into("shop", "Find stock", "bre");
    frame();
    CHECK(list >= 0 && g_chrome.widgets[list].imax == 1, "shop: the search box narrows the rows");
    type_into("shop", "Find stock", "");
    frame();

    CHECK(Editor_Shop_Check(&g_ui) == 0, "shop: Check is clean (%s)",
        Editor_Shop_ProblemCount(&g_ui) ? Editor_Shop_Problem(&g_ui, 0) : "");

    groups = history_applied();
    CHECK(Editor_Shop_Save(&g_ui) == 1, "shop: Save (%s)", status());
    CHECK(history_applied() == groups + 1, "shop: Save is ONE undo step (%d -> %d)", groups, history_applied());
    rs2 = read_at(g_content, "server/scripts/shop/edited/lumbridge_general/scripts/lumbridge_general.rs2");
    inv = read_at(g_content, "server/scripts/shop/edited/lumbridge_general/configs/lumbridge_general.inv");
    Editor_ShopEmitRs2(Editor_Shop_Model(&g_ui), EDITOR_SHOP_MARK, want, sizeof(want));
    CHECK(rs2 && strcmp(rs2, want) == 0, "shop: the .rs2 on disk is the emitter's bytes");
    CHECK(rs2 && strstr(rs2, "[opnpc3,shop_keeper_lumbridge] @lumbridge_general_open;\n")
              && strstr(rs2, "~openshop(lumbridge_general, 400, 1300, 30, \"Lumbridge General\");\n"),
        "shop: the trigger and ~openshop");
    Editor_ShopEmitInv(Editor_Shop_Model(&g_ui), EDITOR_SHOP_MARK, want, sizeof(want));
    CHECK(inv && strcmp(inv, want) == 0 && strstr(inv, "allstock=yes\nstock1=pot,5,100\nstock2=bread,10,50\n"),
        "shop: the .inv on disk is the emitter's bytes");
    free(rs2);
    free(inv);

    /* Undo puts the documents back to nothing; the panel follows; Redo. */
    CHECK(Editor_Undo(&g_editor), "shop: undo the save's edit");
    frames(2);
    CHECK(strstr(status(), "files are gone") != NULL, "shop: the panel says the files went (%s)", status());
    CHECK(Editor_Redo(&g_editor), "shop: redo");
    frames(2);
    CHECK(Editor_Shop_Model(&g_ui)->stock_count == 2, "shop: the redone pair is read back");

    /* Check's catalog refusals. */
    type_into("shop", "obj", "feather");
    type_into("shop", "count", "1000");
    type_into("shop", "rate", "10");
    press("shop", "+ stock");
    type_into("shop", "obj", "unobtainium");
    press("shop", "+ stock");
    type_into("shop", "Owner npc", "other_keeper");
    frame();
    CHECK(Editor_Shop_Check(&g_ui) >= 3, "shop: Check finds the catalog's problems (%d)", Editor_Shop_ProblemCount(&g_ui));
    {
        int overflow = 0;
        int trigger = 0;
        int missing = 0;
        for( int i = 0; i < Editor_Shop_ProblemCount(&g_ui); i++ )
        {
            const char* p = Editor_Shop_Problem(&g_ui, i);
            overflow |= strstr(p, "overflow inv lumbridge_general's 2 slots") != NULL;
            trigger |= strstr(p, "[opnpc3,other_keeper] is already declared in "
                                 "server/scripts/areas/lumbridge/scripts/keepers.rs2:1")
                       != NULL;
            missing |= strstr(p, "unobtainium is not an obj") != NULL;
        }
        CHECK(overflow, "shop: four rows overflow the cache inv's two slots");
        CHECK(trigger, "shop: another file's [opnpc3,other_keeper] is named with its line");
        CHECK(missing, "shop: an obj the tree lacks is named");
    }
    groups = history_applied();
    CHECK(!Editor_Shop_Save(&g_ui) && strstr(status(), "Save refused: ") && history_applied() == groups,
        "shop: Save refuses what Check refuses and writes nothing (%s)", status());
    type_into("shop", "Owner npc", "shop_keeper_lumbridge");
    frame();

    /* The generator's pair: read-only, Save refused with the reason. */
    CHECK(Editor_Shop_Open(&g_ui, "lumbridge_general_store"), "shop: the generator's shop opens");
    shop = Editor_Shop_Model(&g_ui);
    CHECK(shop && shop->origin == EDITOR_SHOP_ORIGIN_GENERATOR && strcmp(shop->inv, "generalshop1") == 0,
        "shop: through the catalog's [label,lumbridge_general_store_open]");
    groups = history_applied();
    CHECK(!Editor_Shop_Save(&g_ui) && strstr(status(), "gen_shop_scripts.py") && history_applied() == groups,
        "shop: Save refuses generated output and says why (%s)", status());

    /* From wiki catalogue: the reviewed rows of the key. */
    Editor_Shop_SetPaths(&g_ui, g_content);
    CHECK(Editor_Shop_Open(&g_ui, "lumbridge_general"), "shop: reopen the builder's shop");
    frame();
    CHECK(!g_chrome.widgets[widget_in("shop", TORIRS_CHROME_W_BUTTON, "From wiki catalogue...")].disabled,
        "shop: the wiki button is enabled where the tree has the catalogue");
    press("shop", "From wiki catalogue...");
    shop = Editor_Shop_Model(&g_ui);
    CHECK(shop->stock_count == 2 && strcmp(shop->stock[0].obj, "pot") == 0 && strcmp(shop->stock[1].obj, "feather") == 0
              && strstr(status(), "1 needing review skipped"),
        "shop: the wiki's two reviewed rows replace the stock (%s)", status());

    /* Test: no spawn of the owner anywhere -> refused; then with one. */
    CHECK(!Editor_Shop_Test(&g_ui) && strstr(status(), "no spawn of shop_keeper_lumbridge"),
        "shop: Test refuses an owner with no spawn here (%s)", status());
    write_at(g_content, "server/scripts/areas/edited/configs/m50_50.spawn",
        "==== NPC ====\nshop_keeper_lumbridge                         3222  3218 0\n");
    CHECK(Editor_OpenDoc(&g_editor, EDITOR_DOC_SPAWN, "m50_50"), "shop: the square's spawn file opens");
    {
        const struct Editor_LiveModel* live = Editor_LivePanelModel(&g_ui);
        int const before = live ? live->run_serial : -1;
        CHECK(live != NULL, "shop: the window has a Live panel");
        CHECK(Editor_Shop_Test(&g_ui) == 1, "shop: Test sends RUN (%s)", status());
        for( int i = 0; i < 200 && live && live->run_serial == before; i++ )
            frame();
        frame();
        CHECK(live && live->run_serial != before, "shop: the RUN landed");
        /* No live link is attached here: RUN stops at the reload, and Test
         * says where instead of teleporting anyone. */
        CHECK(strstr(status(), "Test: RUN stopped at reload: this server has no live link") != NULL,
            "shop: Test says how RUN ended (%s)", status());
        /* A RUN that lands well: the model as FACT_RUN_DONE ok and a play
         * account leave it (this harness has no live server to say so). */
        g_mock->scripts[TORIRSMAPED_BUILD_RUN].idle_polls = 1000000;
        CHECK(Editor_Shop_Test(&g_ui) == 1, "shop: Test again (%s)", status());
        if( live )
        {
            struct Editor_LiveModel* faked = (struct Editor_LiveModel*)live;
            faked->run_serial++;
            faked->run_status = 0;
            snprintf(faked->play_account, sizeof(faked->play_account), "%s", "maped43620");
        }
        frame();
        /* The ::goto goes through the Live panel, which sends a cheat only to
         * an attached lane; this harness has no live server, so the panel
         * says where to go instead (the sentence with the Trade hint needs a
         * real lane: section 16's in-game check). */
        CHECK(strcmp(status(),
                  "Test: RUN done, but the live lane is not attached (Live: Attach); teleport maped43620 by hand "
                  "to 3222,3218")
                  == 0,
            "shop: a landed RUN on a lane that is not attached says where to go (%s)", status());
        /* Let the real RUN finish before the quest board's job. */
        g_mock->scripts[TORIRSMAPED_BUILD_RUN].idle_polls = 0;
        for( int i = 0; i < 400 && live && (live->building || live->run_step != TORIRSMAPED_RUN_STEP_IDLE); i++ )
            frame();
        frames(4);
    }

    /* The keycaps. */
    {
        int const test = widget_in("shop", TORIRS_CHROME_W_BUTTON, "Test");
        int const check = widget_in("shop", TORIRS_CHROME_W_BUTTON, "Check");
        int const save = widget_in("shop", TORIRS_CHROME_W_BUTTON, "Save");
        char run[EDITOR_HK_HINT_CHORD_MAX];
        char check_chord[EDITOR_HK_HINT_CHORD_MAX];
        char save_chord[EDITOR_HK_HINT_CHORD_MAX];
        Editor_HotkeyDisplay(&g_ui.hotkeys, EDITOR_HK_FILE_RUN, run, sizeof(run));
        Editor_HotkeyDisplay(&g_ui.hotkeys, EDITOR_HK_FILE_CHECK_SCRIPTS, check_chord, sizeof(check_chord));
        Editor_HotkeyDisplay(&g_ui.hotkeys, EDITOR_HK_FILE_SAVE_ALL, save_chord, sizeof(save_chord));
        CHECK(test >= 0 && run[0] && strcmp(g_chrome.widgets[test].chord, run) == 0, "shop: Test wears %s (%s)", run,
            test >= 0 ? g_chrome.widgets[test].chord : "-");
        CHECK(check >= 0 && check_chord[0] && strcmp(g_chrome.widgets[check].chord, check_chord) == 0,
            "shop: Check wears %s", check_chord);
        CHECK(save >= 0 && save_chord[0] && strcmp(g_chrome.widgets[save].chord, save_chord) == 0,
            "shop: Save wears %s", save_chord);
    }
}

/* ---- quest ------------------------------------------------------------------------------ */

static const struct Editor_QuestRow*
board_row(enum Editor_QuestPart part)
{
    const struct Editor_QuestBoard* board = Editor_Quest_Board(&g_ui);
    for( int i = 0; board && i < board->row_count; i++ )
    {
        if( board->rows[i].part == part )
            return &board->rows[i];
    }
    return NULL;
}

static int
board_index(enum Editor_QuestPart part)
{
    const struct Editor_QuestBoard* board = Editor_Quest_Board(&g_ui);
    for( int i = 0; board && i < board->row_count; i++ )
    {
        if( board->rows[i].part == part )
            return i;
    }
    return -1;
}

static void
test_quest(void)
{
    const struct Editor_QuestBoard* board;
    const struct Editor_QuestRow* row;
    int list;

    CHECK(!Editor_Quest_Open(&g_ui, "cooks_assistant") && strstr(status(), "no repository"),
        "quest: without paths the board says what it lacks (%s)", status());
    Editor_Quest_SetPaths(&g_ui, g_content, g_repo);
    CHECK(Editor_Quest_Open(&g_ui, "cooks_assistant"), "quest: the board opens (%s)", status());
    board = Editor_Quest_Board(&g_ui);
    CHECK(board && board->row_count == 7, "quest: dbrow, var, start, one stage, complete, journal, test (%d rows)",
        board ? board->row_count : -1);
    row = board_row(EDITOR_QUEST_PART_DBROW);
    CHECK(row && row->state == EDITOR_QUEST_OK && strcmp(row->what, "dbrow quest_cook") == 0
              && strcmp(row->where, "configs/all.dbrow:1") == 0,
        "quest: the dbrow from the catalog, where its block is (%s | %s)", row ? row->what : "-", row ? row->where : "-");
    CHECK(board && board->points == 1, "quest: the points from the .constant");
    row = board_row(EDITOR_QUEST_PART_JOURNAL);
    CHECK(row && row->state == EDITOR_QUEST_OK, "quest: the journal file the catalog's triggers name (%s)",
        row ? row->what : "-");
    row = board_row(EDITOR_QUEST_PART_STAGE);
    CHECK(row && row->state == EDITOR_QUEST_OK && strstr(row->what, "stage started: opnpc1 cook"),
        "quest: the stage row (%s)", row ? row->what : "-");
    list = widget_in("quest", TORIRS_CHROME_W_VLIST,
        TORIRS_CHROME_CELL_SEP_STR "part" TORIRS_CHROME_CELL_SEP_STR "where" TORIRS_CHROME_CELL_SEP_STR
            TORIRS_CHROME_CELL_SEP_STR "state");
    frame();
    CHECK(list >= 0 && vrow(list, 0) && strstr(vrow(list, 0), "dbrow quest_cook") && strstr(vrow(list, 0), "ok"),
        "quest: the parts list shows the dbrow row (%s)", list >= 0 && vrow(list, 0) ? vrow(list, 0) : "-");
    type_into("quest", "Find part", "journal");
    frame();
    CHECK(list >= 0 && g_chrome.widgets[list].imax == 1, "quest: the search box narrows the parts");
    type_into("quest", "Find part", "");
    frame();

    /* Open part: the stage into Triggers, the test into the CODE view. */
    CHECK(Editor_Quest_OpenPart(&g_ui, board_index(EDITOR_QUEST_PART_STAGE)), "quest: Open part on the stage (%s)",
        status());
    CHECK(g_chrome.panels[module_panel("triggers")].visible, "quest: Triggers is shown");
    CHECK(Editor_Quest_OpenPart(&g_ui, board_index(EDITOR_QUEST_PART_TEST)), "quest: Open part on the test (%s)",
        status());
    frames(2);
    {
        int code = -1;
        for( int i = 0; i < g_chrome.widget_count; i++ )
        {
            if( g_chrome.widgets[i].panel == module_panel("quest") && g_chrome.widgets[i].kind == TORIRS_CHROME_W_CODE )
                code = i;
        }
        CHECK(code >= 0 && !g_chrome.widgets[code].hidden && vrow(code, 0) && strcmp(vrow(code, 0), "return {") == 0,
            "quest: the CODE view shows the Lua (%s)", code >= 0 && vrow(code, 0) ? vrow(code, 0) : "-");
    }
    CHECK(Editor_Quest_OpenPart(&g_ui, board_index(EDITOR_QUEST_PART_DBROW))
              && strstr(status(), "edits loc, npc and obj"),
        "quest: the dbrow, which no panel edits, is shown below (%s)", status());

    /* Run gate: refused without the quest client; then the job. */
    CHECK(!Editor_Quest_RunGate(&g_ui) && strstr(status(), "src/torirs_questtest"),
        "quest: Run gate refused without the quest client (%s)", status());
    write_at(g_repo, "src/torirs_questtest", "stand-in\n");
    {
        int const commands = g_mock->command_count;
        CHECK(Editor_Quest_RunGate(&g_ui), "quest: Run gate starts (%s)", status());
        /* The job's verdict, as maped-quest-gate leaves it. */
        write_at(g_repo, "build/maped_quest_board/cooks_assistant.gate",
            "# Written by make maped-quest-gate (src/maped/shop-quest.mk).\ncooks_assistant          green\n");
        {
            struct utimbuf times;
            char path[2048];
            snprintf(path, sizeof(path), "%s/build/quest_gate/cooks_assistant/ledger.tsv", g_repo);
            times.actime = times.modtime = time(NULL) - 60;
            utime(path, &times);
        }
        for( int i = 0; i < 100 && Editor_Quest_Board(&g_ui) && Editor_Quest_Board(&g_ui)->verdict != EDITOR_QUEST_GREEN;
             i++ )
            frame();
        CHECK(g_mock->command_count == commands + 1, "quest: one BUILD job ran");
        if( g_mock->command_count > commands )
        {
            const char* line = g_mock->commands[commands];
            CHECK(strstr(line, "maped-quest-gate MAPED_QUEST=cooks_assistant") && strstr(line, "--no-print-directory")
                      && g_mock->command_whats[commands] == TORIRSMAPED_BUILD_QUEST_GATE,
                "quest: its make line (%s)", line);
        }
        CHECK(Editor_Quest_Board(&g_ui)->verdict == EDITOR_QUEST_GREEN && strstr(status(), "quest gate for cooks_assistant finished: the test is green"),
            "quest: the job's end reads gate.py's verdict back (%s, %s)",
            Editor_QuestStateWord(Editor_Quest_Board(&g_ui)->verdict), status());
    }
    CHECK(!Editor_Quest_Scaffold(&g_ui) && strstr(status(), "refuses to clobber"),
        "quest: Scaffold refused over a test file (%s)", status());

    /* A job that ends while the board is hidden, with another build ending
     * before the board is shown again (Run gate in Script, then Playtest and
     * Ctrl+Return): the board still sees its job end, and is not popped up. */
    {
        const struct Editor_LiveModel* live = Editor_LivePanelModel(&g_ui);
        int const quest = module_panel("quest");
        int const commands = g_mock->command_count;
        int run_serial;
        CHECK(Editor_Quest_RunGate(&g_ui), "quest: Run gate starts again (%s)", status());
        ToriRSChrome_PanelSetVisible(&g_chrome, quest, 0);
        for( int i = 0; i < 200 && live && (g_mock->command_count == commands || live->building); i++ )
            frame();
        frames(2);
        CHECK(!g_chrome.panels[quest].visible, "quest: the job's end does not show a hidden board");
        CHECK(strstr(status(), "quest gate for cooks_assistant finished") != NULL,
            "quest: a hidden board sees its job end (%s)", status());
        run_serial = live ? live->run_serial : -1;
        CHECK(Editor_Live_Run(&g_ui) == 1, "quest: a RUN after it (%s)", status());
        for( int i = 0; i < 400 && live && (live->run_serial == run_serial || live->building); i++ )
            frame();
        frames(2);
        CHECK(live && live->last_build_what != TORIRSMAPED_BUILD_QUEST_GATE, "quest: another build ended last");
        ToriRSChrome_PanelSetVisible(&g_chrome, quest, 1);
        frame();
        CHECK(!g_chrome.widgets[widget_in("quest", TORIRS_CHROME_W_BUTTON, "Run gate")].disabled,
            "quest: Run gate is enabled again once the board is shown");
        CHECK(Editor_Quest_RunGate(&g_ui), "quest: and is not refused as in flight (%s)", status());
        for( int i = 0; i < 200 && live && (g_mock->command_count == commands + 1 || live->building); i++ )
            frame();
        frames(4);
    }

    /* The ledger and its screenshot. */
    press("quest", "Ledger...");
    {
        int const ledger = widget_in("quest", TORIRS_CHROME_W_VLIST, "Ledger");
        int view = -1;
        frame();
        for( int i = 0; i < g_chrome.widget_count; i++ )
        {
            if( g_chrome.widgets[i].panel == module_panel("quest") && g_chrome.widgets[i].kind == TORIRS_CHROME_W_MODELVIEW )
                view = i;
        }
        CHECK(ledger >= 0 && !g_chrome.widgets[ledger].hidden && g_chrome.widgets[ledger].imax == 2
                  && vrow(ledger, 1) && strstr(vrow(ledger, 1), "cook.greet PASS"),
            "quest: the ledger's rows (%s)", ledger >= 0 && vrow(ledger, 1) ? vrow(ledger, 1) : "-");
        ToriRSChrome_VListSetSelected(&g_chrome, ledger, 1);
        g_chrome.activated = ledger;
        frame();
        CHECK(g_png_calls == 1 && strstr(g_png_path, "/build/quest_gate/cooks_assistant/shots/01-cook-greet.png"),
            "quest: the row's shot goes to png_publish (%s)", g_png_path);
        CHECK(view >= 0 && !g_chrome.widgets[view].hidden && g_chrome.widgets[view].view_scene_id == 0x4D000000,
            "quest: and is the MODELVIEW's sprite");
        /* Without the service: the path. */
        g_ui.app_services.png_publish = NULL;
        g_chrome.activated = ledger;
        frame();
        {
            int shown = 0;
            for( int i = 0; i < g_chrome.widget_count; i++ )
            {
                if( g_chrome.widgets[i].panel == module_panel("quest") && g_chrome.widgets[i].kind == TORIRS_CHROME_W_LABEL
                    && strcmp(g_chrome.widgets[i].text, "build/quest_gate/cooks_assistant/shots/01-cook-greet.png") == 0 )
                    shown = 1;
            }
            CHECK(view >= 0 && g_chrome.widgets[view].hidden && shown && g_png_calls == 1,
                "quest: without png_publish the board shows the path");
        }
    }
}

/* The BUILD frame's quest argument: a quest kind without one, a kind that
 * takes none given one, and a quest no make command line should carry are
 * answered REFUSED, and nothing runs. */
static void
test_build_wire(void)
{
    const struct Editor_LiveModel* live = Editor_LivePanelModel(&g_ui);
    static const struct
    {
        uint32_t what;
        const char* quest;
    } k_cases[] = {
        { TORIRSMAPED_BUILD_QUEST_GATE, NULL },
        { TORIRSMAPED_BUILD_QUEST_SCAFFOLD, "Bad id; rm -rf" },
        { TORIRSMAPED_BUILD_PACKS, "cooks_assistant" },
    };

    for( size_t c = 0; c < sizeof(k_cases) / sizeof(k_cases[0]); c++ )
    {
        struct ToriRSMapEdBuf payload;
        int const commands = g_mock->command_count;
        int const serial = live->build_serial;
        memset(&payload, 0, sizeof(payload));
        ToriRSMapEd_PutU32(&payload, k_cases[c].what);
        if( k_cases[c].quest )
            ToriRSMapEd_PutString(&payload, k_cases[c].quest);
        CHECK(Editor_HostMapEdLiveSend(&g_editor.host, TORIRSMAPED_REQ_BUILD, payload.data + payload.head,
                  (uint32_t)ToriRSMapEd_BufAvailable(&payload)),
            "wire: case %zu sent", c);
        ToriRSMapEd_BufFree(&payload);
        for( int i = 0; i < 20 && live->build_serial == serial; i++ )
            frame();
        CHECK(live->build_serial != serial && live->last_build_status == EDITOR_HOST_REFUSED
                  && g_mock->command_count == commands,
            "wire: case %zu is refused and runs nothing (status %u)", c, live->last_build_status);
    }
    {
        struct ToriRSMapEdBuildCommand command;
        struct ToriRSMapEdBuildJob job;
        char line[4096];
        memset(&job, 0, sizeof(job));
        job.what = TORIRSMAPED_BUILD_QUEST_SCAFFOLD;
        snprintf(job.quest, sizeof(job.quest), "%s", "cooks_assistant");
        CHECK(ToriRSMapEd_BuildCommand(&g_mock->lane, &job, &command), "wire: the scaffold has a command");
        ToriRSMapEd_BuildCommandFormat(&command, line, sizeof(line));
        CHECK(strstr(line, "--no-print-directory maped-quest-scaffold MAPED_QUEST=cooks_assistant TORIRSSERVER_CONTENT_DIR=")
                  && strstr(line, " OBJVERIFY_SKIP=1"),
            "wire: the scaffold's make line (%s)", line);
        CHECK(ToriRSMapEd_BuildQuestValid("cooks_assistant") && !ToriRSMapEd_BuildQuestValid("a b")
                  && !ToriRSMapEd_BuildQuestValid("") && !ToriRSMapEd_BuildQuestValid("$(x)"),
            "wire: a quest is [a-z0-9_]");
    }
}

static void
test_views(void)
{
    CHECK(Editor_HotkeyForPanel("shop") == EDITOR_HK_VIEW_SHOP && Editor_HotkeyForPanel("quest") == EDITOR_HK_VIEW_QUEST,
        "views: both panels are View actions");
    CHECK(Editor_HotkeyInfo(EDITOR_HK_VIEW_SHOP)->defaults[0] == NULL
              && Editor_HotkeyInfo(EDITOR_HK_VIEW_QUEST)->defaults[0] == NULL,
        "views: unbound by default, like every other panel's");
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_QUEST) == 1, "views: the action runs");
}

int
main(
    int argc,
    char** argv)
{
    struct EditorHost host;
    struct ToriRSMapEd* maped;
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdBuildRunner runner;
    char live[1100];

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <tmp-dir>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    unsetenv("TORIRS_EDITOR_SHOP");
    unsetenv("TORIRS_EDITOR_QUEST");
    snprintf(g_content, sizeof(g_content), "%s/content", argv[1]);
    snprintf(g_repo, sizeof(g_repo), "%s/repo", argv[1]);
    snprintf(live, sizeof(live), "%s/live", argv[1]);
    if( !RSCache_ProfileByName("osrs241", &g_profile) )
    {
        printf("FAIL: no osrs241 profile\n");
        return 1;
    }
    write_fixture();
    if( !Editor_HostOpenMapEdEmbed(&host, g_content, NULL) )
    {
        printf("FAIL: the embedded ToriRSMapEd did not open %s\n", g_content);
        return 1;
    }
    /* The test runner: BUILD and RUN record their command and answer ok. */
    maped = Editor_HostMapEdEmbedServer(&host);
    assert(maped);
    ToriRSMapEd_LaneDefaults(&lane, g_repo, g_content, live);
    CHECK(ToriRSMapEd_SetLane(maped, &lane), "the lane binds");
    g_mock = calloc(1, sizeof(*g_mock));
    assert(g_mock);
    g_mock->lane = lane;
    ToriRSMapEd_BuildRunnerMock(&runner, g_mock);
    ToriRSMapEd_SetBuildRunner(maped, &runner);

    Editor_OpenHost(&g_editor, &host, g_content, &g_profile);
    CHECK(g_editor.writable, "the tmp tree is writable");

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1530, 1006);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.app_services.user_data = &g_app_stand_in;
    g_ui.app_services.png_publish = fake_png_publish;
    Editor_UiRegisterToolsPanels(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    frame();
    CHECK(Editor_UiFindPanel(&g_ui, "shop") && Editor_UiFindPanel(&g_ui, "quest"), "both panels register");
    CHECK(!g_chrome.panels[module_panel("shop")].visible && !g_chrome.panels[module_panel("quest")].visible,
        "both start hidden");

    test_shop();
    test_quest();
    test_build_wire();
    test_views();

    Editor_UiFree(&g_ui);
    Editor_Close(&g_editor);
    printf("editor_shop_quest_panels_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
