/*
 * History: the session's undo groups, newest first, and a click to go back
 * (or forward) to any of them -- the "rich undo/redo stack" the plain Edit >
 * Undo / Redo pair could not show.
 *
 * The rows are the mirror's DERIVED history (editor_cmd.h Editor_History,
 * kept by editor.c from the FACT_CMD stream and this window's own intents).
 * The authority's stack stays the only stack: Undo, Redo and a jump are
 * UNDO / REDO requests to it, one per step, and the rows follow the facts
 * those requests broadcast. A jump counts COMMANDS, not rows, so a row the
 * mirror grouped differently from the authority (another window's drag) still
 * lands on the authority's nearest step.
 *
 *   newest
 *     undone  Raise 9 tiles m50_50 L0  (9)      redo tail, greyed by "undone"
 *   > Place loc 1530 m50_50  (1)                the current position
 *     Paste 12x8 m50_50 L0  (96)
 *     (session start)                           undo everything seen
 *
 * The list is rebuilt only when the history's serial (or the filter) moves,
 * never per frame: the per-frame tick copies the cached text of the rows on
 * screen and nothing else.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

#define HISTORY_LIST_ROWS 12
/** Every group, plus the session-start row. */
#define HISTORY_ROWS_MAX (EDITOR_HISTORY_GROUPS_MAX + 1)
#define HISTORY_ROW_TEXT 160
/** A row that is no group: undo everything this window has seen. */
#define HISTORY_ROW_START (-1)

struct HistoryPanel
{
    int panel;
    int button_undo;
    int button_redo;
    int input_filter;
    int row_status;
    int list;

    /** Row -> the applied-group count a click on it asks for. */
    int row_target[HISTORY_ROWS_MAX];
    /** Row -> its group index, HISTORY_ROW_START for the start row. */
    int row_group[HISTORY_ROWS_MAX];
    char (*row_text)[HISTORY_ROW_TEXT];
    int row_count;

    /** What the rows were built from: the history serial, the filter text,
     *  whether there was a session at all. */
    uint32_t built_serial;
    char built_filter[64];
    int built;
    /** Rebuilds so far: the tests pin "none on an unchanged frame". */
    int rebuilds;
};

static int
history_matches(
    const struct Editor_HistoryGroup* group,
    const char* filter)
{
    return filter[0] == '\0' || strstr(group->doc_key, filter) != NULL;
}

/** Rebuild the rows from the history: the only walk over the groups, run
 *  when the serial or the filter changed. */
static void
history_rebuild(
    struct EditorUi* ui,
    struct HistoryPanel* self,
    const char* filter)
{
    const struct Editor_History* history = Editor_GetHistory(ui->editor);
    char name[HISTORY_ROW_TEXT];
    char status[TORIRS_CHROME_LABEL_MAX];
    int rows = 0;

    for( int g = history->count - 1; g >= 0 && rows < HISTORY_ROWS_MAX - 1; g-- )
    {
        const struct Editor_HistoryGroup* group = &history->groups[g];
        const char* mark;

        if( !history_matches(group, filter) )
            continue;
        if( g >= history->applied )
            mark = "  undone ";
        else if( g == history->applied - 1 )
            mark = "> ";
        else
            mark = "  ";
        Editor_HistoryGroupName(group, name, sizeof(name));
        snprintf(self->row_text[rows], HISTORY_ROW_TEXT, "%s%s%s  (%d)", mark, group->approximate ? "~" : "",
            name, group->cmd_count);
        self->row_group[rows] = g;
        /* A click on a group leaves it applied and everything newer undone. */
        self->row_target[rows] = g + 1;
        rows++;
    }
    snprintf(self->row_text[rows], HISTORY_ROW_TEXT, "%s(session start%s)", history->applied == 0 ? "> " : "  ",
        history->dropped ? ", older steps not listed" : "");
    self->row_group[rows] = HISTORY_ROW_START;
    self->row_target[rows] = 0;
    rows++;
    self->row_count = rows;

    snprintf(status, sizeof(status), "%d of %d steps applied%s", history->applied, history->count,
        filter[0] ? " (filtered)" : "");
    ToriRSChrome_SetText(ui->chrome, self->row_status, status);
    ToriRSChrome_VListSetCount(ui->chrome, self->list, rows);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_redo, history->applied >= history->count);

    self->rebuilds++;
    self->built_serial = history->serial;
    snprintf(self->built_filter, sizeof(self->built_filter), "%s", filter);
    self->built = 1;
}

static void
history_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct HistoryPanel* self = opaque;
    const char* filter;
    int first;
    int shown;

    if( !ui->editor )
    {
        if( self->built )
        {
            ToriRSChrome_SetText(ui->chrome, self->row_status, "no editor session");
            ToriRSChrome_VListSetCount(ui->chrome, self->list, 0);
            self->row_count = 0;
            self->built = 0;
        }
        return;
    }
    filter = ToriRSChrome_Text(ui->chrome, self->input_filter);
    if( !filter )
        filter = "";
    if( !self->built || self->built_serial != Editor_GetHistory(ui->editor)->serial
        || strcmp(self->built_filter, filter) != 0 )
        history_rebuild(ui, self, filter);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < self->row_count; row++ )
        ToriRSChrome_VListSetRow(ui->chrome, self->list, row, self->row_text[row]);
}

int
Editor_History_Jump(
    struct EditorUi* ui,
    int target_groups)
{
    const struct Editor_History* history;
    int target;
    int steps = 0;

    assert(ui);
    assert(ui->editor);
    history = Editor_GetHistory(ui->editor);
    assert(target_groups >= 0);
    assert(target_groups <= history->count);

    if( ui->tools.stroke_open )
    {
        Editor_UiLog(ui, "editor: finish the stroke before moving through the history");
        return 0;
    }
    /* Commands, not rows: the authority's groups may be cut where this
     * mirror's are not, and a count lands on its nearest step either way. */
    target = Editor_HistoryCommandsThrough(history, target_groups);
    for( int guard = 0; guard < 2 * EDITOR_HISTORY_GROUPS_MAX; guard++ )
    {
        int const now = Editor_HistoryAppliedCommands(history);
        int moved;

        if( now == target )
            break;
        moved = now > target ? Editor_Undo(ui->editor) : Editor_Redo(ui->editor);
        if( moved <= 0 || Editor_HistoryAppliedCommands(history) == now )
            break;
        steps++;
    }
    Editor_UiLog(ui, "editor: history: %d step%s, %d of %d applied", steps, steps == 1 ? "" : "s",
        history->applied, history->count);
    return steps;
}

int
Editor_History_RowTarget(
    struct EditorUi* ui,
    int row)
{
    struct HistoryPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "history");
    assert(self);
    if( row < 0 || row >= self->row_count )
        return -1;
    return self->row_target[row];
}

int
Editor_History_Rebuilds(struct EditorUi* ui)
{
    struct HistoryPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "history");
    assert(self);
    return self->rebuilds;
}

const char*
Editor_History_RowText(
    struct EditorUi* ui,
    int row)
{
    struct HistoryPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "history");
    assert(self);
    if( row < 0 || row >= self->row_count )
        return NULL;
    return self->row_text[row];
}

static int
history_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct HistoryPanel* self = opaque;

    if( widget == self->input_filter )
        return 1;
    if( widget != self->button_undo && widget != self->button_redo && widget != self->list )
        return 0;
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: no editor session");
        return 1;
    }
    if( ui->tools.stroke_open && widget != self->list )
    {
        Editor_UiLog(ui, "editor: finish the stroke before undoing");
        return 1;
    }
    /* The buttons are the plain requests: the authority knows steps this
     * window never saw (made before it connected), the rows do not. */
    if( widget == self->button_undo )
    {
        if( Editor_Undo(ui->editor) <= 0 )
            Editor_UiLog(ui, "editor: nothing to undo");
    }
    else if( widget == self->button_redo )
    {
        if( Editor_Redo(ui->editor) <= 0 )
            Editor_UiLog(ui, "editor: nothing to redo");
    }
    else
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list);
        if( row >= 0 && row < self->row_count )
            (void)Editor_History_Jump(ui, self->row_target[row]);
    }
    return 1;
}

static void
history_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct HistoryPanel* self = opaque;

    (void)ui;
    ToriMem_Free(ToriMem_Editor(), self->row_text);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_History_Register(struct EditorUi* ui)
{
    struct HistoryPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->row_text = ToriMem_Calloc(ToriMem_Editor(), HISTORY_ROWS_MAX, sizeof(*self->row_text));
    assert(self->row_text);

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, EDP_TOP_Y),
        edp_px(ui, EDP_COL_W + 120), "History");
    assert(self->panel >= 0);
    self->button_undo = ToriRSChrome_Button(chrome, self->panel, "Undo");
    ToriRSChrome_SetTooltip(chrome, self->button_undo, "Undo the newest applied step (Ctrl+Z)");
    self->button_redo = ToriRSChrome_Button(chrome, self->panel, "Redo");
    ToriRSChrome_SetTooltip(chrome, self->button_redo, "Redo the step undone last");
    self->input_filter = ToriRSChrome_TextInput(chrome, self->panel, "Filter", "");
    ToriRSChrome_SetTooltip(chrome, self->input_filter, "Only steps on documents whose key contains this (m50_50, loc:)");
    self->row_status = ToriRSChrome_Label(chrome, self->panel, "");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", HISTORY_LIST_ROWS, 0);
    assert(self->list >= 0);
    ToriRSChrome_SetTooltip(chrome, self->list, "Click a step to undo or redo to it");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "history";
    desc.title = "History";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = history_tick;
    desc.activate = history_activate;
    desc.destroy = history_destroy;
    Editor_UiAddPanel(ui, &desc);
}
