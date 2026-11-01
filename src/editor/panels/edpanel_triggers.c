/*
 * The Triggers panel (module "triggers", View > Triggers, Ctrl+E): the
 * script hooks of one loc / npc / obj, or of one file. It helps with
 * triggers; the scripts themselves are edited in the user's own editor.
 *
 *   Show       Record / selection -- the Record panel's record, else the
 *              selected loc, else the Spawns panel's selected npc / obj, else
 *              the catalog's pick; Name -- a record name typed in Subject;
 *              File -- every trigger declared in one file (File, under
 *              server/scripts).
 *   rows       the catalog's TRIGGER contributions (trigger, subject, file,
 *              line), the subject's edited script read from this window's
 *              document when it is open (a trigger added and not saved shows,
 *              "(unsaved)"); Open in editor on the picked row.
 *   Add        a kind from the record type's list (editor_script.h, the
 *              builders' names), Op text, "Add trigger...": the op's menu
 *              field through the Record panel when the record lacks it, and
 *              the template appended to edited/<s>/scripts/<s>.rs2 -- one
 *              stroke, one History row. Open in editor saves that file first.
 *   Refresh    the user edits in their editor: the catalog is asked again (it
 *              re-stats changed files at most every 2 s) and every shown file
 *              is read from disk; a document of one open here whose text
 *              differs is "edited outside", and Reload from disk splices the
 *              document to the disk's text (undoable).
 *   Check      sscompile over the scripts on disk (synchronous, like the
 *              builders' Check; unsaved documents are not in it), the
 *              Problems list with Open in editor.
 *   Run        the Live panel's RUN (Ctrl+Return).
 *
 * TORIRS_EDITOR_TRIGGERS=<subject>[,file][,check][,record][,add=<kind>[:<op
 * text>]] (photographs): once the layout ran, dock the panel over the world
 * on that record (or, with `file`, on that file); `record` opens the Record
 * panel beside it, `add=` runs Add trigger, `check` runs Check.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* realpath */
#endif
#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/builder/builder_text.h"
#include "editor/editor_cfg.h"
#include "editor/editor_host.h"
#include "editor/editor_script.h"
#include "editor/live/live_panel.h"
#include "editor/editor_spawn.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRIGGERS_ROWS 7
#define TRIGGERS_DIAG_ROWS 5
#define TRIGGERS_MAX 128
#define TRIGGERS_STALE_MAX 8
#define TRIGGERS_PATH_MAX 1024

static const char* const k_modes[] = { "Record / selection", "Name", "File" };

struct TriggersPanel
{
    int panel;
    int dropdown_mode;
    int input_subject;
    int input_file;
    int button_show;
    int label_title;
    int label_status;
    int list_rows;
    int button_open;
    int dropdown_kind;
    int input_op;
    int button_add;
    int button_refresh;
    int button_reload;
    int button_check;
    int button_run;
    int list_diags;
    int button_open_diag;

    enum Editor_TriggersMode mode;
    /** What the rows are of. */
    char type[8];
    char subject[EDITOR_SCRIPT_SUBJECT_MAX];
    char file[EDITOR_SCRIPT_PATH_MAX];
    struct Editor_ScriptRef rows[TRIGGERS_MAX];
    int row_count;
    const char* kinds[EDITOR_SCRIPT_KINDS_MAX];
    int kind_count;
    char kinds_type[8];
    /** Ask the rows again on the next tick. */
    int dirty;
    /** Shown on purpose (View, Ctrl+E, a Show call): the editor opening does
     *  not bring it up otherwise. */
    int wanted;
    /** The edited script's document revision the rows were made at. */
    uint32_t edited_revision;
    int edited_open;

    /** Follow: the id a name was last looked up for (kind << 24 | id). */
    int32_t follow_key;
    char follow_name[EDITOR_SCRIPT_SUBJECT_MAX];

    /** "Edited outside": documents open here that differ from the disk. */
    char stale[TRIGGERS_STALE_MAX][EDITOR_SCRIPT_PATH_MAX];
    int stale_count;

    char content_root[TRIGGERS_PATH_MAX];
    struct Builder_Ssc ssc;
    int ssc_ready;
    struct Builder_SscResult* check;
    int check_valid;

    int run_shown;
    uint32_t hint_serial;

    /* TORIRS_EDITOR_TRIGGERS */
    int env_state;
    int env_frames;
    int env_file;
    int env_check;
    int env_record;
    char env_subject[EDITOR_SCRIPT_PATH_MAX];
    char env_add[64];
};

static struct TriggersPanel*
triggers_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "triggers");
}

static void
triggers_status(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* text)
{
    ToriRSChrome_SetText(ui->chrome, self->label_status, text);
}

static int
triggers_has_catalog(struct EditorUi* ui)
{
    return ui->editor && ui->editor->host_open && (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG);
}

/** A TEXT document of `rel` open in this mirror, or NULL. */
static struct Editor_TextDoc*
triggers_doc(
    struct EditorUi* ui,
    const char* rel)
{
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];

    if( !ui->editor )
        return NULL;
    snprintf(key, sizeof(key), "rs2:%s", rel);
    if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) && handle.text && handle.text->loaded )
        return handle.text;
    return NULL;
}

/** server/scripts/`rel` read from disk (owned; NULL when there is no file
 *  or no content root). */
static char*
triggers_read_disk(
    struct TriggersPanel* self,
    const char* rel,
    size_t* out_length)
{
    char path[TRIGGERS_PATH_MAX + EDITOR_SCRIPT_PATH_MAX + 32];
    FILE* file;
    long size;
    char* text;

    *out_length = 0;
    if( !self->content_root[0] )
        return NULL;
    snprintf(path, sizeof(path), "%s/server/scripts/%s", self->content_root, rel);
    file = fopen(path, "rb");
    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(file);
        return NULL;
    }
    text = ToriMem_Alloc(ToriMem_Editor(), (size_t)size + 1);
    assert(text);
    *out_length = fread(text, 1, (size_t)size, file);
    text[*out_length] = '\0';
    fclose(file);
    return text;
}

/** The document of `rel` holds other bytes than the disk (and is not merely
 *  this window's unsaved edit of the same file, which the disk lacks too:
 *  that is "unsaved", not "outside"). */
static int
triggers_doc_stale(
    struct TriggersPanel* self,
    const struct Editor_TextDoc* doc,
    const char* rel)
{
    size_t length;
    char* disk = triggers_read_disk(self, rel, &length);
    int stale;

    if( !disk )
        return 0;
    stale = !doc->dirty && (length != doc->length || memcmp(disk, doc->text ? doc->text : "", length) != 0);
    ToriMem_Free(ToriMem_Editor(), disk);
    return stale;
}

/* ---- the rows --------------------------------------------------------------------- */

static void
ref_from_catalog_row(
    const struct EditorHost_CatalogRow* row,
    struct Editor_ScriptRef* out,
    int* out_ok)
{
    const char* kind = NULL;
    const char* subject = NULL;
    const char* file = NULL;
    const char* line = NULL;

    *out_ok = 0;
    for( int f = 0; f < row->field_count; f++ )
    {
        const char* k = row->fields[f].key;
        if( strcmp(k, "trigger") == 0 )
            kind = row->fields[f].value;
        else if( strcmp(k, "subject") == 0 )
            subject = row->fields[f].value;
        else if( strcmp(k, "file") == 0 )
            file = row->fields[f].value;
        else if( strcmp(k, "line") == 0 )
            line = row->fields[f].value;
    }
    memset(out, 0, sizeof(*out));
    if( !kind || !subject || !file || !Editor_ScriptRelFromContentPath(file, out->rel, sizeof(out->rel)) )
        return;
    snprintf(out->kind, sizeof(out->kind), "%s", kind);
    snprintf(out->subject, sizeof(out->subject), "%s", subject);
    out->line = line ? atoi(line) : 0;
    *out_ok = 1;
}

int
Editor_Triggers_ForSubject(
    struct EditorUi* ui,
    const char* subject,
    struct Editor_ScriptRef* out,
    int max)
{
    char edited[EDITOR_SCRIPT_PATH_MAX];
    const struct Editor_TextDoc* edited_doc;
    int count = 0;

    assert(ui);
    assert(subject);
    assert(out || max == 0);
    if( !ui->editor || !Editor_ScriptEditedPath(subject, edited, sizeof(edited)) )
        return 0;
    edited_doc = triggers_doc(ui, edited);

    if( triggers_has_catalog(ui) )
    {
        struct EditorHost_CatalogPage page;
        char filter[EDITOR_SCRIPT_SUBJECT_MAX + 16];
        memset(&page, 0, sizeof(page));
        snprintf(filter, sizeof(filter), "subject=%s", subject);
        if( Editor_HostMapEdCatalogQuery(&ui->editor->host, TORIRSMAPED_CATALOG_TRIGGER, filter, 0, 0, 200, &page)
            == EDITOR_HOST_OK )
            for( int r = 0; r < page.row_count && count < max; r++ )
            {
                int ok;
                ref_from_catalog_row(&page.rows[r], &out[count], &ok);
                /* `subject=x` also matches x_<more>: the record's own only;
                 * the open edited script is read from its document below. */
                if( !ok || strcmp(out[count].subject, subject) != 0
                    || (edited_doc && strcmp(out[count].rel, edited) == 0) )
                    continue;
                count++;
            }
        Editor_HostCatalogPageFree(&page);
    }
    if( edited_doc )
    {
        struct Editor_ScriptTrigger triggers[64];
        int const n = Editor_ScriptParseTriggers(edited_doc->text ? edited_doc->text : "", edited_doc->length,
            triggers, 64);
        for( int i = 0; i < n && i < 64 && count < max; i++ )
        {
            struct Editor_ScriptRef* ref = &out[count];
            if( strcmp(triggers[i].subject, subject) != 0 )
                continue;
            memset(ref, 0, sizeof(*ref));
            snprintf(ref->kind, sizeof(ref->kind), "%s", triggers[i].kind);
            snprintf(ref->subject, sizeof(ref->subject), "%s", subject);
            snprintf(ref->rel, sizeof(ref->rel), "%s", edited);
            ref->line = triggers[i].line;
            ref->unsaved = edited_doc->dirty;
            count++;
        }
    }
    return count;
}

/** File mode: the file's headers -- its document when open here, else the
 *  disk, else the catalog's rows for it. */
static int
triggers_for_file(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* rel,
    struct Editor_ScriptRef* out,
    int max)
{
    const struct Editor_TextDoc* doc = triggers_doc(ui, rel);
    struct Editor_ScriptTrigger triggers[TRIGGERS_MAX];
    size_t length = 0;
    char* disk = NULL;
    const char* text = NULL;
    int n;
    int count = 0;

    if( doc )
    {
        text = doc->text ? doc->text : "";
        length = doc->length;
    }
    else if( (disk = triggers_read_disk(self, rel, &length)) != NULL )
        text = disk;
    else if( triggers_has_catalog(ui) )
    {
        struct EditorHost_CatalogPage page;
        char filter[EDITOR_SCRIPT_PATH_MAX + 32];
        memset(&page, 0, sizeof(page));
        snprintf(filter, sizeof(filter), "file=server/scripts/%s", rel);
        if( Editor_HostMapEdCatalogQuery(&ui->editor->host, TORIRSMAPED_CATALOG_TRIGGER, filter, 0, 0, 200, &page)
            == EDITOR_HOST_OK )
            for( int r = 0; r < page.row_count && count < max; r++ )
            {
                int ok;
                ref_from_catalog_row(&page.rows[r], &out[count], &ok);
                if( ok && strcmp(out[count].rel, rel) == 0 )
                    count++;
            }
        Editor_HostCatalogPageFree(&page);
        return count;
    }
    if( !text )
        return 0;
    n = Editor_ScriptParseTriggers(text, length, triggers, TRIGGERS_MAX);
    for( int i = 0; i < n && i < TRIGGERS_MAX && count < max; i++ )
    {
        struct Editor_ScriptRef* ref = &out[count++];
        memset(ref, 0, sizeof(*ref));
        snprintf(ref->kind, sizeof(ref->kind), "%s", triggers[i].kind);
        snprintf(ref->subject, sizeof(ref->subject), "%s", triggers[i].subject);
        snprintf(ref->rel, sizeof(ref->rel), "%s", rel);
        ref->line = triggers[i].line;
        ref->unsaved = doc ? doc->dirty : 0;
    }
    ToriMem_Free(ToriMem_Editor(), disk);
    return count;
}

/** The record type of `name` the catalog knows ("" when none). */
static void
triggers_type_of(
    struct EditorUi* ui,
    const char* name,
    char* out,
    size_t out_size)
{
    static const uint32_t k_kinds[3] = { TORIRSMAPED_CATALOG_LOC, TORIRSMAPED_CATALOG_NPC, TORIRSMAPED_CATALOG_OBJ };

    out[0] = '\0';
    if( !triggers_has_catalog(ui) )
        return;
    for( int i = 0; i < 3; i++ )
    {
        struct EditorHost_CatalogRecord record;
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(&ui->editor->host, k_kinds[i], -1, name, &record) == EDITOR_HOST_OK )
        {
            snprintf(out, out_size, "%s", ToriRSMapEdCatalog_KindName(k_kinds[i]));
            Editor_HostCatalogRecordFree(&record);
            return;
        }
        Editor_HostCatalogRecordFree(&record);
    }
}

/** Which documents of the shown files differ from the disk. */
static void
triggers_check_stale(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char edited[EDITOR_SCRIPT_PATH_MAX];

    self->stale_count = 0;
    for( int r = -1; r < self->row_count && self->stale_count < TRIGGERS_STALE_MAX; r++ )
    {
        const char* rel;
        const struct Editor_TextDoc* doc;
        int seen = 0;
        if( r < 0 )
        {
            if( !self->subject[0] || !Editor_ScriptEditedPath(self->subject, edited, sizeof(edited)) )
                continue;
            rel = edited;
        }
        else
            rel = self->rows[r].rel;
        for( int i = 0; i < self->stale_count; i++ )
            seen |= strcmp(self->stale[i], rel) == 0;
        if( seen || !(doc = triggers_doc(ui, rel)) || !triggers_doc_stale(self, doc, rel) )
            continue;
        snprintf(self->stale[self->stale_count++], sizeof(self->stale[0]), "%s", rel);
    }
    ToriRSChrome_SetHidden(ui->chrome, self->button_reload, self->stale_count == 0);
}

static void
triggers_rebuild(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char title[TORIRS_CHROME_LABEL_MAX];
    char edited[EDITOR_SCRIPT_PATH_MAX];
    const struct Editor_TextDoc* doc = NULL;

    self->dirty = 0;
    if( self->mode == EDITOR_TRIGGERS_FILE )
        self->row_count = self->file[0] ? triggers_for_file(ui, self, self->file, self->rows, TRIGGERS_MAX) : 0;
    else
        self->row_count = self->subject[0] ? Editor_Triggers_ForSubject(ui, self->subject, self->rows, TRIGGERS_MAX) : 0;
    if( self->subject[0] && Editor_ScriptEditedPath(self->subject, edited, sizeof(edited)) )
        doc = triggers_doc(ui, edited);
    self->edited_open = doc != NULL;
    self->edited_revision = doc ? doc->revision : 0;
    ToriRSChrome_VListSetCount(ui->chrome, self->list_rows, self->row_count);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_rows, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list_rows, r, "");
    }
    if( self->mode == EDITOR_TRIGGERS_FILE )
        snprintf(title, sizeof(title), "%s: %d trigger%s", self->file[0] ? self->file : "(no file)", self->row_count,
            self->row_count == 1 ? "" : "s");
    else if( self->subject[0] )
        snprintf(title, sizeof(title), "%s%s%s: %d trigger%s", self->type, self->type[0] ? " " : "", self->subject,
            self->row_count, self->row_count == 1 ? "" : "s");
    else
        snprintf(title, sizeof(title), "no subject: open a record, select a loc or type a name");
    ToriRSChrome_SetText(ui->chrome, self->label_title, title);

    /* The kinds Add offers follow the record type (the choice kept while
     * the type stays). */
    if( strcmp(self->kinds_type, self->type) != 0 )
    {
        self->kind_count =
            self->type[0] ? Editor_ScriptTriggerKinds(self->type, self->kinds, EDITOR_SCRIPT_KINDS_MAX) : 0;
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_kind, self->kinds, self->kind_count,
            self->kind_count > 0 ? 0 : -1);
        snprintf(self->kinds_type, sizeof(self->kinds_type), "%s", self->type);
    }
    ToriRSChrome_SetDisabled(ui->chrome, self->button_add, self->kind_count == 0 || self->mode == EDITOR_TRIGGERS_FILE);
    triggers_check_stale(ui, self);
}

static void
triggers_set_subject(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* type,
    const char* name)
{
    char found[8];

    if( !type || !type[0] )
    {
        triggers_type_of(ui, name, found, sizeof(found));
        type = found;
    }
    snprintf(self->type, sizeof(self->type), "%s", type);
    snprintf(self->subject, sizeof(self->subject), "%s", name);
    self->file[0] = '\0';
    self->dirty = 1;
}

/** Follow: the Record panel's record, else the selection, else the pick. */
static int
triggers_follow_target(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char** out_type,
    const char** out_name)
{
    const struct Editor_CfgRecord* record = Editor_UiPanelSelf(ui, "record") ? Editor_Record_Current(ui) : NULL;
    uint32_t kind = 0;
    int32_t id = -1;
    struct Editor_SpawnRow spawn;

    if( record )
    {
        *out_type = record->type;
        *out_name = record->name;
        return 1;
    }
    if( ui->tools.loc_count > 0 )
    {
        kind = TORIRSMAPED_CATALOG_LOC;
        id = ui->tools.locs[0].loc_id;
    }
    else if( Editor_UiPanelSelf(ui, "spawns") && Editor_Spawns_Selection(ui, &spawn, 1) > 0 )
    {
        *out_type = spawn.is_obj ? "obj" : "npc";
        if( spawn.name[0] != '#' )
        {
            snprintf(self->follow_name, sizeof(self->follow_name), "%s", spawn.name);
            *out_name = self->follow_name;
            return 1;
        }
        kind = spawn.is_obj ? TORIRSMAPED_CATALOG_OBJ : TORIRSMAPED_CATALOG_NPC;
        id = atoi(spawn.name + 1);
    }
    else if( ui->tools.pick_kind >= 0 && ui->tools.pick_kind <= 2 && ui->tools.pick_id >= 0 )
    {
        kind = (uint32_t)ui->tools.pick_kind;
        id = ui->tools.pick_id;
    }
    if( id < 0 )
        return 0;
    *out_type = ToriRSMapEdCatalog_KindName(kind);
    /* One catalog lookup per id, not per frame. */
    if( self->follow_key != (int32_t)((kind << 24) | (uint32_t)id) )
    {
        struct EditorHost_CatalogRecord rec;
        self->follow_key = (int32_t)((kind << 24) | (uint32_t)id);
        self->follow_name[0] = '\0';
        memset(&rec, 0, sizeof(rec));
        if( triggers_has_catalog(ui)
            && Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, id, "", &rec) == EDITOR_HOST_OK && rec.name )
            snprintf(self->follow_name, sizeof(self->follow_name), "%s", rec.name);
        Editor_HostCatalogRecordFree(&rec);
    }
    if( !self->follow_name[0] )
        return 0;
    *out_name = self->follow_name;
    return 1;
}

/* ---- open in editor --------------------------------------------------------------- */

int
Editor_Triggers_OpenInEditor(
    struct EditorUi* ui,
    const char* rel_path,
    int line)
{
    struct TriggersPanel* self;
    struct Editor_TextDoc* doc;
    char path[TRIGGERS_PATH_MAX + EDITOR_SCRIPT_PATH_MAX + 32];
    char command[2048];
    char text[TORIRS_CHROME_LABEL_MAX * 3];
    enum EditorHost_Status status;

    assert(ui);
    assert(rel_path);
    self = triggers_self(ui);
    assert(self);
    if( !Editor_ScriptPathValid(rel_path) )
    {
        Editor_UiLog(ui, "editor: Open in editor: %s is not a script path", rel_path);
        return 0;
    }
    /* The editor reads the disk: what this session holds goes there first. */
    doc = triggers_doc(ui, rel_path);
    if( doc && doc->dirty )
    {
        char key[EDITOR_DOC_KEY_MAX];
        snprintf(key, sizeof(key), "rs2:%s", rel_path);
        if( Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, key) > 0 )
            Editor_UiLog(ui, "editor: saved %s first, so the editor opens what the session holds", rel_path);
    }
    if( !self->content_root[0] )
    {
        snprintf(text, sizeof(text), "copy: server/scripts/%s:%d (no content root here)", rel_path, line);
        triggers_status(ui, self, text);
        Editor_UiLog(ui, "editor: %s", text);
        return 0;
    }
    snprintf(path, sizeof(path), "%s/server/scripts/%s", self->content_root, rel_path);
    status = Editor_HostLocalOpenInEditor(path, line, command, sizeof(command));
    if( status != EDITOR_HOST_OK )
    {
        snprintf(text, sizeof(text), "copy: %s:%d%s", path, line,
            status == EDITOR_HOST_UNSUPPORTED ? " (no editor launch on this platform)" : " (the editor did not start)");
        triggers_status(ui, self, text);
        Editor_UiLog(ui, "editor: Open in editor: %s", text);
        return 0;
    }
    Editor_UiLog(ui, "editor: open in editor: %s", command);
    snprintf(text, sizeof(text), "opened %s:%d in your editor", rel_path, line);
    triggers_status(ui, self, text);
    return 1;
}

/* ---- refresh, reload, check ------------------------------------------------------- */

int
Editor_Triggers_Refresh(struct EditorUi* ui)
{
    struct TriggersPanel* self;
    char text[TORIRS_CHROME_LABEL_MAX * 2];

    assert(ui);
    self = triggers_self(ui);
    assert(self);
    triggers_rebuild(ui, self);
    if( self->stale_count > 0 )
        snprintf(text, sizeof(text), "edited outside: %s%s (Reload from disk)", self->stale[0],
            self->stale_count > 1 ? " and more" : "");
    else
        snprintf(text, sizeof(text), "re-read from disk; the catalog re-stats changed files (at most every 2 s)");
    triggers_status(ui, self, text);
    return self->stale_count;
}

int
Editor_Triggers_ReloadFromDisk(struct EditorUi* ui)
{
    struct TriggersPanel* self;
    int reloaded = 0;

    assert(ui);
    self = triggers_self(ui);
    assert(self);
    for( int i = 0; i < self->stale_count; i++ )
    {
        struct Editor_TextDoc* doc = triggers_doc(ui, self->stale[i]);
        char key[EDITOR_DOC_KEY_MAX];
        char label[160];
        size_t length;
        char* disk;
        int n;
        struct Editor_Cmd* commands;
        int applied = 0;

        if( !doc || ui->tools.stroke_open )
            continue;
        disk = triggers_read_disk(self, self->stale[i], &length);
        if( !disk )
            continue;
        snprintf(key, sizeof(key), "rs2:%s", self->stale[i]);
        n = Builder_TextSplices(EDITOR_DOC_TEXT, key, doc->text ? doc->text : "", doc->length, disk, length, NULL, 0);
        commands = n > 0 ? ToriMem_Calloc(ToriMem_Editor(), (size_t)n, sizeof(*commands)) : NULL;
        if( n > 0 )
        {
            assert(commands);
            (void)Builder_TextSplices(EDITOR_DOC_TEXT, key, doc->text ? doc->text : "", doc->length, disk, length,
                commands, n);
            snprintf(label, sizeof(label), "Reload %s from disk", self->stale[i]);
            Editor_StrokeBegin(ui->editor);
            Editor_HistoryBreak(&ui->editor->history, label);
            for( int c = 0; c < n; c++ )
                applied += Editor_Apply(ui->editor, &commands[c]) ? 1 : 0;
            Editor_StrokeEnd(ui->editor);
            reloaded += applied == n;
            Editor_UiLog(ui, "editor: %s (%d splice%s)", label, n, n == 1 ? "" : "s");
        }
        ToriMem_Free(ToriMem_Editor(), commands);
        ToriMem_Free(ToriMem_Editor(), disk);
    }
    (void)Editor_Triggers_Refresh(ui);
    return reloaded;
}

int
Editor_Triggers_SetPaths(
    struct EditorUi* ui,
    const char* content_root,
    const char* sscompile,
    const char* work_dir)
{
    struct TriggersPanel* self;

    assert(ui);
    assert(content_root);
    self = triggers_self(ui);
    if( !self )
        return 0;
    /* Absolute: the editor the path is handed to has its own cwd. */
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if( !realpath(content_root, self->content_root) )
#endif
        snprintf(self->content_root, sizeof(self->content_root), "%s", content_root);
    /* No compiler named: keep what TORIRS_BUILDER_* set up, if anything. */
    if( sscompile && work_dir )
        self->ssc_ready = Builder_SscInit(&self->ssc, sscompile, content_root, work_dir);
    return self->ssc_ready;
}

void
Editor_Triggers_ShowResult(
    struct EditorUi* ui,
    const struct Builder_SscResult* result)
{
    struct TriggersPanel* self;
    char line[TORIRS_CHROME_LABEL_MAX * 2];
    int errors = 0;

    assert(ui);
    assert(result);
    self = triggers_self(ui);
    assert(self);
    if( self->check != result )
        memcpy(self->check, result, sizeof(*self->check));
    self->check_valid = 1;
    /* Errors before warnings (stable): the list's top is what failed. */
    {
        struct Builder_SscDiag sorted[BUILDER_SSC_DIAGS_MAX];
        int n = 0;
        for( int pass = 0; pass < 2; pass++ )
            for( int i = 0; i < self->check->diag_count; i++ )
                if( self->check->diags[i].warning == pass )
                    sorted[n++] = self->check->diags[i];
        memcpy(self->check->diags, sorted, sizeof(sorted[0]) * (size_t)n);
    }
    for( int i = 0; i < self->check->diag_count; i++ )
        errors += !self->check->diags[i].warning;
    ToriRSChrome_VListSetCount(ui->chrome, self->list_diags, self->check->diag_count);
    ToriRSChrome_VListSetScroll(ui->chrome, self->list_diags, 0);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_diags, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list_diags, r, "");
    }
    if( self->check->status == BUILDER_SSC_OK )
        snprintf(line, sizeof(line), "Check: the scripts on disk compile (synchronous, %.1f s)", self->check->seconds);
    else if( self->check->status == BUILDER_SSC_FAILED )
        snprintf(line, sizeof(line), "Check: %d error(s) (synchronous, %.1f s)", errors, self->check->seconds);
    else
        snprintf(line, sizeof(line), "Check: sscompile could not run (%s)",
            self->check->status == BUILDER_SSC_UNSUPPORTED ? "not on this platform" : "no binary or stage");
    triggers_status(ui, self, line);
    Editor_UiLog(ui, "editor: %s%s%s", line, self->check->log_path[0] ? "; log " : "", self->check->log_path);
}

int
Editor_Triggers_Check(struct EditorUi* ui)
{
    struct TriggersPanel* self;
    struct Builder_SscResult* result;
    int unsaved = 0;
    int errors = 0;

    assert(ui);
    self = triggers_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: this window has no Triggers panel");
        return -1;
    }
    if( !self->ssc_ready )
    {
        triggers_status(ui, self, "Check needs sscompile and a work dir (manifest repo_root, or TORIRS_BUILDER_*)");
        Editor_UiLog(ui, "editor: Check: no compile paths for the Triggers panel");
        return -1;
    }
    if( ui->editor )
        for( int i = 0; i < ui->editor->doc.text_count; i++ )
            unsaved += ui->editor->doc.texts[i]->dirty && strncmp(ui->editor->doc.texts[i]->key, "rs2:", 4) == 0;
    if( unsaved )
        Editor_UiLog(ui, "editor: Check reads the disk: %d script document(s) with unsaved edits are not in it",
            unsaved);
    (void)Builder_SscStage(&self->ssc);
    result = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*result));
    assert(result);
    (void)Builder_SscRun(&self->ssc, BUILDER_SSC_COMPILE, result);
    Editor_Triggers_ShowResult(ui, result);
    for( int i = 0; i < result->diag_count; i++ )
        errors += !result->diags[i].warning;
    self->wanted = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    if( result->status != BUILDER_SSC_OK && result->status != BUILDER_SSC_FAILED )
        errors = -1;
    ToriMem_Free(ToriMem_Editor(), result);
    return errors;
}

/* ---- add a trigger ------------------------------------------------------------------ */

int
Editor_Triggers_AddTrigger(
    struct EditorUi* ui,
    const char* kind,
    const char* op_label)
{
    struct TriggersPanel* self;
    const struct Editor_CfgRecord* record = NULL;
    struct Editor_ScriptAddPlan plan;
    struct Editor_ScriptRef refs[64];
    enum Editor_ScriptPlanStatus status;
    struct Editor_Cmd commands[4];
    struct Editor_TextDoc* doc;
    char field[16];
    char label[160];
    const char* op_value = NULL;
    int ref_count;
    int count = 0;
    int applied = 0;

    assert(ui);
    assert(kind);
    self = triggers_self(ui);
    assert(self);
    if( !ui->editor || !self->subject[0] || self->mode == EDITOR_TRIGGERS_FILE )
    {
        Editor_UiLog(ui, "editor: Add trigger: show a record first (Record panel, a selection, or Name)");
        return 0;
    }
    if( ui->tools.stroke_open )
    {
        Editor_UiLog(ui, "editor: Add trigger: finish the stroke in progress first");
        return 0;
    }
    if( !Editor_ScriptKindValid(self->type, kind) )
    {
        Editor_UiLog(ui, "editor: Add trigger %s to %s: %s", kind, self->subject,
            Editor_ScriptPlanStatusText(EDITOR_SCRIPT_PLAN_BAD_KIND));
        return 0;
    }
    /* sscompile refuses a trigger declared twice. */
    ref_count = Editor_Triggers_ForSubject(ui, self->subject, refs, 64);
    for( int i = 0; i < ref_count; i++ )
        if( strcmp(refs[i].kind, kind) == 0 )
        {
            char text[TORIRS_CHROME_LABEL_MAX * 2];
            snprintf(text, sizeof(text), "[%s,%s] is already declared in %s:%d (Open in editor)", kind, self->subject,
                refs[i].rel, refs[i].line);
            triggers_status(ui, self, text);
            Editor_UiLog(ui, "editor: %s", text);
            return 0;
        }

    /* The menu entry: through the Record panel, which must hold the record. */
    if( Editor_ScriptKindOpField(kind, field, sizeof(field)) )
    {
        uint32_t const record_kind = strcmp(self->type, "npc") == 0   ? TORIRSMAPED_CATALOG_NPC
                                     : strcmp(self->type, "obj") == 0 ? TORIRSMAPED_CATALOG_OBJ
                                                                      : TORIRSMAPED_CATALOG_LOC;
        int row;
        if( !Editor_UiPanelSelf(ui, "record") )
        {
            Editor_UiLog(ui, "editor: Add trigger: this window has no Record panel to write %s with", field);
            return 0;
        }
        record = Editor_Record_Current(ui);
        if( !record || strcmp(record->name, self->subject) != 0 || strcmp(record->type, self->type) != 0 )
        {
            if( !Editor_Record_Open(ui, record_kind, self->subject) )
                return 0;
            record = Editor_Record_Current(ui);
        }
        if( !record )
            return 0;
        row = Editor_CfgRecordFindRow(record, field, "", 0);
        if( row >= 0 && record->rows[row].layer >= 0 )
            op_value = record->rows[row].value;
    }

    /* The subject's edited script, opened empty when it has no file. A copy
     * edited outside since it was opened is refused: Reload from disk first. */
    if( !Editor_ScriptEditedPath(self->subject, plan.rel_path, sizeof(plan.rel_path)) )
        return 0;
    snprintf(plan.doc_key, sizeof(plan.doc_key), "rs2:%s", plan.rel_path);
    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, plan.doc_key) || !(doc = triggers_doc(ui, plan.rel_path)) )
    {
        Editor_UiLog(ui, "editor: Add trigger: could not open %s", plan.doc_key);
        return 0;
    }
    if( triggers_doc_stale(self, doc, plan.rel_path) )
    {
        triggers_status(ui, self, "the edited script changed on disk: Refresh, then Reload from disk, first");
        Editor_UiLog(ui, "editor: Add trigger: %s was edited outside; Reload from disk first", plan.rel_path);
        (void)Editor_Triggers_Refresh(ui);
        return 0;
    }
    status = Editor_ScriptPlanAddTrigger(self->type, self->subject, kind, op_value, op_label,
        doc->text ? doc->text : "", doc->length, &plan);
    if( status != EDITOR_SCRIPT_PLAN_OK )
    {
        char text[TORIRS_CHROME_LABEL_MAX * 2];
        snprintf(text, sizeof(text), "Add %s: %s", kind, Editor_ScriptPlanStatusText(status));
        triggers_status(ui, self, text);
        Editor_UiLog(ui, "editor: Add trigger %s to %s: %s", kind, self->subject, Editor_ScriptPlanStatusText(status));
        return 0;
    }
    if( plan.field[0] )
    {
        int const n = Editor_CfgRecordFieldCommands(record, plan.field, "", 0, plan.field_value, -1, commands, 3);
        if( n < 0 )
        {
            Editor_UiLog(ui, "editor: Add trigger: %s=%s cannot be written", plan.field, plan.field_value);
            return 0;
        }
        count = n;
    }
    Editor_CmdInit(&commands[count], EDITOR_DOC_TEXT, plan.doc_key, EDITOR_CMD_TEXT_SPLICE);
    commands[count].has_before = 1;
    commands[count].has_after = 1;
    commands[count].u.text_splice.offset = (uint32_t)plan.offset;
    commands[count].u.text_splice.before_len = 0;
    commands[count].u.text_splice.after_len = (uint32_t)plan.insert_length;
    memcpy(commands[count].u.text_splice.text, plan.insert, plan.insert_length);
    count++;

    if( plan.field[0] )
        snprintf(label, sizeof(label), "Add [%s,%s] (%s=%s)", kind, self->subject, plan.field, plan.field_value);
    else
        snprintf(label, sizeof(label), "Add [%s,%s]", kind, self->subject);
    Editor_StrokeBegin(ui->editor);
    Editor_HistoryBreak(&ui->editor->history, label);
    for( int i = 0; i < count; i++ )
        applied += Editor_Apply(ui->editor, &commands[i]) ? 1 : 0;
    Editor_StrokeEnd(ui->editor);
    if( applied < count )
    {
        Editor_UiLog(ui, "editor: Add trigger: the authority refused %d of %d edits", count - applied, count);
        return 0;
    }
    {
        char text[TORIRS_CHROME_LABEL_MAX * 2];
        snprintf(text, sizeof(text), "added at %s:%d (unsaved: Save writes it; Open in editor saves it first)",
            plan.rel_path, plan.header_line);
        triggers_status(ui, self, text);
        Editor_UiLog(ui, "editor: %s: %s line %d (unsaved; Save writes it, RUN builds it)", label, plan.rel_path,
            plan.header_line);
    }
    self->dirty = 1;
    /* Select the new row once the rows are made again. */
    triggers_rebuild(ui, self);
    for( int r = 0; r < self->row_count; r++ )
        if( strcmp(self->rows[r].kind, kind) == 0 )
            ToriRSChrome_VListSetSelected(ui->chrome, self->list_rows, r);
    return 1;
}

/* ---- show ------------------------------------------------------------------------- */

static void
triggers_show_panel(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    self->wanted = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_mode, (int)self->mode);
}

int
Editor_Triggers_ShowSubject(
    struct EditorUi* ui,
    const char* type,
    const char* name)
{
    struct TriggersPanel* self;

    assert(ui);
    assert(name);
    self = triggers_self(ui);
    assert(self);
    self->mode = EDITOR_TRIGGERS_NAME;
    ToriRSChrome_SetText(ui->chrome, self->input_subject, name);
    triggers_set_subject(ui, self, type, name);
    triggers_rebuild(ui, self);
    triggers_show_panel(ui, self);
    return self->row_count;
}

int
Editor_Triggers_ShowFile(
    struct EditorUi* ui,
    const char* rel_path)
{
    struct TriggersPanel* self;

    assert(ui);
    assert(rel_path);
    self = triggers_self(ui);
    assert(self);
    if( !Editor_ScriptPathValid(rel_path) )
    {
        Editor_UiLog(ui, "editor: %s is not a script path under server/scripts", rel_path);
        return 0;
    }
    self->mode = EDITOR_TRIGGERS_FILE;
    ToriRSChrome_SetText(ui->chrome, self->input_file, rel_path);
    snprintf(self->file, sizeof(self->file), "%s", rel_path);
    self->subject[0] = '\0';
    self->type[0] = '\0';
    triggers_rebuild(ui, self);
    triggers_show_panel(ui, self);
    return self->row_count;
}

void
Editor_Triggers_Follow(struct EditorUi* ui)
{
    struct TriggersPanel* self;

    assert(ui);
    self = triggers_self(ui);
    assert(self);
    self->mode = EDITOR_TRIGGERS_FOLLOW;
    self->subject[0] = '\0';
    self->dirty = 1;
    triggers_show_panel(ui, self);
}

int
Editor_Triggers_Rows(
    struct EditorUi* ui,
    struct Editor_ScriptRef* out,
    int max)
{
    struct TriggersPanel* self;

    assert(ui);
    assert(out || max == 0);
    self = triggers_self(ui);
    if( !self )
        return 0;
    for( int i = 0; i < self->row_count && i < max; i++ )
        out[i] = self->rows[i];
    return self->row_count;
}

const char*
Editor_Triggers_Subject(
    struct EditorUi* ui,
    const char** out_type)
{
    struct TriggersPanel* self;

    assert(ui);
    self = triggers_self(ui);
    if( out_type )
        *out_type = self ? self->type : "";
    return self ? self->subject : "";
}

/* ---- the panel ---------------------------------------------------------------------- */

static void
triggers_hints(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char tip[TORIRS_CHROME_LABEL_MAX * 2];

    edp_hotkey_tip(ui, EDITOR_HK_FILE_CHECK_SCRIPTS, "sscompile over the scripts on disk (synchronous)", tip,
        (int)sizeof(tip));
    ToriRSChrome_SetTooltip(ui->chrome, self->button_check, tip);
    edp_hotkey_tip(ui, EDITOR_HK_FILE_RUN, "The Live panel's RUN: save all, build, reload", tip, (int)sizeof(tip));
    ToriRSChrome_SetTooltip(ui->chrome, self->button_run, tip);
    self->hint_serial = ui->hotkeys.serial;
}

static void
triggers_fill(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    int first;
    int shown;
    char row[TORIRS_CHROME_INPUT_MAX];

    if( ToriRSChrome_VListWindow(ui->chrome, self->list_rows, &first, &shown) )
        for( int r = first; r < first + shown && r < self->row_count; r++ )
        {
            const struct Editor_ScriptRef* ref = &self->rows[r];
            if( self->mode == EDITOR_TRIGGERS_FILE )
                snprintf(row, sizeof(row), "[%s,%s]  line %d%s", ref->kind, ref->subject, ref->line,
                    ref->unsaved ? "  (unsaved)" : "");
            else
            {
                /* The file name and line first: a panel-wide path would
                 * push them off the row. The directory after. */
                const char* slash = strrchr(ref->rel, '/');
                const char* base = slash ? slash + 1 : ref->rel;
                snprintf(row, sizeof(row), "%-8s %s:%d%s   %.*s", ref->kind, base, ref->line,
                    ref->unsaved ? " (unsaved)" : "", slash ? (int)(slash - ref->rel) : 0, ref->rel);
            }
            ToriRSChrome_VListSetRow(ui->chrome, self->list_rows, r, row);
        }
    if( self->check_valid && ToriRSChrome_VListWindow(ui->chrome, self->list_diags, &first, &shown) )
        for( int r = first; r < first + shown && r < self->check->diag_count; r++ )
        {
            /* "hans.rs2:19: message   areas/lumbridge/scripts": the file name,
             * line and message first, as the trigger rows. */
            const struct Builder_SscDiag* diag = &self->check->diags[r];
            const char* slash = strrchr(diag->file, '/');
            snprintf(row, sizeof(row), "%s:%d: %s%s   %.*s", slash ? slash + 1 : diag->file, diag->line,
                diag->warning ? "warning: " : "", diag->message, slash ? (int)(slash - diag->file) : 0, diag->file);
            ToriRSChrome_VListSetRow(ui->chrome, self->list_diags, r, row);
        }
}

static void
triggers_place(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    int x,
    int y,
    int w,
    int h)
{
    ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->panel, w);
    ToriRSChrome_PanelSetFixedHeight(ui->chrome, self->panel, h);
    ToriRSChrome_PanelMove(ui->chrome, self->panel, x, y);
    ui->chrome->panels[self->panel].user_placed = 1;
}

static void
triggers_env(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    if( self->env_state == 0 )
    {
        const char* value = getenv("TORIRS_EDITOR_TRIGGERS");
        char opts[256];
        char* next;

        self->env_state = 3;
        if( !value || !value[0] )
            return;
        snprintf(opts, sizeof(opts), "%s", value);
        for( char* tok = opts; tok; tok = next )
        {
            next = strchr(tok, ',');
            if( next )
                *next++ = '\0';
            if( tok == opts )
                snprintf(self->env_subject, sizeof(self->env_subject), "%s", tok);
            else if( strcmp(tok, "file") == 0 )
                self->env_file = 1;
            else if( strcmp(tok, "check") == 0 )
                self->env_check = 1;
            else if( strcmp(tok, "record") == 0 )
                self->env_record = 1;
            else if( strncmp(tok, "add=", 4) == 0 )
                snprintf(self->env_add, sizeof(self->env_add), "%s", tok + 4);
        }
        self->env_state = 1;
        return;
    }
    if( self->env_state != 1 && self->env_state != 2 )
        return;
    /* A few frames after the layout and the host, so the column has its
     * final boxes and the catalog answers. */
    if( !ui->editor || !ui->editor->host_open || !ui->laid_out || ++self->env_frames < 5 )
        return;
    if( self->env_state == 1 )
    {
        int const left = ui->world.x;
        int const top = ui->dock_top;
        int const h = ui->dock_bottom - ui->dock_top;
        int const room = ui->dock_x - left;
        int const w = room / 2 < edp_px(ui, 460) ? room / 2 : edp_px(ui, 460);
        if( room > 0 && h > 0 )
            triggers_place(ui, self, left + room - w, top, w, h);
        if( self->env_file )
            (void)Editor_Triggers_ShowFile(ui, self->env_subject);
        else
            (void)Editor_Triggers_ShowSubject(ui, NULL, self->env_subject);
        if( self->env_record && !self->env_file && self->type[0] && Editor_UiPanelSelf(ui, "record") )
        {
            uint32_t const kind = strcmp(self->type, "npc") == 0   ? TORIRSMAPED_CATALOG_NPC
                                  : strcmp(self->type, "obj") == 0 ? TORIRSMAPED_CATALOG_OBJ
                                                                   : TORIRSMAPED_CATALOG_LOC;
            if( Editor_Record_Open(ui, kind, self->subject) && room > 0 && h > 0 )
                Editor_Record_Place(ui, left + room - 2 * w, top, w, h);
        }
        Editor_UiLog(ui, "editor: TORIRS_EDITOR_TRIGGERS=%s", getenv("TORIRS_EDITOR_TRIGGERS"));
        self->env_state = 2;
        self->env_frames = 0;
        return;
    }
    /* Add and Check on a later frame, once the rows have been drawn. */
    if( self->env_frames < 3 )
        return;
    if( self->env_add[0] )
    {
        char* colon = strchr(self->env_add, ':');
        if( colon )
            *colon++ = '\0';
        if( Editor_Triggers_AddTrigger(ui, self->env_add, colon ? colon : "") && Editor_UiPanelSelf(ui, "record") )
        {
            char field[16];
            /* The menu entry it wrote, in view in the Record panel. */
            if( Editor_ScriptKindOpField(self->env_add, field, sizeof(field)) )
                (void)Editor_Record_SelectField(ui, field, "");
        }
    }
    if( self->env_check )
    {
        (void)Editor_Triggers_Check(ui);
        /* The Problems list is at the panel's foot. */
        ToriRSChrome_PanelSetScroll(ui->chrome, self->panel, 1 << 20);
    }
    self->env_state = 3;
}

static void
triggers_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct TriggersPanel* self = opaque;
    int busy;

    triggers_env(ui, self);
    if( !ui->chrome->panels[self->panel].visible )
        return;
    self->wanted = 1;
    if( self->hint_serial != ui->hotkeys.serial )
        triggers_hints(ui, self);
    busy = Editor_Live_Busy(ui);
    if( busy != self->run_shown )
    {
        ToriRSChrome_SetDisabled(ui->chrome, self->button_run, busy != 0);
        self->run_shown = busy;
    }
    if( self->mode == EDITOR_TRIGGERS_FOLLOW )
    {
        const char* type = NULL;
        const char* name = NULL;
        if( triggers_follow_target(ui, self, &type, &name)
            && (strcmp(name, self->subject) != 0 || strcmp(type, self->type) != 0) )
            triggers_set_subject(ui, self, type, name);
    }
    /* The subject's edited script changed here (an Add, an undo): again. */
    if( self->subject[0] && !self->dirty )
    {
        char edited[EDITOR_SCRIPT_PATH_MAX];
        const struct Editor_TextDoc* doc =
            Editor_ScriptEditedPath(self->subject, edited, sizeof(edited)) ? triggers_doc(ui, edited) : NULL;
        if( (doc != NULL) != self->edited_open || (doc && doc->revision != self->edited_revision) )
            self->dirty = 1;
    }
    if( self->dirty )
        triggers_rebuild(ui, self);
    triggers_fill(ui, self);
}

static int
triggers_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct TriggersPanel* self = opaque;

    if( widget == self->dropdown_mode )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        self->mode = choice == 1 ? EDITOR_TRIGGERS_NAME : choice == 2 ? EDITOR_TRIGGERS_FILE : EDITOR_TRIGGERS_FOLLOW;
        if( self->mode == EDITOR_TRIGGERS_FOLLOW )
            self->subject[0] = '\0';
        self->dirty = 1;
        return 1;
    }
    if( widget == self->button_show || widget == self->input_subject || widget == self->input_file )
    {
        const char* file = ToriRSChrome_Text(ui->chrome, self->input_file);
        const char* name = ToriRSChrome_Text(ui->chrome, self->input_subject);
        if( (widget == self->input_file || self->mode == EDITOR_TRIGGERS_FILE) && file[0] )
            (void)Editor_Triggers_ShowFile(ui, file);
        else if( name[0] )
            (void)Editor_Triggers_ShowSubject(ui, NULL, name);
        return 1;
    }
    if( widget == self->button_open || widget == self->button_open_diag )
    {
        int const list = widget == self->button_open ? self->list_rows : self->list_diags;
        int const row = ToriRSChrome_VListSelected(ui->chrome, list);
        if( widget == self->button_open && row >= 0 && row < self->row_count )
            (void)Editor_Triggers_OpenInEditor(ui, self->rows[row].rel, self->rows[row].line);
        else if( widget == self->button_open_diag && self->check_valid && row >= 0 && row < self->check->diag_count )
            (void)Editor_Triggers_OpenInEditor(ui, self->check->diags[row].file, self->check->diags[row].line);
        else
            triggers_status(ui, self, "pick a row first");
        return 1;
    }
    if( widget == self->button_add )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_kind);
        if( choice < 0 || choice >= self->kind_count )
            triggers_status(ui, self, "choose the trigger kind first");
        else
            (void)Editor_Triggers_AddTrigger(ui, self->kinds[choice], ToriRSChrome_Text(ui->chrome, self->input_op));
        return 1;
    }
    if( widget == self->button_refresh )
    {
        (void)Editor_Triggers_Refresh(ui);
        return 1;
    }
    if( widget == self->button_reload )
    {
        (void)Editor_Triggers_ReloadFromDisk(ui);
        return 1;
    }
    if( widget == self->button_check )
    {
        (void)Editor_Triggers_Check(ui);
        return 1;
    }
    if( widget == self->button_run )
    {
        (void)Editor_Live_Run(ui);
        return 1;
    }
    return widget == self->list_rows || widget == self->list_diags || widget == self->dropdown_kind
           || widget == self->input_op;
}

/** The editor opening shows registry panels; this one only when it was
 *  open (or asked for) before. */
static void
triggers_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct TriggersPanel* self = opaque;
    if( visible && !self->wanted )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
}

static void
triggers_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct TriggersPanel* self = opaque;
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), self->check);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Triggers_Register(struct EditorUi* ui)
{
    struct TriggersPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->check = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self->check));
    assert(self->check);
    self->run_shown = -2;
    self->hint_serial = UINT32_MAX;
    self->follow_key = -1;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + 8), edp_px(ui, EDP_TOP_Y), edp_px(ui, 420),
        "Triggers");
    assert(self->panel >= 0);
    self->dropdown_mode = ToriRSChrome_Dropdown(chrome, self->panel, "Show", k_modes, 3, 0);
    self->input_subject = ToriRSChrome_TextInput(chrome, self->panel, "Subject", "");
    ToriRSChrome_SetTooltip(chrome, self->input_subject, "a loc, npc or obj name (Show: Name)");
    self->input_file = ToriRSChrome_TextInput(chrome, self->panel, "File", "");
    ToriRSChrome_SetTooltip(chrome, self->input_file, "under server/scripts (Show: File)");
    self->button_show = ToriRSChrome_Button(chrome, self->panel, "Show");
    self->label_title = ToriRSChrome_Label(chrome, self->panel, "no subject");
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "");
    self->list_rows = ToriRSChrome_VList(chrome, self->panel, "", TRIGGERS_ROWS, 0);
    self->button_open = ToriRSChrome_Button(chrome, self->panel, "Open in editor");
    ToriRSChrome_SetTooltip(chrome, self->button_open,
        "the picked row's file at its line, in your editor");
    ToriRSChrome_Separator(chrome, self->panel);
    self->dropdown_kind = ToriRSChrome_Dropdown(chrome, self->panel, "Trigger", NULL, 0, -1);
    self->input_op = ToriRSChrome_TextInput(chrome, self->panel, "Op text", "");
    ToriRSChrome_SetTooltip(chrome, self->input_op, "menu text when the record has no such op (Search)");
    self->button_add = ToriRSChrome_Button(chrome, self->panel, "Add trigger...");
    ToriRSChrome_SetTooltip(chrome, self->button_add,
        "menu text if missing + trigger in edited/<name>; one undo");
    ToriRSChrome_Separator(chrome, self->panel);
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    ToriRSChrome_SetTooltip(chrome, self->button_refresh, "re-read the files from disk, re-index the triggers");
    self->button_reload = ToriRSChrome_Button(chrome, self->panel, "Reload from disk");
    ToriRSChrome_SetTooltip(chrome, self->button_reload, "documents edited outside take the disk's text");
    ToriRSChrome_SetHidden(chrome, self->button_reload, 1);
    self->button_check = ToriRSChrome_Button(chrome, self->panel, "Check");
    self->button_run = ToriRSChrome_Button(chrome, self->panel, "Run");
    self->list_diags = ToriRSChrome_VList(chrome, self->panel, "Problems", TRIGGERS_DIAG_ROWS, 0);
    self->button_open_diag = ToriRSChrome_Button(chrome, self->panel, "Open problem");
    ToriRSChrome_SetTooltip(chrome, self->button_open_diag, "the picked problem's file at its line, in your editor");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "triggers";
    desc.title = "Triggers";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = triggers_tick;
    desc.activate = triggers_activate;
    desc.set_visible = triggers_set_visible;
    desc.destroy = triggers_destroy;
    Editor_UiAddPanel(ui, &desc);

    /* TORIRS_BUILDER_SSCOMPILE / _CONTENT / _WORK when the host sets no paths
     * (editor_panel.c does from the manifest). */
    {
        const char* sscompile = getenv("TORIRS_BUILDER_SSCOMPILE");
        const char* content = getenv("TORIRS_BUILDER_CONTENT");
        const char* work = getenv("TORIRS_BUILDER_WORK");
        if( sscompile && content && work )
        {
            char trigger_work[BUILDER_SSC_PATH_MAX];
            snprintf(trigger_work, sizeof(trigger_work), "%s/triggers_check", work);
            (void)Editor_Triggers_SetPaths(ui, content, sscompile, trigger_work);
        }
    }
}
