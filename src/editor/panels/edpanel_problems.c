/*
 * The Problems panel (module "problems", workbench 6.6; design sections 9
 * and 10): every finding that names a place, as one list -- in the bottom
 * strip's group with Console and History.
 *
 *   rows       (source, file, line, severity, text), and for a record's
 *              finding the record and its field. Two ways in:
 *                the line stream   every build line the Live model hears
 *                                  (FACT_BUILD_LINE: the packs, the client
 *                                  cache, Check, CS2 compile, the quest
 *                                  gate) that reads as a diagnostic --
 *                                  `<file>:<line>: [warning: ]<message>`,
 *                                  `FAIL <name>: <why>`, `error: ...` --
 *                                  is a row whose source is the build's
 *                                  name ("check", "cs2 compile"); a new
 *                                  build of a kind clears that kind's rows;
 *                                  a failed build that said nothing a row
 *                                  reads leaves its DONE line as one.
 *                the push API      Editor_Problems_Report / _ReportRecord /
 *                                  _Clear: the Record panel's unknown keys,
 *                                  the builders' lint, the quest board's and
 *                                  the shop builder's Check.
 *   Find       filters as you type (file, text, source); "errors only".
 *   click      a row with a record opens it in the Record panel at the field
 *              (click-to-record); a row with a script file runs the Triggers
 *              panel's Open in editor at the line (click-to-open).
 *
 * Also here, because every caller of it reports into this list: the BUILD
 * job helper (Editor_BuildJobSend / _Poll), the one way a panel sends a job
 * to the editor server and hears its lines and its end without waiting.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/editor_script.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBLEMS_LIST_ROWS 6

struct ProblemsPanel
{
    int panel;
    int label_summary;
    int input_find;
    int check_errors;
    int list;
    int button_open;
    int button_record;
    int button_clear;

    struct Editor_ProblemRow* rows;
    int row_count;
    /** Rows refused because the list was full, since the last Clear. */
    int dropped;
    /** Bumps on every change to `rows`. */
    uint32_t serial;

    /** The filtered view: indices into `rows`, what the list shows. */
    int* shown;
    int shown_count;
    uint32_t built_serial;
    char built_find[64];
    int built_errors;
    int built;

    /* The line stream: the Live model's log serial read up to, and the build
     * whose lines are arriving (`stream_what`, 0 for none). */
    int live_serial;
    int live_seen;
    uint32_t stream_what;
    int stream_rows;
    int stream_open;
};

static struct ProblemsPanel*
problems_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "problems");
}

static const char*
problems_severity_word(int severity)
{
    return severity == EDITOR_PROBLEM_ERROR ? "error" : severity == EDITOR_PROBLEM_WARNING ? "warning" : "note";
}

/* ---- the model -------------------------------------------------------------------- */

static struct Editor_ProblemRow*
problems_add(
    struct ProblemsPanel* self,
    const char* source,
    const char* file,
    int line,
    int severity,
    const char* text)
{
    struct Editor_ProblemRow* row;

    /* The same finding twice (a re-check that changed nothing) is one row. */
    for( int i = 0; i < self->row_count; i++ )
    {
        row = &self->rows[i];
        if( row->line == line && row->severity == severity && strcmp(row->source, source) == 0
            && strcmp(row->file, file) == 0 && strcmp(row->text, text) == 0 )
            return NULL;
    }
    if( self->row_count >= EDITOR_PROBLEMS_MAX )
    {
        self->dropped++;
        self->serial++;
        return NULL;
    }
    row = &self->rows[self->row_count++];
    memset(row, 0, sizeof(*row));
    snprintf(row->source, sizeof(row->source), "%s", source);
    snprintf(row->file, sizeof(row->file), "%s", file);
    row->line = line;
    row->severity = severity;
    snprintf(row->text, sizeof(row->text), "%s", text);
    self->serial++;
    return row;
}

void
Editor_Problems_Report(
    struct EditorUi* ui,
    const char* source,
    const char* file,
    int line,
    int severity,
    const char* text)
{
    struct ProblemsPanel* self;

    assert(ui);
    assert(source);
    assert(file);
    assert(text);
    assert(severity >= EDITOR_PROBLEM_ERROR);
    assert(severity <= EDITOR_PROBLEM_NOTE);
    self = problems_self(ui);
    if( !self )
        return;
    (void)problems_add(self, source, file, line, severity, text);
}

void
Editor_Problems_ReportRecord(
    struct EditorUi* ui,
    const char* source,
    const char* type,
    const char* name,
    const char* field,
    const char* file,
    int line,
    int severity,
    const char* text)
{
    struct ProblemsPanel* self;
    struct Editor_ProblemRow* row;

    assert(ui);
    assert(source);
    assert(type);
    assert(name);
    assert(field);
    assert(file);
    assert(text);
    assert(severity >= EDITOR_PROBLEM_ERROR);
    assert(severity <= EDITOR_PROBLEM_NOTE);
    self = problems_self(ui);
    if( !self )
        return;
    row = problems_add(self, source, file, line, severity, text);
    if( !row )
        return;
    snprintf(row->record_type, sizeof(row->record_type), "%s", type);
    snprintf(row->record_name, sizeof(row->record_name), "%s", name);
    snprintf(row->record_field, sizeof(row->record_field), "%s", field);
}

int
Editor_Problems_Clear(
    struct EditorUi* ui,
    const char* source)
{
    struct ProblemsPanel* self;
    int kept = 0;
    int removed;

    assert(ui);
    self = problems_self(ui);
    if( !self )
        return 0;
    for( int i = 0; i < self->row_count; i++ )
    {
        if( source && strcmp(self->rows[i].source, source) != 0 )
            self->rows[kept++] = self->rows[i];
    }
    removed = self->row_count - kept;
    self->row_count = kept;
    if( !source )
        self->dropped = 0;
    if( removed )
        self->serial++;
    return removed;
}

int
Editor_Problems_Count(struct EditorUi* ui)
{
    struct ProblemsPanel* self;

    assert(ui);
    self = problems_self(ui);
    return self ? self->row_count : 0;
}

const struct Editor_ProblemRow*
Editor_Problems_Row(
    struct EditorUi* ui,
    int index)
{
    struct ProblemsPanel* self;

    assert(ui);
    self = problems_self(ui);
    assert(self);
    assert(index >= 0);
    assert(index < self->row_count);
    return &self->rows[index];
}

int
Editor_Problems_CountOf(
    struct EditorUi* ui,
    const char* source,
    int severity)
{
    struct ProblemsPanel* self;
    int count = 0;

    assert(ui);
    assert(source);
    self = problems_self(ui);
    if( !self )
        return 0;
    for( int i = 0; i < self->row_count; i++ )
        count += strcmp(self->rows[i].source, source) == 0 && (severity < 0 || self->rows[i].severity == severity);
    return count;
}

/* ---- reading a line --------------------------------------------------------------- */

static int
problems_starts(
    const char* text,
    const char* prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

int
Editor_ProblemsParseLine(
    const char* line,
    struct Editor_ProblemRow* out)
{
    const char* at = line;
    const char* colon;

    assert(line);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->severity = EDITOR_PROBLEM_ERROR;
    while( *at == ' ' || *at == '\t' )
        at++;
    if( problems_starts(at, "sscompile: ") )
        at += 11;

    /* cs2: "FAIL <name or id>: <why>" (rscache/tools/cs2). */
    if( problems_starts(at, "FAIL ") )
    {
        const char* name = at + 5;
        colon = strstr(name, ": ");
        if( !colon || colon == name || (size_t)(colon - name) >= sizeof(out->file) - 12 )
            return 0;
        snprintf(out->file, sizeof(out->file), "scripts/%.*s.cs2", (int)(colon - name), name);
        snprintf(out->text, sizeof(out->text), "%s", colon + 2);
        return 1;
    }
    /* No place: "error: ..." / "warning: ...". */
    if( problems_starts(at, "error: ") || problems_starts(at, "warning: ") )
    {
        out->severity = at[0] == 'w' ? EDITOR_PROBLEM_WARNING : EDITOR_PROBLEM_ERROR;
        snprintf(out->text, sizeof(out->text), "%s", strchr(at, ' ') + 1);
        return 1;
    }
    /* "<file>:<line>: [error: |warning: |note: ]<message>": the file holds no
     * space (make's "make: *** [x] Error 2" is not one). */
    colon = strchr(at, ':');
    while( colon )
    {
        const char* digits = colon + 1;
        const char* after = digits;
        while( *after >= '0' && *after <= '9' )
            after++;
        if( after > digits && after[0] == ':' && after[1] == ' ' && colon > at )
        {
            const char* message = after + 2;
            int spaced = 0;
            for( const char* c = at; c < colon; c++ )
                spaced |= *c == ' ' || *c == '\t';
            if( spaced || (size_t)(colon - at) >= sizeof(out->file) )
                return 0;
            memcpy(out->file, at, (size_t)(colon - at));
            out->file[colon - at] = '\0';
            out->line = atoi(digits);
            if( problems_starts(message, "warning: ") )
            {
                out->severity = EDITOR_PROBLEM_WARNING;
                message += 9;
            }
            else if( problems_starts(message, "note: ") )
            {
                out->severity = EDITOR_PROBLEM_NOTE;
                message += 6;
            }
            else if( problems_starts(message, "error: ") )
                message += 7;
            snprintf(out->text, sizeof(out->text), "%s", message);
            return 1;
        }
        colon = strchr(colon + 1, ':');
    }
    return 0;
}

/* ---- the line stream ---------------------------------------------------------------- */

/** A build of `what` started: its kind's old rows go. */
static void
problems_stream_begin(
    struct EditorUi* ui,
    struct ProblemsPanel* self,
    uint32_t what)
{
    (void)Editor_Problems_Clear(ui, ToriRSMapEd_BuildWhatName(what));
    self->stream_what = what;
    self->stream_rows = 0;
    self->stream_open = 1;
}

static void
problems_stream_line(
    struct EditorUi* ui,
    struct ProblemsPanel* self,
    uint32_t mark,
    const char* line)
{
    uint32_t const what = mark & ~EDITOR_LIVE_LOG_DONE;

    if( !self->stream_open || self->stream_what != what )
        problems_stream_begin(ui, self, what);
    if( mark & EDITOR_LIVE_LOG_DONE )
    {
        /* A failed build none of whose lines read as a finding still says
         * so here: its DONE line is the row. */
        if( !strstr(line, ": ok (") && self->stream_rows == 0 )
            (void)problems_add(self, ToriRSMapEd_BuildWhatName(what), "", 0, EDITOR_PROBLEM_ERROR, line);
        self->stream_open = 0;
        return;
    }
    {
        struct Editor_ProblemRow row;
        if( Editor_ProblemsParseLine(line, &row)
            && problems_add(self, ToriRSMapEd_BuildWhatName(what), row.file, row.line, row.severity, row.text) )
            self->stream_rows++;
    }
}

/** The Live model's rows since the last frame, oldest first. */
static void
problems_stream(
    struct EditorUi* ui,
    struct ProblemsPanel* self)
{
    const struct Editor_LiveModel* live = Editor_LivePanelModel(ui);
    int fresh;

    if( !live )
        return;
    if( !self->live_seen )
    {
        /* What the log held before this panel looked is the Live panel's;
         * the list starts with the next build. */
        self->live_seen = 1;
        self->live_serial = live->log_serial;
        return;
    }
    fresh = live->log_serial - self->live_serial;
    self->live_serial = live->log_serial;
    if( fresh <= 0 )
        return;
    if( fresh > live->log_count )
        fresh = live->log_count;
    for( int i = fresh - 1; i >= 0; i-- )
    {
        uint32_t const mark = Editor_LiveLogWhat(live, i);
        const char* line = Editor_LiveLogLine(live, i);
        if( mark && line )
            problems_stream_line(ui, self, mark, line);
    }
}

/* ---- the panel -------------------------------------------------------------------- */

/** `needle` in `hay`, ignoring ASCII case. */
static int
problems_contains_ci(
    const char* hay,
    const char* needle)
{
    size_t const length = strlen(needle);

    for( const char* h = hay; *h; h++ )
    {
        size_t i = 0;
        while( i < length && h[i] )
        {
            char a = h[i];
            char b = needle[i];
            if( a >= 'A' && a <= 'Z' )
                a = (char)(a - 'A' + 'a');
            if( b >= 'A' && b <= 'Z' )
                b = (char)(b - 'A' + 'a');
            if( a != b )
                break;
            i++;
        }
        if( i == length )
            return 1;
    }
    return 0;
}

static int
problems_matches(
    const struct Editor_ProblemRow* row,
    const char* find,
    int errors_only)
{
    if( errors_only && row->severity != EDITOR_PROBLEM_ERROR )
        return 0;
    if( !find[0] )
        return 1;
    return problems_contains_ci(row->text, find) || problems_contains_ci(row->file, find) || problems_contains_ci(row->source, find)
           || problems_contains_ci(row->record_name, find);
}

static void
problems_rebuild(
    struct EditorUi* ui,
    struct ProblemsPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
    int const errors_only = ToriRSChrome_Checked(ui->chrome, self->check_errors);
    int errors = 0;
    int warnings = 0;
    char line[TORIRS_CHROME_LABEL_MAX];

    find = find ? find : "";
    self->shown_count = 0;
    /* Errors first, then warnings, then notes; arrival order within each. */
    for( int pass = EDITOR_PROBLEM_ERROR; pass <= EDITOR_PROBLEM_NOTE; pass++ )
        for( int i = 0; i < self->row_count; i++ )
            if( self->rows[i].severity == pass && problems_matches(&self->rows[i], find, errors_only) )
                self->shown[self->shown_count++] = i;
    for( int i = 0; i < self->row_count; i++ )
    {
        errors += self->rows[i].severity == EDITOR_PROBLEM_ERROR;
        warnings += self->rows[i].severity == EDITOR_PROBLEM_WARNING;
    }
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->shown_count);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
    }
    if( self->row_count == 0 )
        snprintf(line, sizeof(line), "No problems: builds, Check, CS2 compile and the builders report here");
    else if( self->shown_count == self->row_count )
        snprintf(line, sizeof(line), "%d error%s, %d warning%s%s", errors, errors == 1 ? "" : "s", warnings,
            warnings == 1 ? "" : "s", self->dropped ? " (the list is full: some were not kept)" : "");
    else
        snprintf(line, sizeof(line), "%d of %d shown: %d error%s, %d warning%s", self->shown_count, self->row_count,
            errors, errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
    ToriRSChrome_SetText(ui->chrome, self->label_summary, line);
    ToriRSChrome_PanelSetTitle(ui->chrome, self->panel, errors ? "Problems (!)" : "Problems");
    self->built_serial = self->serial;
    snprintf(self->built_find, sizeof(self->built_find), "%s", find);
    self->built_errors = errors_only;
    self->built = 1;
}

static void
problems_row_cells(
    const struct Editor_ProblemRow* row,
    char* out,
    size_t out_size)
{
    char place[300];

    if( row->record_name[0] )
        snprintf(place, sizeof(place), "%s:%s %s", row->record_type, row->record_name, row->record_field);
    else if( row->file[0] && row->line > 0 )
    {
        const char* slash = strrchr(row->file, '/');
        snprintf(place, sizeof(place), "%s:%d", slash ? slash + 1 : row->file, row->line);
    }
    else
        snprintf(place, sizeof(place), "%s", row->file);
    /* sprite, name (the finding), dim (where), keys, right (the source),
     * mark ("!" for an error). */
    snprintf(out, out_size, "%s" TORIRS_CHROME_CELL_SEP_STR "%s%s" TORIRS_CHROME_CELL_SEP_STR "%s"
                            TORIRS_CHROME_CELL_SEP_STR TORIRS_CHROME_CELL_SEP_STR "%s" TORIRS_CHROME_CELL_SEP_STR "%s",
        TORIRS_CHROME_CELL_NO_SPRITE, row->severity == EDITOR_PROBLEM_WARNING ? "warning: "
                                      : row->severity == EDITOR_PROBLEM_NOTE  ? "note: "
                                                                              : "",
        row->text, place, row->source, row->severity == EDITOR_PROBLEM_ERROR ? "!" : "");
}

static void
problems_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct ProblemsPanel* self = opaque;
    const char* find;
    int first;
    int shown;

    /* Hidden or not: a build's lines are only in the Live log's ring. */
    problems_stream(ui, self);
    if( !ui->chrome->panels[self->panel].visible )
        return;
    find = ToriRSChrome_Text(ui->chrome, self->input_find);
    if( !self->built || self->built_serial != self->serial || strcmp(self->built_find, find ? find : "") != 0
        || self->built_errors != ToriRSChrome_Checked(ui->chrome, self->check_errors) )
        problems_rebuild(ui, self);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->shown_count; r++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        problems_row_cells(&self->rows[self->shown[r]], text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
}

/** The record type a row names -> its catalog kind; -1 for another. */
static int
problems_record_kind(const char* type)
{
    if( strcmp(type, "loc") == 0 )
        return TORIRSMAPED_CATALOG_LOC;
    if( strcmp(type, "npc") == 0 )
        return TORIRSMAPED_CATALOG_NPC;
    if( strcmp(type, "obj") == 0 )
        return TORIRSMAPED_CATALOG_OBJ;
    return -1;
}

int
Editor_Problems_Activate(
    struct EditorUi* ui,
    int index)
{
    struct ProblemsPanel* self;
    const struct Editor_ProblemRow* row;

    assert(ui);
    self = problems_self(ui);
    assert(self);
    assert(index >= 0);
    assert(index < self->row_count);
    row = &self->rows[index];
    if( row->record_name[0] )
    {
        int const kind = problems_record_kind(row->record_type);
        char key[EDITOR_CMD_NAME_MAX];
        const char* sub = "";

        if( kind < 0 || !Editor_UiPanelSelf(ui, "record") )
        {
            Editor_UiStatus(ui, "Open record refused: this window has no Record panel for %s:%s", row->record_type,
                row->record_name);
            return 0;
        }
        if( !Editor_Record_Open(ui, (uint32_t)kind, row->record_name) )
        {
            Editor_UiStatus(ui, "Open record refused: %s:%s did not open (see the console)", row->record_type,
                row->record_name);
            return 0;
        }
        snprintf(key, sizeof(key), "%s", row->record_field);
        if( strncmp(key, "param:", 6) == 0 )
        {
            sub = row->record_field + 6;
            snprintf(key, sizeof(key), "param");
        }
        else if( strchr(key, '#') )
            *strchr(key, '#') = '\0';
        if( row->record_field[0] && Editor_Record_SelectField(ui, key, sub) < 0 )
            Editor_UiStatus(ui, "%s:%s is open; it no longer has %s", row->record_type, row->record_name,
                row->record_field);
        else
            Editor_UiStatus(ui, "%s:%s %s: %s", row->record_type, row->record_name, row->record_field, row->text);
        return 1;
    }
    if( !row->file[0] )
    {
        Editor_UiStatus(ui, "Open refused: this %s names no file (%s)", problems_severity_word(row->severity),
            row->text);
        return 0;
    }
    if( !Editor_ScriptPathValid(row->file) )
    {
        Editor_UiStatus(ui, "Open refused: %s is not a script under server/scripts (copy: %s:%d)", row->file, row->file,
            row->line);
        return 0;
    }
    if( !Editor_UiPanelSelf(ui, "triggers") )
    {
        Editor_UiStatus(ui, "Open refused: this window has no Triggers panel to open %s:%d with", row->file,
            row->line);
        return 0;
    }
    return Editor_Triggers_OpenInEditor(ui, row->file, row->line);
}

static int
problems_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct ProblemsPanel* self = opaque;
    int const selected = ToriRSChrome_VListSelected(ui->chrome, self->list);
    int const index = selected >= 0 && selected < self->shown_count ? self->shown[selected] : -1;

    if( widget == self->list )
    {
        /* A click acts (design section 10, "with a click-to-node"): the
         * record, else the file. */
        if( index >= 0 )
            (void)Editor_Problems_Activate(ui, index);
        return 1;
    }
    if( widget == self->button_open || widget == self->button_record )
    {
        if( index < 0 )
        {
            Editor_UiStatus(ui, "%s refused: pick a row first", widget == self->button_open ? "Open" : "Open record");
            return 1;
        }
        if( widget == self->button_record && !self->rows[index].record_name[0] )
        {
            Editor_UiStatus(ui, "Open record refused: the row is about %s, not a record",
                self->rows[index].file[0] ? self->rows[index].file : "no file");
            return 1;
        }
        (void)Editor_Problems_Activate(ui, index);
        return 1;
    }
    if( widget == self->button_clear )
    {
        int const removed = Editor_Problems_Clear(ui, NULL);
        Editor_UiStatus(ui, "Problems: cleared %d row%s (the next build or Check fills it again)", removed,
            removed == 1 ? "" : "s");
        return 1;
    }
    return widget == self->input_find || widget == self->check_errors;
}

static void
problems_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct ProblemsPanel* self = opaque;
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), self->rows);
    ToriMem_Free(ToriMem_Editor(), self->shown);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Problems_Register(struct EditorUi* ui)
{
    struct ProblemsPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    int const surface_h = ui->chrome->surface_h > 0 ? ui->chrome->surface_h : edp_px(ui, 480);

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->rows = ToriMem_Calloc(ToriMem_Editor(), EDITOR_PROBLEMS_MAX, sizeof(*self->rows));
    assert(self->rows);
    self->shown = ToriMem_Calloc(ToriMem_Editor(), EDITOR_PROBLEMS_MAX, sizeof(*self->shown));
    assert(self->shown);
    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X),
        surface_h - edp_px(ui, 200), edp_px(ui, 420), "Problems");
    assert(self->panel >= 0);
    self->label_summary = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "Filter the rows: a file, a word of the message, a source");
    self->check_errors = ToriRSChrome_Checkbox(chrome, self->panel, "errors only", 0);
    self->list = ToriRSChrome_VListCells(chrome, self->panel,
        "-" TORIRS_CHROME_CELL_SEP_STR "problem" TORIRS_CHROME_CELL_SEP_STR "where" TORIRS_CHROME_CELL_SEP_STR
        TORIRS_CHROME_CELL_SEP_STR "source",
        PROBLEMS_LIST_ROWS, 0);
    assert(self->list >= 0);
    ToriRSChrome_SetTooltip(chrome, self->list, "Click: a record opens at its field, a script in your editor");
    self->button_open = ToriRSChrome_Button(chrome, self->panel, "Open in editor");
    ToriRSChrome_SetTooltip(chrome, self->button_open, "The picked row's file in your editor, at its line");
    self->button_record = ToriRSChrome_Button(chrome, self->panel, "Open record");
    ToriRSChrome_SetTooltip(chrome, self->button_record, "The picked row's record in the Record panel, at its field");
    self->button_clear = ToriRSChrome_Button(chrome, self->panel, "Clear");
    ToriRSChrome_SetTooltip(chrome, self->button_clear, "Empty the list (the next build or Check fills it again)");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "problems";
    desc.title = "Problems";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = problems_tick;
    desc.activate = problems_activate;
    desc.destroy = problems_destroy;
    Editor_UiAddPanel(ui, &desc);
}

/* ---- BUILD jobs -------------------------------------------------------------------- */

int
Editor_BuildJobSend(
    struct EditorUi* ui,
    struct Editor_BuildJob* job,
    uint32_t what,
    const char* arg,
    char* why,
    size_t why_size)
{
    const struct Editor_LiveModel* live;
    struct ToriRSMapEdBuf payload;
    uint32_t type;
    int sent;

    assert(ui);
    assert(job);
    assert(why);
    assert(why_size > 0);
    why[0] = '\0';
    if( job->active )
    {
        snprintf(why, why_size, "its %s job is still running", ToriRSMapEd_BuildWhatName(job->what));
        return 0;
    }
    if( !ui->editor || !ui->editor->host_open )
    {
        snprintf(why, why_size, "no editor server");
        return 0;
    }
    /* No capability test here: the bits are the HELLO's, and a runner bound
     * after it (an embedded server's lane) is still a runner. A server that
     * has none, or is older than the kind, answers the BUILD UNSUPPORTED or
     * REFUSED, and the job ends with that status (Editor_BuildJobWhy). */
    live = Editor_LivePanelModel(ui);
    if( !live )
    {
        snprintf(why, why_size, "this window has no Live panel to hear the job through");
        return 0;
    }
    if( live->building || live->run_step != TORIRSMAPED_RUN_STEP_IDLE )
    {
        snprintf(why, why_size, "a build or a RUN is in flight (see Live)");
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    if( ToriRSMapEd_BuildWhatTakesArg(what) )
    {
        assert(arg);
        if( !ToriRSMapEd_BuildArgValid(what, arg) )
        {
            snprintf(why, why_size, "\"%s\" cannot name a %s job", arg, ToriRSMapEd_BuildWhatName(what));
            return 0;
        }
        type = Editor_LiveEncodeBuildArg(&payload, what, arg);
    }
    else
        type = Editor_LiveEncodeBuild(&payload, what);
    sent = Editor_HostMapEdLiveSend(&ui->editor->host, type, payload.data ? payload.data + payload.head : NULL,
        (uint32_t)ToriRSMapEd_BufAvailable(&payload));
    ToriRSMapEd_BufFree(&payload);
    if( !sent )
    {
        snprintf(why, why_size, "the editor server's connection is gone");
        return 0;
    }
    memset(job, 0, sizeof(*job));
    job->what = what;
    job->active = 1;
    job->build_serial = live->build_serial;
    job->log_serial = live->log_serial;
    job->started_ms = ui->chrome->now_ms;
    return 1;
}

const char*
Editor_BuildJobWhy(uint32_t status)
{
    if( status == EDITOR_HOST_OK )
        return "ok";
    if( status == EDITOR_HOST_UNSUPPORTED )
        return "the editor server runs no such job (no lane bound, or an older server)";
    if( status == EDITOR_HOST_REFUSED )
        return "the editor server refused it (a build or a RUN in flight, or a bad argument)";
    if( status == EDITOR_HOST_ABSENT )
        return "the editor server has nothing of that name";
    if( status == EDITOR_HOST_IO_ERROR )
        return "the editor server could not write what the job reads";
    return "it failed (see its lines in Live)";
}

int
Editor_BuildJobPoll(
    struct EditorUi* ui,
    struct Editor_BuildJob* job,
    void (*line)(void* user, const char* text),
    void* user)
{
    const struct Editor_LiveModel* live;
    int fresh;
    int ended = 0;

    assert(ui);
    assert(job);
    if( !job->active )
        return 0;
    live = Editor_LivePanelModel(ui);
    if( !live )
        return 0;
    fresh = live->log_serial - job->log_serial;
    job->log_serial = live->log_serial;
    if( fresh > live->log_count )
        fresh = live->log_count;
    for( int i = fresh - 1; i >= 0 && !ended; i-- )
    {
        uint32_t const mark = Editor_LiveLogWhat(live, i);
        const char* text = Editor_LiveLogLine(live, i);
        if( !text || (mark & ~EDITOR_LIVE_LOG_DONE) != job->what )
            continue;
        if( mark & EDITOR_LIVE_LOG_DONE )
        {
            ended = 1;
            job->status = strstr(text, ": ok (")           ? EDITOR_HOST_OK
                          : live->last_build_what == job->what ? live->last_build_status
                                                               : (uint32_t)EDITOR_HOST_IO_ERROR;
            break;
        }
        job->lines++;
        if( line )
            line(user, text);
    }
    /* The ring lost the DONE row (more than a ring of lines in one frame):
     * two DONEs since the send mean ours is among them. */
    if( !ended && !live->building && live->build_serial - job->build_serial >= 2 )
    {
        ended = 1;
        job->status = live->last_build_status;
    }
    if( !ended )
        return 0;
    job->active = 0;
    job->seconds = (double)(ui->chrome->now_ms - job->started_ms) / 1000.0;
    return 1;
}
