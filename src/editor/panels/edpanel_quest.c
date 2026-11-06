/*
 * The Quest board (module "quest", View > Quest board; workbench 6.7, design
 * section 11 "Quest board"): one quest's parts, where each lives and its
 * state -- the map of what the builders wrote, read by editor/editor_quest.h.
 * The board writes no script itself.
 *
 *   Quest          a QUEUE.tsv test_id; Open reads the board, Refresh again
 *   parts          a cells VLIST with a search box: part, where, state (the
 *                  design's figure: dbrow, var and constants, start trigger
 *                  and dialogue, each stage's trigger with its file, complete,
 *                  journal lines, the test file with its rows, shots and the
 *                  gate's verdict)
 *   Open part      the panel that edits the picked part: the NPC builder on
 *                  the start npc, Triggers on a script file, the Record panel
 *                  for a loc / npc / obj record; a CODE view below for what
 *                  has no editing panel in this build (the dbrow's block, the
 *                  constants, the test's Lua)
 *   Scaffold test  tools/quest_gate/new_quest.py as a BUILD job (refused, with
 *                  the reason, without a QUEUE row, the guides, or with a test
 *                  file already there)
 *   Run gate       run.py --no-build then gate.py as a BUILD job (refused
 *                  without the tools, the test file, the content tree's
 *                  scripts or the quest client); the job's lines stream into
 *                  the board, and when it lands the board reads again
 *   Ledger...      the ledger's rows in a VLIST with a search box; the picked
 *                  row's first screenshot as a MODELVIEW sprite when the App
 *                  decodes PNGs (EditorUiAppServices png_publish), else its path
 *   RUN            the Live panel's RUN
 *
 * TORIRS_EDITOR_QUEST=<id> (photographs): the board opens with the editor on
 * that quest.
 */
#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/builder/builder_panels.h"
#include "editor/editor_host.h"
#include "editor/editor_quest.h"
#include "editor/live/live_client.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    QUEST_PART_ROWS = 9,
    QUEST_LEDGER_ROWS = 6,
    QUEST_CODE_ROWS = 10,
    QUEST_PATH_MAX = 1024,
    QUEST_CODE_LINES_MAX = 8192,
    /** The screenshot's MODELVIEW, in 1x chrome pixels. */
    QUEST_SHOT_W = 256,
    QUEST_SHOT_H = 168,
};

/** Where tools/questhelper_extract.py looks for the guides when
 *  TORIRS_QUEST_HELPER_ROOT is unset (its DEFAULT_QH): new_quest.py's own
 *  default, so the board refuses exactly what the job would fail on. */
#define QUEST_HELPER_DEFAULT                                                                       \
    "/Users/matthewevers/Documents/git_repos/quest-helper/src/main/java/com/questhelper/helpers/quests"

struct QuestPanel
{
    int panel;
    int input_id;
    int button_open;
    int button_refresh;
    int label_title;
    int input_find;
    int list_parts;
    int button_open_part;
    int button_scaffold;
    int button_gate;
    int button_ledger;
    int button_run;
    int label_job;
    int input_find_ledger;
    int list_ledger;
    int view_shot;
    int label_shot;
    int label_code;
    int code_view;
    int label_status;

    struct Editor_QuestBoard board;
    int open;
    int filtered[EDITOR_QUEST_ROWS_MAX];
    int filtered_count;
    char shown_find[TORIRS_CHROME_INPUT_MAX];
    int ledger_filtered_count;
    int* ledger_filtered;
    char shown_find_ledger[TORIRS_CHROME_INPUT_MAX];
    int ledger_shown;
    int rows_dirty;

    char content_root[QUEST_PATH_MAX];
    char repo_root[QUEST_PATH_MAX];

    /** The BUILD job this board sent (0 none) and the build serial then. */
    uint32_t job_what;
    int job_build_serial;
    int job_log_serial;

    /** The CODE view's file and its lines (offsets into `code_text`). */
    char* code_text;
    size_t code_length;
    uint32_t* code_lines;
    int code_line_count;

    /** The ledger row whose shot is shown. */
    int shot_row;

    uint32_t hint_serial;
};

static struct QuestPanel*
quest_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "quest");
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void
quest_say(
    struct EditorUi* ui,
    struct QuestPanel* self,
    const char* format,
    ...)
{
    char text[EDITOR_UI_CONSOLE_LINE_MAX];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ToriRSChrome_SetText(ui->chrome, self->label_status, text);
    Editor_UiStatus(ui, "%s", text);
}

/* ---- the catalog, for the reader ------------------------------------------------ */

static int
quest_catalog_on(struct EditorUi* ui)
{
    return ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG);
}

static int
quest_catalog_record(
    void* user,
    uint32_t kind,
    const char* name,
    char* out_where,
    size_t where_size,
    char* out_summary,
    size_t summary_size)
{
    struct EditorUi* ui = user;
    struct EditorHost_CatalogRecord record;
    int source = -1;

    (void)summary_size;
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, -1, name, &record) != EDITOR_HOST_OK )
        return 0;
    out_where[0] = '\0';
    out_summary[0] = '\0';
    /* Where the block is written: the cache's file or an overlay, before the
     * member index that only names it. The points are the .constant's (the
     * board's title): a dbrow's `data=` columns repeat one key, which the
     * record's fields hold once, so no column is quoted from them. */
    for( int s = 0; s < record.source_count && source < 0; s++ )
    {
        if( record.sources[s].provenance == TORIRSMAPED_PROV_CACHE
            || record.sources[s].provenance == TORIRSMAPED_PROV_OVERLAY )
            source = s;
    }
    if( source < 0 && record.source_count > 0 )
        source = 0;
    if( source >= 0 )
        snprintf(out_where, where_size, "%s:%u", record.sources[source].path, record.sources[source].line);
    Editor_HostCatalogRecordFree(&record);
    return 1;
}

static int
quest_catalog_triggers(
    void* user,
    const char* dir,
    struct Editor_QuestTrigger* out,
    int max)
{
    struct EditorUi* ui = user;
    char filter[EDITOR_QUEST_PATH_MAX + 8];
    uint32_t page_index = 0;
    int count = 0;

    snprintf(filter, sizeof(filter), "file=%s", dir);
    for( ;; )
    {
        struct EditorHost_CatalogPage page;
        memset(&page, 0, sizeof(page));
        if( Editor_HostMapEdCatalogQuery(&ui->editor->host, TORIRSMAPED_CATALOG_TRIGGER, filter, 0, page_index, 0, &page)
            != EDITOR_HOST_OK )
            return count;
        for( int i = 0; i < page.row_count && count < max; i++ )
        {
            const char* file = "";
            const char* line = "0";
            for( int f = 0; f < page.rows[i].field_count; f++ )
            {
                if( strcmp(page.rows[i].fields[f].key, "file") == 0 )
                    file = page.rows[i].fields[f].value;
                else if( strcmp(page.rows[i].fields[f].key, "line") == 0 )
                    line = page.rows[i].fields[f].value;
            }
            /* The filter is a prefix; a sibling directory sharing it is not ours. */
            if( strncmp(file, dir, strlen(dir)) != 0 )
                continue;
            snprintf(out[count].header, sizeof(out[count].header), "%s", page.rows[i].name ? page.rows[i].name : "");
            snprintf(out[count].file, sizeof(out[count].file), "%s", file);
            out[count].line = atoi(line);
            count++;
        }
        page_index++;
        if( page.row_count == 0 || count >= max || page_index * page.page_size >= page.total )
        {
            Editor_HostCatalogPageFree(&page);
            break;
        }
        Editor_HostCatalogPageFree(&page);
    }
    return count;
}

/* ---- the CODE view ------------------------------------------------------------- */

/** Show `relative` (under `root`) in the CODE view at `line` (1-based). */
static int
quest_show_code(
    struct EditorUi* ui,
    struct QuestPanel* self,
    const char* root,
    const char* relative,
    int line)
{
    char path[QUEST_PATH_MAX * 2];
    FILE* f;
    long size;
    char label[TORIRS_CHROME_INPUT_MAX];

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    f = fopen(path, "rb");
    if( !f )
        return 0;
    ToriMem_Free(ToriMem_Editor(), self->code_text);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if( size < 0 )
        size = 0;
    self->code_text = ToriMem_Alloc(ToriMem_Editor(), (size_t)size + 1);
    assert(self->code_text);
    if( fread(self->code_text, 1, (size_t)size, f) != (size_t)size )
        size = 0;
    self->code_text[size] = '\0';
    fclose(f);
    self->code_length = (size_t)size;
    self->code_line_count = 0;
    for( size_t at = 0; at <= self->code_length && self->code_line_count < QUEST_CODE_LINES_MAX; )
    {
        const char* newline = memchr(self->code_text + at, '\n', self->code_length - at);
        self->code_lines[self->code_line_count++] = (uint32_t)at;
        if( !newline )
            break;
        at = (size_t)(newline - self->code_text) + 1;
        if( at == self->code_length )
            break;
    }
    snprintf(label, sizeof(label), "%s%s", relative, self->code_line_count == QUEST_CODE_LINES_MAX ? " (first 8192 lines)" : "");
    ToriRSChrome_SetText(ui->chrome, self->label_code, label);
    ToriRSChrome_SetHidden(ui->chrome, self->label_code, 0);
    ToriRSChrome_SetHidden(ui->chrome, self->code_view, 0);
    ToriRSChrome_VListSetCount(ui->chrome, self->code_view, self->code_line_count);
    ToriRSChrome_CodeSetCaret(ui->chrome, self->code_view, line > 0 ? line - 1 : 0, 0);
    return 1;
}

static void
quest_fill_code(
    struct EditorUi* ui,
    struct QuestPanel* self)
{
    int first;
    int shown;

    if( !self->code_text || !ToriRSChrome_VListWindow(ui->chrome, self->code_view, &first, &shown) )
        return;
    for( int l = first; l < first + shown && l < self->code_line_count; l++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        size_t const start = self->code_lines[l];
        size_t stop = l + 1 < self->code_line_count ? self->code_lines[l + 1] : self->code_length;
        while( stop > start && (self->code_text[stop - 1] == '\n' || self->code_text[stop - 1] == '\r') )
            stop--;
        snprintf(text, sizeof(text), "%.*s", (int)(stop - start), self->code_text + start);
        ToriRSChrome_CodeSetLine(ui->chrome, self->code_view, l, text);
    }
}

/* ---- the lists ------------------------------------------------------------------ */

static void
quest_title(
    struct EditorUi* ui,
    struct QuestPanel* self)
{
    char text[TORIRS_CHROME_INPUT_MAX];
    const struct Editor_QuestBoard* board = &self->board;
    if( !self->open )
        snprintf(text, sizeof(text), "No quest: type a QUEUE.tsv test_id, then Open");
    else if( board->points >= 0 )
        snprintf(text, sizeof(text), "Quest %s \xc2\xb7 %s \xc2\xb7 %d point%s", board->id,
            board->var[0] ? board->var : "no var", board->points, board->points == 1 ? "" : "s");
    else
        snprintf(text, sizeof(text), "Quest %s \xc2\xb7 %s", board->id, board->var[0] ? board->var : "no var");
    ToriRSChrome_SetText(ui->chrome, self->label_title, text);
}

static void
quest_fill_parts(
    struct EditorUi* ui,
    struct QuestPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
    int first;
    int shown;

    if( !find )
        find = "";
    self->filtered_count = 0;
    for( int i = 0; self->open && i < self->board.row_count; i++ )
    {
        if( !find[0] || strstr(self->board.rows[i].what, find) || strstr(self->board.rows[i].where, find)
            || strstr(Editor_QuestStateWord(self->board.rows[i].state), find) )
            self->filtered[self->filtered_count++] = i;
    }
    snprintf(self->shown_find, sizeof(self->shown_find), "%s", find);
    ToriRSChrome_VListSetCount(ui->chrome, self->list_parts, self->filtered_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_parts, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->filtered_count; r++ )
    {
        const struct Editor_QuestRow* row = &self->board.rows[self->filtered[r]];
        char text[TORIRS_CHROME_INPUT_MAX];
        /* sprite, name, dim, keys, right (TORIRS_CHROME_CELL_SEP). */
        snprintf(text, sizeof(text), TORIRS_CHROME_CELL_SEP_STR "%s" TORIRS_CHROME_CELL_SEP_STR "%s"
                                     TORIRS_CHROME_CELL_SEP_STR TORIRS_CHROME_CELL_SEP_STR "%s",
            row->what, row->where, Editor_QuestStateWord(row->state));
        ToriRSChrome_VListSetRow(ui->chrome, self->list_parts, r, text);
    }
}

static void
quest_fill_ledger(
    struct EditorUi* ui,
    struct QuestPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find_ledger);
    const struct Editor_QuestLedger* ledger = &self->board.ledger;
    int first;
    int shown;

    if( !find )
        find = "";
    ToriMem_Free(ToriMem_Editor(), self->ledger_filtered);
    self->ledger_filtered = NULL;
    self->ledger_filtered_count = 0;
    if( ledger->count > 0 )
    {
        self->ledger_filtered = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)ledger->count);
        assert(self->ledger_filtered);
    }
    for( int i = 0; self->open && i < ledger->count; i++ )
    {
        const struct Editor_QuestLedgerRow* row = &ledger->rows[i];
        if( !find[0] || strstr(row->step, find) || strstr(row->verdict, find) || strstr(row->detail, find) )
            self->ledger_filtered[self->ledger_filtered_count++] = i;
    }
    snprintf(self->shown_find_ledger, sizeof(self->shown_find_ledger), "%s", find);
    ToriRSChrome_VListSetCount(ui->chrome, self->list_ledger, self->ledger_filtered_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_ledger, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->ledger_filtered_count; r++ )
    {
        const struct Editor_QuestLedgerRow* row = &ledger->rows[self->ledger_filtered[r]];
        char text[TORIRS_CHROME_INPUT_MAX];
        snprintf(text, sizeof(text), "%d %s %s %dt%s%s  %s", row->index, row->step, row->verdict, row->ticks,
            row->shots[0] ? "  shots " : "", row->shots, row->detail);
        ToriRSChrome_VListSetRow(ui->chrome, self->list_ledger, r, text);
    }
}

/** The picked ledger row's first shot: a MODELVIEW sprite when the App
 *  decodes PNGs, else the path. */
static void
quest_show_shot(
    struct EditorUi* ui,
    struct QuestPanel* self,
    int ledger_row)
{
    const struct Editor_QuestLedgerRow* row;
    char name[256];
    char relative[EDITOR_QUEST_PATH_MAX];
    char path[QUEST_PATH_MAX * 2];
    char label[TORIRS_CHROME_INPUT_MAX];
    const char* comma;
    int scene_id = 0;
    int w = 0;
    int h = 0;

    self->shot_row = ledger_row;
    if( ledger_row < 0 || ledger_row >= self->board.ledger.count )
        return;
    row = &self->board.ledger.rows[ledger_row];
    if( !row->shots[0] )
    {
        ToriRSChrome_SetText(ui->chrome, self->label_shot, "this row took no screenshot");
        ToriRSChrome_ModelViewSet(ui->chrome, self->view_shot, 0);
        ToriRSChrome_SetHidden(ui->chrome, self->view_shot, 1);
        return;
    }
    comma = strchr(row->shots, ',');
    snprintf(name, sizeof(name), "%.*s", comma ? (int)(comma - row->shots) : (int)strlen(row->shots), row->shots);
    Editor_QuestShotPath(self->board.id, name, relative, sizeof(relative));
    snprintf(path, sizeof(path), "%s/%s", self->repo_root, relative);
    if( ui->app_services.png_publish )
        scene_id = ui->app_services.png_publish(ui->app_services.user_data, 0, path, edp_px(ui, QUEST_SHOT_W),
            edp_px(ui, QUEST_SHOT_H), &w, &h);
    if( scene_id > 0 )
    {
        ToriRSChrome_SetHidden(ui->chrome, self->view_shot, 0);
        ToriRSChrome_ModelViewSetSprite(ui->chrome, self->view_shot, scene_id, w, h);
        snprintf(label, sizeof(label), "%s", relative);
    }
    else
    {
        ToriRSChrome_ModelViewSet(ui->chrome, self->view_shot, 0);
        ToriRSChrome_SetHidden(ui->chrome, self->view_shot, 1);
        snprintf(label, sizeof(label), "%s%s", relative,
            ui->app_services.png_publish ? " (not readable)" : "");
    }
    ToriRSChrome_SetText(ui->chrome, self->label_shot, label);
}

/* ---- reading --------------------------------------------------------------------- */

/** Read quest `id` onto the board; `show` 0 leaves the panel as it is (a
 *  job that ends while the board is hidden reads it again without popping
 *  it up in a workspace that does not list it). */
static int
quest_open(
    struct EditorUi* ui,
    struct QuestPanel* self,
    const char* id,
    int show)
{
    struct Editor_QuestCatalog catalog;

    if( !Editor_QuestIdValid(id) )
    {
        quest_say(ui, self, "Open refused: '%s' is not a QUEUE.tsv test_id (a-z, 0-9 and _)", id);
        return 0;
    }
    if( !self->repo_root[0] || !self->content_root[0] )
    {
        quest_say(ui, self, "Open refused: the board knows no repository or content tree (manifest repo_root)");
        return 0;
    }
    if( show )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    catalog.user = ui;
    catalog.record = quest_catalog_on(ui) ? quest_catalog_record : NULL;
    catalog.triggers = quest_catalog_on(ui) ? quest_catalog_triggers : NULL;
    (void)Editor_QuestRead(&self->board, self->repo_root, self->content_root, id, &catalog);
    self->open = 1;
    self->rows_dirty = 1;
    self->shot_row = -1;
    ToriRSChrome_SetText(ui->chrome, self->input_id, id);
    quest_title(ui, self);
    quest_fill_parts(ui, self);
    quest_fill_ledger(ui, self);
    if( !self->board.queue_found )
        quest_say(ui, self, "Quest %s has no test/quests/QUEUE.tsv row", id);
    else
        quest_say(ui, self, "Quest %s: %d parts, the test %s", id, self->board.row_count,
            Editor_QuestStateWord(self->board.verdict));
    return 1;
}

int
Editor_Quest_Open(
    struct EditorUi* ui,
    const char* id)
{
    struct QuestPanel* self;

    assert(ui);
    assert(id);
    self = quest_self(ui);
    assert(self);
    return quest_open(ui, self, id, 1);
}

const struct Editor_QuestBoard*
Editor_Quest_Board(struct EditorUi* ui)
{
    struct QuestPanel* self;
    assert(ui);
    self = quest_self(ui);
    return self && self->open ? &self->board : NULL;
}

/* ---- the actions ------------------------------------------------------------------ */

int
Editor_Quest_OpenPart(
    struct EditorUi* ui,
    int row_index)
{
    struct QuestPanel* self;
    const struct Editor_QuestRow* row;

    assert(ui);
    self = quest_self(ui);
    assert(self);
    if( !self->open || row_index < 0 || row_index >= self->board.row_count )
    {
        quest_say(ui, self, "Open part refused: pick a part first");
        return 0;
    }
    row = &self->board.rows[row_index];
    if( row->state == EDITOR_QUEST_MISSING && row->open != EDITOR_QUEST_OPEN_RECORD )
    {
        quest_say(ui, self, "Open part refused: %s is missing; there is nothing to open", row->what);
        return 0;
    }
    switch( row->open )
    {
    case EDITOR_QUEST_OPEN_NPC_BUILDER:
        if( Editor_UiFindPanel(ui, "npcbuilder") && Builder_PanelOpenSubject(ui, "npcbuilder", row->open_name) )
        {
            quest_say(ui, self, "Opened %s in the NPC builder", row->open_name);
            return 1;
        }
        /* No builder here: the trigger's file in Triggers instead. */
        if( row->open_file[0] && Editor_UiFindPanel(ui, "triggers") )
        {
            (void)Editor_Triggers_ShowFile(ui, row->open_file);
            quest_say(ui, self, "No NPC builder here: %s's triggers are in Triggers", row->open_name);
            return 1;
        }
        break;
    case EDITOR_QUEST_OPEN_TRIGGERS:
        if( Editor_UiFindPanel(ui, "triggers") )
        {
            (void)Editor_Triggers_ShowFile(ui, row->open_file);
            quest_say(ui, self, "Triggers shows %s", row->open_file);
            return 1;
        }
        break;
    case EDITOR_QUEST_OPEN_RECORD:
        if( row->open_kind <= TORIRSMAPED_CATALOG_OBJ && Editor_UiFindPanel(ui, "record") )
            return Editor_Record_Open(ui, row->open_kind, row->open_name);
        if( row->state == EDITOR_QUEST_MISSING )
        {
            quest_say(ui, self, "Open part refused: the catalog has no %s %s",
                ToriRSMapEdCatalog_KindName(row->open_kind), row->open_name);
            return 0;
        }
        /* The Record panel edits loc, npc and obj records: the dbrow's block
         * is shown here, read-only. */
        if( row->where[0] )
        {
            char file[EDITOR_QUEST_PATH_MAX];
            const char* colon = strrchr(row->where, ':');
            snprintf(file, sizeof(file), "%.*s", colon ? (int)(colon - row->where) : (int)strlen(row->where),
                row->where);
            if( quest_show_code(ui, self, self->content_root, file, colon ? atoi(colon + 1) : 1) )
            {
                quest_say(ui, self, "The Record panel edits loc, npc and obj; %s is shown below", row->open_name);
                return 1;
            }
        }
        break;
    case EDITOR_QUEST_OPEN_VARS:
        /* No Vars panel in this build: the constants are the var's half the
         * quest owns. */
        if( self->board.constant_file[0]
            && quest_show_code(ui, self, self->content_root, self->board.constant_file, 1) )
        {
            quest_say(ui, self, "No Vars panel in this build; %s's constants are shown below", row->open_name);
            return 1;
        }
        break;
    case EDITOR_QUEST_OPEN_CODE:
        if( quest_show_code(ui, self, self->repo_root, row->open_file, 1) )
        {
            quest_say(ui, self, "%s is shown below (read-only)", row->open_file);
            return 1;
        }
        break;
    case EDITOR_QUEST_OPEN_NONE:
        break;
    }
    quest_say(ui, self, "Open part refused: no panel here edits %s", row->what);
    return 0;
}

/** Send one quest BUILD job; the Live model reports it. */
static int
quest_send_job(
    struct EditorUi* ui,
    struct QuestPanel* self,
    uint32_t what)
{
    const struct Editor_LiveModel* live = Editor_LivePanelModel(ui);
    struct ToriRSMapEdBuf payload;
    uint32_t type;
    int sent;

    if( !live )
    {
        quest_say(ui, self, "%s refused: this window has no Live panel to run jobs through",
            ToriRSMapEd_BuildWhatName(what));
        return 0;
    }
    if( live->building || live->run_step != TORIRSMAPED_RUN_STEP_IDLE || self->job_what )
    {
        quest_say(ui, self, "%s refused: a build or a RUN is in flight (see Live)", ToriRSMapEd_BuildWhatName(what));
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    type = Editor_LiveEncodeBuildQuest(&payload, what, self->board.id);
    sent = ui->editor
           && Editor_HostMapEdLiveSend(&ui->editor->host, type, payload.data ? payload.data + payload.head : NULL,
               (uint32_t)ToriRSMapEd_BufAvailable(&payload));
    ToriRSMapEd_BufFree(&payload);
    if( !sent )
    {
        quest_say(ui, self, "%s refused: the editor server's connection is gone", ToriRSMapEd_BuildWhatName(what));
        return 0;
    }
    self->job_what = what;
    self->job_build_serial = live->build_serial;
    self->job_log_serial = live->log_serial;
    ToriRSChrome_SetText(ui->chrome, self->label_job, "started...");
    quest_say(ui, self, "%s started for %s (the editor server runs it)", ToriRSMapEd_BuildWhatName(what),
        self->board.id);
    return 1;
}

static const char*
quest_helper_root(void)
{
    const char* root = getenv("TORIRS_QUEST_HELPER_ROOT");
    return root && root[0] ? root : QUEST_HELPER_DEFAULT;
}

int
Editor_Quest_Scaffold(struct EditorUi* ui)
{
    struct QuestPanel* self;
    char why[512];

    assert(ui);
    self = quest_self(ui);
    assert(self);
    if( !self->open )
    {
        quest_say(ui, self, "Scaffold test refused: open a quest first");
        return 0;
    }
    if( !Editor_QuestCanScaffold(&self->board, self->repo_root, quest_helper_root(), why, sizeof(why)) )
    {
        quest_say(ui, self, "Scaffold test refused: %s", why);
        return 0;
    }
    return quest_send_job(ui, self, TORIRSMAPED_BUILD_QUEST_SCAFFOLD);
}

int
Editor_Quest_RunGate(struct EditorUi* ui)
{
    struct QuestPanel* self;
    char why[512];

    assert(ui);
    self = quest_self(ui);
    assert(self);
    if( !self->open )
    {
        quest_say(ui, self, "Run gate refused: open a quest first");
        return 0;
    }
    if( !Editor_QuestCanGate(&self->board, self->repo_root, self->content_root, why, sizeof(why)) )
    {
        quest_say(ui, self, "Run gate refused: %s", why);
        return 0;
    }
    return quest_send_job(ui, self, TORIRSMAPED_BUILD_QUEST_GATE);
}

/* ---- the frame ------------------------------------------------------------------------ */

static void
quest_hints(
    struct EditorUi* ui,
    struct QuestPanel* self)
{
    edp_button_chord(ui, self->button_run, EDITOR_HK_FILE_RUN);
    self->hint_serial = ui->hotkeys.serial;
}

/** The job this board sent: its newest line, then its end. Run every frame,
 *  shown or not: the Live model keeps only the LAST build's what, so a
 *  board that looked only while visible missed its job's end whenever
 *  another build finished first (a Ctrl+Return in Playtest), and Scaffold
 *  and Run gate stayed refused until a restart. */
static void
quest_watch_job(
    struct EditorUi* ui,
    struct QuestPanel* self)
{
    const struct Editor_LiveModel* live;
    uint32_t what;
    char id[EDITOR_QUEST_ID_MAX];
    int seen;

    if( !self->job_what )
        return;
    live = Editor_LivePanelModel(ui);
    if( !live )
        return;
    if( live->log_serial != self->job_log_serial )
    {
        const char* line = Editor_LiveLogLine(live, 0);
        self->job_log_serial = live->log_serial;
        if( line )
            ToriRSChrome_SetText(ui->chrome, self->label_job, line);
    }
    if( live->build_serial == self->job_build_serial || live->building )
        return;
    /* One DONE since the send, for another kind: a build already queued when
     * this one was sent (the send refuses one whose lines have arrived)
     * ended first; ours is still to come. Two or more: ours is among them
     * (the server runs one build at a time), whatever the last one was. */
    seen = live->last_build_what == self->job_what;
    if( !seen && live->build_serial - self->job_build_serial < 2 )
    {
        self->job_build_serial = live->build_serial;
        return;
    }
    what = self->job_what;
    self->job_what = 0;
    /* What the job wrote (the test file, the ledger, the verdict) is on disk
     * now: read the board again, then say how it ended. */
    snprintf(id, sizeof(id), "%s", self->board.id);
    (void)quest_open(ui, self, id, 0);
    if( !seen )
        quest_say(ui, self, "%s for %s ended (another build finished after it); its lines are in Live",
            ToriRSMapEd_BuildWhatName(what), id);
    else if( live->last_build_status == EDITOR_HOST_OK )
        quest_say(ui, self, "%s for %s finished: the test is %s", ToriRSMapEd_BuildWhatName(what), id,
            Editor_QuestStateWord(self->board.verdict));
    else
        quest_say(ui, self, "%s for %s failed (%s); its lines are in Live", ToriRSMapEd_BuildWhatName(what), id,
            live->last_build_status == EDITOR_HOST_REFUSED ? "refused" : "see the log");
}

static void
quest_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct QuestPanel* self = opaque;
    const char* find;

    if( self->hint_serial != ui->hotkeys.serial )
        quest_hints(ui, self);
    quest_watch_job(ui, self);
    if( !ui->chrome->panels[self->panel].visible )
        return;

    find = ToriRSChrome_Text(ui->chrome, self->input_find);
    if( self->rows_dirty || strcmp(find ? find : "", self->shown_find) != 0 )
        quest_fill_parts(ui, self);
    else if( ToriRSChrome_VListNeedsRows(ui->chrome, self->list_parts) )
        quest_fill_parts(ui, self);
    find = ToriRSChrome_Text(ui->chrome, self->input_find_ledger);
    if( self->rows_dirty || strcmp(find ? find : "", self->shown_find_ledger) != 0
        || ToriRSChrome_VListNeedsRows(ui->chrome, self->list_ledger) )
        quest_fill_ledger(ui, self);
    if( ToriRSChrome_VListNeedsRows(ui->chrome, self->code_view) )
        quest_fill_code(ui, self);
    self->rows_dirty = 0;
    ToriRSChrome_SetDisabled(ui->chrome, self->button_open_part, !self->open);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_scaffold, !self->open || self->job_what != 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_gate, !self->open || self->job_what != 0);
}

static void
quest_ledger_toggle(
    struct EditorUi* ui,
    struct QuestPanel* self,
    int shown)
{
    self->ledger_shown = shown;
    ToriRSChrome_SetHidden(ui->chrome, self->input_find_ledger, !shown);
    ToriRSChrome_SetHidden(ui->chrome, self->list_ledger, !shown);
    ToriRSChrome_SetHidden(ui->chrome, self->label_shot, !shown);
    if( !shown )
        ToriRSChrome_SetHidden(ui->chrome, self->view_shot, 1);
}

static int
quest_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct QuestPanel* self = opaque;

    if( widget == self->button_open || widget == self->input_id || widget == self->button_refresh )
    {
        const char* id = widget == self->button_refresh && self->open ? self->board.id
                                                                       : ToriRSChrome_Text(ui->chrome, self->input_id);
        char copy[EDITOR_QUEST_ID_MAX + 1];
        snprintf(copy, sizeof(copy), "%s", id ? id : "");
        (void)Editor_Quest_Open(ui, copy);
        return 1;
    }
    if( widget == self->button_open_part )
    {
        int const r = ToriRSChrome_VListSelected(ui->chrome, self->list_parts);
        (void)Editor_Quest_OpenPart(ui, r >= 0 && r < self->filtered_count ? self->filtered[r] : -1);
        return 1;
    }
    if( widget == self->list_parts )
        return 1;
    if( widget == self->button_scaffold )
    {
        (void)Editor_Quest_Scaffold(ui);
        return 1;
    }
    if( widget == self->button_gate )
    {
        if( Editor_Quest_RunGate(ui) )
            quest_ledger_toggle(ui, self, 1);
        return 1;
    }
    if( widget == self->button_ledger )
    {
        quest_ledger_toggle(ui, self, !self->ledger_shown);
        return 1;
    }
    if( widget == self->button_run )
    {
        if( Editor_Live_Run(ui) <= 0 )
            quest_say(ui, self, "RUN refused: no Live panel, or a RUN or build is in flight (see Live)");
        return 1;
    }
    if( widget == self->list_ledger )
    {
        int const r = ToriRSChrome_VListSelected(ui->chrome, self->list_ledger);
        if( r >= 0 && r < self->ledger_filtered_count )
            quest_show_shot(ui, self, self->ledger_filtered[r]);
        return 1;
    }
    if( widget == self->input_find || widget == self->input_find_ledger || widget == self->code_view )
        return 1;
    return 0;
}

static void
quest_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct QuestPanel* self = opaque;
    const char* id = getenv("TORIRS_EDITOR_QUEST");

    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    if( visible && id && id[0] && self->repo_root[0] )
        (void)Editor_Quest_Open(ui, id);
}

static void
quest_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct QuestPanel* self = opaque;
    (void)ui;
    Editor_QuestBoardFree(&self->board);
    ToriMem_Free(ToriMem_Editor(), self->ledger_filtered);
    ToriMem_Free(ToriMem_Editor(), self->code_text);
    ToriMem_Free(ToriMem_Editor(), self->code_lines);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Quest_SetPaths(
    struct EditorUi* ui,
    const char* content_root,
    const char* repo_root)
{
    struct QuestPanel* self;

    assert(ui);
    assert(content_root);
    assert(repo_root);
    self = quest_self(ui);
    if( !self )
        return;
    snprintf(self->content_root, sizeof(self->content_root), "%s", content_root);
    snprintf(self->repo_root, sizeof(self->repo_root), "%s", repo_root);
}

void
Editor_Quest_Register(struct EditorUi* ui)
{
    struct QuestPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    if( !ui->editor )
        return;
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->code_lines = ToriMem_Calloc(ToriMem_Editor(), QUEST_CODE_LINES_MAX, sizeof(*self->code_lines));
    assert(self->code_lines);
    Editor_QuestBoardInit(&self->board);
    self->hint_serial = UINT32_MAX;
    self->shot_row = -1;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + 24), edp_px(ui, EDP_TOP_Y), edp_px(ui, 460),
        "Quest board");
    assert(self->panel >= 0);
    self->input_id = ToriRSChrome_TextInput(chrome, self->panel, "Quest", "");
    ToriRSChrome_SetTooltip(chrome, self->input_id, "a test/quests/QUEUE.tsv test_id");
    self->button_open = ToriRSChrome_Button(chrome, self->panel, "Open");
    ToriRSChrome_SetTooltip(chrome, self->button_open, "read the quest's parts from the tree");
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    ToriRSChrome_SetTooltip(chrome, self->button_refresh, "read the files and the ledger again");
    self->label_title = ToriRSChrome_Label(chrome, self->panel, "No quest: type a QUEUE.tsv test_id, then Open");
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find part", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "filters the parts by text, file or state");
    self->list_parts = ToriRSChrome_VListCells(chrome, self->panel,
        TORIRS_CHROME_CELL_SEP_STR "part" TORIRS_CHROME_CELL_SEP_STR "where" TORIRS_CHROME_CELL_SEP_STR
            TORIRS_CHROME_CELL_SEP_STR "state",
        QUEST_PART_ROWS, 0);
    assert(self->list_parts >= 0);
    self->button_open_part = ToriRSChrome_Button(chrome, self->panel, "Open part");
    ToriRSChrome_SetTooltip(chrome, self->button_open_part, "the panel that edits the picked part");
    self->button_scaffold = ToriRSChrome_Button(chrome, self->panel, "Scaffold test");
    ToriRSChrome_SetTooltip(chrome, self->button_scaffold, "new_quest.py as a BUILD job");
    self->button_gate = ToriRSChrome_Button(chrome, self->panel, "Run gate");
    ToriRSChrome_SetTooltip(chrome, self->button_gate, "run.py --no-build, then gate.py, as a BUILD job");
    self->button_ledger = ToriRSChrome_Button(chrome, self->panel, "Ledger...");
    ToriRSChrome_SetTooltip(chrome, self->button_ledger, "show or hide the last run's ledger rows");
    self->button_run = ToriRSChrome_Button(chrome, self->panel, "RUN");
    ToriRSChrome_SetTooltip(chrome, self->button_run, "The Live panel's RUN: save all, build, reload");
    self->label_job = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_find_ledger = ToriRSChrome_TextInput(chrome, self->panel, "Find row", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find_ledger, "filters the ledger by step, verdict or detail");
    self->list_ledger = ToriRSChrome_VList(chrome, self->panel, "Ledger", QUEST_LEDGER_ROWS, 0);
    assert(self->list_ledger >= 0);
    self->label_shot = ToriRSChrome_Label(chrome, self->panel, "pick a row to see its screenshot");
    self->view_shot = ToriRSChrome_ModelView(chrome, self->panel, edp_px(ui, QUEST_SHOT_W), edp_px(ui, QUEST_SHOT_H));
    assert(self->view_shot >= 0);
    self->label_code = ToriRSChrome_Label(chrome, self->panel, "");
    self->code_view = ToriRSChrome_Code(chrome, self->panel, "", QUEST_CODE_ROWS, 0);
    assert(self->code_view >= 0);
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "");
    quest_hints(ui, self);
    quest_ledger_toggle(ui, self, 0);
    ToriRSChrome_SetHidden(chrome, self->label_code, 1);
    ToriRSChrome_SetHidden(chrome, self->code_view, 1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "quest";
    desc.title = "Quest board";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = quest_tick;
    desc.activate = quest_activate;
    desc.set_visible = quest_set_visible;
    desc.destroy = quest_destroy;
    Editor_UiAddPanel(ui, &desc);
}
