#include "plugin/plugin_panel.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct PluginPanel*
PluginPanel_Create(void)
{
    /* PLUGINS: the window's whole state; PluginPanel_Free releases it. */
    struct PluginPanel* panel = ToriMem_Calloc(ToriMem_Plugins(), 1, sizeof(*panel));

    panel->panel = -1;
    return panel;
}

void
PluginPanel_Free(struct PluginPanel* panel)
{
    if( !panel )
        return;
    ToriMem_Free(ToriMem_Plugins(), panel->custom_pixels);
    panel->custom_pixels = NULL;
    panel->custom_pixel_capacity = 0;
    ToriMem_Free(ToriMem_Plugins(), panel);
}

void
PluginPanel_ChoicesClear(struct PluginPanel* panel)
{
    assert(panel);
    panel->choice_count = 0;
}

char const* const*
PluginPanel_ChoicesAdd(
    struct PluginPanel* panel,
    char const* choices,
    int* out_count)
{
    int first;
    int n = 0;
    char const* at;

    assert(panel);
    assert(choices);
    assert(out_count);

    first = panel->choice_count;
    at = choices;
    *out_count = 0;
    if( !choices[0] )
        return NULL;

    while( *at )
    {
        char const* end = strchr(at, '|');
        int len = end ? (int)(end - at) : (int)strlen(at);

        if( panel->choice_count >= PLUGIN_PANEL_CHOICES_MAX )
            break;
        if( len > (int)sizeof(panel->choice_text[0]) - 1 )
            len = (int)sizeof(panel->choice_text[0]) - 1;
        memcpy(panel->choice_text[panel->choice_count], at, (size_t)len);
        panel->choice_text[panel->choice_count][len] = '\0';
        panel->choice_ptrs[panel->choice_count] = panel->choice_text[panel->choice_count];
        panel->choice_count++;
        n++;
        if( !end )
            break;
        at = end + 1;
    }

    if( n == 0 )
        return NULL;
    *out_count = n;
    return &panel->choice_ptrs[first];
}

void
PluginPanel_RowsClear(struct PluginPanel* panel)
{
    assert(panel);
    panel->row_count = 0;
    panel->custom_row_count = 0;
    panel->custom_pending_count = 0;
    memset(panel->model_rows, 0, sizeof(panel->model_rows));
}

struct PluginPanelRow*
PluginPanel_RowAdd(
    struct PluginPanel* panel,
    int widget,
    int plugin,
    enum PluginPanelRowKind kind,
    int cfg_index,
    char const* widget_id)
{
    struct PluginPanelRow* row;

    assert(panel);
    assert(widget_id);
    if( widget < 0 )
        return NULL;
    if( panel->row_count >= PLUGIN_PANEL_ROWS_MAX )
        return NULL;

    row = &panel->rows[panel->row_count++];
    memset(row, 0, sizeof(*row));
    row->widget = widget;
    row->plugin = plugin;
    row->kind = (int)kind;
    row->cfg_index = cfg_index;
    snprintf(row->widget_id, sizeof(row->widget_id), "%s", widget_id);
    row->widget_serial = 0;
    row->widget_kind = -1;
    return row;
}

struct PluginPanelRow*
PluginPanel_RowAddModel(
    struct PluginPanel* panel,
    int widget,
    int plugin,
    int model_index,
    uint32_t serial,
    int widget_kind,
    char const* widget_id)
{
    struct PluginPanelRow* row;
    int row_index;

    assert(panel);
    assert(widget_id);
    if( widget < 0 || serial == 0 || model_index < 0 ||
        model_index >= TORIRS_PLUGIN_WIDGETS_MAX ||
        panel->row_count >= PLUGIN_PANEL_ROWS_MAX )
        return NULL;

    row_index = panel->row_count++;
    panel->model_rows[model_index] = row_index + 1;
    row = &panel->rows[row_index];
    memset(row, 0, sizeof(*row));
    row->widget = widget;
    row->plugin = plugin;
    row->kind = PLUGIN_PANEL_ROW_PANEL_WIDGET;
    row->cfg_index = -1;
    row->model_index = model_index;
    row->widget_serial = serial;
    row->widget_kind = widget_kind;
    snprintf(row->widget_id, sizeof(row->widget_id), "%s", widget_id);
    if( widget_kind == TORIRS_PANEL_WIDGET_CUSTOM &&
        panel->custom_row_count < TORIRS_PLUGIN_WIDGETS_MAX )
        panel->custom_rows[panel->custom_row_count++] = row_index;
    return row;
}

struct PluginPanelRow*
PluginPanel_Row(
    struct PluginPanel* panel,
    int row_index)
{
    assert(panel);
    if( row_index < 0 || row_index >= panel->row_count )
        return NULL;
    return &panel->rows[row_index];
}

int
PluginPanel_RowForModel(
    struct PluginPanel const* panel,
    int model_index)
{
    int mapped;

    assert(panel);
    if( model_index < 0 || model_index >= TORIRS_PLUGIN_WIDGETS_MAX )
        return -1;
    mapped = panel->model_rows[model_index] - 1;
    if( mapped < 0 || mapped >= panel->row_count )
        return -1;
    return mapped;
}

int
PluginPanel_RowFindWidget(
    struct PluginPanel const* panel,
    int widget)
{
    assert(panel);
    for( int i = 0; i < panel->row_count; i++ )
        if( panel->rows[i].widget == widget )
            return i;
    return -1;
}

struct PluginPanelRow*
PluginPanel_CustomRow(
    struct PluginPanel* panel,
    int custom)
{
    assert(panel);
    assert(custom >= 0);
    assert(custom < panel->custom_row_count);
    return &panel->rows[panel->custom_rows[custom]];
}

void
PluginPanel_CustomPendingSet(
    struct PluginPanel* panel,
    struct PluginPanelRow* row,
    int pending)
{
    assert(panel);
    assert(row);
    pending = pending ? 1 : 0;
    if( row->custom_present_pending == pending )
        return;
    row->custom_present_pending = pending;
    panel->custom_pending_count += pending ? 1 : -1;
    assert(panel->custom_pending_count >= 0);
    assert(panel->custom_pending_count <= panel->custom_row_count);
}

void
PluginPanel_CustomPendingClear(struct PluginPanel* panel)
{
    assert(panel);
    for( int i = 0; i < panel->custom_row_count; i++ )
    {
        int const row = panel->custom_rows[i];
        panel->rows[row].custom_present_pending = 0;
    }
    panel->custom_pending_count = 0;
}

void
PluginPanel_BuiltStamp(
    struct PluginPanel* panel,
    int count,
    int rev,
    uint32_t model_rev,
    uint32_t generation,
    uint32_t registry_rev)
{
    assert(panel);
    panel->built_for = count;
    panel->built_rev = rev;
    panel->built_model_rev = model_rev;
    panel->built_generation = generation;
    panel->built_registry_rev = registry_rev;
}

bool
PluginPanel_BuiltFor(
    struct PluginPanel const* panel,
    int count,
    int rev,
    uint32_t generation)
{
    assert(panel);
    return panel->built_for == count && panel->built_rev == rev &&
           panel->built_generation == generation;
}

bool
PluginPanel_BuiltForRegistry(
    struct PluginPanel const* panel,
    uint32_t registry_rev)
{
    assert(panel);
    return panel->built_registry_rev == registry_rev;
}

bool
PluginPanel_BuiltForModel(
    struct PluginPanel const* panel,
    uint32_t model_rev)
{
    assert(panel);
    return panel->built_model_rev == model_rev;
}
