/*
 * The Accounts panel (accounts_panel.h). A chrome WINDOW, hidden until the
 * View menu opens it; everything it shows is the Editor_AccountsModel the
 * transport's facts fill, and every button is one request. Which path an
 * edit takes (a cheat on the attached lane, or the save file) is the
 * server's decision; the Log shows its answer.
 */

#include "accounts_panel.h"

#include "editor/editor.h"
#include "editor/editor_host.h"
#include "editor/live/live_panel.h"
#include "editor/panels/edpanel_common.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    ACCT_TAB_POSITION = 0,
    ACCT_TAB_STATS,
    ACCT_TAB_VARS,
    ACCT_TAB_QUESTS,
    ACCT_TAB_ITEMS,
    ACCT_TABS,
};

enum
{
    ACCT_STAT_ROWS = 8,
    ACCT_FIND_ROWS = 5,
    ACCT_SAVED_ROWS = 5,
    ACCT_QUEST_ROWS = 9,
    ACCT_QUEUE_ROWS = 3,
    ACCT_ITEM_ROWS = 8,
    ACCT_LOG_ROWS = 6,
    ACCT_FIND_MAX = 40,
    /** Frames the Find text must rest before the catalog is asked. */
    ACCT_FIND_SETTLE_FRAMES = 10,
    /** Frames after an online cheat during which the Live log is echoed. */
    ACCT_ECHO_FRAMES = 240,
    ACCT_PANEL_W = 300,
    ACCT_ENV_DOCK_FRAMES = 4,
};

static const char* const k_tab_titles[ACCT_TABS] = { "Position", "Stats", "Vars", "Quests", "Items" };
static const char* const k_container_rows[3] = { "Backpack", "Worn", "Bank" };

struct AcctFind
{
    /** 1 varp, 2 varbit. */
    int kind;
    int id;
    char name[96];
};

struct AccountsPanel
{
    struct Editor_AccountsTransport transport;
    struct Editor_AccountsModel model;

    int panel;
    int label_lane;
    int dropdown_account;
    int label_status;
    int button_refresh;
    int button_use;
    int button_duplicate;
    int button_delete;
    int input_name;
    int input_password;
    int input_fixture;
    int button_new;
    int tabs;
    /* Position */
    int label_position;
    int input_x;
    int input_z;
    int input_level;
    int button_move;
    int button_to_selection;
    int button_place_tool;
    /* Stats */
    int list_stats;
    int input_stat_level;
    int input_stat_xp;
    int button_stat_level;
    int button_stat_xp;
    int button_max_all;
    int button_reset_all;
    /* Vars */
    int input_find;
    int list_find;
    int list_saved;
    int label_var;
    int input_var_value;
    int button_var_set;
    /* Quests */
    int input_quest_find;
    int list_quests;
    int button_quest_complete;
    int button_quest_mark;
    int button_quest_mark_all;
    int label_queue;
    int list_queue;
    /* Items */
    int dropdown_container;
    int list_items;
    int input_slot;
    int input_obj;
    int input_count;
    int button_item_pick;
    int button_item_set;
    int button_item_remove;
    int button_inv_clear;
    /* Log */
    int list_log;

    /** The shown account (a save file name; "" none). */
    char account[64];
    /* The dropdown's options (borrowed by the chrome). */
    char names[EDITOR_ACCOUNTS_MAX][64];
    const char* options[EDITOR_ACCOUNTS_MAX];
    int option_count;

    int shown_list_serial;
    int shown_view_serial;
    int shown_quests_serial;
    int shown_log_serial;
    int shown_status_serial;
    int shown_tab;
    int shown_container;
    int shown_pick_id;
    int log_follow;
    int list_asked;
    int quests_asked;

    /* Vars: the selected var and the find. */
    int var_kind;
    int var_id;
    char var_name[96];
    char find_text[96];
    char find_asked[96];
    int find_rest;
    struct AcctFind finds[ACCT_FIND_MAX];
    int find_count;
    /* Quests: the find filter and the shown rows (indices into the model). */
    char quest_find[64];
    int quest_rows[EDITOR_ACCOUNTS_QUESTS_MAX];
    int quest_row_count;
    /* Items: the shown rows' slots. */
    uint32_t item_slots[TORIRSMAPED_ACCT_BANK_SLOTS];
    int item_row_count;

    /* The Live log echoed after an online cheat. */
    int echo_frames;
    int echo_live_count;

    /* TORIRS_EDITOR_ACCOUNTS */
    int env_read;
    int env_active;
    int env_tab;
    char env_account[64];
    int env_dock_frames;
    int env_selected;
};

static struct AccountsPanel*
acct_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "accounts");
}

/* ---- sending ------------------------------------------------------------------ */

static int
acct_send(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    uint32_t type,
    struct ToriRSMapEdBuf* payload)
{
    if( Editor_AccountsSend(&self->transport, type, payload) )
        return 1;
    Editor_UiLog(ui, "accounts: the editor server connection is gone");
    return 0;
}

static int
acct_set(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    const char* key,
    const char* value)
{
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    if( !self->account[0] )
    {
        Editor_UiLog(ui, "accounts: choose an account first");
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    type = Editor_AccountsEncodeSet(&payload, self->account, key, value);
    return acct_send(ui, self, type, &payload);
}

static void
acct_ask_view(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    if( !self->account[0] )
        return;
    memset(&payload, 0, sizeof(payload));
    type = Editor_AccountsEncodeView(&payload, self->account);
    acct_send(ui, self, type, &payload);
}

static void
acct_ask_list(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    memset(&payload, 0, sizeof(payload));
    type = Editor_AccountsEncodeList(&payload);
    if( acct_send(ui, self, type, &payload) )
        self->list_asked = 1;
}

static void
acct_ask_quests(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    memset(&payload, 0, sizeof(payload));
    type = Editor_AccountsEncodeQuests(&payload);
    if( acct_send(ui, self, type, &payload) )
        self->quests_asked = 1;
}

/* ---- rows ------------------------------------------------------------------------- */

static void
acct_set_rows(
    struct EditorUi* ui,
    int list,
    int count,
    const char* (*row_text)(struct AccountsPanel*, int, char*, size_t),
    struct AccountsPanel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, list, count);
    if( !ToriRSChrome_VListWindow(ui->chrome, list, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < count; row++ )
    {
        char text[EDITOR_ACCOUNTS_LOG_LINE_MAX];
        ToriRSChrome_VListSetRow(ui->chrome, list, row, row_text(self, row, text, sizeof(text)));
    }
}

static const char*
acct_stat_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    const struct Editor_AccountView* view = &self->model.view;
    if( view->status != EDITOR_HOST_OK )
        snprintf(out, out_size, "%-13s", ToriRSMapEd_AcctStatName(row));
    else
        snprintf(out, out_size, "%-13s lvl %-3d xp %d", ToriRSMapEd_AcctStatName(row), view->boosted[row],
            view->xp_tenths[row] / 10);
    return out;
}

static const char*
acct_find_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    const struct AcctFind* find = &self->finds[row];
    int32_t value;
    int known = find->kind == 1 && Editor_AccountsVarp(&self->model.view, (uint32_t)find->id, &value);
    if( known )
        snprintf(out, out_size, "%s %d %s = %d", find->kind == 2 ? "varbit" : "varp", find->id, find->name, value);
    else
        snprintf(out, out_size, "%s %d %s", find->kind == 2 ? "varbit" : "varp", find->id, find->name);
    return out;
}

static const char*
acct_saved_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    const struct Editor_AccountVarp* varp = &self->model.view.varps[row];
    snprintf(out, out_size, "varp %u%s%s = %d", varp->id, varp->name[0] ? " " : "", varp->name, varp->value);
    return out;
}

static const char*
acct_quest_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    const struct Editor_AccountQuest* quest = &self->model.quests[self->quest_rows[row]];
    const char* mark = "[?]";
    for( int i = 0; i < self->model.view.quest_count; i++ )
        if( strcmp(self->model.view.quests[i].dbrow, quest->dbrow) == 0 )
        {
            mark = self->model.view.quests[i].state == TORIRSMAPED_QUEST_COMPLETE       ? "[x]"
                   : self->model.view.quests[i].state == TORIRSMAPED_QUEST_NOT_COMPLETE ? "[ ]"
                                                                                        : "[?]";
            break;
        }
    snprintf(out, out_size, "%s %s", mark, quest->display);
    return out;
}

static const char*
acct_queue_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "::%s", self->model.view.queue[row]);
    return out;
}

static const char*
acct_item_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    int const section = self->shown_container;
    uint32_t const slot = self->item_slots[row];
    const struct Editor_AccountItem* item = Editor_AccountsItem(&self->model.view, section, slot);
    if( item )
        snprintf(out, out_size, "%u: obj %d x%d", slot, item->obj, item->count);
    else
        snprintf(out, out_size, "%u: (empty)", slot);
    return out;
}

static const char*
acct_log_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    /* Row 0 is the oldest kept. */
    const char* line = Editor_AccountsLogLine(&self->model, self->model.log_count - 1 - row);
    snprintf(out, out_size, "%s", line ? line : "");
    return out;
}

/* ---- derived lists ------------------------------------------------------------------- */

/** The quest rows the Find filter keeps (case-insensitive substring of the
 *  display name or the dbrow). */
static void
acct_quest_filter(struct AccountsPanel* self)
{
    char needle[64];
    for( size_t i = 0; i <= strlen(self->quest_find) && i < sizeof(needle) - 1; i++ )
        needle[i] = (char)((self->quest_find[i] >= 'A' && self->quest_find[i] <= 'Z') ? self->quest_find[i] + 32
                                                                                      : self->quest_find[i]);
    needle[sizeof(needle) - 1] = '\0';
    self->quest_row_count = 0;
    for( int i = 0; i < self->model.quest_count; i++ )
    {
        const struct Editor_AccountQuest* quest = &self->model.quests[i];
        int keep = needle[0] == '\0';
        if( !keep )
        {
            char hay[160];
            snprintf(hay, sizeof(hay), "%s %s", quest->display, quest->dbrow);
            for( char* c = hay; *c; c++ )
                if( *c >= 'A' && *c <= 'Z' )
                    *c = (char)(*c + 32);
            keep = strstr(hay, needle) != NULL;
        }
        if( keep )
            self->quest_rows[self->quest_row_count++] = i;
    }
}

/** The item rows: every slot of the backpack / worn, the held slots of the
 *  bank. */
static void
acct_item_rows(struct AccountsPanel* self)
{
    const struct Editor_AccountView* view = &self->model.view;
    int const section = self->shown_container;
    self->item_row_count = 0;
    if( section < 2 )
    {
        int const slots = section == 0 ? TORIRSMAPED_ACCT_INV_SLOTS : TORIRSMAPED_ACCT_WORN_SLOTS;
        for( int i = 0; i < slots; i++ )
            self->item_slots[self->item_row_count++] = (uint32_t)i;
        return;
    }
    for( int i = 0; i < view->item_count && self->item_row_count < TORIRSMAPED_ACCT_BANK_SLOTS; i++ )
        if( view->items[i].section == 2 )
            self->item_slots[self->item_row_count++] = view->items[i].slot;
}

/** The catalog's varps and varbits matching the Find text. */
static void
acct_find_run(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    static const uint32_t kinds[2] = { TORIRSMAPED_CATALOG_VARP, TORIRSMAPED_CATALOG_VARBIT };

    self->find_count = 0;
    snprintf(self->find_asked, sizeof(self->find_asked), "%s", self->find_text);
    if( !self->find_text[0] )
        return;
    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        Editor_UiLog(ui, "accounts: the server has no catalog to find vars in");
        return;
    }
    for( int k = 0; k < 2 && self->find_count < ACCT_FIND_MAX; k++ )
    {
        struct EditorHost_CatalogPage page;
        memset(&page, 0, sizeof(page));
        if( Editor_HostMapEdCatalogQuery(&ui->editor->host, kinds[k], self->find_text, 0, 0, ACCT_FIND_MAX / 2, &page)
            == EDITOR_HOST_OK )
        {
            for( int i = 0; i < page.row_count && self->find_count < ACCT_FIND_MAX; i++ )
            {
                struct AcctFind* find = &self->finds[self->find_count++];
                find->kind = k == 0 ? 1 : 2;
                find->id = page.rows[i].id;
                snprintf(find->name, sizeof(find->name), "%s", page.rows[i].name ? page.rows[i].name : "");
            }
        }
        Editor_HostCatalogPageFree(&page);
    }
}

/** The selected var's detail line: its catalog record (a varbit's base
 *  varp and bits), its value in the view when known. */
static void
acct_var_detail(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    char text[TORIRS_CHROME_LABEL_MAX];
    int32_t value;

    if( self->var_kind == 0 )
    {
        ToriRSChrome_SetText(ui->chrome, self->label_var, "pick a var above (Find, or a saved varp)");
        return;
    }
    if( self->var_kind == 1 )
    {
        if( Editor_AccountsVarp(&self->model.view, (uint32_t)self->var_id, &value) )
            snprintf(text, sizeof(text), "varp %d %s = %d", self->var_id, self->var_name, value);
        else
            snprintf(text, sizeof(text), "varp %d %s = 0 (not in the save)", self->var_id, self->var_name);
    }
    else
    {
        struct EditorHost_CatalogRecord record;
        const char* basevar = "?";
        const char* start = "?";
        const char* end = "?";
        memset(&record, 0, sizeof(record));
        if( ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG)
            && Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_VARBIT, self->var_id, "", &record)
                   == EDITOR_HOST_OK )
        {
            for( int i = 0; i < record.field_count; i++ )
            {
                if( strcmp(record.fields[i].key, "basevar") == 0 )
                    basevar = record.fields[i].value;
                else if( strcmp(record.fields[i].key, "startbit") == 0 )
                    start = record.fields[i].value;
                else if( strcmp(record.fields[i].key, "endbit") == 0 )
                    end = record.fields[i].value;
            }
        }
        snprintf(text, sizeof(text), "varbit %d %s: %s bits %s..%s", self->var_id, self->var_name, basevar, start, end);
        Editor_HostCatalogRecordFree(&record);
    }
    ToriRSChrome_SetText(ui->chrome, self->label_var, text);
}

/* ---- refresh ------------------------------------------------------------------------- */

static void
acct_options(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    int selected = -1;

    self->option_count = 0;
    for( int i = 0; i < self->model.row_count && i < EDITOR_ACCOUNTS_MAX; i++ )
    {
        snprintf(self->names[i], sizeof(self->names[i]), "%s", self->model.rows[i].name);
        self->options[i] = self->names[i];
        if( strcmp(self->names[i], self->account) == 0 )
            selected = i;
        self->option_count++;
    }
    if( self->option_count == 0 )
    {
        snprintf(self->names[0], sizeof(self->names[0]), "%s",
            self->model.list_status == EDITOR_HOST_OK ? "(no accounts: New account)" : "(no lane)");
        self->options[0] = self->names[0];
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_account, self->options, 1, 0);
        return;
    }
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_account, self->options, self->option_count, selected);
}

/** The account the panel shows follows the list: still there, else the play
 *  account, else the first. */
static void
acct_follow_list(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct Editor_AccountsModel* m = &self->model;
    const char* fresh = NULL;

    if( self->account[0] && Editor_AccountsRow(m, self->account) )
        return;
    if( m->row_count == 0 )
    {
        self->account[0] = '\0';
        return;
    }
    if( Editor_AccountsRow(m, m->play_account) )
        fresh = m->play_account;
    else
        fresh = m->rows[0].name;
    snprintf(self->account, sizeof(self->account), "%s", fresh);
    acct_ask_view(ui, self);
}

static void
acct_refresh(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct Editor_AccountsModel* m = &self->model;
    const struct Editor_AccountView* view = &m->view;
    int const tab = ToriRSChrome_PanelActiveTab(ui->chrome, self->panel);
    int const container = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_container);
    int const have_view = view->status == EDITOR_HOST_OK && strcmp(view->name, self->account) == 0;
    char text[TORIRS_CHROME_LABEL_MAX];
    int list_changed = self->shown_list_serial != m->list_serial;
    int view_changed = self->shown_view_serial != m->view_serial;
    int quests_changed = self->shown_quests_serial != m->quests_serial;

    if( list_changed )
    {
        acct_follow_list(ui, self);
        acct_options(ui, self);
        if( m->list_status != EDITOR_HOST_OK )
            snprintf(text, sizeof(text), "no lane: start torirsmaped with --live-dir (or set TORIRSMAPED_LIVE_DIR)");
        else
        {
            char tip[TORIRS_CHROME_LABEL_MAX];
            snprintf(text, sizeof(text), "%d account(s); lane %s; play account %s", m->row_count,
                m->attached ? "attached" : "not attached", m->play_account);
            snprintf(tip, sizeof(tip), "%.60s/saves", m->live_dir);
            ToriRSChrome_SetTooltip(ui->chrome, self->label_lane, tip);
        }
        ToriRSChrome_SetText(ui->chrome, self->label_lane, text);
        self->shown_list_serial = m->list_serial;
    }
    if( list_changed || view_changed )
    {
        const struct Editor_AccountRow* row = self->account[0] ? Editor_AccountsRow(m, self->account) : NULL;
        int const online = row && (row->flags & TORIRSMAPED_ACCT_ONLINE);
        if( !self->account[0] )
            snprintf(text, sizeof(text), "no account chosen");
        else if( have_view )
            snprintf(text, sizeof(text), "%s%s: %s", self->account, (view->flags & TORIRSMAPED_ACCT_PLAY) ? " (play account)" : "",
                view->status_text);
        else if( view->status == EDITOR_HOST_ABSENT && strcmp(view->name, self->account) == 0 )
            snprintf(text, sizeof(text), "%s: no save file", self->account);
        else if( row )
        {
            char line[96];
            Editor_AccountsRowLine(row, line, sizeof(line));
            snprintf(text, sizeof(text), "%s: %s", self->account, line);
        }
        else
            snprintf(text, sizeof(text), "%s", self->account);
        ToriRSChrome_SetText(ui->chrome, self->label_status, text);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_delete, !self->account[0] || online);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_use, !self->account[0] || !row || !(row->flags & TORIRSMAPED_ACCT_PASSWORD));
        ToriRSChrome_SetDisabled(ui->chrome, self->button_duplicate, !self->account[0]);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_refresh, !self->account[0]);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_item_remove, online);
        ToriRSChrome_SetTooltip(ui->chrome, self->button_refresh,
            online ? "Ask the server for a checkpoint of the account (its live values)" : "Read the save again");
    }
    if( view_changed || list_changed )
    {
        if( have_view )
            snprintf(text, sizeof(text), "x %d  z %d  level %d  (%s)", view->x, view->z, view->level, view->display);
        else
            snprintf(text, sizeof(text), "no values yet");
        ToriRSChrome_SetText(ui->chrome, self->label_position, text);
        acct_set_rows(ui, self->list_stats, TORIRSMAPED_ACCT_STATS, acct_stat_row, self);
        acct_set_rows(ui, self->list_saved, have_view ? view->varp_count : 0, acct_saved_row, self);
        acct_set_rows(ui, self->list_find, self->find_count, acct_find_row, self);
        acct_var_detail(ui, self);
        snprintf(text, sizeof(text), "%d queued cheat(s) for the next login", have_view ? view->queue_count : 0);
        ToriRSChrome_SetText(ui->chrome, self->label_queue, text);
        acct_set_rows(ui, self->list_queue, have_view ? view->queue_count : 0, acct_queue_row, self);
        self->shown_view_serial = m->view_serial;
    }
    if( quests_changed || view_changed || list_changed )
    {
        acct_quest_filter(self);
        acct_set_rows(ui, self->list_quests, self->quest_row_count, acct_quest_row, self);
        self->shown_quests_serial = m->quests_serial;
    }
    if( view_changed || list_changed || container != self->shown_container )
    {
        self->shown_container = container;
        acct_item_rows(self);
        acct_set_rows(ui, self->list_items, self->item_row_count, acct_item_row, self);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_inv_clear, container != 0);
    }
    if( tab != self->shown_tab )
    {
        self->shown_tab = tab;
        if( tab == ACCT_TAB_QUESTS && !self->quests_asked )
            acct_ask_quests(ui, self);
    }
    if( self->shown_log_serial != m->log_serial )
    {
        int first;
        int shown;
        ToriRSChrome_VListSetCount(ui->chrome, self->list_log, m->log_count);
        if( self->log_follow )
            ToriRSChrome_VListSetScroll(ui->chrome, self->list_log, m->log_count - ACCT_LOG_ROWS);
        acct_set_rows(ui, self->list_log, m->log_count, acct_log_row, self);
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_log, &first, &shown) )
            self->log_follow = first + shown >= m->log_count;
        self->shown_log_serial = m->log_serial;
    }
    if( ui->tools.pick_kind == 2 && ui->tools.pick_id >= 0 && ui->tools.pick_id != self->shown_pick_id )
    {
        self->shown_pick_id = ui->tools.pick_id;
        snprintf(text, sizeof(text), "the Catalog's pick (obj %d %s) into Obj", ui->tools.pick_id, ui->tools.pick_name);
        ToriRSChrome_SetTooltip(ui->chrome, self->button_item_pick, text);
    }
}

/** Every status fact goes to the editor log too: the Place player tool's
 *  click says which path it took there. An online cheat opens the echo of
 *  the Live log's answers. */
static void
acct_note_status(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct Editor_AccountsModel* m = &self->model;
    const struct Editor_LiveModel* live;

    if( self->shown_status_serial == m->status_serial )
        return;
    self->shown_status_serial = m->status_serial;
    Editor_UiLog(ui, "accounts: %s%s%s%s", m->last_account, m->last_account[0] ? ": " : "", m->last_message,
        m->last_status == EDITOR_HOST_OK ? "" : " (refused)");
    /* An account this window just made (New account / Duplicate) becomes
     * the shown one; the list fact behind the status fills the dropdown. */
    if( m->last_status == EDITOR_HOST_OK && m->last_account[0]
        && (m->last_request == TORIRSMAPED_REQ_ACCOUNT_CREATE || m->last_request == TORIRSMAPED_REQ_ACCOUNT_DUPLICATE)
        && strcmp(self->account, m->last_account) != 0 )
    {
        snprintf(self->account, sizeof(self->account), "%s", m->last_account);
        self->var_kind = 0;
        acct_ask_view(ui, self);
        self->shown_view_serial = -1;
        self->shown_list_serial = -1;
    }
    if( strncmp(m->last_message, "online:", 7) == 0 )
    {
        live = Editor_LivePanelModel(ui);
        self->echo_frames = ACCT_ECHO_FRAMES;
        self->echo_live_count = live ? live->log_count : 0;
    }
}

/** The Live log's new lines, into this log, while a cheat is being answered. */
static void
acct_echo_live(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct Editor_LiveModel* live;

    if( self->echo_frames <= 0 )
        return;
    self->echo_frames--;
    live = Editor_LivePanelModel(ui);
    if( !live )
        return;
    while( self->echo_live_count < live->log_count )
    {
        /* Index from the newest: the oldest unseen is at log_count - 1 - seen. */
        const char* line = Editor_LiveLogLine(live, live->log_count - 1 - self->echo_live_count);
        char text[EDITOR_ACCOUNTS_LOG_LINE_MAX];
        self->echo_live_count++;
        if( !line )
            continue;
        snprintf(text, sizeof(text), "server: %s", line);
        Editor_AccountsModelNote(&self->model, text);
    }
}

/* ---- the find's settling ------------------------------------------------------------ */

static void
acct_find_tick(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const char* typed = ToriRSChrome_Text(ui->chrome, self->input_find);
    const char* quest_typed = ToriRSChrome_Text(ui->chrome, self->input_quest_find);

    if( strcmp(typed, self->find_text) != 0 )
    {
        snprintf(self->find_text, sizeof(self->find_text), "%s", typed);
        self->find_rest = 0;
    }
    else if( strcmp(self->find_text, self->find_asked) != 0 && ++self->find_rest >= ACCT_FIND_SETTLE_FRAMES )
    {
        acct_find_run(ui, self);
        acct_set_rows(ui, self->list_find, self->find_count, acct_find_row, self);
    }
    if( strcmp(quest_typed, self->quest_find) != 0 )
    {
        snprintf(self->quest_find, sizeof(self->quest_find), "%s", quest_typed);
        acct_quest_filter(self);
        acct_set_rows(ui, self->list_quests, self->quest_row_count, acct_quest_row, self);
    }
}

/* ---- TORIRS_EDITOR_ACCOUNTS ------------------------------------------------------------ */

static void
acct_env_read(struct AccountsPanel* self)
{
    const char* env = getenv("TORIRS_EDITOR_ACCOUNTS");
    char opts[192];
    char* at;

    self->env_read = 1;
    if( !env || !env[0] )
        return;
    snprintf(opts, sizeof(opts), "%s", env);
    for( at = strtok(opts, ","); at; at = strtok(NULL, ",") )
    {
        if( strncmp(at, "tab=", 4) == 0 )
            self->env_tab = atoi(at + 4);
        else if( strncmp(at, "account=", 8) == 0 )
            snprintf(self->env_account, sizeof(self->env_account), "%s", at + 8);
    }
    self->env_active = 1;
}

static void
acct_env_dock(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    int x = ui->world.x + edp_px(ui, 8);
    int y = ui->world.y + edp_px(ui, 8);
    int const w = ui->dock_w > 0 ? ui->dock_w : edp_px(ui, ACCT_PANEL_W);

    /* Over the dock column from its top: the panel is tall (a tab's rows and
     * the log), so under Terrain it would run off a small window. */
    if( ui->dock_w > 0 )
    {
        x = ui->dock_x;
        y = ui->world.y;
    }
    ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->panel, w);
    ToriRSChrome_PanelMove(ui->chrome, self->panel, x, y);
    ui->chrome->panels[self->panel].user_placed = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    if( self->env_tab >= 0 && self->env_tab < ACCT_TABS )
        ToriRSChrome_PanelSetActiveTab(ui->chrome, self->panel, self->env_tab);
}

static void
acct_env_tick(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    if( !self->env_read )
        acct_env_read(self);
    if( !self->env_active || ui->solo_panel >= 0 )
        return;
    if( self->env_dock_frames >= 0 && ui->laid_out && ++self->env_dock_frames > ACCT_ENV_DOCK_FRAMES )
    {
        acct_env_dock(ui, self);
        self->env_dock_frames = -1;
        Editor_UiLog(ui, "accounts: TORIRS_EDITOR_ACCOUNTS shows the panel%s%s", self->env_account[0] ? " on " : "",
            self->env_account);
    }
    if( !self->env_selected && self->env_account[0] && Editor_AccountsRow(&self->model, self->env_account) )
    {
        self->env_selected = 1;
        (void)Editor_Accounts_Show(ui, self->env_account);
    }
}

/* ---- the module ------------------------------------------------------------------------ */

static void
acct_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct AccountsPanel* self = opaque;

    Editor_AccountsDrain(&self->transport, &self->model, self->account);
    acct_note_status(ui, self);
    acct_env_tick(ui, self);
    if( !self->list_asked && ui->editor )
        acct_ask_list(ui, self);
    if( !ui->chrome->panels[self->panel].visible )
        return;
    acct_echo_live(ui, self);
    acct_find_tick(ui, self);
    acct_refresh(ui, self);
}

/** The selection as an absolute tile (the Live panel's rule): the first
 *  selected loc, else the latched tile, else the rectangle's centre. */
static int
acct_selection_tile(
    const struct EditorUi* ui,
    int* out_x,
    int* out_z,
    int* out_level)
{
    const struct EditorUiTools* tools = &ui->tools;

    if( tools->loc_count > 0 )
    {
        *out_x = tools->locs[0].map_x * 64 + tools->locs[0].x;
        *out_z = tools->locs[0].map_z * 64 + tools->locs[0].z;
        *out_level = tools->locs[0].level;
        return 1;
    }
    if( tools->tile_valid )
    {
        *out_x = tools->tile_x;
        *out_z = tools->tile_z;
        *out_level = tools->tile_level;
        return 1;
    }
    if( tools->rect.valid )
    {
        *out_x = (tools->rect.x0 + tools->rect.x1) / 2;
        *out_z = (tools->rect.z0 + tools->rect.z1) / 2;
        *out_level = tools->rect.level;
        return 1;
    }
    return 0;
}

static int
acct_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct AccountsPanel* self = opaque;
    struct ToriRSMapEdBuf payload;
    uint32_t type;
    char key[96];
    char value[256];

    memset(&payload, 0, sizeof(payload));
    if( widget == self->dropdown_account )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( choice >= 0 && choice < self->option_count && strcmp(self->names[choice], self->account) != 0 )
        {
            snprintf(self->account, sizeof(self->account), "%s", self->names[choice]);
            self->var_kind = 0;
            acct_ask_view(ui, self);
            self->shown_view_serial = -1;
        }
        return 1;
    }
    if( widget == self->tabs || widget == self->dropdown_container )
        return 1;
    if( widget == self->button_refresh )
    {
        const struct Editor_AccountRow* row = Editor_AccountsRow(&self->model, self->account);
        if( row && (row->flags & TORIRSMAPED_ACCT_ONLINE) )
            acct_set(ui, self, "refresh", "");
        else
            acct_ask_view(ui, self);
        acct_ask_list(ui, self);
        return 1;
    }
    if( widget == self->button_use )
    {
        if( !self->account[0] )
            return 1;
        type = Editor_AccountsEncodeUse(&payload, self->account);
        acct_send(ui, self, type, &payload);
        return 1;
    }
    if( widget == self->button_delete )
    {
        if( !self->account[0] )
            return 1;
        type = Editor_AccountsEncodeDelete(&payload, self->account);
        acct_send(ui, self, type, &payload);
        return 1;
    }
    if( widget == self->button_new || widget == self->button_duplicate )
    {
        const char* name = ToriRSChrome_Text(ui->chrome, self->input_name);
        const char* password = ToriRSChrome_Text(ui->chrome, self->input_password);
        const char* fixture = ToriRSChrome_Text(ui->chrome, self->input_fixture);
        if( !name[0] || !password[0] )
        {
            Editor_UiLog(ui, "accounts: type the new account's Name and Password first");
            return 1;
        }
        if( widget == self->button_new )
            type = Editor_AccountsEncodeCreate(&payload, name, password, fixture);
        else
        {
            if( !self->account[0] )
            {
                Editor_UiLog(ui, "accounts: choose the account to duplicate first");
                return 1;
            }
            type = Editor_AccountsEncodeDuplicate(&payload, self->account, name, password);
        }
        /* The new account is followed once the server says it exists
         * (acct_note_status): a refused name must not become the shown one. */
        acct_send(ui, self, type, &payload);
        return 1;
    }
    /* Position */
    if( widget == self->button_move )
    {
        snprintf(value, sizeof(value), "%s %s %s", ToriRSChrome_Text(ui->chrome, self->input_x),
            ToriRSChrome_Text(ui->chrome, self->input_z), ToriRSChrome_Text(ui->chrome, self->input_level));
        acct_set(ui, self, "player.pos", value);
        return 1;
    }
    if( widget == self->button_to_selection )
    {
        int x;
        int z;
        int level;
        if( !acct_selection_tile(ui, &x, &z, &level) )
        {
            Editor_UiLog(ui, "accounts: select a tile or a loc first");
            return 1;
        }
        snprintf(value, sizeof(value), "%d %d %d", x, z, level < 0 ? 0 : level);
        acct_set(ui, self, "player.pos", value);
        return 1;
    }
    if( widget == self->button_place_tool )
    {
        Editor_UiSetTool(ui, EDITOR_TOOL_PLACE_PLAYER);
        Editor_UiLog(ui, "accounts: Place player armed: click a tile to put %s there",
            self->model.play_account[0] ? self->model.play_account : "the play account");
        return 1;
    }
    /* Stats */
    if( widget == self->button_stat_level || widget == self->button_stat_xp || widget == self->input_stat_level
        || widget == self->input_stat_xp )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_stats);
        int const xp = widget == self->button_stat_xp || widget == self->input_stat_xp;
        if( row < 0 || row >= TORIRSMAPED_ACCT_STATS )
        {
            Editor_UiLog(ui, "accounts: pick a stat in the list first");
            return 1;
        }
        snprintf(key, sizeof(key), xp ? "stats.%s.xp" : "stats.%s", ToriRSMapEd_AcctStatName(row));
        acct_set(ui, self, key, ToriRSChrome_Text(ui->chrome, xp ? self->input_stat_xp : self->input_stat_level));
        return 1;
    }
    if( widget == self->button_max_all )
    {
        acct_set(ui, self, "stats.all", "max");
        return 1;
    }
    if( widget == self->button_reset_all )
    {
        acct_set(ui, self, "stats.all", "reset");
        return 1;
    }
    if( widget == self->list_stats )
        return 1;
    /* Vars */
    if( widget == self->list_find )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, widget);
        if( row >= 0 && row < self->find_count )
        {
            int32_t current;
            self->var_kind = self->finds[row].kind;
            self->var_id = self->finds[row].id;
            snprintf(self->var_name, sizeof(self->var_name), "%s", self->finds[row].name);
            if( self->var_kind == 1 && Editor_AccountsVarp(&self->model.view, (uint32_t)self->var_id, &current) )
            {
                snprintf(value, sizeof(value), "%d", current);
                ToriRSChrome_SetText(ui->chrome, self->input_var_value, value);
            }
            acct_var_detail(ui, self);
        }
        return 1;
    }
    if( widget == self->list_saved )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, widget);
        if( row >= 0 && row < self->model.view.varp_count )
        {
            self->var_kind = 1;
            self->var_id = (int)self->model.view.varps[row].id;
            snprintf(self->var_name, sizeof(self->var_name), "%s", self->model.view.varps[row].name);
            snprintf(value, sizeof(value), "%d", self->model.view.varps[row].value);
            ToriRSChrome_SetText(ui->chrome, self->input_var_value, value);
            acct_var_detail(ui, self);
        }
        return 1;
    }
    if( widget == self->button_var_set || widget == self->input_var_value )
    {
        if( self->var_kind == 0 )
        {
            Editor_UiLog(ui, "accounts: pick a var first (Find, or a saved varp)");
            return 1;
        }
        snprintf(key, sizeof(key), "%s.%d", self->var_kind == 2 ? "varbits" : "varps", self->var_id);
        acct_set(ui, self, key, ToriRSChrome_Text(ui->chrome, self->input_var_value));
        return 1;
    }
    if( widget == self->input_find || widget == self->input_quest_find )
        return 1;
    /* Quests */
    if( widget == self->button_quest_complete || widget == self->button_quest_mark )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_quests);
        if( row < 0 || row >= self->quest_row_count )
        {
            Editor_UiLog(ui, "accounts: pick a quest in the list first");
            return 1;
        }
        snprintf(key, sizeof(key), "quests.%s", self->model.quests[self->quest_rows[row]].dbrow);
        acct_set(ui, self, key, widget == self->button_quest_complete ? "complete" : "mark");
        return 1;
    }
    if( widget == self->button_quest_mark_all )
    {
        acct_set(ui, self, "quests.all", "mark");
        return 1;
    }
    if( widget == self->list_quests || widget == self->list_queue )
        return 1;
    /* Items */
    if( widget == self->list_items )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, widget);
        if( row >= 0 && row < self->item_row_count )
        {
            const struct Editor_AccountItem* item =
                Editor_AccountsItem(&self->model.view, self->shown_container, self->item_slots[row]);
            snprintf(value, sizeof(value), "%u", self->item_slots[row]);
            ToriRSChrome_SetText(ui->chrome, self->input_slot, value);
            if( item )
            {
                snprintf(value, sizeof(value), "%d", item->obj);
                ToriRSChrome_SetText(ui->chrome, self->input_obj, value);
                snprintf(value, sizeof(value), "%d", item->count);
                ToriRSChrome_SetText(ui->chrome, self->input_count, value);
            }
        }
        return 1;
    }
    if( widget == self->button_item_pick )
    {
        if( ui->tools.pick_kind != 2 || ui->tools.pick_id < 0 )
        {
            Editor_UiLog(ui, "accounts: pick an obj in the Catalog first");
            return 1;
        }
        snprintf(value, sizeof(value), "%d", ui->tools.pick_id);
        ToriRSChrome_SetText(ui->chrome, self->input_obj, value);
        return 1;
    }
    if( widget == self->button_item_set || widget == self->button_item_remove )
    {
        int const section = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_container);
        const char* slot = ToriRSChrome_Text(ui->chrome, self->input_slot);
        const char* obj = ToriRSChrome_Text(ui->chrome, self->input_obj);
        const char* count = ToriRSChrome_Text(ui->chrome, self->input_count);
        if( !slot[0] )
        {
            Editor_UiLog(ui, "accounts: type (or click) the Slot first");
            return 1;
        }
        snprintf(key, sizeof(key), "%s.%s", section == 1 ? "worn" : section == 2 ? "bank" : "inv", slot);
        if( widget == self->button_item_remove )
            acct_set(ui, self, key, "-");
        else
        {
            if( !obj[0] )
            {
                Editor_UiLog(ui, "accounts: type an obj id or name, or From Catalog pick");
                return 1;
            }
            snprintf(value, sizeof(value), "%s %s", obj, count[0] ? count : "1");
            acct_set(ui, self, key, value);
        }
        return 1;
    }
    if( widget == self->button_inv_clear )
    {
        acct_set(ui, self, "inv.clear", "");
        return 1;
    }
    if( widget == self->list_log || widget == self->input_name || widget == self->input_password
        || widget == self->input_fixture || widget == self->input_x || widget == self->input_z
        || widget == self->input_level || widget == self->input_slot || widget == self->input_obj
        || widget == self->input_count )
        return 1;
    return 0;
}

/** The editor opening shows every titled panel; this one opens from the View
 *  menu (or the photograph's env), so it starts closed. */
static void
acct_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct AccountsPanel* self = opaque;
    (void)visible;
    if( !self->env_active )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
}

static void
acct_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Accounts_RegisterWith(
    struct EditorUi* ui,
    const struct Editor_AccountsTransport* transport)
{
    struct AccountsPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    assert(transport);
    assert(transport->send);
    assert(transport->take);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->transport = *transport;
    Editor_AccountsModelInit(&self->model);
    self->shown_list_serial = -1;
    self->shown_view_serial = -1;
    self->shown_quests_serial = -1;
    self->shown_log_serial = -1;
    self->shown_status_serial = 0;
    self->shown_tab = -1;
    self->shown_container = -1;
    self->shown_pick_id = -1;
    self->log_follow = 1;
    self->env_tab = -1;

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, 40), edp_px(ui, ACCT_PANEL_W), "Accounts");
    assert(self->panel >= 0);
    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
    self->label_lane = ToriRSChrome_Label(chrome, self->panel, "asking the server for its lane...");
    self->names[0][0] = '\0';
    self->options[0] = self->names[0];
    self->dropdown_account = ToriRSChrome_Dropdown(chrome, self->panel, "Account", self->options, 0, -1);
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "no account chosen");
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    self->button_use = ToriRSChrome_Button(chrome, self->panel, "Use for RUN / Play");
    ToriRSChrome_SetTooltip(chrome, self->button_use, "RUN and Play log in as this account (needs its password)");
    self->button_duplicate = ToriRSChrome_Button(chrome, self->panel, "Duplicate");
    ToriRSChrome_SetTooltip(chrome, self->button_duplicate, "A copy of this account under Name with Password");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete");
    ToriRSChrome_SetTooltip(chrome, self->button_delete, "Its save, password record and queue (offline only)");
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    self->input_password = ToriRSChrome_TextInput(chrome, self->panel, "Password", "");
    ToriRSChrome_SetTooltip(chrome, self->input_password, "8 to 20 characters with a letter and a digit, not the name");
    self->input_fixture = ToriRSChrome_TextInput(chrome, self->panel, "Fixture", "");
    ToriRSChrome_SetTooltip(chrome, self->input_fixture, "The save a new account starts from (empty: the lane's)");
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New account");
    self->tabs = ToriRSChrome_Tabs(chrome, self->panel, k_tab_titles, ACCT_TABS, 0);
    assert(self->tabs >= 0);

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_POSITION);
    self->label_position = ToriRSChrome_Label(chrome, self->panel, "no values yet");
    self->input_x = ToriRSChrome_TextInput(chrome, self->panel, "X", "");
    self->input_z = ToriRSChrome_TextInput(chrome, self->panel, "Z", "");
    self->input_level = ToriRSChrome_TextInput(chrome, self->panel, "Level", "0");
    self->button_move = ToriRSChrome_Button(chrome, self->panel, "Move");
    ToriRSChrome_SetTooltip(chrome, self->button_move, "Online: ::tele; offline: the save's position");
    self->button_to_selection = ToriRSChrome_Button(chrome, self->panel, "To selection");
    ToriRSChrome_SetTooltip(chrome, self->button_to_selection, "The Select tool's tile or loc");
    self->button_place_tool = ToriRSChrome_Button(chrome, self->panel, "Place player tool");
    ToriRSChrome_SetTooltip(chrome, self->button_place_tool, "Click a world tile to put the PLAY account there");

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_STATS);
    self->list_stats = ToriRSChrome_VList(chrome, self->panel, "Stats", ACCT_STAT_ROWS, TORIRSMAPED_ACCT_STATS);
    self->input_stat_level = ToriRSChrome_TextInput(chrome, self->panel, "Level", "");
    self->input_stat_xp = ToriRSChrome_TextInput(chrome, self->panel, "XP", "");
    self->button_stat_level = ToriRSChrome_Button(chrome, self->panel, "Set level");
    self->button_stat_xp = ToriRSChrome_Button(chrome, self->panel, "Set XP");
    ToriRSChrome_SetTooltip(chrome, self->button_stat_xp, "Online xp only grows (::xp adds)");
    self->button_max_all = ToriRSChrome_Button(chrome, self->panel, "Max all");
    self->button_reset_all = ToriRSChrome_Button(chrome, self->panel, "Reset all");
    ToriRSChrome_SetTooltip(chrome, self->button_reset_all, "Every stat back to a fresh character's");

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_VARS);
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "A varp or varbit by name or id (the catalog)");
    self->list_find = ToriRSChrome_VList(chrome, self->panel, "Matches", ACCT_FIND_ROWS, 0);
    self->list_saved = ToriRSChrome_VList(chrome, self->panel, "Saved", ACCT_SAVED_ROWS, 0);
    self->label_var = ToriRSChrome_Label(chrome, self->panel, "pick a var above (Find, or a saved varp)");
    self->input_var_value = ToriRSChrome_TextInput(chrome, self->panel, "Value", "");
    self->button_var_set = ToriRSChrome_Button(chrome, self->panel, "Set");
    ToriRSChrome_SetTooltip(chrome, self->button_var_set, "Online: ::setvar by name; offline: the save's varp bits");

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_QUESTS);
    self->input_quest_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    self->list_quests = ToriRSChrome_VList(chrome, self->panel, "Quests", ACCT_QUEST_ROWS, 0);
    self->button_quest_complete = ToriRSChrome_Button(chrome, self->panel, "Complete");
    ToriRSChrome_SetTooltip(chrome, self->button_quest_complete, "::complete: the state and the quest points");
    self->button_quest_mark = ToriRSChrome_Button(chrome, self->panel, "Mark complete");
    ToriRSChrome_SetTooltip(chrome, self->button_quest_mark, "::questmark: the state only");
    self->button_quest_mark_all = ToriRSChrome_Button(chrome, self->panel, "Mark all complete");
    self->label_queue = ToriRSChrome_Label(chrome, self->panel, "0 queued cheat(s) for the next login");
    self->list_queue = ToriRSChrome_VList(chrome, self->panel, "Queued", ACCT_QUEUE_ROWS, 0);

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_ITEMS);
    self->dropdown_container = ToriRSChrome_Dropdown(chrome, self->panel, "Container", k_container_rows, 3, 0);
    self->list_items = ToriRSChrome_VList(chrome, self->panel, "Slots", ACCT_ITEM_ROWS, 0);
    self->input_slot = ToriRSChrome_TextInput(chrome, self->panel, "Slot", "");
    self->input_obj = ToriRSChrome_TextInput(chrome, self->panel, "Obj", "");
    ToriRSChrome_SetTooltip(chrome, self->input_obj, "An obj id or pack name");
    self->input_count = ToriRSChrome_TextInput(chrome, self->panel, "Count", "1");
    self->button_item_pick = ToriRSChrome_Button(chrome, self->panel, "From Catalog pick");
    self->button_item_set = ToriRSChrome_Button(chrome, self->panel, "Set");
    ToriRSChrome_SetTooltip(chrome, self->button_item_set, "Online: ::item to the first free slot (worn: then ::wield)");
    self->button_item_remove = ToriRSChrome_Button(chrome, self->panel, "Remove");
    self->button_inv_clear = ToriRSChrome_Button(chrome, self->panel, "Clear backpack");

    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
    self->list_log = ToriRSChrome_VList(chrome, self->panel, "Log", ACCT_LOG_ROWS, 0);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "accounts";
    desc.title = "Accounts";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = acct_tick;
    desc.activate = acct_activate;
    desc.set_visible = acct_set_visible;
    desc.destroy = acct_destroy;
    Editor_UiAddPanel(ui, &desc);
}

/* ---- over the editor host ---------------------------------------------------------------- */

static int
acct_host_send(
    void* user,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    struct Editor* editor = user;
    return Editor_HostMapEdAccountsSend(&editor->host, type, payload, length);
}

static int
acct_host_take(
    void* user,
    uint32_t* out_type,
    uint8_t** out_body,
    uint32_t* out_length)
{
    struct Editor* editor = user;
    return Editor_HostMapEdAccountsTake(&editor->host, out_type, out_body, out_length);
}

static void
acct_host_release(
    void* user,
    uint8_t* body)
{
    (void)user;
    ToriMem_Free(ToriMem_Editor(), body);
}

void
Editor_Accounts_Register(struct EditorUi* ui)
{
    struct Editor_AccountsTransport transport;

    assert(ui);
    if( !ui->editor || !ui->editor->host_open )
        return;
    transport.user = ui->editor;
    transport.send = acct_host_send;
    transport.take = acct_host_take;
    transport.release = acct_host_release;
    Editor_Accounts_RegisterWith(ui, &transport);
}

const struct Editor_AccountsModel*
Editor_AccountsPanelModel(struct EditorUi* ui)
{
    struct AccountsPanel* self;

    assert(ui);
    self = acct_self(ui);
    return self ? &self->model : NULL;
}

const char*
Editor_Accounts_Current(struct EditorUi* ui)
{
    struct AccountsPanel* self;

    assert(ui);
    self = acct_self(ui);
    return self ? self->account : "";
}

int
Editor_Accounts_Show(
    struct EditorUi* ui,
    const char* name)
{
    struct AccountsPanel* self;
    char file[64];

    assert(ui);
    assert(name);
    self = acct_self(ui);
    if( !self )
        return 0;
    ToriRSMapEd_AcctFileName(name, file, sizeof(file));
    if( !file[0] )
        return 0;
    snprintf(self->account, sizeof(self->account), "%s", file);
    self->var_kind = 0;
    acct_ask_view(ui, self);
    self->shown_view_serial = -1;
    self->shown_list_serial = -1;
    return 1;
}

int
Editor_Accounts_PlacePlayer(
    struct EditorUi* ui,
    int x,
    int z,
    int level)
{
    struct AccountsPanel* self;
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    assert(ui);
    self = acct_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "accounts: this window has no Accounts panel (no editor server)");
        return 0;
    }
    Editor_AccountsDrain(&self->transport, &self->model, self->account);
    if( self->model.list_status != EDITOR_HOST_OK || !self->model.play_account[0] )
    {
        if( !self->list_asked )
            acct_ask_list(ui, self);
        Editor_UiLog(ui, "accounts: the lane's play account is not known yet (no lane bound, or the list has not arrived)");
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    type = Editor_AccountsEncodePlace(&payload, self->model.play_account, x, z, level < 0 ? 0 : level);
    if( !acct_send(ui, self, type, &payload) )
        return 0;
    Editor_UiLog(ui, "accounts: placing %s at %d,%d level %d (%s)", self->model.play_account, x, z, level < 0 ? 0 : level,
        Editor_AccountsRow(&self->model, self->model.play_account)
                && (Editor_AccountsRow(&self->model, self->model.play_account)->flags & TORIRSMAPED_ACCT_ONLINE)
            ? "online: a ::tele"
            : "offline: the save");
    return 1;
}
