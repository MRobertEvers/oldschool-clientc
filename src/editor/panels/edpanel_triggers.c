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
 *   the file   one click from the user's own tools (owner, 2026-11-09:
 *              "there should be a button to open in the explorer"): Show in
 *              Explorer / Finder on the picked row's file (else the folder
 *              the subject's scripts are in), Copy path, Copy name (the
 *              record's name as a script writes it), and New script file --
 *              edited/<s>/scripts/<s>.rs2 with a two-line header, through
 *              the document as Add trigger writes, then opened in the editor.
 *   Snippets   RuneScript to paste (editor_snippets.h): the titles for the
 *              subject's kind, then the ones any record can use; the text
 *              rendered for the subject's name in a read-only preview; Copy
 *              snippet puts it on the OS clipboard and says which file it
 *              goes into.
 *   Add        a kind from the record type's list (editor_script.h, the
 *              builders' names), Op text, "Add trigger...": the op's menu
 *              field through the Record panel when the record lacks it, and
 *              the template appended to edited/<s>/scripts/<s>.rs2 -- one
 *              stroke, one History row. Open in editor saves that file first.
 *   Refresh    the user edits in their editor: the catalog is asked again (it
 *              re-stats changed files at most every 2 s) and every shown file
 *              is read from disk; a document of one open here whose text
 *              differs with nothing unsaved is "edited outside": its rows are
 *              the file's, Add trigger takes the disk's text into it first,
 *              and Reload from disk splices it to the disk's text (undoable).
 *   Check      the editor server's BUILD check job (workbench 6.6,
 *              torirs_maped_build.h TORIRSMAPED_BUILD_CHECK): sscompile over
 *              the scripts on disk with every unsaved script document the
 *              server holds written over them, on the server's pump -- the
 *              window never waits. Its lines fill the Problems list here as
 *              they arrive (and the Problems panel), with Open in editor;
 *              the status says "check: ok, 0.6 s" or the first error.
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
#include "editor/builder/builder_emit.h"
#include "editor/builder/builder_text.h"
#include "editor/editor_alloc.h"
#include "editor/editor_cfg.h"
#include "editor/editor_host.h"
#include "editor/editor_script.h"
#include "editor/editor_snippets.h"
#include "editor/live/live_panel.h"
#include "editor/editor_spawn.h"
#include "mem/torirs_mem.h"
#include "platform/platform_clipboard.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TRIGGERS_ROWS 7
#define TRIGGERS_DIAG_ROWS 5
#define TRIGGERS_MAX 128
#define TRIGGERS_STALE_MAX 8
#define TRIGGERS_PATH_MAX 1024
/** A path under server/scripts as the file buttons build it: a row's file,
 *  or a subject's folder ("edited/<subject>/scripts"). */
#define TRIGGERS_REL_MAX 256
/** A folder shown outside the editor (Show in Explorer, a new quest's
 *  workspace) has its files handed out with it up to this many. A quest's
 *  folder holds a dozen; a top folder of the tree is not copied whole. */
#define TRIGGERS_HANDOUT_FOLDER_MAX 200
/** The snippet preview's rows (the chrome's row pool is shared by every
 *  list of every panel: ten, as the Quest board's code view takes); the
 *  titles one subject is offered (its kind's and the "any" ones); the lines
 *  of the longest snippet. */
#define TRIGGERS_SNIPPET_ROWS 10
#define TRIGGERS_SNIPPETS_MAX 24
#define TRIGGERS_SNIPPET_LINES_MAX 96
/** How deep File mode lists a folder: a quest's folder holds configs/ and
 *  scripts/, and a file or two under each. */
#define TRIGGERS_FOLDER_DEPTH 3

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
    int button_reveal;
    int button_copy_path;
    int button_copy_name;
    int button_new_file;
    int input_quest;
    int button_new_quest;
    int dropdown_snippet;
    int code_snippet;
    int button_copy_snippet;
    int dropdown_kind;
    int input_op;
    int dropdown_write_to;
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
    /** File mode on a FOLDER (a quest's): the rows are its files, each with
     *  the triggers it declares (-1: not a script). */
    int file_is_folder;
    int row_triggers[TRIGGERS_MAX];
    /** The folder row the last activation picked: a second one on the same
     *  row (Enter, a double-click) shows that file's triggers. -1 none. */
    int picked_row;
    /** The quest being worked on ("" none): the one New quest made, or whose
     *  file or folder File mode last showed. The snippets read ITS variable
     *  and stages, and Add trigger can write into its script. */
    char quest[EDITOR_QUEST_NAME_MAX];
    /** That quest's variable as a script names it ("varp9517_thistle_quest"),
     *  read off its .varp file; "" when the file has none (the snippets then
     *  write <quest>_quest). */
    char quest_var[EDITOR_SNIPPET_SUBJECT_MAX];
    /** "Write to": where Add trigger appends -- the record's own script, or
     *  the quest's. The labels' storage, and what they were made for. */
    char write_to_labels[2][TORIRS_CHROME_LABEL_MAX];
    const char* write_to_options[2];
    char write_to_quest[EDITOR_QUEST_NAME_MAX];
    int write_to_listed;
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
    /** The subject's edited script holds text here or is a file on disk (as
     *  of the last rebuild): what "New script file" would otherwise make. */
    int edited_exists;

    /* Snippets: the titles offered (the subject's kind, then "any"), which
     * catalogue entry each is, and the one shown, rendered for the name. */
    const char* snippet_titles[TRIGGERS_SNIPPETS_MAX];
    unsigned char snippet_kinds[TRIGGERS_SNIPPETS_MAX];
    unsigned char snippet_indices[TRIGGERS_SNIPPETS_MAX];
    int snippet_count;
    /** The kind the titles were listed for, once they have been. */
    enum Editor_SnippetKind snippet_kind;
    int snippet_listed;
    /** The option the preview shows, -1 when it shows none. */
    int snippet_shown;
    char snippet_name[EDITOR_SNIPPET_SUBJECT_MAX];
    /** The quest and the quest variable the preview was rendered for. */
    char snippet_quest[EDITOR_QUEST_NAME_MAX];
    char snippet_var[EDITOR_SNIPPET_SUBJECT_MAX];
    char snippet_text[EDITOR_SNIPPET_MAX];
    size_t snippet_length;
    int snippet_line_start[TRIGGERS_SNIPPET_LINES_MAX];
    int snippet_line_count;

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
    /** Check's job in flight, and what its lines said so far. */
    struct Editor_BuildJob check_job;
    struct Builder_SscResult* check_stream;

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

/** One sentence on the panel's status row and in the status bar (which the
 *  Console keeps): what a file button or a copy did, or why it did not. */
static void
triggers_say(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* text)
{
    triggers_status(ui, self, text);
    Editor_UiStatus(ui, "%s", text);
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

/** `path` as this machine spells an absolute path. On Windows the content
 *  root is kept as it was given (Editor_Triggers_SetPaths), which may be
 *  relative and holds forward slashes: Explorer reads neither, and a path
 *  pasted into another program should not depend on this process's
 *  directory. Elsewhere the root is already resolved. */
static void
triggers_native_path(
    const char* path,
    char* out,
    size_t out_size)
{
#if defined(_WIN32)
    if( _fullpath(out, path, out_size) )
        return;
#endif
    snprintf(out, out_size, "%s", path);
}

/**
 * The path of `content_rel` (relative to the content root:
 * "server/scripts/quests/x/scripts/x.rs2", a folder) on this machine, for a
 * program OUTSIDE the editor -- the user's text editor, the file manager,
 * the clipboard -- with the lane hand-out done first.
 *
 * On a lane a file is a link to the main tree's until the editor saves it,
 * and most folders are links too: a text editor saves in place, so its save
 * of a linked file lands in the main tree, and so does a file made by hand
 * in a linked folder. The server makes the path the lane's own before it
 * leaves (LANE_HANDOUT, torirs_maped_lanes.h). The part handed out is the
 * nearest one that is on disk: a file that is there, else the folder the
 * path will be made in. `*out_on_disk` says whether `content_rel` itself is
 * there.
 *
 * `shows_folder`: the program shows the folder too -- a file manager shows
 * the file among its neighbours, a workspace shows the folder's tree -- so
 * the folder (the path when it is one, else the one it is in) is handed out
 * with every file in it (TRIGGERS_HANDOUT_FOLDER_MAX of them at most). A
 * neighbour left a link was one double-click and a save from the main tree.
 *
 * 1 with the path (absolute, this machine's spelling); 0 with `why` when it
 * must not be handed out -- the caller says so and starts nothing.
 */
static int
triggers_outside_path(
    struct EditorUi* ui,
    const struct TriggersPanel* self,
    const char* content_rel,
    int shows_folder,
    char* out,
    size_t out_size,
    int* out_on_disk,
    char* why,
    size_t why_size)
{
    char existing[TRIGGERS_REL_MAX + 32];
    char joined[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 64];
    char handed[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 64];
    struct stat info;
    enum EditorHost_Status status = EDITOR_HOST_UNSUPPORTED;
    size_t kept;

    out[0] = '\0';
    why[0] = '\0';
    *out_on_disk = 0;
    memset(&info, 0, sizeof(info));
    if( !self->content_root[0] )
    {
        snprintf(why, why_size, "this window has no content folder on this machine");
        return 0;
    }
    if( strlen(content_rel) >= sizeof(existing) )
    {
        snprintf(why, why_size, "the path is too long");
        return 0;
    }
    /* The nearest part of the path that is on disk. The content root always
     * is; a path with nothing of it there hands out its first folder. */
    snprintf(existing, sizeof(existing), "%s", content_rel);
    for( ;; )
    {
        char* slash;
        snprintf(joined, sizeof(joined), "%s/%s", self->content_root, existing);
        if( stat(joined, &info) == 0 )
            break;
        slash = strrchr(existing, '/');
        if( !slash )
            break;
        *slash = '\0';
    }
    kept = strlen(existing);
    *out_on_disk = content_rel[kept] == '\0' && stat(joined, &info) == 0;

    if( ui->editor && ui->editor->host_open && Editor_HostIsMapEd(&ui->editor->host) )
        status = Editor_HostMapEdLaneHandOut(&ui->editor->host, existing, 0, handed, sizeof(handed), why, why_size);
    if( status == EDITOR_HOST_OK && shows_folder )
    {
        char folder[TRIGGERS_REL_MAX + 32];
        char folder_path[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 64];
        char note[256];
        char* slash;
        enum EditorHost_Status folder_status;

        snprintf(folder, sizeof(folder), "%s", existing);
        slash = strrchr(folder, '/');
        if( !S_ISDIR(info.st_mode) && slash )
            *slash = 0;
        folder_status = Editor_HostMapEdLaneHandOut(&ui->editor->host, folder, TRIGGERS_HANDOUT_FOLDER_MAX,
            folder_path, sizeof(folder_path), note, sizeof(note));
        if( folder_status != EDITOR_HOST_OK )
        {
            snprintf(why, why_size, "%s", note[0] ? note : "the editor server did not answer");
            return 0;
        }
        if( note[0] )
            Editor_UiLog(ui, "editor: %s: %s; open its other files through the editor", folder, note);
    }
    if( status == EDITOR_HOST_UNSUPPORTED )
    {
        /* No lanes server to ask (a plain local session, an older server).
         * On main the tree's own path is the answer; on a lane it would be
         * a link's, and that is exactly what must not leave. */
        if( ui->lane.known && !ui->lane.is_main )
        {
            snprintf(why, why_size, "this editor server cannot make lane %s's copy of the file its own",
                ui->lane.name);
            return 0;
        }
        snprintf(handed, sizeof(handed), "%s", joined);
    }
    else if( status != EDITOR_HOST_OK )
    {
        if( !why[0] )
            snprintf(why, why_size, "the editor server did not answer");
        return 0;
    }
    snprintf(joined, sizeof(joined), "%s%s", handed, content_rel + kept);
    triggers_native_path(joined, out, out_size);
    return 1;
}

/** " (lane <name>)" on a lane, "" on main: which tree a path that left the
 *  editor is in, for the sentence that says it left. */
static const char*
triggers_lane_note(
    const struct EditorUi* ui,
    char* out,
    size_t out_size)
{
    out[0] = '\0';
    if( ui->lane.known && !ui->lane.is_main )
        snprintf(out, out_size, " (lane %s)", ui->lane.name);
    return out;
}

/** 1 when server/scripts/`rel` is on disk (a file or a folder). */
static int
triggers_rel_on_disk(
    const struct TriggersPanel* self,
    const char* rel)
{
    char path[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 32];
    struct stat info;

    if( !self->content_root[0] )
        return 0;
    snprintf(path, sizeof(path), "%s/server/scripts/%s", self->content_root, rel);
    return stat(path, &info) == 0;
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

/** The triggers of `subject` in script `rel`, which this window holds a copy
 *  of (`doc`), appended to `out` from `count`: the copy's text while it
 *  holds an unsaved edit, else the file's, which may have been saved outside
 *  since (as triggers_for_file). Returns the new count. */
static int
triggers_subject_in_doc(
    struct EditorUi* ui,
    const char* subject,
    const char* rel,
    const struct Editor_TextDoc* doc,
    struct Editor_ScriptRef* out,
    int count,
    int max)
{
    struct Editor_ScriptTrigger triggers[64];
    struct TriggersPanel* self = triggers_self(ui);
    const char* text = doc->text ? doc->text : "";
    size_t length = doc->length;
    char* disk = NULL;
    int n;

    if( !doc->dirty && self && (disk = triggers_read_disk(self, rel, &length)) != NULL )
        text = disk;
    n = Editor_ScriptParseTriggers(text, length, triggers, 64);
    ToriMem_Free(ToriMem_Editor(), disk);
    for( int i = 0; i < n && i < 64 && count < max; i++ )
    {
        struct Editor_ScriptRef* ref = &out[count];
        if( strcmp(triggers[i].subject, subject) != 0 )
            continue;
        memset(ref, 0, sizeof(*ref));
        snprintf(ref->kind, sizeof(ref->kind), "%s", triggers[i].kind);
        snprintf(ref->subject, sizeof(ref->subject), "%s", subject);
        snprintf(ref->rel, sizeof(ref->rel), "%s", rel);
        ref->line = triggers[i].line;
        ref->unsaved = doc->dirty;
        count++;
    }
    return count;
}

int
Editor_Triggers_ForSubject(
    struct EditorUi* ui,
    const char* subject,
    struct Editor_ScriptRef* out,
    int max)
{
    struct TriggersPanel* self;
    /* The scripts read from this window's copies rather than the catalog:
     * the record's own edited script, and the quest's script being worked
     * on, which Add trigger also writes (Write to) -- its new row was not
     * listed until the file was saved and the catalog had read it. */
    char rels[2][EDITOR_SCRIPT_PATH_MAX];
    const struct Editor_TextDoc* docs[2] = { NULL, NULL };
    int count = 0;

    assert(ui);
    assert(subject);
    assert(out || max == 0);
    if( !ui->editor || !Editor_ScriptEditedPath(subject, rels[0], sizeof(rels[0])) )
        return 0;
    docs[0] = triggers_doc(ui, rels[0]);
    self = triggers_self(ui);
    rels[1][0] = '\0';
    if( self && self->quest[0] )
    {
        Editor_QuestFilePath(self->quest, EDITOR_SNIPPET_FILE_SCRIPT, rels[1], sizeof(rels[1]));
        docs[1] = triggers_doc(ui, rels[1]);
    }

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
                 * a script held here is read from its copy below. */
                if( !ok || strcmp(out[count].subject, subject) != 0
                    || (docs[0] && strcmp(out[count].rel, rels[0]) == 0)
                    || (docs[1] && strcmp(out[count].rel, rels[1]) == 0) )
                    continue;
                count++;
            }
        Editor_HostCatalogPageFree(&page);
    }
    for( int d = 0; d < 2; d++ )
    {
        if( docs[d] )
            count = triggers_subject_in_doc(ui, subject, rels[d], docs[d], out, count, max);
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

    /* This window's copy only while it holds an unsaved edit. A copy with
     * none (New quest's, a saved Add trigger's) is the disk as it was: the
     * file is written in the owner's own editor, and Refresh after a save
     * there listed the triggers of the copy, not of the file (2026-11-11). */
    if( doc && doc->dirty )
    {
        text = doc->text ? doc->text : "";
        length = doc->length;
    }
    else if( (disk = triggers_read_disk(self, rel, &length)) != NULL )
        text = disk;
    else if( doc )
    {
        text = doc->text ? doc->text : "";
        length = doc->length;
    }
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

static int
triggers_ref_compare(
    const void* a,
    const void* b)
{
    return strcmp(((const struct Editor_ScriptRef*)a)->rel, ((const struct Editor_ScriptRef*)b)->rel);
}

/** The files of server/scripts/`rel` (a folder), TRIGGERS_FOLDER_DEPTH
 *  levels down, one row each. Run on a Show or a Refresh, never per frame. */
static void
triggers_folder_walk(
    const struct TriggersPanel* self,
    const char* rel,
    int depth,
    struct Editor_ScriptRef* out,
    int max,
    int* count)
{
    char path[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 32];
    DIR* dir;
    struct dirent* entry;

    snprintf(path, sizeof(path), "%s/server/scripts/%s", self->content_root, rel);
    dir = opendir(path);
    if( !dir )
        return;
    while( *count < max && (entry = readdir(dir)) != NULL )
    {
        char sub[TRIGGERS_REL_MAX];
        struct stat info;
        size_t const name_length = strlen(entry->d_name);
        int written;

        /* ".", "..", and the dot-names and temporaries saves leave. */
        if( entry->d_name[0] == '.' || (name_length > 4 && strcmp(entry->d_name + name_length - 4, ".tmp") == 0) )
            continue;
        written = snprintf(sub, sizeof(sub), "%s/%s", rel, entry->d_name);
        if( written < 0 || (size_t)written >= sizeof(out->rel) )
            continue;
        snprintf(path, sizeof(path), "%s/server/scripts/%s", self->content_root, sub);
        if( stat(path, &info) != 0 )
            continue;
        if( S_ISDIR(info.st_mode) )
        {
            if( depth < TRIGGERS_FOLDER_DEPTH )
                triggers_folder_walk(self, sub, depth + 1, out, max, count);
            continue;
        }
        memset(&out[*count], 0, sizeof(out[*count]));
        snprintf(out[*count].rel, sizeof(out[*count].rel), "%s", sub);
        out[*count].line = 1;
        (*count)++;
    }
    closedir(dir);
}

/** File mode on a folder: its files by path, and for each script how many
 *  triggers it declares. */
static int
triggers_for_folder(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* rel,
    struct Editor_ScriptRef* out,
    int max)
{
    int count = 0;

    if( !self->content_root[0] )
        return 0;
    triggers_folder_walk(self, rel, 1, out, max, &count);
    if( count > 1 )
        qsort(out, (size_t)count, sizeof(*out), triggers_ref_compare);
    for( int i = 0; i < count; i++ )
    {
        const struct Editor_TextDoc* doc;
        size_t length = 0;
        char* disk;

        self->row_triggers[i] = -1;
        if( !Editor_ScriptPathValid(out[i].rel) )
            continue;
        doc = triggers_doc(ui, out[i].rel);
        if( doc )
        {
            self->row_triggers[i] = Editor_ScriptParseTriggers(doc->text ? doc->text : "", doc->length, NULL, 0);
            out[i].unsaved = doc->dirty;
            continue;
        }
        disk = triggers_read_disk(self, out[i].rel, &length);
        if( !disk )
            continue;
        self->row_triggers[i] = Editor_ScriptParseTriggers(disk, length, NULL, 0);
        ToriMem_Free(ToriMem_Editor(), disk);
    }
    return count;
}

/** 1 when server/scripts/`rel` is a folder on disk. */
static int
triggers_rel_is_folder(
    const struct TriggersPanel* self,
    const char* rel)
{
    char path[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 32];
    struct stat info;

    if( !self->content_root[0] )
        return 0;
    snprintf(path, sizeof(path), "%s/server/scripts/%s", self->content_root, rel);
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
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
triggers_write_to_follow(
    struct EditorUi* ui,
    struct TriggersPanel* self);

static void
triggers_read_quest_var(
    struct EditorUi* ui,
    struct TriggersPanel* self);

static void
triggers_rebuild(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char title[TORIRS_CHROME_LABEL_MAX];
    char edited[EDITOR_SCRIPT_PATH_MAX];
    const struct Editor_TextDoc* doc = NULL;

    self->dirty = 0;
    self->picked_row = -1;
    if( self->mode == EDITOR_TRIGGERS_FILE && self->file[0] )
    {
        /* Asked again on every rebuild: a folder New quest just made, a
         * file that replaced one. */
        self->file_is_folder = triggers_rel_is_folder(self, self->file);
        self->row_count = self->file_is_folder ? triggers_for_folder(ui, self, self->file, self->rows, TRIGGERS_MAX)
                                               : triggers_for_file(ui, self, self->file, self->rows, TRIGGERS_MAX);
    }
    else if( self->mode == EDITOR_TRIGGERS_FILE )
        self->row_count = 0;
    else
        self->row_count = self->subject[0] ? Editor_Triggers_ForSubject(ui, self->subject, self->rows, TRIGGERS_MAX) : 0;
    self->edited_exists = 0;
    if( self->subject[0] && Editor_ScriptEditedPath(self->subject, edited, sizeof(edited)) )
    {
        doc = triggers_doc(ui, edited);
        /* One stat per rebuild (a subject, a revision, a Refresh), never
         * per frame. */
        self->edited_exists = (doc && doc->length > 0) || triggers_rel_on_disk(self, edited);
    }
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
    if( self->mode == EDITOR_TRIGGERS_FILE && self->file[0] && self->file_is_folder )
        snprintf(title, sizeof(title), "%s/: %d file%s", self->file, self->row_count, self->row_count == 1 ? "" : "s");
    else if( self->mode == EDITOR_TRIGGERS_FILE )
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
    triggers_write_to_follow(ui, self);
    /* A record's own script file, where Add trigger writes: a file shown by
     * path has none to make. */
    ToriRSChrome_SetDisabled(ui->chrome, self->button_new_file,
        !self->subject[0] || self->mode == EDITOR_TRIGGERS_FILE);
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

/**
 * server/scripts/`rel_path` at `line` to the user's editor; with
 * `folder_rel` (under server/scripts) that folder as the editor's workspace
 * and the file in view. The file -- and the folder -- are the lane's own
 * before they leave (triggers_outside_path). Says in the panel and the
 * status bar what was started, with which program, and in which lane; the
 * command line goes to the Console.
 */
static int
triggers_open(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* folder_rel,
    const char* rel_path,
    int line)
{
    struct Editor_TextDoc* doc;
    char content_rel[TRIGGERS_REL_MAX + 32];
    char path[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 64];
    char folder[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 64];
    char command[2048];
    char why[512];
    char lane[EDITOR_UI_LANE_NAME_MAX + 16];
    char text[EDITOR_UI_CONSOLE_LINE_MAX];
    enum EditorHost_Status status;
    int on_disk;

    if( !Editor_ScriptFileValid(rel_path) || (folder_rel && !Editor_ScriptFileValid(folder_rel)) )
    {
        snprintf(text, sizeof(text), "Open in editor refused: %s is not a path under server/scripts", rel_path);
        triggers_say(ui, self, text);
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
    snprintf(content_rel, sizeof(content_rel), "server/scripts/%s", rel_path);
    if( !triggers_outside_path(ui, self, content_rel, 0, path, sizeof(path), &on_disk, why, sizeof(why)) )
    {
        snprintf(text, sizeof(text), "Open in editor refused: %s (%s:%d)", why, rel_path, line);
        triggers_say(ui, self, text);
        return 0;
    }
    if( folder_rel )
    {
        snprintf(content_rel, sizeof(content_rel), "server/scripts/%s", folder_rel);
        if( !triggers_outside_path(ui, self, content_rel, 1, folder, sizeof(folder), &on_disk, why, sizeof(why)) )
        {
            snprintf(text, sizeof(text), "Open in editor refused: %s (%s)", why, folder_rel);
            triggers_say(ui, self, text);
            return 0;
        }
    }
    status = Editor_HostLocalOpenFolderInEditor(folder_rel ? folder : NULL, path, line, command, sizeof(command));
    if( status != EDITOR_HOST_OK )
    {
        snprintf(text, sizeof(text), "Open in editor %s: the path of %s:%d is in the Console (Copy path copies it)",
            status == EDITOR_HOST_UNSUPPORTED ? "cannot start a program here" : "did not start", rel_path, line);
        triggers_say(ui, self, text);
        Editor_UiLog(ui, "editor: the path is %s", path);
        return 0;
    }
    Editor_UiLog(ui, "editor: open in editor: %s", command);
    triggers_lane_note(ui, lane, sizeof(lane));
    if( Editor_HostLocalEditorFound() )
        snprintf(text, sizeof(text), "Opened %s:%d in %s%s", rel_path, line,
            Editor_HostLocalEditorWho(folder_rel != NULL), lane);
    else
        /* The desktop's opener, not an editor: what it starts for a .rs2 is
         * whatever the file type was given to, often nothing. */
        snprintf(text, sizeof(text),
            "Handed %s to %s%s: no editor was found (VS Code's `code` is not on PATH). Set TORIRS_EDITOR_OPEN_CMD, "
            "e.g. notepad {path}",
            rel_path, Editor_HostLocalEditorWho(folder_rel != NULL), lane);
    triggers_say(ui, self, text);
    return 1;
}

int
Editor_Triggers_OpenInEditor(
    struct EditorUi* ui,
    const char* rel_path,
    int line)
{
    struct TriggersPanel* self;

    assert(ui);
    assert(rel_path);
    self = triggers_self(ui);
    assert(self);
    return triggers_open(ui, self, NULL, rel_path, line);
}

/* ---- the file in the user's own tools: reveal, copy path, copy name ----------------- */

int
Editor_Triggers_Reveal(
    struct EditorUi* ui,
    const char* content_path)
{
    struct TriggersPanel* self;
    char path[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 64];
    char command[2048];
    char why[512];
    char lane[EDITOR_UI_LANE_NAME_MAX + 16];
    char text[EDITOR_UI_CONSOLE_LINE_MAX];
    enum EditorHost_Status status;
    int on_disk;

    assert(ui);
    assert(content_path);
    self = triggers_self(ui);
    assert(self);
    /* A file shown in the file manager is one double-click from a program
     * that saves in place, and a folder is where files get made by hand:
     * the lane's own first. */
    if( !triggers_outside_path(ui, self, content_path, 1, path, sizeof(path), &on_disk, why, sizeof(why)) )
    {
        snprintf(text, sizeof(text), "%s refused: %s (%s)", EDITOR_REVEAL_CAPTION, why, content_path);
        triggers_say(ui, self, text);
        return 0;
    }
    status = Editor_HostLocalRevealInFileManager(path, command, sizeof(command));
    if( status != EDITOR_HOST_OK )
    {
        snprintf(text, sizeof(text), "%s %s: %s", EDITOR_REVEAL_CAPTION,
            status == EDITOR_HOST_UNSUPPORTED ? "is not available on this platform"
            : status == EDITOR_HOST_REFUSED   ? "found nothing of this path on disk"
                                              : "did not start",
            content_path);
        triggers_say(ui, self, text);
        Editor_UiLog(ui, "editor: the path is %s", path);
        return 0;
    }
    Editor_UiLog(ui, "editor: show in %s: %s", EDITOR_REVEAL_APP, command);
    triggers_lane_note(ui, lane, sizeof(lane));
    if( on_disk )
        snprintf(text, sizeof(text), "Showing %s in %s%s", content_path, EDITOR_REVEAL_APP, lane);
    else
        snprintf(text, sizeof(text), "%s is not on disk yet: showing the nearest folder that is, in %s%s",
            content_path, EDITOR_REVEAL_APP, lane);
    triggers_say(ui, self, text);
    return 1;
}

/**
 * What Show in Explorer and Copy path act on, as a path under server/scripts:
 * the picked row's file; else, in File mode, the file shown; else the folder
 * the subject's scripts are in -- its first trigger's, or with no trigger
 * yet edited/<s>/scripts, where Add trigger and New script file write.
 * 0 when the panel shows nothing (or the path does not fit).
 */
static int
triggers_target(
    struct EditorUi* ui,
    const struct TriggersPanel* self,
    char* out,
    size_t out_size,
    int* out_is_folder)
{
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_rows);
    int written;

    *out_is_folder = 0;
    out[0] = '\0';
    if( row >= 0 && row < self->row_count )
        written = snprintf(out, out_size, "%s", self->rows[row].rel);
    else if( self->mode == EDITOR_TRIGGERS_FILE )
    {
        if( !self->file[0] )
            return 0;
        *out_is_folder = self->file_is_folder;
        written = snprintf(out, out_size, "%s", self->file);
    }
    else if( !self->subject[0] )
        return 0;
    else
    {
        *out_is_folder = 1;
        if( self->row_count > 0 )
        {
            const char* slash = strrchr(self->rows[0].rel, '/');
            written = snprintf(out, out_size, "%.*s", slash ? (int)(slash - self->rows[0].rel) : 0, self->rows[0].rel);
        }
        else
            written = snprintf(out, out_size, "edited/%s/scripts", self->subject);
    }
    return written >= 0 && (size_t)written < out_size;
}

static void
triggers_reveal(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char rel[TRIGGERS_REL_MAX];
    char content_path[TRIGGERS_REL_MAX + 32];
    int is_folder;

    if( !triggers_target(ui, self, rel, sizeof(rel), &is_folder) )
    {
        triggers_say(ui, self,
            EDITOR_REVEAL_CAPTION ": show a record or a file first (Record panel, a selection, Name or File)");
        return;
    }
    snprintf(content_path, sizeof(content_path), "server/scripts/%s", rel);
    (void)Editor_Triggers_Reveal(ui, content_path);
}

/** Copy path: the absolute path of what Show in Explorer would show. */
static void
triggers_copy_path(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char rel[TRIGGERS_REL_MAX];
    char content_rel[TRIGGERS_REL_MAX + 32];
    char path[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 64];
    char why[512];
    char lane[EDITOR_UI_LANE_NAME_MAX + 16];
    char text[EDITOR_UI_CONSOLE_LINE_MAX];
    int is_folder;
    int on_disk;

    if( !triggers_target(ui, self, rel, sizeof(rel), &is_folder) )
    {
        triggers_say(ui, self, "Copy path: show a record or a file first (Record panel, a selection, Name or File)");
        return;
    }
    snprintf(content_rel, sizeof(content_rel), "server/scripts/%s", rel);
    if( !self->content_root[0] )
        /* No content folder on this machine: the path inside the tree. */
        snprintf(path, sizeof(path), "%s", content_rel);
    else if( !triggers_outside_path(ui, self, content_rel, 0, path, sizeof(path), &on_disk, why, sizeof(why)) )
    {
        /* A copied path is pasted into a program that writes there: the
         * lane's own first, as for Open in editor. */
        snprintf(text, sizeof(text), "Copy path refused: %s (%s)", why, rel);
        triggers_say(ui, self, text);
        return;
    }
    if( !PlatformClipboard_SetText(path) )
    {
        triggers_say(ui, self, "Copy refused: the system clipboard did not take the path (it is in the Console)");
        Editor_UiLog(ui, "editor: the path is %s", path);
        return;
    }
    /* The bar names the file inside the tree (a whole machine path does not
     * fit it); the Console has what was copied. */
    snprintf(text, sizeof(text), "Copied the path of %s%s%s", rel[0] ? rel : "server/scripts",
        is_folder ? " (the folder its scripts are in)" : "", triggers_lane_note(ui, lane, sizeof(lane)));
    triggers_say(ui, self, text);
    Editor_UiLog(ui, "editor: copied %s", path);
}

/** Copy name: the record's name as a script writes it ("fenwick"). In File
 *  mode a file has none of its own: the picked trigger's subject. */
static void
triggers_copy_name(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_rows);
    const char* name = self->subject;
    char text[EDITOR_UI_CONSOLE_LINE_MAX];

    if( self->mode == EDITOR_TRIGGERS_FILE )
        name = row >= 0 && row < self->row_count ? self->rows[row].subject : "";
    if( !name[0] )
    {
        triggers_say(ui, self,
            self->mode == EDITOR_TRIGGERS_FILE ? "Copy name: pick a trigger row first (a file has no name of its own)"
                                               : "Copy name: show a record first (Record panel, a selection, or Name)");
        return;
    }
    if( !PlatformClipboard_SetText(name) )
        snprintf(text, sizeof(text), "Copy refused: the system clipboard did not take the name %s", name);
    else
        snprintf(text, sizeof(text), "Copied the name: %s", name);
    triggers_say(ui, self, text);
}

/* ---- snippets ---------------------------------------------------------------------- */

/** The record name the snippets are written for: the subject; in File mode
 *  the file's own name ("quest_cook" of quests/quest_cook/scripts/
 *  quest_cook.rs2), which is what a file's triggers are usually named after.
 *  "" when nothing is shown or the name is longer than a record's. */
static void
triggers_snippet_name(
    const struct TriggersPanel* self,
    char* out,
    size_t out_size)
{
    const char* name = self->subject;
    size_t length = strlen(name);

    out[0] = '\0';
    if( self->mode == EDITOR_TRIGGERS_FILE )
    {
        const char* slash = strrchr(self->file, '/');
        const char* dot;
        name = slash ? slash + 1 : self->file;
        dot = strrchr(name, '.');
        length = dot ? (size_t)(dot - name) : strlen(name);
        /* A quest's file (or folder) is quest_<name>: the snippets write
         * %<name>_quest and quests/quest_<name>, as New quest does. */
        if( length > 6 && strncmp(name, "quest_", 6) == 0 )
        {
            name += 6;
            length -= 6;
        }
    }
    if( length == 0 || length >= out_size )
        return;
    memcpy(out, name, length);
    out[length] = '\0';
}

/** The preview's visible lines, from the rendered text. */
static void
triggers_snippet_fill(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    int first;
    int shown;

    if( !ToriRSChrome_VListWindow(ui->chrome, self->code_snippet, &first, &shown) )
        return;
    for( int l = first; l < first + shown && l < self->snippet_line_count; l++ )
    {
        char line[TORIRS_CHROME_INPUT_MAX];
        size_t const start = (size_t)self->snippet_line_start[l];
        size_t stop =
            l + 1 < self->snippet_line_count ? (size_t)self->snippet_line_start[l + 1] : self->snippet_length;
        while( stop > start && self->snippet_text[stop - 1] == '\n' )
            stop--;
        snprintf(line, sizeof(line), "%.*s", (int)(stop - start), self->snippet_text + start);
        ToriRSChrome_CodeSetLine(ui->chrome, self->code_snippet, l, line);
    }
}

/** Render the chosen snippet for the name and cut it into the preview's
 *  lines; no choice (or no name) empties the preview. */
static void
triggers_snippet_render(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    int const choice = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_snippet);

    self->snippet_shown = -1;
    self->snippet_length = 0;
    self->snippet_line_count = 0;
    self->snippet_text[0] = '\0';
    if( choice >= 0 && choice < self->snippet_count && self->snippet_name[0] )
    {
        self->snippet_length = Editor_SnippetRender((enum Editor_SnippetKind)self->snippet_kinds[choice],
            self->snippet_indices[choice], self->snippet_name, self->quest, self->quest_var, self->snippet_text,
            sizeof(self->snippet_text));
        self->snippet_shown = choice;
        for( size_t at = 0; at < self->snippet_length; )
        {
            const char* newline = memchr(self->snippet_text + at, '\n', self->snippet_length - at);
            assert(self->snippet_line_count < TRIGGERS_SNIPPET_LINES_MAX);
            self->snippet_line_start[self->snippet_line_count++] = (int)at;
            if( !newline )
                break;
            at = (size_t)(newline - self->snippet_text) + 1;
        }
    }
    snprintf(self->snippet_quest, sizeof(self->snippet_quest), "%s", self->quest);
    snprintf(self->snippet_var, sizeof(self->snippet_var), "%s", self->quest_var);
    ToriRSChrome_VListSetCount(ui->chrome, self->code_snippet, self->snippet_line_count);
    ToriRSChrome_VListSetScroll(ui->chrome, self->code_snippet, 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_copy_snippet, self->snippet_shown < 0);
    triggers_snippet_fill(ui, self);
}

/** The titles follow the subject's kind (its own snippets, then the ones
 *  any record can use); the text follows the choice and the name. Run each
 *  frame the panel shows: it compares, and renders only on a change. */
static void
triggers_snippets_follow(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char name[EDITOR_SNIPPET_SUBJECT_MAX];
    enum Editor_SnippetKind const kind =
        self->mode == EDITOR_TRIGGERS_FILE ? EDITOR_SNIPPET_ANY : Editor_SnippetKindOf(self->type);

    triggers_snippet_name(self, name, sizeof(name));
    if( !self->snippet_listed || kind != self->snippet_kind || (name[0] != '\0') != (self->snippet_name[0] != '\0') )
    {
        /* The choice is kept by its title when the new list still has it
         * (another npc: still "Talk-to: one line"). */
        const char* kept = self->snippet_shown >= 0 ? self->snippet_titles[self->snippet_shown] : NULL;
        int selected = -1;

        self->snippet_count = 0;
        if( name[0] )
        {
            for( int pass = 0; pass < 2; pass++ )
            {
                enum Editor_SnippetKind const from = pass == 0 ? kind : EDITOR_SNIPPET_ANY;
                if( pass == 1 && kind == EDITOR_SNIPPET_ANY )
                    break;
                for( int i = 0; i < Editor_SnippetCount(from); i++ )
                {
                    assert(self->snippet_count < TRIGGERS_SNIPPETS_MAX);
                    self->snippet_titles[self->snippet_count] = Editor_SnippetTitle(from, i);
                    self->snippet_kinds[self->snippet_count] = (unsigned char)from;
                    self->snippet_indices[self->snippet_count] = (unsigned char)i;
                    self->snippet_count++;
                }
            }
            selected = 0;
            for( int i = 0; i < self->snippet_count; i++ )
                if( self->snippet_titles[i] == kept )
                    selected = i;
        }
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_snippet, self->snippet_titles, self->snippet_count,
            selected);
        self->snippet_kind = kind;
        self->snippet_listed = 1;
        snprintf(self->snippet_name, sizeof(self->snippet_name), "%s", name);
        triggers_snippet_render(ui, self);
        return;
    }
    if( ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_snippet) != self->snippet_shown
        || strcmp(name, self->snippet_name) != 0 || strcmp(self->quest, self->snippet_quest) != 0
        || strcmp(self->quest_var, self->snippet_var) != 0 )
    {
        snprintf(self->snippet_name, sizeof(self->snippet_name), "%s", name);
        triggers_snippet_render(ui, self);
    }
    else if( ToriRSChrome_VListNeedsRows(ui->chrome, self->code_snippet) )
        triggers_snippet_fill(ui, self);
}

/** 1 when server/scripts/`rel` is a builder's output (its header line): the
 *  builder writes the file again from its model, so text pasted into it is
 *  lost unless the file is detached there first. Asked on a click, never
 *  per frame: with no document of the file open here it reads the disk. */
static int
triggers_rel_generated(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* rel)
{
    static const char k_prefix[] = BUILDER_HEADER_PREFIX;
    const struct Editor_TextDoc* doc = triggers_doc(ui, rel);
    size_t length = 0;
    char* disk;
    int generated;

    if( doc )
        return doc->length >= sizeof(k_prefix) - 1 && memcmp(doc->text, k_prefix, sizeof(k_prefix) - 1) == 0;
    disk = triggers_read_disk(self, rel, &length);
    if( !disk )
        return 0;
    generated = length >= sizeof(k_prefix) - 1 && memcmp(disk, k_prefix, sizeof(k_prefix) - 1) == 0;
    ToriMem_Free(ToriMem_Editor(), disk);
    return generated;
}

/** Where the snippet shown is pasted, for the sentence Copy snippet says: a
 *  script goes into the picked row's file, else the subject's first
 *  trigger's, else the file New script file makes; a variable or a stage
 *  list into the quest's configs folder. */
static void
triggers_snippet_where(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    enum Editor_SnippetFile file,
    char* out,
    size_t out_size)
{
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_rows);
    char edited[EDITOR_SCRIPT_PATH_MAX];
    const char* rel = NULL;

    if( file != EDITOR_SNIPPET_FILE_SCRIPT )
    {
        /* The quest's own configs file of that kind: New quest made the
         * .varp and the .constant; another kind is a new file beside them. */
        const char* extension = Editor_SnippetFileExtension(file);
        const char* quest = self->quest[0] ? self->quest : self->snippet_name;
        snprintf(out, out_size, "%s quests/quest_%s/configs/quest_%s%s", self->quest[0] ? "the quest's" : "a file like",
            quest, quest, extension);
        return;
    }
    if( row >= 0 && row < self->row_count && Editor_ScriptPathValid(self->rows[row].rel) )
        rel = self->rows[row].rel;
    else if( self->mode == EDITOR_TRIGGERS_FILE && !self->file_is_folder && Editor_ScriptPathValid(self->file) )
        rel = self->file;
    else if( self->quest[0] )
    {
        /* The quest being worked on: its script, whatever is shown. */
        Editor_QuestFilePath(self->quest, EDITOR_SNIPPET_FILE_SCRIPT, edited, sizeof(edited));
        snprintf(out, out_size, "%s (the quest's script)", edited);
        return;
    }
    else if( self->mode == EDITOR_TRIGGERS_FILE )
        rel = NULL;
    else if( self->row_count > 0 )
        rel = self->rows[0].rel;
    else if( Editor_ScriptEditedPath(self->subject, edited, sizeof(edited)) )
    {
        snprintf(out, out_size, "%s%s", edited, self->edited_exists ? "" : " (New script file makes it)");
        return;
    }
    if( !rel )
        snprintf(out, out_size, "a .rs2 script file");
    else if( triggers_rel_generated(ui, self, rel) )
        /* The builder's own header says the same; a paste would not survive it. */
        snprintf(out, out_size, "%s once it is detached: a builder writes that file", rel);
    else
        snprintf(out, out_size, "%s", rel);
}

/** Copy snippet: the previewed text to the OS clipboard. */
static void
triggers_copy_snippet(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char text[EDITOR_UI_CONSOLE_LINE_MAX];
    char where[TRIGGERS_REL_MAX + 64];
    const char* title;

    if( self->snippet_shown < 0 )
    {
        triggers_say(ui, self, "Copy snippet: show a record first (Record panel, a selection, or Name), then choose one");
        return;
    }
    title = self->snippet_titles[self->snippet_shown];
    if( !PlatformClipboard_SetText(self->snippet_text) )
    {
        snprintf(text, sizeof(text), "Copy refused: the system clipboard did not take %s (its text is in the preview)",
            title);
        triggers_say(ui, self, text);
        return;
    }
    triggers_snippet_where(ui, self,
        Editor_SnippetFileKind((enum Editor_SnippetKind)self->snippet_kinds[self->snippet_shown],
            self->snippet_indices[self->snippet_shown]),
        where, sizeof(where));
    snprintf(text, sizeof(text), "Copied %s (%d lines): paste it into %s", title, self->snippet_line_count, where);
    triggers_say(ui, self, text);
}

/* ---- a new script file ------------------------------------------------------------- */

/**
 * New script file: the subject's own script, edited/<s>/scripts/<s>.rs2 --
 * the file Add trigger writes into, so a trigger added later lands beside
 * what was pasted -- started as a two-line header and handed to the user's
 * editor. Through the document, as Add trigger goes: one splice the
 * authority applies (one undo step), saved by Open in editor before the
 * editor reads the disk; nothing is written behind the server's back. A
 * file that is already there is opened, not made again. Returns 1 when the
 * editor was handed the file.
 */
static int
triggers_new_file(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char rel[EDITOR_SCRIPT_PATH_MAX];
    char key[EDITOR_DOC_KEY_MAX];
    char label[160];
    char text[EDITOR_UI_CONSOLE_LINE_MAX];
    struct Editor_TextDoc* doc;
    struct Editor_Cmd command;
    int written;
    int applied;

    if( !ui->editor || !self->subject[0] || self->mode == EDITOR_TRIGGERS_FILE )
    {
        triggers_say(ui, self, "New script file: show a record first (Record panel, a selection, or Name)");
        return 0;
    }
    if( ui->tools.stroke_open )
    {
        triggers_say(ui, self, "New script file: finish the stroke in progress first");
        return 0;
    }
    if( !Editor_ScriptEditedPath(self->subject, rel, sizeof(rel)) )
    {
        snprintf(text, sizeof(text), "New script file refused: %s is too long a name for a script path", self->subject);
        triggers_say(ui, self, text);
        return 0;
    }
    snprintf(key, sizeof(key), "rs2:%s", rel);
    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, key) || !(doc = triggers_doc(ui, rel)) )
    {
        snprintf(text, sizeof(text), "New script file refused: the editor server did not open %s", rel);
        triggers_say(ui, self, text);
        return 0;
    }
    /* A copy that differs from the disk with nothing unsaved here: the file
     * was written outside, so it is there. */
    if( doc->length > 0 || triggers_doc_stale(self, doc, rel) )
    {
        /* Already a file (or a document with text): the editor, not a
         * second header. A builder's output says so before it is edited. */
        int const generated = triggers_rel_generated(ui, self, rel);
        int const opened = Editor_Triggers_OpenInEditor(ui, rel, 1);
        snprintf(text, sizeof(text), "%s is already there%s%s", rel,
            generated ? " (a builder writes it: detach it there before editing)" : "",
            opened ? ": opened it in your editor" : ": your editor did not start (see the Triggers panel)");
        Editor_UiStatus(ui, "%s", text);
        return opened;
    }

    Editor_CmdInit(&command, EDITOR_DOC_TEXT, key, EDITOR_CMD_TEXT_SPLICE);
    written = snprintf(command.u.text_splice.text, sizeof(command.u.text_splice.text),
        "// %s: scripts written by hand for the %s %s (ToriRSEd, Triggers > New script file).\n"
        "// Paste a snippet from the Triggers panel under this line, or use Add trigger. RUN builds it.\n",
        self->subject, self->type[0] ? self->type : "record", self->subject);
    assert(written > 0);
    assert((size_t)written < sizeof(command.u.text_splice.text));
    command.has_before = 1;
    command.has_after = 1;
    command.u.text_splice.offset = 0;
    command.u.text_splice.before_len = 0;
    command.u.text_splice.after_len = (uint32_t)written;

    snprintf(label, sizeof(label), "New script file %s", rel);
    Editor_StrokeBegin(ui->editor);
    Editor_HistoryBreak(&ui->editor->history, label);
    applied = Editor_Apply(ui->editor, &command) ? 1 : 0;
    Editor_StrokeEnd(ui->editor);
    if( !applied )
    {
        snprintf(text, sizeof(text), "New script file refused: the editor server did not take %s", rel);
        triggers_say(ui, self, text);
        return 0;
    }
    Editor_UiLog(ui, "editor: %s (a two-line header; Open in editor saves it first)", label);
    self->dirty = 1;
    /* Line 3: the first line under the header. Open in editor saves the
     * document, so the editor opens a file that exists. */
    if( !Editor_Triggers_OpenInEditor(ui, rel, 3) )
    {
        snprintf(text, sizeof(text), "Made %s, unsaved: your editor did not start (Save writes the file)", rel);
        Editor_UiStatus(ui, "%s", text);
        return 0;
    }
    snprintf(text, sizeof(text), "Made %s and opened it in your editor: paste a snippet under its header", rel);
    Editor_UiStatus(ui, "%s", text);
    return 1;
}

/* ---- a new quest -------------------------------------------------------------------- */

/** The three files New quest makes, in the order they are written. */
static const enum Editor_SnippetFile k_quest_files[] = {
    EDITOR_SNIPPET_FILE_VARP,
    EDITOR_SNIPPET_FILE_CONSTANT,
    EDITOR_SNIPPET_FILE_SCRIPT,
};
#define QUEST_FILE_COUNT ((int)(sizeof(k_quest_files) / sizeof(k_quest_files[0])))
/** The ledger line of a new quest's variable, as TEXT_SPLICEs: one, or two
 *  when the line is the ledger's first (its header goes with it). */
#define QUEST_LEDGER_CMDS_MAX 4

/**
 * A new quest's variable given its id now, the way the build's allocator
 * would (editor_alloc.h): `<name>_quest` coined varp<N>_<name>_quest against
 * the tree's layers and the lane's ledger, and the ledger's line as commands
 * for the caller's stroke. So the quest's script names a variable that
 * exists: Check passes before the first RUN, and no RUN has to rename it in
 * files the user already has open.
 *
 * The ledger is the document "pack:varp.alloc", opened here. A RUN appends
 * to that file behind the document (a var declared with no id), so a clean
 * document that differs from the disk is brought up to it first, in the same
 * stroke: allocating against a stale copy would hand out an id the file has
 * given away, and saving it would drop the file's lines.
 *
 * 1 with `out_var` and `*out_count` commands in `out` (`*out_sync`: the
 * splices that bring the document up to the disk, owned by the caller, to
 * apply first); 0 when the variable cannot be numbered here -- `why` says so
 * and the quest is written with the unnumbered name, which RUN numbers.
 */
static int
triggers_alloc_quest_var(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* name,
    char* out_var,
    size_t var_size,
    char* out_key,
    size_t key_size,
    struct Editor_Cmd** out_sync,
    int* out_sync_count,
    struct Editor_Cmd* out,
    int* out_count,
    char* why,
    size_t why_size)
{
    struct Editor_DocHandle handle;
    const struct Editor_TextDoc* doc = NULL;
    struct Editor_AllocNs* state;
    struct Editor_AllocRequest request;
    struct Editor_AllocResult result;
    struct Editor_AllocText ledger;
    char base_name[EDITOR_QUEST_NAME_MAX + 8];
    char path[TRIGGERS_PATH_MAX + 64];
    char* disk = NULL;
    size_t disk_length = 0;
    enum Editor_AllocStatus status;
    FILE* file;

    out_var[0] = '\0';
    *out_sync = NULL;
    *out_sync_count = 0;
    *out_count = 0;
    why[0] = '\0';
    if( !self->content_root[0] || !Editor_AllocDocKey("varp", "alloc", out_key, key_size) )
    {
        snprintf(why, why_size, "this window has no content folder to read the id ledger from");
        return 0;
    }
    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, out_key)
        || !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, out_key, &handle) || !handle.text
        || !handle.text->loaded )
    {
        snprintf(why, why_size, "the editor server did not open pack/varp.alloc");
        return 0;
    }
    doc = handle.text;
    ledger.text = doc->text ? doc->text : "";
    ledger.length = doc->length;

    snprintf(path, sizeof(path), "%s/pack/varp.alloc", self->content_root);
    file = fopen(path, "rb");
    if( file )
    {
        long size;
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if( size >= 0 )
        {
            disk = ToriMem_Alloc(ToriMem_Editor(), (size_t)size + 1);
            assert(disk);
            disk_length = fread(disk, 1, (size_t)size, file);
            disk[disk_length] = '\0';
        }
        fclose(file);
    }
    if( disk && !doc->dirty && (disk_length != doc->length || memcmp(disk, ledger.text, disk_length) != 0) )
    {
        int const n =
            Builder_TextSplices(EDITOR_DOC_TEXT, out_key, ledger.text, ledger.length, disk, disk_length, NULL, 0);
        if( n > 0 )
        {
            *out_sync = ToriMem_Calloc(ToriMem_Editor(), (size_t)n, sizeof(**out_sync));
            assert(*out_sync);
            (void)Builder_TextSplices(EDITOR_DOC_TEXT, out_key, ledger.text, ledger.length, disk, disk_length,
                *out_sync, n);
            *out_sync_count = n;
        }
        ledger.text = disk;
        ledger.length = disk_length;
    }

    state = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*state));
    assert(state);
    snprintf(base_name, sizeof(base_name), "%s_quest", name);
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.ns = "varp";
    request.name = base_name;
    request.target = EDITOR_ALLOC_LEDGER;
    request.id_file = ledger;
    if( !Editor_AllocNsLoadWith(state, self->content_root, "varp", NULL, &ledger) )
    {
        snprintf(why, why_size, "the content tree's id layers cannot be read");
        status = EDITOR_ALLOC_REFUSED;
    }
    else
    {
        status = Editor_AllocPlan(state, &request, out, QUEST_LEDGER_CMDS_MAX, &result);
        if( status == EDITOR_ALLOC_OK )
            *out_count = result.command_count;
        else if( status != EDITOR_ALLOC_EXISTS )
            snprintf(why, why_size, "%s",
                status == EDITOR_ALLOC_FULL      ? "the server has no free varp id left"
                : status == EDITOR_ALLOC_REFUSED ? "this tree does not let the server number varps"
                                                 : "the id ledger did not take the name");
    }
    Editor_AllocNsFree(state);
    ToriMem_Free(ToriMem_Editor(), state);
    ToriMem_Free(ToriMem_Editor(), disk);
    /* A name the ledger holds already (a quest made and deleted before) keeps
     * the id it has: nothing to append. */
    if( (status == EDITOR_ALLOC_OK || status == EDITOR_ALLOC_EXISTS) && result.name[0]
        && strlen(result.name) < var_size )
    {
        snprintf(out_var, var_size, "%s", result.name);
        return 1;
    }
    ToriMem_Free(ToriMem_Editor(), *out_sync);
    *out_sync = NULL;
    *out_sync_count = 0;
    *out_count = 0;
    if( !why[0] )
        snprintf(why, why_size, "the id ledger did not take the name");
    return 0;
}

int
Editor_Triggers_NewQuest(
    struct EditorUi* ui,
    const char* typed)
{
    struct TriggersPanel* self;
    char name[EDITOR_QUEST_NAME_MAX];
    char folder[EDITOR_SCRIPT_PATH_MAX];
    char rels[QUEST_FILE_COUNT][EDITOR_SCRIPT_PATH_MAX];
    char keys[QUEST_FILE_COUNT][EDITOR_DOC_KEY_MAX];
    char label[160];
    char lane[EDITOR_UI_LANE_NAME_MAX + 16];
    char text[EDITOR_UI_CONSOLE_LINE_MAX];
    char var[EDITOR_SNIPPET_SUBJECT_MAX];
    char ledger_key[EDITOR_DOC_KEY_MAX];
    char why[160];
    struct Editor_Cmd* ledger_cmds;
    struct Editor_Cmd* sync_cmds = NULL;
    int sync_count = 0;
    int ledger_count = 0;
    int numbered;
    char* body;
    int applied = 1;
    int saved = 0;
    int opened;

    assert(ui);
    self = triggers_self(ui);
    assert(self);
    /* Asked for from the File menu or a chord: the panel, with the name's
     * field under the caret. */
    self->wanted = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    if( !typed )
        typed = ToriRSChrome_Text(ui->chrome, self->input_quest);
    if( !ui->editor )
    {
        triggers_say(ui, self, "New quest refused: this window has no editor session");
        return 0;
    }
    if( !Editor_QuestNameFrom(typed, name, sizeof(name)) )
    {
        (void)ToriRSChrome_FocusWidget(ui->chrome, self->input_quest, (int)strlen(typed));
        if( typed[0] )
            snprintf(text, sizeof(text),
                "New quest refused: \"%s\" cannot name a quest. Small letters, digits and _, a letter first, up to "
                "%d: thistle",
                typed, EDITOR_QUEST_NAME_MAX - 1);
        else
            snprintf(text, sizeof(text),
                "New quest: type the quest's name in Quest name, then press New quest (thistle makes "
                "quests/quest_thistle)");
        triggers_say(ui, self, text);
        return 0;
    }
    if( ui->tools.stroke_open )
    {
        triggers_say(ui, self, "New quest: finish the stroke in progress first");
        return 0;
    }
    Editor_QuestFolder(name, folder, sizeof(folder));
    for( int i = 0; i < QUEST_FILE_COUNT; i++ )
    {
        const struct Editor_TextDoc* doc;
        Editor_QuestFilePath(name, k_quest_files[i], rels[i], sizeof(rels[i]));
        snprintf(keys[i], sizeof(keys[i]), "rs2:%s", rels[i]);
        if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, keys[i]) || !(doc = triggers_doc(ui, rels[i])) )
        {
            snprintf(text, sizeof(text), "New quest refused: the editor server did not open %s", rels[i]);
            triggers_say(ui, self, text);
            return 0;
        }
        /* A quest that is there is shown, never written over. */
        if( doc->length > 0 || triggers_rel_on_disk(self, rels[i]) )
        {
            (void)Editor_Triggers_ShowFile(ui, folder);
            snprintf(text, sizeof(text), "%s is already there: its files are the rows (Open in editor opens it)",
                folder);
            triggers_say(ui, self, text);
            return 0;
        }
    }

    /* Through the documents, as New script file and Add trigger write: the
     * server's save makes the folder and the files the lane's own, and the
     * whole quest is one History row. */
    body = ToriMem_Alloc(ToriMem_Editor(), EDITOR_SNIPPET_MAX);
    assert(body);
    ledger_cmds = ToriMem_Calloc(ToriMem_Editor(), QUEST_LEDGER_CMDS_MAX, sizeof(*ledger_cmds));
    assert(ledger_cmds);
    /* The variable's id now, so the script names one that exists. */
    numbered = triggers_alloc_quest_var(ui, self, name, var, sizeof(var), ledger_key, sizeof(ledger_key), &sync_cmds,
        &sync_count, ledger_cmds, &ledger_count, why, sizeof(why));
    if( !numbered )
        Editor_UiLog(ui, "editor: New quest: %s; the variable is written [%s_quest] and the first RUN numbers it", why,
            name);
    snprintf(label, sizeof(label), "New quest %s", name);
    Editor_StrokeBegin(ui->editor);
    Editor_HistoryBreak(&ui->editor->history, label);
    for( int i = 0; i < sync_count && applied; i++ )
        applied = Editor_Apply(ui->editor, &sync_cmds[i]) ? 1 : 0;
    for( int i = 0; i < ledger_count && applied; i++ )
        applied = Editor_Apply(ui->editor, &ledger_cmds[i]) ? 1 : 0;
    ToriMem_Free(ToriMem_Editor(), sync_cmds);
    ToriMem_Free(ToriMem_Editor(), ledger_cmds);
    for( int i = 0; i < QUEST_FILE_COUNT && applied; i++ )
    {
        size_t const length =
            Editor_QuestFileText(name, numbered ? var : NULL, k_quest_files[i], body, EDITOR_SNIPPET_MAX);
        for( size_t done = 0; done < length && applied; )
        {
            struct Editor_Cmd command;
            size_t chunk = length - done;
            if( chunk > EDITOR_CMD_SPLICE_MAX )
                chunk = EDITOR_CMD_SPLICE_MAX;
            Editor_CmdInit(&command, EDITOR_DOC_TEXT, keys[i], EDITOR_CMD_TEXT_SPLICE);
            command.has_before = 1;
            command.has_after = 1;
            command.u.text_splice.offset = (uint32_t)done;
            command.u.text_splice.before_len = 0;
            command.u.text_splice.after_len = (uint32_t)chunk;
            memcpy(command.u.text_splice.text, body + done, chunk);
            applied = Editor_Apply(ui->editor, &command) ? 1 : 0;
            done += chunk;
        }
    }
    Editor_StrokeEnd(ui->editor);
    ToriMem_Free(ToriMem_Editor(), body);
    if( !applied )
    {
        snprintf(text, sizeof(text), "New quest refused: the editor server did not take %s's files (Undo takes back "
                                     "what it did take)",
            folder);
        triggers_say(ui, self, text);
        return 0;
    }
    /* The ledger first: the files name an id only it gives. */
    if( numbered && ledger_count > 0 && Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, ledger_key) <= 0 )
        Editor_UiLog(ui, "editor: New quest: pack/varp.alloc did not save (Save all writes it)");
    for( int i = 0; i < QUEST_FILE_COUNT; i++ )
        saved += Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, keys[i]) > 0;
    Editor_UiLog(ui, "editor: %s: %s (%d of %d files saved)%s%s", label, folder, saved, QUEST_FILE_COUNT,
        numbered ? "; its variable is %" : "", numbered ? var : "");
    /* The name stays in the field: a second press (Enter, then the button)
     * says the quest is there. Emptied, it said "type the quest's name" over
     * the sentence saying where the quest was made (2026-11-11). */
    (void)Editor_Triggers_ShowFile(ui, folder);
    if( saved < QUEST_FILE_COUNT )
    {
        snprintf(text, sizeof(text), "Made %s, but %d of its %d files did not save (Save all writes them; the "
                                     "Console says why)",
            folder, QUEST_FILE_COUNT - saved, QUEST_FILE_COUNT);
        triggers_say(ui, self, text);
        return 0;
    }
    /* The folder as the editor's workspace, the script at its first cheat. */
    opened = triggers_open(ui, self, folder, rels[QUEST_FILE_COUNT - 1], Editor_QuestScriptLine());
    {
        /* Where it is, in full: the Console keeps it, on a line of its own
         * (a status sentence has no room for a machine path). */
        char content_rel[TRIGGERS_REL_MAX + 32];
        char path[TRIGGERS_PATH_MAX + TRIGGERS_REL_MAX + 64];
        char why[256];
        int on_disk;
        snprintf(content_rel, sizeof(content_rel), "server/scripts/%s", folder);
        if( triggers_outside_path(ui, self, content_rel, 0, path, sizeof(path), &on_disk, why, sizeof(why)) )
        {
            Editor_UiLog(ui, "editor: quest %s is in the folder:", name);
            Editor_UiLog(ui, "%s", path);
        }
    }
    triggers_lane_note(ui, lane, sizeof(lane));
    /* The variable first: the status bar holds EDITOR_UI_CONSOLE_LINE_MAX
     * bytes, and with the lane and the editor's name the sentence ran past
     * it and lost the one thing a script needs (2026-11-11). */
    if( opened && numbered )
        snprintf(text, sizeof(text), "Quest %s made, its variable %%%s: %s%s, opened in %s", name, var, folder, lane,
            Editor_HostLocalEditorWho(1));
    else if( opened )
        snprintf(text, sizeof(text), "Made quest %s in server/scripts/%s%s and opened it in %s. RUN gives %%%s_quest "
                                     "its id",
            name, folder, lane, Editor_HostLocalEditorWho(1), name);
    else
        snprintf(text, sizeof(text), "Made quest %s in server/scripts/%s%s; your editor did not start (Copy path "
                                     "copies the folder)",
            name, folder, lane);
    triggers_say(ui, self, text);
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
    /* The quest's variable too: a RUN renames one declared with no id. */
    triggers_read_quest_var(ui, self);
    triggers_rebuild(ui, self);
    if( self->stale_count > 0 )
        snprintf(text, sizeof(text), "re-read from disk: %s%s was saved outside, and its rows are the file's", self->stale[0],
            self->stale_count > 1 ? " and more" : "");
    else
        snprintf(text, sizeof(text), "re-read from disk; the catalog re-stats changed files (at most every 2 s)");
    triggers_status(ui, self, text);
    return self->stale_count;
}

/** This window's copy of script `rel` made the disk's text again, as one
 *  undo step (it is then unsaved: the bytes are the disk's). 1 when it was
 *  (or already was), 0 when there is no copy, no file or a stroke is open. */
static int
triggers_reload_doc(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* rel)
{
    struct Editor_TextDoc* doc = triggers_doc(ui, rel);
    char key[EDITOR_DOC_KEY_MAX];
    char label[160];
    size_t length;
    char* disk;
    int n;
    struct Editor_Cmd* commands;
    int applied = 0;

    if( !doc || ui->tools.stroke_open )
        return 0;
    disk = triggers_read_disk(self, rel, &length);
    if( !disk )
        return 0;
    snprintf(key, sizeof(key), "rs2:%s", rel);
    n = Builder_TextSplices(EDITOR_DOC_TEXT, key, doc->text ? doc->text : "", doc->length, disk, length, NULL, 0);
    commands = n > 0 ? ToriMem_Calloc(ToriMem_Editor(), (size_t)n, sizeof(*commands)) : NULL;
    if( n > 0 )
    {
        assert(commands);
        (void)Builder_TextSplices(EDITOR_DOC_TEXT, key, doc->text ? doc->text : "", doc->length, disk, length,
            commands, n);
        snprintf(label, sizeof(label), "Reload %s from disk", rel);
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryBreak(&ui->editor->history, label);
        for( int c = 0; c < n; c++ )
            applied += Editor_Apply(ui->editor, &commands[c]) ? 1 : 0;
        Editor_StrokeEnd(ui->editor);
        Editor_UiLog(ui, "editor: %s (%d splice%s)", label, n, n == 1 ? "" : "s");
    }
    ToriMem_Free(ToriMem_Editor(), commands);
    ToriMem_Free(ToriMem_Editor(), disk);
    return applied == n;
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
        reloaded += triggers_reload_doc(ui, self, self->stale[i]);
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
    /* Absolute: the editor the path is handed to has its own cwd. realpath
     * writes up to PATH_MAX bytes whatever the buffer is, so it resolves into
     * one of that size (a smaller one is an overflow, which _FORTIFY_SOURCE
     * aborts on at boot with any content dir); a root that does not exist
     * yet is kept as given. The result must fit the panel's own buffer:
     * truncating it would point every script path at another directory. */
    {
        char const* absolute = content_root;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        char resolved[PATH_MAX];
        if( realpath(content_root, resolved) )
            absolute = resolved;
#endif
        assert(strlen(absolute) < sizeof(self->content_root));
        snprintf(self->content_root, sizeof(self->content_root), "%s", absolute);
    }
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
    if( self->check->status == BUILDER_SSC_OK && errors == 0 )
        snprintf(line, sizeof(line), "check: ok, %.1f s", self->check->seconds);
    else if( self->check->status == BUILDER_SSC_OK || self->check->status == BUILDER_SSC_FAILED )
    {
        /* The first error is the sentence; the rest are in the list. */
        const struct Builder_SscDiag* first = self->check->diag_count ? &self->check->diags[0] : NULL;
        if( first && !first->warning )
            snprintf(line, sizeof(line), "check: %d error(s), %.1f s; first %s:%d: %s", errors, self->check->seconds,
                first->file, first->line, first->message);
        else
            snprintf(line, sizeof(line), "check: %d error(s), %.1f s", errors, self->check->seconds);
    }
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
    char why[160];
    int unsaved = 0;

    assert(ui);
    self = triggers_self(ui);
    if( !self )
    {
        Editor_UiStatus(ui, "Check refused: this window has no Triggers panel");
        return -1;
    }
    if( !Editor_BuildJobSend(ui, &self->check_job, TORIRSMAPED_BUILD_CHECK, NULL, why, sizeof(why)) )
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        snprintf(line, sizeof(line), "Check refused: %s", why);
        triggers_status(ui, self, line);
        Editor_UiStatus(ui, "%s", line);
        return 0;
    }
    if( ui->editor )
        for( int i = 0; i < ui->editor->doc.text_count; i++ )
            unsaved += ui->editor->doc.texts[i]->dirty && strncmp(ui->editor->doc.texts[i]->key, "rs2:", 4) == 0;
    memset(self->check_stream, 0, sizeof(*self->check_stream));
    self->check_stream->status = BUILDER_SSC_NOT_RUN;
    triggers_status(ui, self, "check: running on the editor server...");
    Editor_UiLog(ui, "editor: Check sent to the editor server%s", unsaved ? " (with the unsaved scripts)" : "");
    self->wanted = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    return 1;
}

int
Editor_Triggers_CheckRunning(struct EditorUi* ui)
{
    struct TriggersPanel* self;

    assert(ui);
    self = triggers_self(ui);
    return self && self->check_job.active;
}

static void
triggers_check_line(
    void* user,
    const char* text)
{
    struct TriggersPanel* self = user;
    size_t const word = strcspn(text, ":[ ");

    /* make's own line about the failed recipe ("mingw32-make: *** [C:/.../
     * records.mk:33: maped-script-check] Error 1") reads as "<file>:<line>:"
     * to the compiler's parser: it was counted, and listed first, as a
     * second error of the script (2026-11-11). The Problems panel's parser
     * refuses it already (a file with a space). */
    if( word >= 4 && strncmp(text + word - 4, "make", 4) == 0 && (text[word] == ':' || text[word] == '[') )
        return;
    (void)Builder_SscTakeLine(self->check_stream, text);
}

/** Check's job: its lines into the stream as they arrive, then its end. Run
 *  every frame, shown or not, so a Check that ends while the panel is
 *  hidden still lands (the quest board's lesson, 6.7). */
static void
triggers_watch_check(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    int const before = self->check_stream->diag_count;

    if( !self->check_job.active )
        return;
    if( !Editor_BuildJobPoll(ui, &self->check_job, triggers_check_line, self) )
    {
        if( self->check_stream->diag_count != before )
        {
            char line[TORIRS_CHROME_LABEL_MAX];
            snprintf(line, sizeof(line), "check: running... %d problem(s) so far", self->check_stream->diag_count);
            triggers_status(ui, self, line);
        }
        return;
    }
    if( !self->check_stream->summary[0] && self->check_stream->diag_count == 0
        && self->check_job.status != EDITOR_HOST_OK )
    {
        /* The server never ran it: say why, not "it compiles". */
        char line[TORIRS_CHROME_LABEL_MAX];
        snprintf(line, sizeof(line), "Check: %s", Editor_BuildJobWhy(self->check_job.status));
        triggers_status(ui, self, line);
        Editor_UiStatus(ui, "%s", line);
        return;
    }
    if( self->check_stream->status == BUILDER_SSC_NOT_RUN && self->check_job.status == EDITOR_HOST_OK )
        self->check_stream->status = BUILDER_SSC_OK;
    else if( self->check_stream->status == BUILDER_SSC_NOT_RUN && self->check_stream->diag_count > 0 )
        self->check_stream->status = BUILDER_SSC_FAILED;
    if( self->check_stream->seconds <= 0.0 )
        self->check_stream->seconds = self->check_job.seconds;
    Editor_Triggers_ShowResult(ui, self->check_stream);
    Editor_UiStatus(ui, "%s", ToriRSChrome_Text(ui->chrome, self->label_status));
}

/* ---- add a trigger ------------------------------------------------------------------ */

/** The quest's variable, read off its .varp file -- this window's document
 *  of it when one is open, else the disk: the name of the file's first
 *  block. "" when the quest has no such file. Asked when the quest is noted
 *  and on a Refresh (a RUN renames a variable declared without an id), never
 *  per frame. */
static void
triggers_read_quest_var(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    char rel[EDITOR_SCRIPT_PATH_MAX];
    const struct Editor_TextDoc* doc;
    size_t length = 0;
    char* disk;

    self->quest_var[0] = '\0';
    if( !self->quest[0] )
        return;
    Editor_QuestFilePath(self->quest, EDITOR_SNIPPET_FILE_VARP, rel, sizeof(rel));
    doc = triggers_doc(ui, rel);
    if( doc )
    {
        (void)Editor_QuestVarName(doc->text ? doc->text : "", doc->length, self->quest_var, sizeof(self->quest_var));
        return;
    }
    disk = triggers_read_disk(self, rel, &length);
    if( !disk )
        return;
    (void)Editor_QuestVarName(disk, length, self->quest_var, sizeof(self->quest_var));
    ToriMem_Free(ToriMem_Editor(), disk);
}

/** The quest `rel_path` (under server/scripts) belongs to, remembered with
 *  its variable: a file or folder under quests/quest_<name>. Any other path
 *  leaves the quest being worked on as it was. */
static void
triggers_note_quest(
    struct EditorUi* ui,
    struct TriggersPanel* self,
    const char* rel_path)
{
    static const char k_prefix[] = "quests/quest_";
    char segment[EDITOR_SCRIPT_PATH_MAX];
    char name[EDITOR_QUEST_NAME_MAX];
    char* slash;

    if( strncmp(rel_path, k_prefix, sizeof(k_prefix) - 1) != 0 )
        return;
    snprintf(segment, sizeof(segment), "%s", rel_path + sizeof(k_prefix) - 1);
    slash = strchr(segment, '/');
    if( slash )
        *slash = '\0';
    if( !Editor_QuestNameFrom(segment, name, sizeof(name)) )
        return;
    snprintf(self->quest, sizeof(self->quest), "%s", name);
    triggers_read_quest_var(ui, self);
}

/** "Write to" follows the quest being worked on: the record's own script,
 *  and the quest's once there is one -- which is then the choice, since a
 *  quest's triggers are easier kept in one file. Run on a rebuild. */
static void
triggers_write_to_follow(
    struct EditorUi* ui,
    struct TriggersPanel* self)
{
    int count = 1;

    if( self->write_to_listed && strcmp(self->write_to_quest, self->quest) == 0 )
        return;
    snprintf(self->write_to_labels[0], sizeof(self->write_to_labels[0]), "The record's own script (edited/)");
    if( self->quest[0] )
    {
        snprintf(self->write_to_labels[1], sizeof(self->write_to_labels[1]), "Quest script quest_%s.rs2", self->quest);
        count = 2;
    }
    self->write_to_options[0] = self->write_to_labels[0];
    self->write_to_options[1] = self->write_to_labels[1];
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_write_to, self->write_to_options, count, count - 1);
    snprintf(self->write_to_quest, sizeof(self->write_to_quest), "%s", self->quest);
    self->write_to_listed = 1;
}

/** The script Add trigger appends to, under server/scripts: the quest's when
 *  Write to says so, else the subject's own edited script. 0 when the path
 *  does not fit. */
static int
triggers_write_target(
    struct EditorUi* ui,
    const struct TriggersPanel* self,
    char* out,
    size_t out_size)
{
    if( self->quest[0] && ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_write_to) == 1 )
    {
        assert(out_size >= EDITOR_SCRIPT_PATH_MAX);
        Editor_QuestFilePath(self->quest, EDITOR_SNIPPET_FILE_SCRIPT, out, out_size);
        return 1;
    }
    return Editor_ScriptEditedPath(self->subject, out, out_size);
}

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
    char target[EDITOR_SCRIPT_PATH_MAX];
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

    /* The script it is written to (Write to: the subject's own edited
     * script, or the quest's), opened empty when it has no file. */
    if( !triggers_write_target(ui, self, target, sizeof(target)) )
        return 0;
    snprintf(plan.doc_key, sizeof(plan.doc_key), "rs2:%s", target);
    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, plan.doc_key) || !(doc = triggers_doc(ui, target)) )
    {
        Editor_UiLog(ui, "editor: Add trigger: could not open %s", plan.doc_key);
        return 0;
    }
    /* Saved outside since this copy was taken (the file is written in the
     * owner's own editor): the trigger goes into the file as it is now, so
     * the copy takes the disk's text first. It has nothing unsaved -- a
     * stale copy never has. It used to refuse, and send the person to
     * Refresh and Reload from disk, after every save in VS Code. */
    if( triggers_doc_stale(self, doc, target) && !triggers_reload_doc(ui, self, target) )
    {
        char text[EDITOR_UI_CONSOLE_LINE_MAX];
        snprintf(text, sizeof(text), "Add trigger: %s was saved outside and could not be read again", target);
        triggers_say(ui, self, text);
        return 0;
    }
    status = Editor_ScriptPlanAddTrigger(self->type, self->subject, kind, op_value, op_label,
        doc->text ? doc->text : "", doc->length, &plan);
    /* The plan names the subject's own script: this one is the target's. */
    snprintf(plan.rel_path, sizeof(plan.rel_path), "%s", target);
    snprintf(plan.doc_key, sizeof(plan.doc_key), "rs2:%s", target);
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
    /* A script, any other file of a quest (its .varp, its .constant), or a
     * folder: the folder's files are the rows then. */
    if( !Editor_ScriptFileValid(rel_path) )
    {
        char text[EDITOR_UI_CONSOLE_LINE_MAX];
        snprintf(text, sizeof(text), "Show: %s is not a path under server/scripts (quests/quest_cook, or a file in it)",
            rel_path);
        triggers_say(ui, self, text);
        return 0;
    }
    self->mode = EDITOR_TRIGGERS_FILE;
    ToriRSChrome_SetText(ui->chrome, self->input_file, rel_path);
    /* Another file's rows: the row picked among the old ones is none of them. */
    if( strcmp(self->file, rel_path) != 0 )
        ToriRSChrome_VListSetSelected(ui->chrome, self->list_rows, -1);
    snprintf(self->file, sizeof(self->file), "%s", rel_path);
    triggers_note_quest(ui, self, rel_path);
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
    /* The chords are keycaps (design section 15 surface 3); the tips say
     * what the click does, and end with the chord the button wears. */
    ToriRSChrome_SetTooltip(ui->chrome, self->button_check,
        "sscompile on the editor server, unsaved scripts included");
    edp_button_chord(ui, self->button_check, EDITOR_HK_FILE_CHECK_SCRIPTS);
    ToriRSChrome_SetTooltip(ui->chrome, self->button_run, "The Live panel's RUN: save all, build, reload");
    edp_button_chord(ui, self->button_run, EDITOR_HK_FILE_RUN);
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
            if( self->mode == EDITOR_TRIGGERS_FILE && self->file_is_folder )
            {
                /* A folder's file, by its path inside the folder. */
                const char* inside = ref->rel + strlen(self->file);
                int const triggers = self->row_triggers[r];
                char count[32] = "";
                if( triggers >= 0 )
                    snprintf(count, sizeof(count), "   %d trigger%s", triggers, triggers == 1 ? "" : "s");
                snprintf(row, sizeof(row), "%s%s%s", *inside == '/' ? inside + 1 : inside, count,
                    ref->unsaved ? "  (unsaved)" : "");
            }
            else if( self->mode == EDITOR_TRIGGERS_FILE )
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
    triggers_watch_check(ui, self);
    /* Hidden or not, so the keycaps and tips are right the frame it opens. */
    if( self->hint_serial != ui->hotkeys.serial )
        triggers_hints(ui, self);
    if( !ui->chrome->panels[self->panel].visible )
        return;
    self->wanted = 1;
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
    triggers_snippets_follow(ui, self);
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
        {
            /* A folder typed with its slash is the folder. */
            char trimmed[EDITOR_SCRIPT_PATH_MAX + 8];
            size_t length;
            snprintf(trimmed, sizeof(trimmed), "%s", file);
            length = strlen(trimmed);
            while( length > 1 && trimmed[length - 1] == '/' )
                trimmed[--length] = '\0';
            (void)Editor_Triggers_ShowFile(ui, trimmed);
        }
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
        else if( widget == self->button_open && self->mode == EDITOR_TRIGGERS_FILE && self->file[0] )
        {
            /* No row picked: the file shown; of a folder, the folder as the
             * editor's workspace with its first script in view. */
            if( !self->file_is_folder )
                (void)triggers_open(ui, self, NULL, self->file, 1);
            else if( self->row_count == 0 )
                triggers_say(ui, self, "Open in editor: the folder has no file yet (" EDITOR_REVEAL_CAPTION " opens it)");
            else
            {
                int first = 0;
                for( int r = self->row_count - 1; r >= 0; r-- )
                    if( self->row_triggers[r] >= 0 )
                        first = r;
                (void)triggers_open(ui, self, self->file, self->rows[first].rel, 1);
            }
        }
        else
            triggers_say(ui, self,
                widget == self->button_open ? "Open in editor: pick a row first (or show a file with Show: File)"
                                            : "Open problem: pick a problem's row first");
        return 1;
    }
    if( widget == self->list_rows )
    {
        /* A folder's file: the first activation picks it, a second on the
         * same row (Enter, a double-click) shows its triggers. */
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_rows);
        if( self->mode == EDITOR_TRIGGERS_FILE && self->file_is_folder && row >= 0 && row < self->row_count )
        {
            if( row == self->picked_row && self->row_triggers[row] >= 0 )
            {
                char rel[EDITOR_SCRIPT_PATH_MAX];
                snprintf(rel, sizeof(rel), "%s", self->rows[row].rel);
                (void)Editor_Triggers_ShowFile(ui, rel);
            }
            else
                self->picked_row = row;
        }
        return 1;
    }
    if( widget == self->button_new_quest || widget == self->input_quest )
    {
        (void)Editor_Triggers_NewQuest(ui, NULL);
        return 1;
    }
    if( widget == self->button_reveal )
    {
        triggers_reveal(ui, self);
        return 1;
    }
    if( widget == self->button_copy_path )
    {
        triggers_copy_path(ui, self);
        return 1;
    }
    if( widget == self->button_copy_name )
    {
        triggers_copy_name(ui, self);
        return 1;
    }
    if( widget == self->button_new_file )
    {
        (void)triggers_new_file(ui, self);
        return 1;
    }
    if( widget == self->button_copy_snippet )
    {
        /* The choice may have been made this frame (the tick runs after). */
        triggers_snippets_follow(ui, self);
        triggers_copy_snippet(ui, self);
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
    return widget == self->list_diags || widget == self->dropdown_kind || widget == self->input_op
           || widget == self->dropdown_write_to || widget == self->dropdown_snippet || widget == self->code_snippet;
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
    ToriMem_Free(ToriMem_Editor(), self->check_stream);
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
    self->check_stream = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self->check_stream));
    assert(self->check_stream);
    self->run_shown = -2;
    self->hint_serial = UINT32_MAX;
    self->follow_key = -1;
    self->snippet_shown = -1;
    self->picked_row = -1;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + 8), edp_px(ui, EDP_TOP_Y), edp_px(ui, 420),
        "Triggers");
    assert(self->panel >= 0);
    self->dropdown_mode = ToriRSChrome_Dropdown(chrome, self->panel, "Show", k_modes, 3, 0);
    self->input_subject = ToriRSChrome_TextInput(chrome, self->panel, "Subject", "");
    ToriRSChrome_SetTooltip(chrome, self->input_subject, "a loc, npc or obj name (Show: Name)");
    self->input_file = ToriRSChrome_TextInput(chrome, self->panel, "File", "");
    ToriRSChrome_SetTooltip(chrome, self->input_file, "under server/scripts (Show: File)");
    /* Not "Show": the dropdown above is labelled that, and a control found by
     * its label (the agent API, a screen reader) must be the only one. */
    self->button_show = ToriRSChrome_Button(chrome, self->panel, "Show rows");
    ToriRSChrome_SetTooltip(chrome, self->button_show, "the rows of the Subject or the File typed above");
    self->label_title = ToriRSChrome_Label(chrome, self->panel, "no subject");
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "");
    self->list_rows = ToriRSChrome_VList(chrome, self->panel, "", TRIGGERS_ROWS, 0);
    self->button_open = ToriRSChrome_Button(chrome, self->panel, "Open in editor");
    ToriRSChrome_SetTooltip(chrome, self->button_open,
        "the picked row's file at its line, in your editor");
    /* The file, one click from the user's own tools (owner, 2026-11-09):
     * scripts are written in their editor, not here. */
    self->button_reveal = ToriRSChrome_Button(chrome, self->panel, EDITOR_REVEAL_CAPTION);
    ToriRSChrome_SetTooltip(chrome, self->button_reveal, "the picked row's file, else the folder its scripts are in");
    self->button_copy_path = ToriRSChrome_Button(chrome, self->panel, "Copy path");
    ToriRSChrome_SetTooltip(chrome, self->button_copy_path, "that file's (or folder's) full path, to the clipboard");
    self->button_copy_name = ToriRSChrome_Button(chrome, self->panel, "Copy name");
    ToriRSChrome_SetTooltip(chrome, self->button_copy_name, "the record's name as a script writes it");
    self->button_new_file = ToriRSChrome_Button(chrome, self->panel, "New script file");
    ToriRSChrome_SetTooltip(chrome, self->button_new_file, "edited/<name>/scripts/<name>.rs2, opened in your editor");
    /* The file's buttons fill the row: at a caption's width "Show in
     * Explorer" was "Show in Expl..." in a panel 350 px wide. */
    ToriRSChrome_ButtonSetFill(chrome, self->button_open, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_reveal, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_copy_path, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_copy_name, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_new_file, 1);
    /* A new quest's folder: named here, made through the documents (so it is
     * the lane's), then handed to the user's editor. */
    self->input_quest = ToriRSChrome_TextInput(chrome, self->panel, "Quest name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_quest, "letters, digits, _ : thistle makes quests/quest_thistle");
    self->button_new_quest = ToriRSChrome_Button(chrome, self->panel, "New quest");
    ToriRSChrome_SetTooltip(chrome, self->button_new_quest, "its variable, stages and script, opened in your editor");
    ToriRSChrome_ButtonSetFill(chrome, self->button_new_quest, 1);
    ToriRSChrome_Separator(chrome, self->panel);
    /* Snippets: text to paste there. The preview is a CODE view, the
     * chrome's read-only multi-line widget (as the Quest board shows a file). */
    self->dropdown_snippet = ToriRSChrome_Dropdown(chrome, self->panel, "Snippet", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_snippet, "RuneScript to paste, written for this record's name");
    self->code_snippet = ToriRSChrome_Code(chrome, self->panel, "", TRIGGERS_SNIPPET_ROWS, 0);
    assert(self->code_snippet >= 0);
    self->button_copy_snippet = ToriRSChrome_Button(chrome, self->panel, "Copy snippet");
    ToriRSChrome_SetTooltip(chrome, self->button_copy_snippet, "the text above to the clipboard: paste it in your editor");
    ToriRSChrome_ButtonSetFill(chrome, self->button_copy_snippet, 1);
    ToriRSChrome_SetDisabled(chrome, self->button_copy_snippet, 1);
    ToriRSChrome_Separator(chrome, self->panel);
    self->dropdown_kind = ToriRSChrome_Dropdown(chrome, self->panel, "Trigger", NULL, 0, -1);
    self->input_op = ToriRSChrome_TextInput(chrome, self->panel, "Op text", "");
    ToriRSChrome_SetTooltip(chrome, self->input_op, "menu text when the record has no such op (Search)");
    /* Where the trigger is written: the record's own script, or the script
     * of the quest being worked on (one New quest made, or whose file File
     * mode showed). */
    self->dropdown_write_to = ToriRSChrome_Dropdown(chrome, self->panel, "Write to", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_write_to, "the script Add trigger appends the trigger to");
    self->button_add = ToriRSChrome_Button(chrome, self->panel, "Add trigger...");
    ToriRSChrome_SetTooltip(chrome, self->button_add, "menu text if missing + the trigger in Write to; one undo");
    ToriRSChrome_Separator(chrome, self->panel);
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    ToriRSChrome_SetTooltip(chrome, self->button_refresh, "re-read the files from disk, re-index the triggers");
    self->button_reload = ToriRSChrome_Button(chrome, self->panel, "Reload from disk");
    ToriRSChrome_SetTooltip(chrome, self->button_reload, "documents edited outside take the disk's text");
    ToriRSChrome_SetHidden(chrome, self->button_reload, 1);
    self->button_check = ToriRSChrome_Button(chrome, self->panel, "Check");
    self->button_run = ToriRSChrome_Button(chrome, self->panel, "Run");
    /* Both wear a keycap (Ctrl+Shift+B, Ctrl+Enter): in the label column's
     * width the cap left "Ch..." of Check. They take the row. */
    ToriRSChrome_ButtonSetFill(chrome, self->button_check, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_run, 1);
    triggers_hints(ui, self);
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
