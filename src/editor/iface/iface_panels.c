#include "editor/iface/iface_panels.h"

#include "editor/editor.h"
#include "editor/editor_doc.h"
#include "mem/torirs_mem.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tables (borrowed by dropdowns and tabs: static) --------------------- */

static const char* const k_mount_names[] = { "Root", "Modal", "Overlay", "Tab" };
/* The App_OpenSubInterface type for each sub row (index 1..3). */
static const int k_mount_sub_type[] = { -1, 0, 1, 3 };

static const char* const k_hook_keys[] = {
    "onload",       "onclick",     "onop",          "onmouseover",    "onmouseleave",
    "onmouserepeat", "onclickrepeat", "onrelease",   "onhold",         "ondrag",
    "ondragcomplete", "onscrollwheel", "ontimer",    "onvarptransmit", "oninvtransmit",
    "onstattransmit", "ontargetenter", "ontargetleave",
};
#define HOOK_KEY_COUNT ((int)(sizeof(k_hook_keys) / sizeof(k_hook_keys[0])))

static const char* const k_group_titles[EDITOR_IF_GROUP_COUNT] = {
    "Id", "Geometry", "Graphic", "Text", "Model", "Inventory", "Interaction", "Scripts",
};

static const char* const k_cs2_tabs[] = { "Run", "Trace", "Watch", "Code" };

static uint32_t
token_colour(int token)
{
    switch( token )
    {
    case EDITOR_CS2_TOKEN_KEYWORD:
        return 0xFFC586C0u;
    case EDITOR_CS2_TOKEN_STRING:
        return 0xFFCE9178u;
    case EDITOR_CS2_TOKEN_COMMENT:
        return 0xFF6A9955u;
    case EDITOR_CS2_TOKEN_NUMBER:
        return 0xFFB5CEA8u;
    case EDITOR_CS2_TOKEN_LOCAL:
        return 0xFF9CDCFEu;
    case EDITOR_CS2_TOKEN_VAR:
        return 0xFF4FC1FFu;
    case EDITOR_CS2_TOKEN_CALL:
        return 0xFFDCDCAAu;
    case EDITOR_CS2_TOKEN_HEADER:
        return 0xFF569CD6u;
    }
    return 0;
}

static const char*
type_name(int type)
{
    switch( type )
    {
    case 0:
        return "layer";
    case 2:
        return "inv";
    case 3:
        return "rect";
    case 4:
        return "text";
    case 5:
        return "graphic";
    case 6:
        return "model";
    case 8:
        return "tooltip";
    case 9:
        return "line";
    }
    return "?";
}

/* ---- state --------------------------------------------------------------- */

void
EditorIfacePanels_Init(struct EditorIfacePanels* panels)
{
    assert(panels);
    memset(panels, 0, sizeof(*panels));
    EditorIfaceModel_Init(&panels->model);
    EditorCs2Code_Init(&panels->code_model);
    /* Every widget handle starts at -1, so an unbuilt panel claims nothing. */
    {
        int* handles[] = {
            &panels->panel_iface, &panels->in_open, &panels->bt_open, &panels->dd_mount,
            &panels->in_slot, &panels->bt_mount, &panels->cb_pick, &panels->bt_add,
            &panels->bt_delete, &panels->bt_up, &panels->bt_down, &panels->in_reparent,
            &panels->bt_reparent, &panels->lb_status, &panels->panel_tree,
            &panels->panel_inspector, &panels->inspector_tabs, &panels->in_field_key,
            &panels->in_field_value, &panels->bt_field_set, &panels->bt_field_remove,
            &panels->panel_hooks, &panels->in_hook_search, &panels->vl_scripts,
            &panels->dd_hook_key, &panels->bt_hook_set, &panels->lb_hook, &panels->panel_cs2,
            &panels->cs2_tabs, &panels->in_run_script, &panels->in_run_args,
            &panels->cb_run_trace, &panels->bt_run, &panels->bt_harness, &panels->lb_run,
            &panels->vl_trace, &panels->in_watch, &panels->bt_watch_add, &panels->vl_watch,
            &panels->in_watch_value, &panels->bt_watch_set, &panels->bt_watch_remove,
            &panels->in_code_name, &panels->bt_code_open, &panels->code, &panels->bt_compile,
            &panels->bt_code_save, &panels->vl_output,
        };
        for( size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); i++ )
            *handles[i] = -1;
        for( int i = 0; i < EDITOR_IFACE_ARG_ROWS_MAX; i++ )
            panels->arg_rows[i] = -1;
    }
    panels->picked_script = -1;
    panels->inspector_selected = -2;
    panels->tree_selected_shown = -2;
}

void
EditorIfacePanels_Free(struct EditorIfacePanels* panels)
{
    if( !panels )
        return;
    EditorIfaceModel_Free(&panels->model);
    EditorCs2Catalog_Free(&panels->catalog);
    EditorCs2Code_Free(&panels->code_model);
    EditorCs2Trace_Free(&panels->trace);
    ToriMem_Free(ToriMem_Editor(), panels->compile);
    panels->compile = NULL;
}

void
EditorIfacePanels_Configure(
    struct EditorIfacePanels* panels,
    const char* content_dir,
    const char* cache_dir,
    const char* cs2_bin,
    const char* names_dir,
    const char* work_dir)
{
    assert(panels);
    assert(content_dir);
    assert(cache_dir);
    assert(cs2_bin);
    assert(names_dir);
    assert(work_dir);
    snprintf(panels->content_dir, sizeof(panels->content_dir), "%s", content_dir);
    snprintf(panels->cache_dir, sizeof(panels->cache_dir), "%s", cache_dir);
    snprintf(panels->cs2_bin, sizeof(panels->cs2_bin), "%s", cs2_bin);
    snprintf(panels->names_dir, sizeof(panels->names_dir), "%s", names_dir);
    snprintf(panels->work_dir, sizeof(panels->work_dir), "%s", work_dir);
}

static void
push_request(
    struct EditorIfacePanels* panels,
    const struct EditorIfaceRequest* request)
{
    if( panels->request_count >= EDITOR_IFACE_REQUEST_QUEUE )
        return;
    panels->requests[panels->request_count++] = *request;
}

int
EditorIfacePanels_TakeRequest(
    struct EditorIfacePanels* panels,
    struct EditorIfaceRequest* out)
{
    assert(panels);
    assert(out);
    if( panels->request_count == 0 )
        return 0;
    *out = panels->requests[0];
    memmove(&panels->requests[0], &panels->requests[1],
            sizeof(panels->requests[0]) * (size_t)(panels->request_count - 1));
    panels->request_count--;
    return 1;
}

static void
status(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    const char* text)
{
    if( panels->lb_status >= 0 )
        ToriRSChrome_SetText(ui, panels->lb_status, text);
}

const char*
EditorIfacePanels_Text(
    const struct EditorIfacePanels* panels,
    struct Editor* editor,
    size_t* out_length)
{
    struct Editor_DocHandle handle;

    assert(panels);
    assert(out_length);
    *out_length = 0;
    if( !editor || !panels->model.doc_key[0]
        || !Editor_DocFind(&editor->doc, EDITOR_DOC_IFACE, panels->model.doc_key, &handle)
        || !handle.text || !handle.text->loaded )
        return NULL;
    *out_length = handle.text->length;
    return handle.text->text ? handle.text->text : "";
}

/* ---- build --------------------------------------------------------------- */

void
EditorIfacePanels_Build(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui)
{
    int p;

    assert(panels);
    assert(ui);
    if( panels->built )
        return;

    /* Interface. */
    p = panels->panel_iface = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_WINDOW, 8, 48, 250, "Interface");
    assert(p >= 0);
    panels->in_open = ToriRSChrome_TextInput(ui, p, "Name", "");
    panels->bt_open = ToriRSChrome_Button(ui, p, "Open interface");
    panels->dd_mount = ToriRSChrome_Dropdown(ui, p, "Mount as", k_mount_names, 4, 0);
    panels->in_slot = ToriRSChrome_TextInput(ui, p, "Slot (iface:child)", "");
    panels->bt_mount = ToriRSChrome_Button(ui, p, "Mount");
    panels->cb_pick = ToriRSChrome_Checkbox(ui, p, "Pick in view", 0);
    ToriRSChrome_Separator(ui, p);
    panels->bt_add = ToriRSChrome_Button(ui, p, "Add child");
    panels->bt_delete = ToriRSChrome_Button(ui, p, "Delete subtree");
    panels->bt_up = ToriRSChrome_Button(ui, p, "Move up");
    panels->bt_down = ToriRSChrome_Button(ui, p, "Move down");
    panels->in_reparent = ToriRSChrome_TextInput(ui, p, "New parent (name)", "");
    panels->bt_reparent = ToriRSChrome_Button(ui, p, "Reparent");
    panels->lb_status = ToriRSChrome_Label(ui, p, "No interface open");
    ToriRSChrome_SetTooltip(ui, panels->bt_add, "Appends at the next child id: nothing renumbers");
    ToriRSChrome_SetTooltip(ui, panels->bt_up, "Changes child ids: layer=, linked= and hook args follow");
    ToriRSChrome_PanelSetVisible(ui, p, 0);

    /* Components. */
    p = panels->panel_tree = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_WINDOW, 8, 400, 250, "Components");
    assert(p >= 0);
    ToriRSChrome_PanelSetScrollable(ui, p, 1);
    ToriRSChrome_PanelSetResizable(ui, p, 1);
    ToriRSChrome_PanelSetFixedHeight(ui, p, 320);
    ToriRSChrome_PanelSetVisible(ui, p, 0);

    /* Inspector. */
    p = panels->panel_inspector = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_WINDOW, 520, 48, 300, "Inspector");
    assert(p >= 0);
    ToriRSChrome_PanelSetScrollable(ui, p, 1);
    ToriRSChrome_PanelSetResizable(ui, p, 1);
    ToriRSChrome_PanelSetVisible(ui, p, 0);

    /* Hooks. */
    p = panels->panel_hooks = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_WINDOW, 520, 420, 300, "Hooks");
    assert(p >= 0);
    panels->in_hook_search = ToriRSChrome_TextInput(ui, p, "Search scripts", "");
    panels->vl_scripts = ToriRSChrome_VList(ui, p, "Scripts", 10, 0);
    panels->dd_hook_key = ToriRSChrome_Dropdown(ui, p, "Hook", k_hook_keys, HOOK_KEY_COUNT, 0);
    for( int i = 0; i < EDITOR_IFACE_ARG_ROWS_MAX; i++ )
    {
        panels->arg_rows[i] = ToriRSChrome_TextInput(ui, p, "arg", "");
        ToriRSChrome_SetHidden(ui, panels->arg_rows[i], 1);
    }
    panels->bt_hook_set = ToriRSChrome_Button(ui, p, "Set hook");
    panels->lb_hook = ToriRSChrome_Label(ui, p, "Pick a script");
    ToriRSChrome_PanelSetVisible(ui, p, 0);

    /* CS2. */
    p = panels->panel_cs2 = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_WINDOW, 840, 48, 420, "CS2");
    assert(p >= 0);
    panels->cs2_tabs = ToriRSChrome_Tabs(ui, p, k_cs2_tabs, 4, 0);
    ToriRSChrome_PanelBeginTab(ui, p, 0);
    panels->in_run_script = ToriRSChrome_TextInput(ui, p, "Script (id or name)", "");
    panels->in_run_args = ToriRSChrome_TextInput(ui, p, "Args (1, s:text, event_com, 548:12)", "");
    panels->cb_run_trace = ToriRSChrome_Checkbox(ui, p, "Trace the run", 1);
    panels->bt_run = ToriRSChrome_Button(ui, p, "Run");
    panels->bt_harness = ToriRSChrome_Button(ui, p, "Write harness case");
    panels->lb_run = ToriRSChrome_Label(ui, p, "");
    ToriRSChrome_PanelBeginTab(ui, p, 1);
    panels->vl_trace = ToriRSChrome_VList(ui, p, "Trace", 16, 0);
    ToriRSChrome_PanelBeginTab(ui, p, 2);
    panels->in_watch = ToriRSChrome_TextInput(ui, p, "Watch (varp 173, varbit 4153, varc 51)", "");
    panels->bt_watch_add = ToriRSChrome_Button(ui, p, "Add watch");
    panels->vl_watch = ToriRSChrome_VList(ui, p, "Values", 10, 0);
    panels->in_watch_value = ToriRSChrome_TextInput(ui, p, "Value", "");
    panels->bt_watch_set = ToriRSChrome_Button(ui, p, "Set selected");
    panels->bt_watch_remove = ToriRSChrome_Button(ui, p, "Remove selected");
    ToriRSChrome_PanelBeginTab(ui, p, 3);
    panels->in_code_name = ToriRSChrome_TextInput(ui, p, "Script name", "");
    panels->bt_code_open = ToriRSChrome_Button(ui, p, "Open .cs2");
    panels->code = ToriRSChrome_Code(ui, p, "Source", 18, 1);
    panels->bt_compile = ToriRSChrome_Button(ui, p, "Compile");
    panels->bt_code_save = ToriRSChrome_Button(ui, p, "Save");
    panels->vl_output = ToriRSChrome_VList(ui, p, "Compiler", 8, 0);
    ToriRSChrome_PanelBeginTab(ui, p, -1);
    ToriRSChrome_PanelSetVisible(ui, p, 0);

    EditorCs2Trace_Init(&panels->trace, 20000);
    panels->compile = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*panels->compile));
    assert(panels->compile);
    panels->built = 1;
}

void
EditorIfacePanels_ShowInterface(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    int visible)
{
    assert(panels);
    assert(ui);
    if( !panels->built )
        return;
    ToriRSChrome_PanelSetVisible(ui, panels->panel_iface, visible);
    ToriRSChrome_PanelSetVisible(ui, panels->panel_tree, visible);
    ToriRSChrome_PanelSetVisible(ui, panels->panel_inspector, visible);
    ToriRSChrome_PanelSetVisible(ui, panels->panel_hooks, visible);
}

void
EditorIfacePanels_ShowCs2(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    int visible)
{
    assert(panels);
    assert(ui);
    if( panels->built )
        ToriRSChrome_PanelSetVisible(ui, panels->panel_cs2, visible);
}

/* ---- tree and inspector -------------------------------------------------- */

static void
tree_rebuild(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui)
{
    const struct EditorIfaceModel* model = &panels->model;
    int shown = model->row_count < EDITOR_IFACE_TREE_ROWS_MAX ? model->row_count : EDITOR_IFACE_TREE_ROWS_MAX;

    ToriRSChrome_PanelClearWidgets(ui, panels->panel_tree);
    panels->tree_row_count = 0;
    for( int i = 0; i < shown; i++ )
    {
        int position = model->rows[i];
        const struct EditorIfaceNode* node = &model->nodes[position];
        char label[TORIRS_CHROME_LABEL_MAX];
        int expander = node->child_count == 0 ? TORIRS_CHROME_TREE_LEAF
                       : node->expanded       ? TORIRS_CHROME_TREE_OPEN
                                              : TORIRS_CHROME_TREE_CLOSED;
        int widget;

        snprintf(label, sizeof(label), "%d %s  %s%s%s", position, node->name, type_name(node->type),
                 node->if3 ? "" : " if1", node->hidden ? " hidden" : "");
        widget = ToriRSChrome_TreeRow(ui, panels->panel_tree, label, node->depth, expander);
        if( widget < 0 )
            break;
        if( position == model->selected )
            ToriRSChrome_TreeSelect(ui, widget);
        panels->tree_rows[panels->tree_row_count] = widget;
        panels->tree_row_position[panels->tree_row_count] = position;
        panels->tree_row_count++;
    }
    if( model->row_count > shown )
    {
        char more[TORIRS_CHROME_LABEL_MAX];
        snprintf(more, sizeof(more), "... %d more rows (collapse a node)", model->row_count - shown);
        ToriRSChrome_Label(ui, panels->panel_tree, more);
    }
    panels->tree_generation = model->generation;
    panels->tree_selected_shown = model->selected;
}

/** What the inspector shows for a raw value: text fields unescaped. */
static void
field_display(
    const struct EditorIfaceField* field,
    char* out,
    size_t out_size)
{
    if( Editor_IfKeyAt(field->row)->kind == EDITOR_IF_VALUE_STR )
        Editor_IfStringText(field->raw, out, out_size);
    else
        snprintf(out, out_size, "%s", field->raw);
}

static void
inspector_rebuild(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    const char* text,
    size_t length)
{
    const struct EditorIfaceModel* model = &panels->model;
    int p = panels->panel_inspector;

    ToriRSChrome_PanelClearWidgets(ui, p);
    panels->field_count = 0;
    panels->inspector_tabs = ToriRSChrome_Tabs(ui, p, k_group_titles, EDITOR_IF_GROUP_COUNT, panels->inspector_tab);
    /* Only the active group's rows are built, so they belong to every tab:
     * the strip picks what is built, not what is shown. */
    ToriRSChrome_PanelBeginTab(ui, p, -1);
    if( model->selected < 0 || !text )
    {
        ToriRSChrome_Label(ui, p, "Select a component");
    }
    else
    {
        const struct EditorIfaceNode* node = &model->nodes[model->selected];
        char heading[TORIRS_CHROME_LABEL_MAX];

        snprintf(heading, sizeof(heading), "%s  child %d  id %d", node->name, model->selected,
                 EditorIfaceModel_ComponentId(model, model->selected));
        ToriRSChrome_Heading(ui, p, heading);
        panels->field_count = EditorIfaceModel_Fields(
            text, length, model->selected, panels->inspector_tab, 1, panels->fields, EDITOR_IFACE_FIELD_ROWS_MAX);
        for( int i = 0; i < panels->field_count; i++ )
        {
            const struct EditorIfaceField* field = &panels->fields[i];
            char label[TORIRS_CHROME_LABEL_MAX];
            char shown[TORIRS_CHROME_INPUT_MAX];
            int widget;

            snprintf(label, sizeof(label), "%s%s", field->present ? "" : "+ ", field->key);
            field_display(field, shown, sizeof(shown));
            if( field->too_long )
            {
                char cut[TORIRS_CHROME_LABEL_MAX];
                snprintf(cut, sizeof(cut), "%s = %.40s... (too long to edit here)", field->key, field->raw);
                widget = ToriRSChrome_Label(ui, p, cut);
            }
            else
                widget = ToriRSChrome_TextInput(ui, p, label, shown);
            panels->field_rows[i] = widget;
            if( widget >= 0 && Editor_IfKeyAt(field->row)->kind == EDITOR_IF_VALUE_HOOK && field->present )
            {
                char described[TORIRS_CHROME_LABEL_MAX];
                EditorCs2_DescribeHook(
                    panels->catalog_loaded ? &panels->catalog : NULL, field->raw, described, sizeof(described));
                ToriRSChrome_SetTooltip(ui, widget, described);
            }
        }
    }
    ToriRSChrome_Separator(ui, p);
    panels->in_field_key = ToriRSChrome_TextInput(ui, p, "Field (e.g. op3)", "");
    panels->in_field_value = ToriRSChrome_TextInput(ui, p, "Value", "");
    panels->bt_field_set = ToriRSChrome_Button(ui, p, "Set field");
    panels->bt_field_remove = ToriRSChrome_Button(ui, p, "Remove field");
    panels->inspector_generation = model->generation;
    panels->inspector_selected = model->selected;
}

static void
apply_one(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    const struct Editor_Cmd* command,
    const char* what)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    if( !editor || !Editor_Apply(editor, command) )
        snprintf(line, sizeof(line), "%s: refused", what);
    else
        snprintf(line, sizeof(line), "%s", what);
    status(panels, ui, line);
}

static void
apply_stroke(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    const struct Editor_Cmd* commands,
    int count,
    const char* what)
{
    int applied = 0;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( !editor || count <= 0 )
        return;
    Editor_StrokeBegin(editor);
    for( int i = 0; i < count; i++ )
    {
        if( !Editor_Apply(editor, &commands[i]) )
            break;
        applied++;
    }
    Editor_StrokeEnd(editor);
    snprintf(line, sizeof(line), "%s: %d of %d commands%s", what, applied, count,
             applied == count ? "" : " (refused; undo the stroke)");
    status(panels, ui, line);
}

/** Commit inspector field `index` from its input's text. */
static void
commit_field(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    const char* key,
    const char* typed,
    int remove)
{
    struct Editor_Cmd command;
    size_t length = 0;
    const char* text = EditorIfacePanels_Text(panels, editor, &length);
    int row = Editor_IfKeyLookup(key, NULL);
    char raw[EDITOR_CMD_VALUE_MAX];

    if( !text || panels->model.selected < 0 )
        return;
    if( strcmp(key, "@name") == 0 )
        snprintf(raw, sizeof(raw), "%s", typed);
    else if( row < 0 )
    {
        status(panels, ui, "Not a key cachepack reads");
        return;
    }
    else if( Editor_IfKeyAt(row)->kind == EDITOR_IF_VALUE_STR )
    {
        if( Editor_IfStringRaw(typed, raw, sizeof(raw)) >= sizeof(raw) )
        {
            status(panels, ui, "Too long for one edit");
            return;
        }
    }
    else
        snprintf(raw, sizeof(raw), "%s", typed);
    if( !EditorIfaceModel_PlanField(&panels->model, text, length, panels->model.selected, key,
                                    remove ? NULL : raw, &command) )
    {
        status(panels, ui, "No change, or a value cachepack would refuse");
        return;
    }
    apply_one(panels, ui, editor, &command, remove ? "Field removed" : "Field set");
}

/* ---- hooks --------------------------------------------------------------- */

static void
catalog_ensure(struct EditorIfacePanels* panels)
{
    char dir[1100];

    if( panels->catalog_loaded || !panels->content_dir[0] )
        return;
    snprintf(dir, sizeof(dir), "%s/scripts", panels->content_dir);
    EditorCs2Catalog_Load(&panels->catalog, dir);
    panels->catalog_loaded = 1;
}

static void
hooks_refresh(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui)
{
    const char* search = ToriRSChrome_Text(ui, panels->in_hook_search);
    int first = 0;
    int count = 0;

    if( !panels->catalog_loaded )
        return;
    if( strcmp(search, panels->search_shown) != 0 )
    {
        snprintf(panels->search_shown, sizeof(panels->search_shown), "%s", search);
        panels->search_count = EditorCs2Catalog_Search(
            &panels->catalog, search, panels->search_hits,
            (int)(sizeof(panels->search_hits) / sizeof(panels->search_hits[0])));
        ToriRSChrome_VListSetCount(ui, panels->vl_scripts, panels->search_count);
    }
    if( ToriRSChrome_VListWindow(ui, panels->vl_scripts, &first, &count) )
    {
        for( int row = first; row < first + count && row < panels->search_count; row++ )
        {
            const struct EditorCs2Script* script = &panels->catalog.scripts[panels->search_hits[row]];
            char line[TORIRS_CHROME_INPUT_MAX];
            snprintf(line, sizeof(line), "%d %s (%d args)%s", script->id, script->name, script->arg_count,
                     script->clientscript ? "" : " proc");
            ToriRSChrome_VListSetRow(ui, panels->vl_scripts, row, line);
        }
    }
}

static void
hook_pick(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    int row)
{
    const struct EditorCs2Script* script;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( row < 0 || row >= panels->search_count )
        return;
    panels->picked_script = panels->search_hits[row];
    script = &panels->catalog.scripts[panels->picked_script];
    for( int i = 0; i < EDITOR_IFACE_ARG_ROWS_MAX; i++ )
    {
        int used = i < script->arg_count;
        ToriRSChrome_SetHidden(ui, panels->arg_rows[i], !used);
        if( used )
        {
            ToriRSChrome_SetLabel(ui, panels->arg_rows[i], script->arg_decl[i]);
            ToriRSChrome_SetText(ui, panels->arg_rows[i], "");
        }
    }
    snprintf(line, sizeof(line), "%s: %d args%s", script->name, script->arg_count,
             script->arg_count > EDITOR_IFACE_ARG_ROWS_MAX ? " (too many for this panel)" : "");
    ToriRSChrome_SetText(ui, panels->lb_hook, line);
}

static void
hook_set(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor)
{
    const struct EditorCs2Script* script;
    const char* values[EDITOR_IFACE_ARG_ROWS_MAX];
    struct Editor_IfHook* hook;
    char raw[EDITOR_CMD_VALUE_MAX];
    int key = ToriRSChrome_DropdownSelected(ui, panels->dd_hook_key);

    if( panels->picked_script < 0 || panels->model.selected < 0 || key < 0 || key >= HOOK_KEY_COUNT )
    {
        ToriRSChrome_SetText(ui, panels->lb_hook, "Select a component and a script");
        return;
    }
    script = &panels->catalog.scripts[panels->picked_script];
    if( script->arg_count > EDITOR_IFACE_ARG_ROWS_MAX )
        return;
    for( int i = 0; i < script->arg_count; i++ )
        values[i] = ToriRSChrome_Text(ui, panels->arg_rows[i]);
    hook = ToriMem_Alloc(ToriMem_Editor(), sizeof(*hook));
    assert(hook);
    if( !EditorCs2_BuildHook(script, values, script->arg_count, hook) )
        ToriRSChrome_SetText(ui, panels->lb_hook, "An argument does not fit its type");
    else if( Editor_IfHookFormat(hook, raw, sizeof(raw)) >= sizeof(raw) - 1 )
        ToriRSChrome_SetText(ui, panels->lb_hook, "Hook too long for one edit");
    else
        commit_field(panels, ui, editor, k_hook_keys[key], raw, 0);
    ToriMem_Free(ToriMem_Editor(), hook);
}

/* ---- CS2 ----------------------------------------------------------------- */

static int
find_script_id(
    struct EditorIfacePanels* panels,
    const char* text)
{
    char* end = NULL;
    long id = strtol(text, &end, 10);

    if( text[0] && end && *end == '\0' )
        return (int)id;
    catalog_ensure(panels);
    for( int i = 0; i < panels->catalog.count; i++ )
    {
        if( strcmp(panels->catalog.scripts[i].name, text) == 0 )
            return panels->catalog.scripts[i].id;
    }
    return -1;
}

/** Parse "1, s:text, event_com, 548:12" into a run request's arguments. */
static int
parse_run_args(
    const char* text,
    struct EditorIfaceRequest* request)
{
    char buf[TORIRS_CHROME_INPUT_MAX];
    char* cursor;

    snprintf(buf, sizeof(buf), "%s", text);
    request->arg_count = 0;
    request->string_count = 0;
    request->string_mask = 0;
    cursor = buf;
    while( *cursor )
    {
        char* comma = strchr(cursor, ',');
        char* token;
        int position = request->arg_count;

        if( comma )
            *comma = '\0';
        token = cursor;
        while( *token == ' ' )
            token++;
        for( char* tail = token + strlen(token); tail > token && tail[-1] == ' '; )
            *--tail = '\0';
        if( position >= EDITOR_IFACE_RUN_ARGS_MAX )
            return 0;
        if( token[0] )
        {
            int value = 0;
            int found = 0;
            int interface_id = 0;
            int child = 0;

            if( token[0] == 's' && token[1] == ':' )
            {
                if( request->string_count >= 4 )
                    return 0;
                snprintf(request->strings[request->string_count++], sizeof(request->strings[0]), "%s", token + 2);
                request->string_mask |= (uint64_t)1 << position;
                found = 1;
            }
            for( int e = 0; !found && e < Editor_IfEventArgCount(); e++ )
            {
                if( strcmp(token, Editor_IfEventArgAt(e)->name) == 0 )
                {
                    value = Editor_IfEventArgAt(e)->value;
                    found = 1;
                }
            }
            if( !found && strchr(token, ':') && sscanf(token, "%d:%d", &interface_id, &child) == 2 )
            {
                value = (interface_id << 16) | (child & 0xFFFF);
                found = 1;
            }
            if( !found )
            {
                char* end = NULL;
                long number = strtol(token, &end, 0);
                if( !end || *end != '\0' )
                    return 0;
                value = (int)number;
            }
            request->args[position] = value;
            request->arg_count++;
        }
        if( !comma )
            break;
        cursor = comma + 1;
    }
    return 1;
}

static void
run_script(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    int harness)
{
    struct EditorIfaceRequest request;
    char line[TORIRS_CHROME_LABEL_MAX];

    memset(&request, 0, sizeof(request));
    request.kind = EDITOR_IFACE_REQUEST_RUN;
    request.script_id = find_script_id(panels, ToriRSChrome_Text(ui, panels->in_run_script));
    request.component_id = panels->model.selected >= 0
        ? EditorIfaceModel_ComponentId(&panels->model, panels->model.selected)
        : -1;
    request.interface_id = panels->model.interface_id;
    request.trace = ToriRSChrome_Checked(ui, panels->cb_run_trace);
    if( request.script_id < 0 || !parse_run_args(ToriRSChrome_Text(ui, panels->in_run_args), &request) )
    {
        ToriRSChrome_SetText(ui, panels->lb_run, "Unknown script, or an argument that does not parse");
        return;
    }
    if( harness )
    {
        int ints[EDITOR_IFACE_RUN_ARGS_MAX];
        const char* strings[4];
        int int_count = 0;
        char path[1200];
        char id[64];

        for( int i = 0; i < request.arg_count; i++ )
        {
            if( !(request.string_mask & ((uint64_t)1 << i)) )
                ints[int_count++] = request.args[i];
        }
        for( int i = 0; i < request.string_count; i++ )
            strings[i] = request.strings[i];
        snprintf(id, sizeof(id), "editor_script%d", request.script_id);
        snprintf(path, sizeof(path), "%s/cs2_case_%d.json", panels->work_dir[0] ? panels->work_dir : ".",
                 request.script_id);
        snprintf(line, sizeof(line), "%s %s",
                 EditorCs2_WriteHarnessCase(path, id, request.script_id, ints, int_count, strings,
                                            request.string_count, 1)
                     ? "wrote"
                     : "could not write",
                 path);
        ToriRSChrome_SetText(ui, panels->lb_run, line);
        return;
    }
    push_request(panels, &request);
    snprintf(line, sizeof(line), "Running script %d (%d args)", request.script_id, request.arg_count);
    ToriRSChrome_SetText(ui, panels->lb_run, line);
}

static void
code_open(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor)
{
    const char* name = ToriRSChrome_Text(ui, panels->in_code_name);
    char key[EDITOR_DOC_KEY_MAX];

    if( !name[0] || !editor )
        return;
    snprintf(key, sizeof(key), "cs2:%s", name);
    if( !Editor_OpenDoc(editor, EDITOR_DOC_TEXT, key) )
    {
        ToriRSChrome_VListSetCount(ui, panels->vl_output, 1);
        ToriRSChrome_VListSetRow(ui, panels->vl_output, 0, "Could not open that script");
        return;
    }
    snprintf(panels->code_key, sizeof(panels->code_key), "%s", key);
    panels->code_model.line_count = 0;
}

static const char*
code_text(
    struct EditorIfacePanels* panels,
    struct Editor* editor,
    size_t* out_length)
{
    struct Editor_DocHandle handle;

    *out_length = 0;
    if( !editor || !panels->code_key[0]
        || !Editor_DocFind(&editor->doc, EDITOR_DOC_TEXT, panels->code_key, &handle) || !handle.text
        || !handle.text->loaded )
        return NULL;
    *out_length = handle.text->length;
    return handle.text->text ? handle.text->text : "";
}

static void
code_refresh(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor)
{
    size_t length = 0;
    const char* text = code_text(panels, editor, &length);
    int first = 0;
    int count = 0;

    if( !text )
        return;
    EditorCs2Code_Sync(&panels->code_model, panels->code_key, text, length);
    ToriRSChrome_VListSetCount(ui, panels->code, panels->code_model.line_count);
    if( !ToriRSChrome_VListWindow(ui, panels->code, &first, &count) )
        return;
    for( int line = first; line < first + count && line < panels->code_model.line_count; line++ )
    {
        char buf[TORIRS_CHROME_INPUT_MAX];
        struct EditorCs2Span spans[32];
        struct ToriRSChromeSpanInput inputs[32];
        int span_count;

        EditorCs2Code_Line(&panels->code_model, text, line, buf, sizeof(buf));
        ToriRSChrome_CodeSetLine(ui, panels->code, line, buf);
        span_count = EditorCs2_Highlight(buf, spans, 32);
        for( int i = 0; i < span_count; i++ )
        {
            inputs[i].start = spans[i].start;
            inputs[i].len = spans[i].length;
            inputs[i].color = token_colour(spans[i].token);
        }
        ToriRSChrome_CodeSetLineSpans(ui, panels->code, line, inputs, span_count);
    }
}

static void
mkdir_one(const char* path)
{
#if defined(_WIN32)
    (void)mkdir(path);
#else
    (void)mkdir(path, 0755);
#endif
}

static void
code_compile(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor)
{
    size_t length = 0;
    const char* text = code_text(panels, editor, &length);
    struct EditorCs2CompileJob job;
    char work[1200];
    char names[1300];
    int names_ok;
    int rows = 0;

    if( !text )
        return;
    snprintf(work, sizeof(work), "%s/cs2_compile", panels->work_dir[0] ? panels->work_dir : ".");
    snprintf(names, sizeof(names), "%s/names", work);
    catalog_ensure(panels);
    /* The content's script names, over RuneStar's other tables. */
    {
        char parent[1200];
        snprintf(parent, sizeof(parent), "%s", panels->work_dir[0] ? panels->work_dir : ".");
        mkdir_one(parent);
        mkdir_one(work);
    }
    names_ok = EditorCs2Catalog_WriteNamesDir(&panels->catalog, panels->names_dir, names);
    memset(&job, 0, sizeof(job));
    job.cs2_bin = panels->cs2_bin;
    job.cache_dir = panels->cache_dir;
    job.names_dir = names_ok ? names : (panels->names_dir[0] ? panels->names_dir : NULL);
    job.work_dir = work;
    job.script_name = panels->code_key + 4;
    job.source = text;
    job.source_length = length;
    EditorCs2_Compile(&job, panels->compile);

    ToriRSChrome_VListSetCount(ui, panels->vl_output, panels->compile->line_count + 1);
    {
        char head[TORIRS_CHROME_INPUT_MAX];
        snprintf(head, sizeof(head), "%s: compiled %d, failed %d, %d declined",
                 panels->compile->status == EDITOR_CS2_COMPILED  ? "OK"
                 : panels->compile->status == EDITOR_CS2_DECLINED ? "DECLINED"
                                                                  : "NOT RUN",
                 panels->compile->compiled, panels->compile->failed, panels->compile->decline_count);
        ToriRSChrome_VListSetRow(ui, panels->vl_output, rows++, head);
    }
    for( int i = 0; i < panels->compile->line_count; i++ )
        ToriRSChrome_VListSetRow(ui, panels->vl_output, rows++, panels->compile->lines[i]);
}

void
EditorIfacePanels_TraceDone(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    const char* summary)
{
    assert(panels);
    assert(ui);
    assert(summary);
    if( !panels->built )
        return;
    ToriRSChrome_VListSetCount(ui, panels->vl_trace, panels->trace.count);
    ToriRSChrome_VListSetScroll(ui, panels->vl_trace, 0);
    ToriRSChrome_SetText(ui, panels->lb_run, summary);
}

void
EditorIfacePanels_SelectComponent(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    int component_id)
{
    int position;

    (void)ui;
    assert(panels);
    assert(ui);
    position = EditorIfaceModel_FindComponent(&panels->model, component_id);
    if( position < 0 )
        return;
    EditorIfaceModel_Select(&panels->model, position);
}

/* ---- the frame ----------------------------------------------------------- */

int
EditorIfacePanels_OpenInterface(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    const char* name)
{
    char key[EDITOR_DOC_KEY_MAX];
    char line[TORIRS_CHROME_LABEL_MAX];

    assert(panels);
    assert(ui);
    assert(name);
    if( !editor || !name[0] )
        return 0;
    snprintf(key, sizeof(key), "if:%s", name);
    if( !Editor_DocKeyValid(EDITOR_DOC_IFACE, key) || !Editor_OpenDoc(editor, EDITOR_DOC_IFACE, key) )
    {
        snprintf(line, sizeof(line), "Could not open %s", key);
        status(panels, ui, line);
        return 0;
    }
    snprintf(panels->iface_name, sizeof(panels->iface_name), "%s", name);
    EditorIfaceModel_Free(&panels->model);
    snprintf(panels->model.doc_key, sizeof(panels->model.doc_key), "%s", key);
    panels->tree_generation = (uint32_t)-1;
    catalog_ensure(panels);
    return 1;
}

void
EditorIfacePanels_Tick(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor)
{
    size_t length = 0;
    const char* text;

    assert(panels);
    assert(ui);
    if( !panels->built )
        return;

    text = EditorIfacePanels_Text(panels, editor, &length);
    if( text )
    {
        char key[EDITOR_DOC_KEY_MAX];
        snprintf(key, sizeof(key), "%s", panels->model.doc_key);
        if( EditorIfaceModel_Sync(&panels->model, key, text, length) )
        {
            char line[TORIRS_CHROME_LABEL_MAX];
            snprintf(line, sizeof(line), "%s: interface %d, %d components", panels->iface_name,
                     panels->model.interface_id, panels->model.count);
            status(panels, ui, line);
        }
    }
    if( panels->tree_generation != panels->model.generation
        || panels->tree_selected_shown != panels->model.selected )
        tree_rebuild(panels, ui);
    if( panels->inspector_generation != panels->model.generation
        || panels->inspector_selected != panels->model.selected )
        inspector_rebuild(panels, ui, text, length);

    hooks_refresh(panels, ui);
    code_refresh(panels, ui, editor);

    /* Watch and trace rows: the visible window, compare-then-set. */
    {
        int first = 0;
        int count = 0;
        ToriRSChrome_VListSetCount(ui, panels->vl_watch, panels->watch.count);
        if( ToriRSChrome_VListWindow(ui, panels->vl_watch, &first, &count) )
        {
            for( int row = first; row < first + count && row < panels->watch.count; row++ )
            {
                char line[TORIRS_CHROME_INPUT_MAX];
                EditorCs2Watch_Format(&panels->watch, row, line, sizeof(line));
                ToriRSChrome_VListSetRow(ui, panels->vl_watch, row, line);
            }
        }
        ToriRSChrome_VListSetCount(ui, panels->vl_trace, panels->trace.count);
        if( ToriRSChrome_VListWindow(ui, panels->vl_trace, &first, &count) )
        {
            for( int row = first; row < first + count && row < panels->trace.count; row++ )
            {
                char line[TORIRS_CHROME_INPUT_MAX];
                EditorCs2Trace_Format(&panels->trace, row, line, sizeof(line));
                ToriRSChrome_VListSetRow(ui, panels->vl_trace, row, line);
            }
        }
    }
}

static int
structure_op(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    int widget)
{
    size_t length = 0;
    const char* text = EditorIfacePanels_Text(panels, editor, &length);
    const struct EditorIfaceModel* model = &panels->model;
    struct Editor_Cmd command;
    int selected = model->selected;

    if( !text )
        return 1;
    if( widget == panels->bt_add )
    {
        if( EditorIfaceModel_PlanAddChild(model, selected, &command) )
            apply_one(panels, ui, editor, &command, "Component added");
        else
            status(panels, ui, "Structure edits are locked here");
        return 1;
    }
    if( selected < 0 )
    {
        status(panels, ui, "Select a component first");
        return 1;
    }
    if( widget == panels->bt_delete )
    {
        struct Editor_Cmd* plan = ToriMem_Alloc(ToriMem_Editor(), sizeof(*plan) * 2048);
        int planned;

        assert(plan);
        planned = EditorIfaceModel_PlanDelete(model, text, length, selected, plan, 2048);
        if( planned > 0 )
            apply_stroke(panels, ui, editor, plan, planned, "Deleted");
        else
            status(panels, ui, "Cannot delete: referenced, locked, or a value too long to undo");
        ToriMem_Free(ToriMem_Editor(), plan);
        return 1;
    }
    if( widget == panels->bt_up || widget == panels->bt_down )
    {
        int target = selected + (widget == panels->bt_up ? -1 : 1);
        if( EditorIfaceModel_PlanMove(model, selected, target, &command) )
            apply_one(panels, ui, editor, &command, "Moved (child ids changed)");
        return 1;
    }
    if( widget == panels->bt_reparent )
    {
        const char* name = ToriRSChrome_Text(ui, panels->in_reparent);
        int parent = -1;

        if( name[0] )
        {
            for( int i = 0; i < model->count; i++ )
            {
                if( strcmp(model->nodes[i].name, name) == 0 )
                    parent = i;
            }
            if( parent < 0 )
            {
                status(panels, ui, "No component by that name");
                return 1;
            }
        }
        if( EditorIfaceModel_PlanReparent(model, selected, parent, &command) )
            apply_one(panels, ui, editor, &command, "Reparented");
        else
            status(panels, ui, "Cannot reparent there");
        return 1;
    }
    return 0;
}

int
EditorIfacePanels_OnActivate(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    int widget)
{
    assert(panels);
    assert(ui);
    if( !panels->built || widget < 0 )
        return 0;

    for( int i = 0; i < panels->tree_row_count; i++ )
    {
        if( panels->tree_rows[i] != widget )
            continue;
        if( ToriRSChrome_ActivationWasExpand(ui) )
            EditorIfaceModel_Toggle(&panels->model, panels->tree_row_position[i]);
        else
            EditorIfaceModel_Select(&panels->model, panels->tree_row_position[i]);
        return 1;
    }
    for( int i = 0; i < panels->field_count; i++ )
    {
        if( panels->field_rows[i] != widget )
            continue;
        if( !panels->fields[i].too_long )
            commit_field(panels, ui, editor, panels->fields[i].key, ToriRSChrome_Text(ui, widget), 0);
        return 1;
    }
    if( widget == panels->bt_open || widget == panels->in_open )
    {
        EditorIfacePanels_OpenInterface(panels, ui, editor, ToriRSChrome_Text(ui, panels->in_open));
        return 1;
    }
    if( widget == panels->bt_mount )
    {
        struct EditorIfaceRequest request;
        int mount = ToriRSChrome_DropdownSelected(ui, panels->dd_mount);

        memset(&request, 0, sizeof(request));
        request.interface_id = panels->model.interface_id;
        if( request.interface_id < 0 )
        {
            status(panels, ui, "Open an interface first");
            return 1;
        }
        if( mount <= 0 )
            request.kind = EDITOR_IFACE_REQUEST_MOUNT_ROOT;
        else
        {
            int parent = 0;
            int child = 0;
            if( sscanf(ToriRSChrome_Text(ui, panels->in_slot), "%d:%d", &parent, &child) != 2 )
            {
                status(panels, ui, "Slot is <interface>:<child>");
                return 1;
            }
            request.kind = EDITOR_IFACE_REQUEST_MOUNT_SUB;
            request.target_uid = (parent << 16) | (child & 0xFFFF);
            request.sub_type = k_mount_sub_type[mount < 4 ? mount : 1];
        }
        push_request(panels, &request);
        return 1;
    }
    if( widget == panels->dd_mount )
        return 1;
    if( widget == panels->cb_pick )
    {
        struct EditorIfaceRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = EDITOR_IFACE_REQUEST_PICK;
        request.value = ToriRSChrome_Checked(ui, panels->cb_pick);
        push_request(panels, &request);
        return 1;
    }
    if( widget == panels->bt_add || widget == panels->bt_delete || widget == panels->bt_up
        || widget == panels->bt_down || widget == panels->bt_reparent )
        return structure_op(panels, ui, editor, widget);
    if( widget == panels->bt_field_set || widget == panels->bt_field_remove )
    {
        commit_field(panels, ui, editor, ToriRSChrome_Text(ui, panels->in_field_key),
                     ToriRSChrome_Text(ui, panels->in_field_value), widget == panels->bt_field_remove);
        return 1;
    }
    if( widget == panels->inspector_tabs )
    {
        /* The inspector's tab strip: the active tab picks the group, and the
         * page is rebuilt with that group's rows. */
        int tab = ToriRSChrome_PanelActiveTab(ui, panels->panel_inspector);
        if( tab >= 0 && tab < EDITOR_IF_GROUP_COUNT && tab != panels->inspector_tab )
        {
            panels->inspector_tab = tab;
            panels->inspector_generation = (uint32_t)-1;
        }
        return 1;
    }
    if( widget == panels->cs2_tabs || widget == panels->in_field_key || widget == panels->in_field_value )
        return 1;
    if( widget == panels->in_hook_search )
        return 1;
    if( widget == panels->vl_scripts )
    {
        hook_pick(panels, ui, ToriRSChrome_VListSelected(ui, panels->vl_scripts));
        return 1;
    }
    if( widget == panels->bt_hook_set )
    {
        hook_set(panels, ui, editor);
        return 1;
    }
    for( int i = 0; i < EDITOR_IFACE_ARG_ROWS_MAX; i++ )
    {
        if( panels->arg_rows[i] == widget )
            return 1;
    }
    if( widget == panels->bt_run || widget == panels->in_run_args )
    {
        run_script(panels, ui, 0);
        return 1;
    }
    if( widget == panels->bt_harness )
    {
        run_script(panels, ui, 1);
        return 1;
    }
    if( widget == panels->cb_run_trace || widget == panels->vl_trace || widget == panels->vl_output
        || widget == panels->vl_watch || widget == panels->in_run_script )
        return 1;
    if( widget == panels->bt_watch_add || widget == panels->in_watch )
    {
        int kind = 0;
        int id = 0;
        if( EditorCs2Watch_Parse(ToriRSChrome_Text(ui, panels->in_watch), &kind, &id) )
            EditorCs2Watch_Add(&panels->watch, kind, id);
        return 1;
    }
    if( widget == panels->bt_watch_set || widget == panels->in_watch_value )
    {
        int row = ToriRSChrome_VListSelected(ui, panels->vl_watch);
        struct EditorIfaceRequest request;
        const char* typed = ToriRSChrome_Text(ui, panels->in_watch_value);

        if( row < 0 || row >= panels->watch.count )
            return 1;
        memset(&request, 0, sizeof(request));
        request.kind = EDITOR_IFACE_REQUEST_VAR_SET;
        request.var_kind = panels->watch.rows[row].kind;
        request.var_id = panels->watch.rows[row].id;
        request.value = (int)strtol(typed, NULL, 0);
        snprintf(request.text, sizeof(request.text), "%s", typed);
        push_request(panels, &request);
        return 1;
    }
    if( widget == panels->bt_watch_remove )
    {
        EditorCs2Watch_Remove(&panels->watch, ToriRSChrome_VListSelected(ui, panels->vl_watch));
        return 1;
    }
    if( widget == panels->bt_code_open || widget == panels->in_code_name )
    {
        code_open(panels, ui, editor);
        return 1;
    }
    if( widget == panels->bt_compile )
    {
        code_compile(panels, ui, editor);
        return 1;
    }
    if( widget == panels->bt_code_save )
    {
        if( editor && panels->code_key[0] )
            Editor_SaveDoc(editor, EDITOR_DOC_TEXT, panels->code_key);
        return 1;
    }
    return widget == panels->code;
}

int
EditorIfacePanels_OnEvent(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    const struct ToriRSChromeEvent* event)
{
    size_t length = 0;
    const char* text;
    struct Editor_Cmd command;
    int planned = 0;

    (void)ui;
    assert(panels);
    assert(ui);
    assert(event);
    if( !panels->built || event->widget != panels->code || panels->code < 0 )
        return 0;
    text = code_text(panels, editor, &length);
    if( !text )
        return 1;
    EditorCs2Code_Sync(&panels->code_model, panels->code_key, text, length);
    if( event->kind == TORIRS_CHROME_EVENT_CODE_REPLACE )
        planned = EditorCs2Code_PlanReplace(&panels->code_model, text, event->line, event->text, &command);
    else if( event->kind == TORIRS_CHROME_EVENT_CODE_SPLIT )
        planned = EditorCs2Code_PlanSplit(&panels->code_model, text, event->line, event->col, &command);
    else if( event->kind == TORIRS_CHROME_EVENT_CODE_JOIN )
        planned = EditorCs2Code_PlanJoin(&panels->code_model, text, event->line, &command);
    if( planned && editor )
        Editor_Apply(editor, &command);
    return 1;
}
