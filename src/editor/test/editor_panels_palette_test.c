/*
 * The Catalog palette panel (editor/panels/edpanel_catalog_palette.c),
 * headless, against the real chrome model and an embedded ToriRSMapEd over a
 * small content tree written under the directory given (a catalog fixture of
 * two locs, an npc and an obj, and an editor/palettes.txt):
 *
 *   ./editor_panels_palette_test <tmp-content-dir>
 *
 * The panel lists the palettes; a slot click makes its record the catalog
 * pick and arms Place with the slot's pose; Add current pick fills slot 1
 * then 2 of a new palette; a second window of the Client sees the added slot
 * after a pump; the right-click menu's swap, Rename and Delete are each one
 * undo step; the thumbnail cache asks the App once per record; Save all
 * writes the document's bytes. Links the client's objects minus main.o, as
 * test-editor-panels does.
 */

#include "editor/editor.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_palette.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
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
/* Non-NULL services user data: what registers the App-bound modules (the
 * Catalog, and the palette with it) in a window with no App. */
static int g_app_stand_in;

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

static const char k_palettes[] = "# team palettes\n"
                                 "palette Lumbridge | note castle\n"
                                 "palette Lumbridge | slot 1 | loc:test_crate | shape 10 rot 2\n"
                                 "palette Lumbridge | slot 2 | npc:test_guard\n"
                                 "palette Shops | slot 1 | obj:test_knife\n";

static void
write_fixture(const char* root)
{
    make_dir(root, "");
    make_dir(root, "configs");
    make_dir(root, "maps");
    make_dir(root, "pack");
    make_dir(root, "editor");
    write_text(root, "configs/all.loc", "[test_crate]\nname=Crate\nmodels=15402\n\n[test_door]\nname=Door\n");
    write_text(root, "configs/all.loc.compack", "0=test_crate\n1=test_door\n");
    write_text(root, "configs/all.npc", "[test_guard]\nname=Guard\n");
    write_text(root, "configs/all.npc.compack", "0=test_guard\n");
    write_text(root, "configs/all.obj", "[test_knife]\nname=Knife\n");
    write_text(root, "configs/all.obj.compack", "0=test_knife\n");
    write_text(root, "editor/palettes.txt", k_palettes);
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

static void
activate(int widget)
{
    assert(widget >= 0);
    g_chrome.activated = widget;
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

static int
find_widget(
    struct ToriRSChrome* chrome,
    int panel,
    int kind,
    const char* caption)
{
    for( int i = 0; i < chrome->widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( !caption || strcmp(w->label, caption) == 0 || strcmp(w->text, caption) == 0 )
            return i;
    }
    return -1;
}

/** The nth label of `panel` (0 the first). */
static int
nth_label(
    struct ToriRSChrome* chrome,
    int panel,
    int nth)
{
    for( int i = 0; i < chrome->widget_count; i++ )
        if( chrome->widgets[i].panel == panel && chrome->widgets[i].kind == TORIRS_CHROME_W_LABEL && nth-- == 0 )
            return i;
    return -1;
}

/** A GRAPH gesture on slot `slot` (its cell's middle-ish). */
static void
push_slot(
    struct ToriRSChrome* chrome,
    int graph,
    int kind,
    int slot)
{
    struct ToriRSChromeEvent event;
    int const x = ((slot - 1) % 5) * 48 + 20;
    int const y = ((slot - 1) / 5) * 48 + 20;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.widget = graph;
    event.serial = chrome->widgets[graph].serial;
    event.x = x;
    event.y = y;
    event.key = ToriRSChrome_GraphKeyAt(chrome, graph, x, y);
    (void)ToriRSChrome_PushEvent(chrome, &event);
}

static int
console_has(
    struct EditorUi* ui,
    const char* needle)
{
    for( int i = 0; i < 12; i++ )
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

static const char*
doc_text(struct Editor* editor)
{
    struct Editor_DocHandle handle;
    static char text[8192];

    if( !Editor_DocFind(&editor->doc, EDITOR_DOC_PALETTES, EDITOR_PALETTES_DOC_KEY, &handle) || !handle.text
        || !handle.text->loaded || handle.text->length >= sizeof(text) )
        return "";
    memcpy(text, handle.text->text, handle.text->length);
    text[handle.text->length] = '\0';
    return text;
}

/** The newest History row. */
static const char*
history_top(void)
{
    static char row[EDITOR_HISTORY_LABEL_MAX];
    const struct Editor_History* history = Editor_GetHistory(&g_editor);
    if( history->applied <= 0 )
        return "";
    Editor_HistoryGroupName(&history->groups[history->applied - 1], row, (int)sizeof(row));
    return row;
}

static void
test_panel(const char* root)
{
    struct Editor_PalettesFile file;
    struct Editor peer;
    struct EditorHost peer_host;
    struct ToriRSChrome pv_chrome;
    struct EditorUi pv_ui;
    char before[8192];
    int panel;
    int graph;
    int dropdown;
    int status;
    int add;
    int target;
    int name;
    int button_new;
    int button_rename;
    int button_delete;
    int menu;
    int kind;
    int id;
    int slot;

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1280, 800);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.app_services.user_data = &g_app_stand_in;
    Editor_Catalog_Register(&g_ui);
    Editor_CatalogPalette_Register(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    Editor_SetStateCallback(&g_editor, on_state, &g_ui);
    panel = find_panel(&g_chrome, "Catalog palette");
    CHECK(panel >= 0 && Editor_UiFindPanel(&g_ui, "catalog_palette"), "the panel registers as catalog_palette");
    if( panel < 0 )
        return;
    CHECK(Editor_UiTogglePanel(&g_ui, "catalog_palette") == 1, "View toggles it open");
    graph = find_widget(&g_chrome, panel, TORIRS_CHROME_W_GRAPH, NULL);
    dropdown = find_widget(&g_chrome, panel, TORIRS_CHROME_W_DROPDOWN, "Palette");
    target = find_widget(&g_chrome, panel, TORIRS_CHROME_W_DROPDOWN, "Add to");
    add = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Add current pick");
    name = find_widget(&g_chrome, panel, TORIRS_CHROME_W_TEXTINPUT, "Name");
    button_new = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "New palette");
    button_rename = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Rename");
    button_delete = find_widget(&g_chrome, panel, TORIRS_CHROME_W_BUTTON, "Delete");
    status = nth_label(&g_chrome, panel, 0);
    menu = find_panel(&g_chrome, "Slot");
    CHECK(graph >= 0 && dropdown >= 0 && target >= 0 && add >= 0 && name >= 0 && button_new >= 0 && button_rename >= 0
              && button_delete >= 0 && status >= 0 && menu >= 0,
        "its widgets are there");
    if( graph < 0 || dropdown < 0 || target < 0 || add < 0 || name < 0 || button_new < 0 || button_rename < 0
        || button_delete < 0 || status < 0 || menu < 0 )
        return;
    frame();
    frame();

    /* The list. */
    CHECK(g_chrome.widgets[dropdown].option_count == 2 && strcmp(g_chrome.widgets[dropdown].options[0], "Lumbridge") == 0
              && strcmp(g_chrome.widgets[dropdown].options[1], "Shops") == 0,
        "the dropdown lists the two palettes (%d)", g_chrome.widgets[dropdown].option_count);
    CHECK(strcmp(Editor_CatalogPalette_Current(&g_ui), "Lumbridge") == 0, "the first palette is shown");
    CHECK(strcmp(g_chrome.widgets[status].text, "2 of 10 slots - castle") == 0, "the status row (%s)",
        g_chrome.widgets[status].text);

    /* A loc slot: the pick, Place armed with its pose. */
    g_ui.tools.place_shape = 4;
    g_ui.tools.place_rotation = 0;
    push_slot(&g_chrome, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, 1);
    frame();
    CHECK(g_ui.tools.pick_kind == 0 && g_ui.tools.pick_id == 0, "slot 1 picks test_crate (%d %d)", g_ui.tools.pick_kind,
        g_ui.tools.pick_id);
    CHECK(g_ui.tools.tool == EDITOR_TOOL_LOC_PLACE && g_ui.tools.place_shape == 10 && g_ui.tools.place_rotation == 2,
        "and arms Place with shape 10 rot 2 (tool %d, %d %d)", (int)g_ui.tools.tool, g_ui.tools.place_shape,
        g_ui.tools.place_rotation);
    Editor_PumpFacts(&g_editor);
    frame();

    /* An npc slot: the pick; Place stays armed (the spawn ghost). */
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_SELECT);
    push_slot(&g_chrome, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, 2);
    frame();
    CHECK(g_ui.tools.pick_kind == 1 && g_ui.tools.pick_id == 0 && g_ui.tools.tool == EDITOR_TOOL_LOC_PLACE,
        "slot 2 picks test_guard and arms Place (%d %d)", g_ui.tools.pick_kind, g_ui.tools.pick_id);
    push_slot(&g_chrome, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, 7);
    frame();
    CHECK(g_ui.tools.pick_kind == 1 && console_has(&g_ui, "slot 7 of palette Lumbridge is empty"),
        "an empty slot leaves the pick alone and says so");
    Editor_PumpFacts(&g_editor);

    /* The thumbnail cache: each shown record asked of the App once. */
    {
        int asked = 0;
        int scene = 100;
        while( Editor_CatalogPalette_ThumbWanted(&g_ui, &kind, &id, &slot) && asked < 8 )
        {
            Editor_CatalogPalette_ThumbDone(&g_ui, slot, scene++);
            asked++;
        }
        CHECK(asked == 2, "two records shown, two thumbnails asked (%d)", asked);
        frame();
        frame();
        CHECK(!Editor_CatalogPalette_ThumbWanted(&g_ui, &kind, &id, &slot), "redrawn, nothing is asked again");
    }

    /* New palette, then Add current pick: slot 1, then slot 2. */
    ToriRSChrome_SetText(&g_chrome, name, "Build");
    activate(button_new);
    CHECK(strcmp(Editor_CatalogPalette_Current(&g_ui), "Build") == 0 && strstr(doc_text(&g_editor), "palette Build\n"),
        "New palette makes Build and shows it");
    CHECK(strcmp(history_top(), "New palette Build") == 0, "History: %s", history_top());
    CHECK(Editor_Catalog_PickRecord(&g_ui, TORIRSMAPED_CATALOG_LOC, "test_door"), "pick test_door");
    g_ui.tools.place_shape = 0;
    g_ui.tools.place_rotation = 1;
    activate(add);
    CHECK(strstr(doc_text(&g_editor), "palette Build | slot 1 | loc:test_door | shape 0 rot 1\n") != NULL,
        "Add current pick: test_door into slot 1 with the Place pose");
    CHECK(strcmp(history_top(), "Add test_door to palette Build slot 1") == 0, "History: %s", history_top());
    CHECK(Editor_Catalog_PickRecord(&g_ui, TORIRSMAPED_CATALOG_OBJ, "test_knife"), "pick test_knife");
    activate(add);
    CHECK(strstr(doc_text(&g_editor), "palette Build | slot 2 | obj:test_knife\n") != NULL, "then test_knife into slot 2");
    frame();
    CHECK(strcmp(g_chrome.widgets[status].text, "2 of 10 slots") == 0, "status follows (%s)", g_chrome.widgets[status].text);
    ToriRSChrome_DropdownSetSelected(&g_chrome, target, 5);
    activate(add);
    CHECK(strstr(doc_text(&g_editor), "palette Build | slot 5 | obj:test_knife\n") != NULL, "Add to slot 5");
    CHECK(Editor_Undo(&g_editor) >= 1 && !strstr(doc_text(&g_editor), "slot 5"), "and one undo takes it back");
    ToriRSChrome_DropdownSetSelected(&g_chrome, target, 0);

    /* A second window of the Client sees the slots after a pump. */
    if( Editor_HostOpenMapEdEmbedPeer(&peer_host, &g_editor.host, TORIRSMAPED_ROLE_GENERIC, 1) )
    {
        int pv_panel;
        int pv_status;
        int pv_graph;

        Editor_OpenHost(&peer, &peer_host, "popped-out palette", &g_profile);
        ToriRSChrome_Init(&pv_chrome);
        ToriRSChrome_SetSurface(&pv_chrome, 800, 600);
        Editor_UiInit(&pv_ui, &pv_chrome, &peer);
        pv_ui.app_services.user_data = &g_app_stand_in;
        Editor_Catalog_Register(&pv_ui);
        Editor_CatalogPalette_Register(&pv_ui);
        CHECK(Editor_UiSetSolo(&pv_ui, "catalog_palette"), "a window can be the Catalog palette alone");
        Editor_UiSetVisible(&pv_ui, 1);
        Editor_SetStateCallback(&peer, on_state, &pv_ui);
        pv_panel = find_panel(&pv_chrome, "Catalog palette");
        pv_status = nth_label(&pv_chrome, pv_panel, 0);
        pv_graph = find_widget(&pv_chrome, pv_panel, TORIRS_CHROME_W_GRAPH, NULL);
        frame_of(&pv_chrome, &pv_ui);
        CHECK(Editor_CatalogPalette_Show(&pv_ui, "Build"), "the second window has Build from its open");
        frame_of(&pv_chrome, &pv_ui);
        CHECK(pv_status >= 0 && strcmp(pv_chrome.widgets[pv_status].text, "2 of 10 slots") == 0,
            "and its two slots (%s)", pv_status >= 0 ? pv_chrome.widgets[pv_status].text : "-");

        CHECK(Editor_Catalog_PickRecord(&g_ui, TORIRSMAPED_CATALOG_NPC, "test_guard"), "main window picks test_guard");
        activate(add);
        Editor_PumpFacts(&g_editor);
        Editor_PumpFacts(&peer);
        frame_of(&pv_chrome, &pv_ui);
        CHECK(strcmp(pv_chrome.widgets[pv_status].text, "3 of 10 slots") == 0
                  && strcmp(doc_text(&peer), doc_text(&g_editor)) == 0,
            "the main window's Add reaches it after a pump (%s)", pv_chrome.widgets[pv_status].text);

        /* The popped-out palette picks for the main window. */
        push_slot(&pv_chrome, pv_graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, 1);
        frame_of(&pv_chrome, &pv_ui);
        Editor_PumpFacts(&peer);
        Editor_PumpFacts(&g_editor);
        frame();
        CHECK(pv_ui.tools.pick_kind == 0 && pv_ui.tools.pick_id == 1 && g_ui.tools.pick_kind == 0
                  && g_ui.tools.pick_id == 1,
            "its slot click is the main window's pick too (main %d %d)", g_ui.tools.pick_kind, g_ui.tools.pick_id);

        Editor_SetStateCallback(&peer, NULL, NULL);
        Editor_UiFree(&pv_ui);
        Editor_Close(&peer);
        Editor_PumpFacts(&g_editor);
    }
    else
        CHECK(0, "a second window joins the Client");

    /* Right-click: Move to slot 3 swaps nothing (empty), Move to slot 2 swaps. */
    snprintf(before, sizeof(before), "%s", doc_text(&g_editor));
    push_slot(&g_chrome, graph, TORIRS_CHROME_EVENT_GRAPH_MENU, 1);
    frame();
    CHECK(g_chrome.panels[menu].visible, "right-click opens the slot menu");
    activate(find_widget(&g_chrome, menu, TORIRS_CHROME_W_MENUITEM, "Move to slot 2"));
    Editor_PalettesInit(&file);
    {
        const char* text = doc_text(&g_editor);
        Editor_PalettesParse(&file, text, strlen(text));
    }
    CHECK(Editor_PalettesEntry(&file, "Build", 2) && strcmp(Editor_PalettesEntry(&file, "Build", 2)->record, "test_door") == 0
              && Editor_PalettesEntry(&file, "Build", 1)
              && strcmp(Editor_PalettesEntry(&file, "Build", 1)->record, "test_knife") == 0,
        "Move to slot 2 swapped test_door and test_knife");
    CHECK(strcmp(history_top(), "Swap slots 1 and 2 of palette Build") == 0, "History: %s", history_top());
    CHECK(!g_chrome.panels[menu].visible, "the menu closed");
    CHECK(Editor_Undo(&g_editor) == 3 && strcmp(doc_text(&g_editor), before) == 0, "one undo puts the swap back");

    /* Set pose from the Place tool. */
    g_ui.tools.place_shape = 2;
    g_ui.tools.place_rotation = 3;
    push_slot(&g_chrome, graph, TORIRS_CHROME_EVENT_GRAPH_MENU, 1);
    frame();
    activate(find_widget(&g_chrome, menu, TORIRS_CHROME_W_MENUITEM, "Set pose from Place tool"));
    CHECK(strstr(doc_text(&g_editor), "palette Build | slot 1 | loc:test_door | shape 2 rot 3\n") != NULL,
        "Set pose remembers the Place tool's shape and rotation");

    /* Remove. */
    push_slot(&g_chrome, graph, TORIRS_CHROME_EVENT_GRAPH_MENU, 3);
    frame();
    activate(find_widget(&g_chrome, menu, TORIRS_CHROME_W_MENUITEM, "Remove"));
    CHECK(!strstr(doc_text(&g_editor), "slot 3 | npc:test_guard"), "Remove empties slot 3");
    CHECK(strcmp(history_top(), "Remove test_guard from palette Build slot 3") == 0, "History: %s", history_top());

    /* Rename moves every line; Delete takes them; each one undo step. */
    snprintf(before, sizeof(before), "%s", doc_text(&g_editor));
    ToriRSChrome_SetText(&g_chrome, name, "Shops");
    activate(button_rename);
    CHECK(strcmp(doc_text(&g_editor), before) == 0 && strcmp(Editor_CatalogPalette_Current(&g_ui), "Build") == 0,
        "renaming onto a taken name is refused");
    ToriRSChrome_SetText(&g_chrome, name, "Building");
    activate(button_rename);
    CHECK(!strstr(doc_text(&g_editor), "palette Build ") && !strstr(doc_text(&g_editor), "palette Build\n")
              && strstr(doc_text(&g_editor), "palette Building | slot 2 | obj:test_knife")
              && strcmp(Editor_CatalogPalette_Current(&g_ui), "Building") == 0,
        "Rename moves every line of the palette");
    CHECK(strcmp(history_top(), "Rename palette Build to Building") == 0, "History: %s", history_top());
    CHECK(Editor_Undo(&g_editor) == 3 && strcmp(doc_text(&g_editor), before) == 0, "one undo takes the rename back");
    frame();
    CHECK(strcmp(Editor_CatalogPalette_Current(&g_ui), "Build") == 0, "and the panel follows the name back");
    activate(button_delete);
    CHECK(!strstr(doc_text(&g_editor), "palette Build") && strcmp(Editor_CatalogPalette_Current(&g_ui), "Lumbridge") == 0,
        "Delete takes every line; the first palette is shown");
    CHECK(strcmp(history_top(), "Delete palette Build") == 0, "History: %s", history_top());
    CHECK(Editor_Undo(&g_editor) == 3 && strcmp(doc_text(&g_editor), before) == 0, "one undo brings it back");

    /* Next / previous palette. */
    frame();
    CHECK(Editor_CatalogPalette_Show(&g_ui, "Lumbridge") && Editor_CatalogPalette_Step(&g_ui, 1)
              && strcmp(Editor_CatalogPalette_Current(&g_ui), "Shops") == 0 && Editor_CatalogPalette_Step(&g_ui, -1)
              && strcmp(Editor_CatalogPalette_Current(&g_ui), "Lumbridge") == 0,
        "Step walks the palettes");

    /* Save writes the document's bytes to editor/palettes.txt. */
    CHECK(Editor_SaveAll(&g_editor) >= 1, "Save all saves the palettes");
    {
        struct Editor_DocHandle handle;
        CHECK(Editor_DocFind(&g_editor.doc, EDITOR_DOC_PALETTES, EDITOR_PALETTES_DOC_KEY, &handle) && handle.text
                  && !handle.text->dirty,
            "and the mirror's copy is clean");
    }
    {
        char path[2048];
        char disk[8192];
        size_t got;
        FILE* f;
        snprintf(path, sizeof(path), "%s/editor/palettes.txt", root);
        f = fopen(path, "rb");
        got = f ? fread(disk, 1, sizeof(disk) - 1, f) : 0;
        if( f )
            fclose(f);
        disk[got] = '\0';
        CHECK(strcmp(disk, doc_text(&g_editor)) == 0 && strncmp(disk, k_palettes, strlen(k_palettes)) == 0,
            "the file is the document, the hand-written lines untouched");
    }
    Editor_PalettesFree(&file);
    Editor_SetStateCallback(&g_editor, NULL, NULL);
    Editor_UiFree(&g_ui);
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
    CHECK(strcmp(doc_text(&g_editor), k_palettes) == 0, "Editor_OpenHost mirrors the palettes document");
    test_panel(root);
    Editor_Close(&g_editor);
    printf("editor_panels_palette_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
