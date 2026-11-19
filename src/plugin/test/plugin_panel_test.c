/*
 * The plugin window's state (plugin/plugin_panel.h).
 *
 * A created panel is zeroed with no chrome panel built. A row is tracked only
 * for a real widget and while the table has room, and a model row maps its
 * slot and lists itself as a custom row when it is one. A slot or widget that
 * names no row answers -1. The pending-present count follows the rows and is
 * cleared with them. A choice list is split on '|', each entry cut at 47
 * bytes, and the pool stops where it is full. The build stamps answer for
 * exactly what was recorded.
 *
 * Run: make -C src test-plugin-panel
 */

#include "plugin/plugin_panel.h"

#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_create_is_zeroed(void)
{
    struct PluginPanel* panel = PluginPanel_Create();
    int other = 0;

    TEST_ASSERT(panel->panel == -1, "no chrome panel built yet");
    TEST_ASSERT(panel->built_for == 0, "built_for starts at zero");
    TEST_ASSERT(panel->row_count == 0, "no rows");
    TEST_ASSERT(panel->choice_count == 0, "empty pool");
    TEST_ASSERT(panel->custom_pixels == NULL, "no custom raster yet");
    TEST_ASSERT(panel->exec_kind == 0, "exec kind zero");
    TEST_ASSERT(panel->chrome_requested == 0, "chrome not yet requested");
    for( size_t i = 0; i < sizeof(panel->model_rows) / sizeof(panel->model_rows[0]); i++ )
        other |= panel->model_rows[i];
    TEST_ASSERT(other == 0, "every model slot unmapped");

    /* The custom raster is released with the panel, from the same group. */
    panel->custom_pixels = ToriMem_Alloc(ToriMem_Plugins(), 64 * sizeof(uint32_t));
    panel->custom_pixel_capacity = 64;
    PluginPanel_Free(panel);
    PluginPanel_Free(NULL);
}

static void
test_row_add(void)
{
    struct PluginPanel* panel = PluginPanel_Create();
    struct PluginPanelRow* row;

    TEST_ASSERT(
        PluginPanel_RowAdd(panel, -1, 0, PLUGIN_PANEL_ROW_ENABLE, -1, "") == NULL,
        "a refused widget tracks nothing");
    TEST_ASSERT(panel->row_count == 0, "nothing tracked for -1");

    row = PluginPanel_RowAdd(panel, 7, 2, PLUGIN_PANEL_ROW_CONFIG, 3, "colour");
    TEST_ASSERT(row == &panel->rows[0], "first row");
    TEST_ASSERT(row->widget == 7 && row->plugin == 2, "widget and plugin");
    TEST_ASSERT(row->kind == PLUGIN_PANEL_ROW_CONFIG && row->cfg_index == 3, "kind and key");
    TEST_ASSERT(strcmp(row->widget_id, "colour") == 0, "id copied");
    TEST_ASSERT(row->widget_serial == 0 && row->widget_kind == -1, "no model identity");
    TEST_ASSERT(row->model_index == 0 && row->custom_layout_valid == 0, "rest zeroed");
    TEST_ASSERT(panel->custom_row_count == 0, "a config row is not custom");

    for( int i = 1; i < PLUGIN_PANEL_ROWS_MAX; i++ )
        TEST_ASSERT(
            PluginPanel_RowAdd(panel, 100 + i, 0, PLUGIN_PANEL_ROW_SAVE, -1, "") != NULL,
            "room up to the cap");
    TEST_ASSERT(panel->row_count == PLUGIN_PANEL_ROWS_MAX, "table full");
    TEST_ASSERT(
        PluginPanel_RowAdd(panel, 999, 0, PLUGIN_PANEL_ROW_SAVE, -1, "") == NULL,
        "a full table refuses");
    TEST_ASSERT(
        PluginPanel_RowAddModel(panel, 999, 0, 0, 5, TORIRS_PANEL_WIDGET_BUTTON, "b") == NULL,
        "a full table refuses a model row too");
    TEST_ASSERT(panel->model_rows[0] == 0, "and maps no slot");
    PluginPanel_Free(panel);
}

static void
test_row_add_model_and_lookups(void)
{
    struct PluginPanel* panel = PluginPanel_Create();
    struct PluginPanelRow* row;

    TEST_ASSERT(
        PluginPanel_RowAddModel(panel, -1, 0, 0, 5, TORIRS_PANEL_WIDGET_BUTTON, "b") == NULL,
        "no widget");
    TEST_ASSERT(
        PluginPanel_RowAddModel(panel, 3, 0, 0, 0, TORIRS_PANEL_WIDGET_BUTTON, "b") == NULL,
        "serial 0 is no identity");
    TEST_ASSERT(
        PluginPanel_RowAddModel(panel, 3, 0, -1, 5, TORIRS_PANEL_WIDGET_BUTTON, "b") == NULL,
        "slot below range");
    TEST_ASSERT(
        PluginPanel_RowAddModel(
            panel, 3, 0, TORIRS_PLUGIN_WIDGETS_MAX, 5, TORIRS_PANEL_WIDGET_BUTTON, "b") == NULL,
        "slot above range");
    TEST_ASSERT(panel->row_count == 0, "nothing tracked");

    (void)PluginPanel_RowAdd(panel, 10, 1, PLUGIN_PANEL_ROW_ENABLE, -1, "");
    row = PluginPanel_RowAddModel(panel, 11, 1, 4, 77, TORIRS_PANEL_WIDGET_BUTTON, "go");
    TEST_ASSERT(row == &panel->rows[1], "second row");
    TEST_ASSERT(row->kind == PLUGIN_PANEL_ROW_PANEL_WIDGET && row->cfg_index == -1, "model row");
    TEST_ASSERT(row->model_index == 4 && row->widget_serial == 77, "slot and serial");
    TEST_ASSERT(row->widget_kind == TORIRS_PANEL_WIDGET_BUTTON, "kind kept");
    TEST_ASSERT(strcmp(row->widget_id, "go") == 0, "id copied");
    TEST_ASSERT(panel->model_rows[4] == 2, "slot maps to index + 1");
    TEST_ASSERT(panel->custom_row_count == 0, "a button is not custom");

    row = PluginPanel_RowAddModel(panel, 12, 1, 6, 78, TORIRS_PANEL_WIDGET_CUSTOM, "well");
    TEST_ASSERT(row == &panel->rows[2], "third row");
    TEST_ASSERT(panel->custom_row_count == 1 && panel->custom_rows[0] == 2, "listed as custom");
    TEST_ASSERT(PluginPanel_CustomRow(panel, 0) == row, "custom row 0 is that row");

    TEST_ASSERT(PluginPanel_RowForModel(panel, 4) == 1, "slot 4 -> row 1");
    TEST_ASSERT(PluginPanel_RowForModel(panel, 6) == 2, "slot 6 -> row 2");
    TEST_ASSERT(PluginPanel_RowForModel(panel, 5) == -1, "unmapped slot");
    TEST_ASSERT(PluginPanel_RowForModel(panel, -1) == -1, "slot below range");
    TEST_ASSERT(
        PluginPanel_RowForModel(panel, TORIRS_PLUGIN_WIDGETS_MAX) == -1, "slot above range");

    TEST_ASSERT(PluginPanel_RowFindWidget(panel, 11) == 1, "widget 11 is row 1");
    TEST_ASSERT(PluginPanel_RowFindWidget(panel, 99) == -1, "no such widget");
    panel->rows[2].widget = 10;
    TEST_ASSERT(PluginPanel_RowFindWidget(panel, 10) == 0, "the first row tracking it");

    TEST_ASSERT(PluginPanel_Row(panel, 0) == &panel->rows[0], "row 0");
    TEST_ASSERT(PluginPanel_Row(panel, 2) == &panel->rows[2], "row 2");
    TEST_ASSERT(PluginPanel_Row(panel, 3) == NULL, "past the last row");
    TEST_ASSERT(PluginPanel_Row(panel, -1) == NULL, "negative index");

    /* A slot mapped by an earlier row set that is not live any more. */
    PluginPanel_RowsClear(panel);
    TEST_ASSERT(panel->row_count == 0 && panel->custom_row_count == 0, "rows gone");
    TEST_ASSERT(PluginPanel_RowForModel(panel, 4) == -1, "mapping gone with them");
    panel->model_rows[4] = 2;
    TEST_ASSERT(PluginPanel_RowForModel(panel, 4) == -1, "a stale mapping names no row");
    PluginPanel_Free(panel);
}

static void
test_custom_pending(void)
{
    struct PluginPanel* panel = PluginPanel_Create();
    struct PluginPanelRow* a;
    struct PluginPanelRow* b;

    a = PluginPanel_RowAddModel(panel, 1, 0, 0, 1, TORIRS_PANEL_WIDGET_CUSTOM, "a");
    b = PluginPanel_RowAddModel(panel, 2, 0, 1, 2, TORIRS_PANEL_WIDGET_CUSTOM, "b");
    PluginPanel_CustomPendingSet(panel, a, 1);
    PluginPanel_CustomPendingSet(panel, a, 5);
    TEST_ASSERT(panel->custom_pending_count == 1, "setting twice counts once");
    TEST_ASSERT(a->custom_present_pending == 1, "any non-zero is pending");
    PluginPanel_CustomPendingSet(panel, b, 1);
    TEST_ASSERT(panel->custom_pending_count == 2, "two pending");
    PluginPanel_CustomPendingSet(panel, a, 0);
    TEST_ASSERT(panel->custom_pending_count == 1, "one presented");
    TEST_ASSERT(a->custom_present_pending == 0, "row cleared");

    PluginPanel_CustomPendingClear(panel);
    TEST_ASSERT(panel->custom_pending_count == 0, "none pending");
    TEST_ASSERT(b->custom_present_pending == 0, "every custom row cleared");
    TEST_ASSERT(panel->custom_row_count == 2, "the rows themselves stay");

    PluginPanel_CustomPendingSet(panel, b, 1);
    PluginPanel_RowsClear(panel);
    TEST_ASSERT(panel->custom_pending_count == 0, "a cleared table owes nothing");
    PluginPanel_Free(panel);
}

static void
test_choices(void)
{
    struct PluginPanel* panel = PluginPanel_Create();
    char const* const* slice;
    char long_entry[80];
    int count = -1;

    slice = PluginPanel_ChoicesAdd(panel, "", &count);
    TEST_ASSERT(slice == NULL && count == 0, "an empty list is no dropdown");

    slice = PluginPanel_ChoicesAdd(panel, "red|green|blue", &count);
    TEST_ASSERT(count == 3, "three entries");
    TEST_ASSERT(slice == &panel->choice_ptrs[0], "slice starts the pool");
    TEST_ASSERT(strcmp(slice[0], "red") == 0, "first");
    TEST_ASSERT(strcmp(slice[1], "green") == 0, "middle");
    TEST_ASSERT(strcmp(slice[2], "blue") == 0, "last");

    slice = PluginPanel_ChoicesAdd(panel, "a||b|", &count);
    TEST_ASSERT(slice == &panel->choice_ptrs[3], "next slice follows the first");
    TEST_ASSERT(count == 3, "an empty middle entry is kept, a trailing bar adds none");
    TEST_ASSERT(strcmp(slice[1], "") == 0, "the empty entry");

    memset(long_entry, 'x', sizeof(long_entry) - 1);
    long_entry[sizeof(long_entry) - 1] = '\0';
    slice = PluginPanel_ChoicesAdd(panel, long_entry, &count);
    TEST_ASSERT(count == 1, "one long entry");
    TEST_ASSERT(strlen(slice[0]) == PLUGIN_PANEL_CHOICE_TEXT_MAX - 1, "cut at 47 bytes");

    PluginPanel_ChoicesClear(panel);
    TEST_ASSERT(panel->choice_count == 0, "pool emptied");
    while( panel->choice_count < PLUGIN_PANEL_CHOICES_MAX - 2 )
        (void)PluginPanel_ChoicesAdd(panel, "f", &count);
    slice = PluginPanel_ChoicesAdd(panel, "p|q|r|s", &count);
    TEST_ASSERT(count == 2, "a list longer than the room left is cut where the pool ends");
    TEST_ASSERT(strcmp(slice[1], "q") == 0, "the entries that fit");
    count = -1;
    slice = PluginPanel_ChoicesAdd(panel, "z", &count);
    TEST_ASSERT(slice == NULL && count == 0, "a full pool refuses");
    PluginPanel_Free(panel);
}

static void
test_built_stamps(void)
{
    struct PluginPanel* panel = PluginPanel_Create();

    PluginPanel_BuiltStamp(panel, 4, 0, 11, 22, 33);
    TEST_ASSERT(PluginPanel_BuiltFor(panel, 4, 0, 22), "built for what was stamped");
    TEST_ASSERT(!PluginPanel_BuiltFor(panel, 5, 0, 22), "a plugin count moved");
    TEST_ASSERT(!PluginPanel_BuiltFor(panel, 4, 1, 22), "the window revision moved");
    TEST_ASSERT(!PluginPanel_BuiltFor(panel, 4, 0, 23), "the selection moved");
    TEST_ASSERT(PluginPanel_BuiltForRegistry(panel, 33), "same registry");
    TEST_ASSERT(!PluginPanel_BuiltForRegistry(panel, 34), "registry moved");
    TEST_ASSERT(PluginPanel_BuiltForModel(panel, 11), "same model");
    TEST_ASSERT(!PluginPanel_BuiltForModel(panel, 12), "model moved");
    TEST_ASSERT(panel->built_generation == 22, "generation readable for the fences");
    PluginPanel_Free(panel);
}

int
main(void)
{
    ToriMem_Init();
    test_create_is_zeroed();
    test_row_add();
    test_row_add_model_and_lookups();
    test_custom_pending();
    test_choices();
    test_built_stamps();
    ToriMem_Shutdown();
    if( g_failures )
    {
        printf("plugin_panel_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("plugin_panel_test: ok\n");
    return 0;
}
