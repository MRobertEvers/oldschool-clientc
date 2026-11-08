/*
 * The Accounts panel (accounts_panel.h). A chrome WINDOW, hidden until the
 * View menu opens it; everything it shows is the Editor_AccountsModel the
 * transport's facts fill, and every button is one request. Which path an
 * edit takes (a cheat on the attached lane, or the save file) is the
 * server's decision; the Log shows its answer.
 *
 * Only the account picker and its one-line status sit above the tabs; each
 * tab is one job. The Items tab is the account builder: a container picker
 * (Equipment, Backpack, Bank, the save's other containers, any inv by name)
 * over one GRAPH well that draws the container the way the game does -- the
 * worn-equipment layout with its slot plates and silhouettes, the backpack's
 * 4 x 7, a paged grid for the bank and every other inv -- with the items'
 * own icons (EditorUiAppServices.obj_icon). A click selects a slot, whose
 * line names what it holds (the catalog's pack name, the id, the count).
 * Under it PUT HERE is an item finder: a settled catalog query over objs,
 * rows with the game's icons, narrowed to the slot's wear position while the
 * Equipment is shown (the "wear: <slot>" chip; its x widens the search), and
 * chips for the Catalog's pick. A click on a row SETS AT ONCE -- the request
 * carries `#<id> <count>`, never text the server has to resolve -- and once
 * the view lands the slot is checked against what was asked (a green "view
 * agrees" line, or a red one naming both). Remove / Clear container write.
 */

#include "accounts_panel.h"

#include "editor/editor.h"
#include "editor/editor_host.h"
#include "editor/live/live_panel.h"
#include "editor/panels/edpanel_chips.h"
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
    ACCT_TAB_ACCOUNT = 0,
    ACCT_TAB_ITEMS,
    ACCT_TAB_LOADOUTS,
    ACCT_TAB_STATS,
    ACCT_TAB_VARS,
    ACCT_TAB_QUESTS,
    ACCT_TAB_LOG,
    ACCT_TABS,
};

enum
{
    ACCT_STAT_ROWS = 10,
    ACCT_FIND_ROWS = 5,
    ACCT_SAVED_ROWS = 5,
    ACCT_QUEST_ROWS = 11,
    ACCT_QUEUE_ROWS = 3,
    ACCT_LOADOUT_ROWS = 6,
    ACCT_LOG_ROWS = 16,
    ACCT_FIND_MAX = 40,
    /** Frames the Find text must rest before the catalog is asked. */
    ACCT_FIND_SETTLE_FRAMES = 10,
    /** The Items tab's finder: rows on screen, rows held, the settle. */
    ACCT_PUT_ROWS = 6,
    ACCT_PUT_MAX = 24,
    ACCT_PUT_SETTLE_FRAMES = 10,
    /** Obj names the slot line asked the catalog for, kept per id. */
    ACCT_NAMES_MAX = 64,
    /** Frames after an online cheat during which the Live log is echoed. */
    ACCT_ECHO_FRAMES = 240,
    ACCT_PANEL_W = 360,
    ACCT_ENV_DOCK_FRAMES = 4,
    /** Frames before the play account's look is asked for again. */
    ACCT_LOOK_ASK_FRAMES = 300,
    /** Quest dbrows per `quests.list` batch (the request's value is 512). */
    ACCT_QUEST_BATCH_CHARS = 440,
};

/* ---- the wells' geometry (well-local logical px) --------------------------------- */

enum
{
    /** Every items well: tall enough for the backpack's seven rows. */
    ACCT_WELL_H = 262,
    /** The loadout preview: the worn layout alone. */
    ACCT_PREVIEW_H = 206,
    /** The worn layout's offset in its well. */
    ACCT_WORN_X = 8,
    ACCT_WORN_Y = 2,
    /** The backpack: the gameframe's 4 x 7 at 42 x 36. */
    ACCT_INV_COLS = 4,
    ACCT_INV_CELL_W = 42,
    ACCT_INV_CELL_H = 36,
    ACCT_INV_X = 8,
    ACCT_INV_Y = 4,
    /** The bank and every other inv: 8 x 7 a page at 40 x 36. */
    ACCT_GRID_COLS = 8,
    ACCT_GRID_ROWS = 7,
    ACCT_GRID_CELL_W = 40,
    ACCT_GRID_CELL_H = 36,
    ACCT_GRID_X = 4,
    ACCT_GRID_Y = 4,
    /** The cache's worn-slot plate (36 x 36) and its links. */
    ACCT_SPRITE_SLOT_PLATE = 170,
    ACCT_SPRITE_LINK_V = 172,
    ACCT_SPRITE_LINK_H = 173,
};

/** The containers the Items tab's picker offers before the save's own. */
enum
{
    ACCT_CONT_WORN = 0,
    ACCT_CONT_INV = 1,
    ACCT_CONT_BANK = 2,
    /** containers[i] of the view is ACCT_CONT_OTHER + i; past them, the
     *  inv opened by name (Open). */
    ACCT_CONT_OTHER = 3,
};

/** One worn slot of wornitems (interfaces/wornitems.if): where the game
 *  draws it, and the silhouette sprite it shows while empty. */
struct AcctWornSlot
{
    int slot;
    int x;
    int y;
    int silhouette;
    const char* name;
};

static const struct AcctWornSlot k_worn_slots[] = {
    { 0, 77, 4, 156, "head" },    { 1, 36, 43, 157, "cape" },   { 2, 77, 43, 158, "neck" },
    { 13, 118, 43, 166, "ammo" }, { 3, 21, 82, 159, "weapon" }, { 4, 77, 82, 161, "body" },
    { 5, 133, 82, 162, "shield" }, { 7, 77, 122, 163, "legs" }, { 9, 21, 162, 164, "hands" },
    { 10, 77, 162, 165, "feet" }, { 12, 133, 162, 160, "ring" },
};

/** wornitems' links between the slots (graphic 172 tiled down, 173 across). */
struct AcctWornLink
{
    int x;
    int y;
    int w;
    int h;
    int vertical;
};

static const struct AcctWornLink k_worn_links[] = {
    { 77, 39, 36, 124, 1 }, { 21, 118, 36, 45, 1 }, { 133, 118, 36, 45, 1 },
    { 56, 81, 78, 36, 0 },  { 71, 42, 48, 36, 0 },  { 118, 19, 36, 45, 1 },
};

static const char* const k_tab_titles[ACCT_TABS] = { "Account", "Items", "Loadouts", "Stats", "Vars", "Quests", "Log" };
static const char* const k_quest_filters[4] = { "All", "Not complete", "Complete", "Unknown" };

struct AcctFind
{
    /** 1 varp, 2 varbit. */
    int kind;
    int id;
    char name[96];
};

/** One row of the Items tab's finder (a catalog obj row). */
struct AcctPutRow
{
    int id;
    char name[64];
    char display[64];
    /** The obj's primary wear position, -1 none / unknown. */
    int wearpos;
};

/** An obj the catalog was asked about: its pack name ("" none) and wear
 *  position, so the slot line and the pick chip say what they hold. */
struct AcctName
{
    int id;
    char name[64];
    int wearpos;
};

struct AccountsPanel
{
    struct Editor_AccountsTransport transport;
    struct Editor_AccountsModel model;

    int panel;
    /* Above the tabs */
    int dropdown_account;
    int label_status;
    int tabs;
    /* Account */
    int label_lane;
    int button_refresh;
    int button_use;
    int button_duplicate;
    int button_delete;
    int label_position;
    int input_x;
    int input_z;
    int input_level;
    int button_move;
    int button_to_selection;
    int button_place_tool;
    int input_name;
    int input_password;
    int input_fixture;
    int button_new;
    /* Items */
    int dropdown_container;
    int input_inv;
    int button_inv_open;
    int graph_items;
    int label_page;
    int button_page_prev;
    int button_page_next;
    int label_slot;
    /** "view agrees with the last Set" / "view shows X, asked for Y". */
    int label_verify;
    int input_put;
    int input_count;
    /** wear: <slot> (x widens), Catalog pick: <name> (<id>). */
    struct Edp_ChipStrip put_strip;
    int list_put;
    int button_item_remove;
    int button_clear;
    /* Loadouts */
    int list_loadouts;
    int graph_loadout;
    int label_loadout;
    int input_loadout_name;
    int button_loadout_save;
    int button_loadout_apply;
    int button_loadout_delete;
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
    int label_quest_summary;
    int input_quest_find;
    int dropdown_quest_filter;
    int list_quests;
    int label_quest_detail;
    int button_quest_complete;
    int button_quest_mark;
    int button_quest_reset;
    int button_quest_complete_shown;
    int button_quest_mark_shown;
    int button_quest_mark_all;
    int label_queue;
    int list_queue;
    /* Log */
    int list_log;

    /** The shown account (a save file name; "" none). */
    char account[64];
    /* The account dropdown's options (borrowed by the chrome). */
    char names[EDITOR_ACCOUNTS_MAX][64];
    const char* options[EDITOR_ACCOUNTS_MAX];
    int option_count;
    /* The container dropdown's options (borrowed by the chrome). */
    char container_names[ACCT_CONT_OTHER + TORIRSMAPED_ACCT_CONTAINERS_MAX + 1][96];
    const char* container_options[ACCT_CONT_OTHER + TORIRSMAPED_ACCT_CONTAINERS_MAX + 1];
    int container_option_count;

    int shown_list_serial;
    int shown_view_serial;
    int shown_quests_serial;
    int shown_log_serial;
    int shown_status_serial;
    int shown_loadouts_serial;
    int shown_invs_serial;
    int shown_tab;
    int shown_quest_filter;
    int shown_pick_id;
    int log_follow;
    int list_asked;
    /** When Editor_Accounts_PlayPosition last asked for a fresh list. */
    int position_asked_frame;
    int quests_asked;
    int loadouts_asked;
    int invs_asked;
    int frame;

    /* Items: the container shown, its page, the selected slot. */
    int container;
    int page;
    int selected_slot;
    /** The inv opened by name (Open); -1 none. */
    int open_inv;
    char open_inv_name[64];
    int open_inv_size;
    /** The wells asked for an icon still loading: draw again next tick. */
    int items_dirty;
    int loadout_dirty;
    int selected_loadout;
    int shown_selected_loadout;
    /* Items: the finder (Put here), its chips, and the check of the last Set. */
    char put_text[96];
    char put_asked[160];
    int put_rest;
    int put_wear_filter;
    struct AcctPutRow put_rows[ACCT_PUT_MAX];
    int put_count;
    /** Rows still waiting on an icon: restated next tick. */
    int put_dirty;
    int chip_wear;
    int chip_pick;
    /** The last Set from this tab: where, what, and the view serial it was
     *  sent at; pending until a newer view says what landed. */
    int verify_pending;
    int verify_section;
    int verify_slot;
    int verify_obj;
    int verify_count;
    int verify_view_serial;
    struct AcctName obj_names[ACCT_NAMES_MAX];
    int obj_name_count;
    int obj_name_next;

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

    /* The Live log echoed after an online cheat. */
    int echo_frames;
    int echo_live_count;

    /* Place player's ghost: the play account's look asked for. */
    char look_asked[64];
    int look_asked_frame;

    /* TORIRS_EDITOR_ACCOUNTS */
    int env_read;
    int env_active;
    int env_tab;
    char env_account[64];
    char env_loadout[64];
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
acct_ask_view_of(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    const char* name)
{
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    if( !name[0] )
        return;
    memset(&payload, 0, sizeof(payload));
    type = Editor_AccountsEncodeView(&payload, name);
    acct_send(ui, self, type, &payload);
}

static void
acct_ask_view(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    acct_ask_view_of(ui, self, self->account);
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
    if( !self->loadouts_asked )
    {
        memset(&payload, 0, sizeof(payload));
        type = Editor_AccountsEncodeLoadouts(&payload);
        if( acct_send(ui, self, type, &payload) )
            self->loadouts_asked = 1;
    }
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

static void
acct_ask_invs(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    memset(&payload, 0, sizeof(payload));
    type = Editor_AccountsEncodeInvs(&payload);
    if( acct_send(ui, self, type, &payload) )
        self->invs_asked = 1;
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

/** The view's state of quest `dbrow` (TORIRSMAPED_QUEST_*) and its value. */
static uint32_t
acct_quest_state(
    const struct AccountsPanel* self,
    const char* dbrow,
    int32_t* out_value)
{
    *out_value = 0;
    for( int i = 0; i < self->model.view.quest_count; i++ )
        if( strcmp(self->model.view.quests[i].dbrow, dbrow) == 0 )
        {
            *out_value = self->model.view.quests[i].value;
            return self->model.view.quests[i].state;
        }
    return TORIRSMAPED_QUEST_UNKNOWN;
}

static const char*
acct_quest_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    const struct Editor_AccountQuest* quest = &self->model.quests[self->quest_rows[row]];
    int32_t value;
    uint32_t const state = acct_quest_state(self, quest->dbrow, &value);
    const char* mark = state == TORIRSMAPED_QUEST_COMPLETE       ? "[x]"
                       : state == TORIRSMAPED_QUEST_NOT_COMPLETE ? "[ ]"
                                                                 : "[?]";
    if( state != TORIRSMAPED_QUEST_UNKNOWN && quest->complete_value > 0 && state != TORIRSMAPED_QUEST_COMPLETE
        && value > 0 )
        snprintf(out, out_size, "%s %s  (%d/%d)", mark, quest->display, value, quest->complete_value);
    else
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
acct_loadout_row(
    struct AccountsPanel* self,
    int row,
    char* out,
    size_t out_size)
{
    const struct Editor_AccountLoadout* loadout = &self->model.loadouts[row];
    int worn = 0;
    int inv = 0;
    for( int i = 0; i < loadout->item_count; i++ )
    {
        worn += loadout->items[i].section == EDITOR_ACCOUNTS_SECTION_WORN;
        inv += loadout->items[i].section == EDITOR_ACCOUNTS_SECTION_INV;
    }
    snprintf(out, out_size, "%s  (%d worn, %d in the backpack)", loadout->name, worn, inv);
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

/* ---- quests ---------------------------------------------------------------------- */

/** The quest rows the Find text and the Show filter keep (case-insensitive
 *  substring of the display name or the dbrow). */
static void
acct_quest_filter(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    int const filter = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_quest_filter);
    char needle[64];
    for( size_t i = 0; i <= strlen(self->quest_find) && i < sizeof(needle) - 1; i++ )
        needle[i] = (char)((self->quest_find[i] >= 'A' && self->quest_find[i] <= 'Z') ? self->quest_find[i] + 32
                                                                                      : self->quest_find[i]);
    needle[sizeof(needle) - 1] = '\0';
    self->quest_row_count = 0;
    for( int i = 0; i < self->model.quest_count; i++ )
    {
        const struct Editor_AccountQuest* quest = &self->model.quests[i];
        int32_t value;
        uint32_t const state = acct_quest_state(self, quest->dbrow, &value);
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
        if( keep && filter == 1 )
            keep = state == TORIRSMAPED_QUEST_NOT_COMPLETE;
        else if( keep && filter == 2 )
            keep = state == TORIRSMAPED_QUEST_COMPLETE;
        else if( keep && filter == 3 )
            keep = state == TORIRSMAPED_QUEST_UNKNOWN;
        if( keep )
            self->quest_rows[self->quest_row_count++] = i;
    }
}

/** The counts over every row, and the selected row's detail. */
static void
acct_quest_labels(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    char text[TORIRS_CHROME_LABEL_MAX];
    int complete = 0;
    int open = 0;
    int unknown = 0;
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_quests);

    for( int i = 0; i < self->model.quest_count; i++ )
    {
        int32_t value;
        uint32_t const state = acct_quest_state(self, self->model.quests[i].dbrow, &value);
        complete += state == TORIRSMAPED_QUEST_COMPLETE;
        open += state == TORIRSMAPED_QUEST_NOT_COMPLETE;
        unknown += state == TORIRSMAPED_QUEST_UNKNOWN;
    }
    snprintf(text, sizeof(text), "%d complete, %d not, %d unknown of %d; %d shown", complete, open, unknown,
        self->model.quest_count, self->quest_row_count);
    ToriRSChrome_SetText(ui->chrome, self->label_quest_summary, text);
    if( row >= 0 && row < self->quest_row_count )
    {
        const struct Editor_AccountQuest* quest = &self->model.quests[self->quest_rows[row]];
        int32_t value;
        uint32_t const state = acct_quest_state(self, quest->dbrow, &value);
        if( strcmp(quest->var, "proc") == 0 || !quest->var[0] )
            snprintf(text, sizeof(text), "%s: its state goes through a proc (cannot be read or reset)", quest->dbrow);
        else
            snprintf(text, sizeof(text), "%s: %s = %d, complete at %d%s", quest->dbrow, quest->var, value,
                quest->complete_value, state == TORIRSMAPED_QUEST_COMPLETE ? " (complete)" : "");
    }
    else
        snprintf(text, sizeof(text), "pick a quest for its var and value");
    ToriRSChrome_SetText(ui->chrome, self->label_quest_detail, text);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_quest_reset, row < 0 || row >= self->quest_row_count);
}

static void
acct_quest_refresh(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    acct_quest_filter(ui, self);
    acct_set_rows(ui, self->list_quests, self->quest_row_count, acct_quest_row, self);
    acct_quest_labels(ui, self);
}

/** `quests.list <verb> <dbrow>...` for the shown rows, in batches. */
static void
acct_quest_batch(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    const char* verb)
{
    char value[512];
    int sent = 0;
    int in_batch = 0;

    snprintf(value, sizeof(value), "%s", verb);
    for( int i = 0; i < self->quest_row_count; i++ )
    {
        const struct Editor_AccountQuest* quest = &self->model.quests[self->quest_rows[i]];
        int32_t v;
        if( acct_quest_state(self, quest->dbrow, &v) == TORIRSMAPED_QUEST_COMPLETE )
            continue;
        if( strlen(value) + strlen(quest->dbrow) + 1 > ACCT_QUEST_BATCH_CHARS )
        {
            acct_set(ui, self, "quests.list", value);
            snprintf(value, sizeof(value), "%s", verb);
            in_batch = 0;
        }
        snprintf(value + strlen(value), sizeof(value) - strlen(value), " %s", quest->dbrow);
        in_batch++;
        sent++;
    }
    if( in_batch )
        acct_set(ui, self, "quests.list", value);
    if( !sent )
        Editor_UiLog(ui, "accounts: every shown quest is complete already");
}

/* ---- items: the containers -------------------------------------------------------- */

/** The view's containers[i] is picker entry ACCT_CONT_OTHER + i; the inv
 *  opened by name follows them. */
static int
acct_container_is_open_inv(const struct AccountsPanel* self)
{
    return self->open_inv >= 0 && self->container == ACCT_CONT_OTHER + self->model.view.container_count;
}

/** The shown container's item section (EDITOR_ACCOUNTS_SECTION_*), -1 for
 *  the opened inv the save does not hold yet. */
static int
acct_container_section(const struct AccountsPanel* self)
{
    if( self->container == ACCT_CONT_WORN )
        return EDITOR_ACCOUNTS_SECTION_WORN;
    if( self->container == ACCT_CONT_INV )
        return EDITOR_ACCOUNTS_SECTION_INV;
    if( self->container == ACCT_CONT_BANK )
        return EDITOR_ACCOUNTS_SECTION_BANK;
    if( self->container - ACCT_CONT_OTHER < self->model.view.container_count )
        return EDITOR_ACCOUNTS_SECTION_CONTAINER + (self->container - ACCT_CONT_OTHER);
    return -1;
}

/** The shown container's inv id (a generic one), -1 for the spelled three. */
static int
acct_container_inv(const struct AccountsPanel* self)
{
    int const i = self->container - ACCT_CONT_OTHER;
    if( i < 0 )
        return -1;
    if( i < self->model.view.container_count )
        return (int)self->model.view.containers[i].inv;
    return acct_container_is_open_inv(self) ? self->open_inv : -1;
}

/** The shown container's slots: its size, else one row past its last item. */
static int
acct_container_slots(const struct AccountsPanel* self)
{
    int const section = acct_container_section(self);
    int last = -1;

    if( self->container == ACCT_CONT_WORN )
        return TORIRSMAPED_ACCT_WORN_SLOTS;
    if( self->container == ACCT_CONT_INV )
        return TORIRSMAPED_ACCT_INV_SLOTS;
    if( self->container == ACCT_CONT_BANK )
    {
        const struct Editor_AccountInv* bank = Editor_AccountsInv(&self->model, 95);
        int const size = bank && bank->size > 0 ? (int)bank->size : TORIRSMAPED_ACCT_BANK_SLOTS;
        return size < TORIRSMAPED_ACCT_BANK_SLOTS ? size : TORIRSMAPED_ACCT_BANK_SLOTS;
    }
    if( acct_container_is_open_inv(self) && self->open_inv_size > 0 )
        return self->open_inv_size;
    if( section >= EDITOR_ACCOUNTS_SECTION_CONTAINER )
    {
        const struct Editor_AccountContainer* c = &self->model.view.containers[section - EDITOR_ACCOUNTS_SECTION_CONTAINER];
        if( c->size > 0 )
            return (int)c->size;
    }
    for( int i = 0; section >= 0 && i < self->model.view.item_count; i++ )
        if( self->model.view.items[i].section == section && (int)self->model.view.items[i].slot > last )
            last = (int)self->model.view.items[i].slot;
    return ((last + 1 + ACCT_GRID_COLS) / ACCT_GRID_COLS) * ACCT_GRID_COLS;
}

/** The SET key of `slot` (or "clear") in the shown container. */
static void
acct_container_key(
    const struct AccountsPanel* self,
    const char* field,
    char* out,
    size_t out_size)
{
    int const inv = acct_container_inv(self);
    if( inv >= 0 )
        snprintf(out, out_size, "container.%d.%s", inv, field);
    else
        snprintf(out, out_size, "%s.%s",
            self->container == ACCT_CONT_WORN ? "worn" : self->container == ACCT_CONT_BANK ? "bank" : "inv", field);
}

static const char*
acct_worn_slot_name(int slot)
{
    for( size_t i = 0; i < sizeof(k_worn_slots) / sizeof(k_worn_slots[0]); i++ )
        if( k_worn_slots[i].slot == slot )
            return k_worn_slots[i].name;
    return "slot";
}

/** The picker's entries: the three, the save's containers, the opened inv. */
static void
acct_container_options(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct Editor_AccountView* view = &self->model.view;
    int n = 0;

    snprintf(self->container_names[n++], sizeof(self->container_names[0]), "Equipment");
    snprintf(self->container_names[n++], sizeof(self->container_names[0]), "Backpack");
    snprintf(self->container_names[n++], sizeof(self->container_names[0]), "Bank");
    for( int i = 0; i < view->container_count; i++ )
        snprintf(self->container_names[n++], sizeof(self->container_names[0]), "%s (inv %u)",
            view->containers[i].name[0] ? view->containers[i].name : "inv", view->containers[i].inv);
    if( self->open_inv >= 0 )
    {
        int already = 0;
        for( int i = 0; i < view->container_count; i++ )
            already |= (int)view->containers[i].inv == self->open_inv;
        if( already )
            self->open_inv = -1;
        else
            snprintf(self->container_names[n++], sizeof(self->container_names[0]), "%s (inv %d, new)",
                self->open_inv_name, self->open_inv);
    }
    for( int i = 0; i < n; i++ )
        self->container_options[i] = self->container_names[i];
    self->container_option_count = n;
    if( self->container >= n )
        self->container = ACCT_CONT_INV;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_container, self->container_options, n, self->container);
}

/* ---- items: the wells ---------------------------------------------------------------- */

/** One item into the box (x, y) .. +32: its icon, or its id while the icon
 *  is loading or the host draws none. 1 while still loading. */
static int
acct_draw_item(
    struct EditorUi* ui,
    int well,
    int x,
    int y,
    int obj,
    int count)
{
    const struct EditorUiAppServices* services = &ui->app_services;
    int scene_id = 0;

    if( services->obj_icon )
        scene_id = services->obj_icon(services->user_data, obj, count, NULL, 0);
    if( scene_id > 0 )
    {
        ToriRSChrome_GraphImage(ui->chrome, well, x, y, 32, 32, scene_id, 0, 1);
        /* The stack the way an inventory prints it: the number in yellow,
         * from 100,000 thousands in white, from 10,000,000 millions in
         * green. */
        if( count > 1 )
        {
            char text[16];
            uint32_t colour = 0xFFFFFF00u;
            if( count >= 10000000 )
            {
                snprintf(text, sizeof(text), "%dM", count / 1000000);
                colour = 0xFF00FF80u;
            }
            else if( count >= 100000 )
            {
                snprintf(text, sizeof(text), "%dK", count / 1000);
                colour = 0xFFFFFFFFu;
            }
            else
                snprintf(text, sizeof(text), "%d", count);
            ToriRSChrome_GraphText(ui->chrome, well, x + 1, y + 11, 0xFF000000u, text);
            ToriRSChrome_GraphText(ui->chrome, well, x, y + 10, colour, text);
        }
        return 0;
    }
    {
        char text[24];
        snprintf(text, sizeof(text), "%d", obj);
        ToriRSChrome_GraphText(ui->chrome, well, x + 1, y + 14, 0xFFFFFF00u, text);
        if( count > 1 )
        {
            snprintf(text, sizeof(text), "x%d", count);
            ToriRSChrome_GraphText(ui->chrome, well, x + 1, y + 28, 0xFFFFFFFFu, text);
        }
    }
    return scene_id == 0 && services->obj_icon;
}

/** A cache sprite into (x, y, w, h); 1 while it loads. A host without the
 *  service draws `fallback` filled instead. */
static int
acct_draw_sprite(
    struct EditorUi* ui,
    int well,
    int archive,
    int x,
    int y,
    int w,
    int h,
    uint32_t fallback,
    int trans)
{
    const struct EditorUiAppServices* services = &ui->app_services;
    int const scene_id = services->cache_sprite ? services->cache_sprite(services->user_data, archive) : -1;

    if( scene_id > 0 )
    {
        ToriRSChrome_GraphImage(ui->chrome, well, x, y, w, h, scene_id, trans, 1);
        return 0;
    }
    if( fallback )
        ToriRSChrome_GraphRect(ui->chrome, well, x, y, w, h, fallback, 1, 0);
    return scene_id == 0;
}

/**
 * The worn layout (interfaces/wornitems.if): the links, then each slot's
 * plate with its item or, empty, its silhouette. `worn` is the obj at each
 * worn slot (-1 empty), `counts` its stack (NULL: 1). Keys are 1 + slot when
 * `keyed`. 1 while anything is still loading.
 */
static int
acct_draw_worn(
    struct EditorUi* ui,
    int well,
    const int32_t worn[TORIRSMAPED_ACCT_WORN_SLOTS],
    const int32_t* counts,
    int selected,
    int keyed)
{
    int pending = 0;

    for( size_t i = 0; i < sizeof(k_worn_links) / sizeof(k_worn_links[0]); i++ )
    {
        const struct AcctWornLink* link = &k_worn_links[i];
        int const step = link->vertical ? 45 : 36;
        int const extent = link->vertical ? link->h : link->w;
        for( int at = 0; at < extent; at += step )
        {
            int const piece = extent - at < step ? extent - at : step;
            if( link->vertical )
                pending |= acct_draw_sprite(ui, well, ACCT_SPRITE_LINK_V, ACCT_WORN_X + link->x, ACCT_WORN_Y + link->y + at,
                    link->w, piece, 0xFF3A3227u, 0);
            else
                pending |= acct_draw_sprite(ui, well, ACCT_SPRITE_LINK_H, ACCT_WORN_X + link->x + at, ACCT_WORN_Y + link->y,
                    piece, link->h, 0xFF3A3227u, 0);
        }
    }
    for( size_t i = 0; i < sizeof(k_worn_slots) / sizeof(k_worn_slots[0]); i++ )
    {
        const struct AcctWornSlot* s = &k_worn_slots[i];
        int const x = ACCT_WORN_X + s->x;
        int const y = ACCT_WORN_Y + s->y;
        int const obj = worn[s->slot];
        pending |= acct_draw_sprite(ui, well, ACCT_SPRITE_SLOT_PLATE, x, y, 36, 36, 0xFF4A4034u, 0);
        if( obj >= 0 )
            pending |= acct_draw_item(ui, well, x + 2, y + 2, obj, counts ? counts[s->slot] : 1);
        else
            pending |= acct_draw_sprite(ui, well, s->silhouette, x + 2, y + 2, 32, 32, 0, 0);
        if( keyed )
            ToriRSChrome_GraphRect(ui->chrome, well, x, y, 36, 36, 0x00000000u, 0, 1 + s->slot);
        if( s->slot == selected )
            ToriRSChrome_GraphRect(ui->chrome, well, x, y, 36, 36, 0xFFFFFF00u, 0, 0);
    }
    return pending;
}

/** Redraw the Items well from the view. */
static void
acct_draw_items(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct Editor_AccountView* view = &self->model.view;
    int const well = self->graph_items;
    int const section = acct_container_section(self);
    int const slots = acct_container_slots(self);
    int pending = 0;
    char text[TORIRS_CHROME_LABEL_MAX];

    ToriRSChrome_GraphBegin(ui->chrome, well);
    {
        int const paged = self->container != ACCT_CONT_WORN && self->container != ACCT_CONT_INV;
        ToriRSChrome_SetHidden(ui->chrome, self->label_page, !paged);
        ToriRSChrome_SetHidden(ui->chrome, self->button_page_prev, !paged);
        ToriRSChrome_SetHidden(ui->chrome, self->button_page_next, !paged);
    }
    if( view->status != EDITOR_HOST_OK || strcmp(view->name, self->account) != 0 )
    {
        ToriRSChrome_GraphText(ui->chrome, well, 8, 20, 0xFFC0C0C0u, "no account values yet");
        ToriRSChrome_GraphEnd(ui->chrome, well);
        self->items_dirty = 0;
        return;
    }
    if( self->container == ACCT_CONT_WORN )
    {
        int32_t worn[TORIRSMAPED_ACCT_WORN_SLOTS];
        int32_t counts[TORIRSMAPED_ACCT_WORN_SLOTS];
        for( int i = 0; i < TORIRSMAPED_ACCT_WORN_SLOTS; i++ )
        {
            worn[i] = -1;
            counts[i] = 1;
        }
        for( int i = 0; i < view->item_count; i++ )
            if( view->items[i].section == EDITOR_ACCOUNTS_SECTION_WORN && view->items[i].slot < TORIRSMAPED_ACCT_WORN_SLOTS )
            {
                worn[view->items[i].slot] = view->items[i].obj;
                counts[view->items[i].slot] = view->items[i].count;
            }
        pending = acct_draw_worn(ui, well, worn, counts, self->selected_slot, 1);
    }
    else
    {
        int const backpack = self->container == ACCT_CONT_INV;
        int const cols = backpack ? ACCT_INV_COLS : ACCT_GRID_COLS;
        int const cell_w = backpack ? ACCT_INV_CELL_W : ACCT_GRID_CELL_W;
        int const cell_h = backpack ? ACCT_INV_CELL_H : ACCT_GRID_CELL_H;
        int const x0 = backpack ? ACCT_INV_X : ACCT_GRID_X;
        int const y0 = backpack ? ACCT_INV_Y : ACCT_GRID_Y;
        int const per_page = backpack ? TORIRSMAPED_ACCT_INV_SLOTS : ACCT_GRID_COLS * ACCT_GRID_ROWS;
        int const pages = (slots + per_page - 1) / per_page > 0 ? (slots + per_page - 1) / per_page : 1;
        int first;
        if( self->page >= pages )
            self->page = pages - 1;
        if( self->page < 0 )
            self->page = 0;
        first = self->page * per_page;
        for( int cell = 0; cell < per_page && first + cell < slots; cell++ )
        {
            int const slot = first + cell;
            int const x = x0 + (cell % cols) * cell_w;
            int const y = y0 + (cell / cols) * cell_h;
            const struct Editor_AccountItem* item = section >= 0 ? Editor_AccountsItem(view, section, (uint32_t)slot) : NULL;
            ToriRSChrome_GraphRect(ui->chrome, well, x, y, cell_w - 2, cell_h - 2,
                backpack ? 0x00000000u : 0xFF3A3227u, !backpack, 0);
            if( item )
                pending |= acct_draw_item(ui, well, x + (cell_w - 2 - 32) / 2, y + (cell_h - 2 - 32) / 2, item->obj,
                    item->count);
            ToriRSChrome_GraphRect(ui->chrome, well, x, y, cell_w - 2, cell_h - 2,
                slot == self->selected_slot ? 0xFFFFFF00u : (backpack ? 0xFF5A4E3Eu : 0x00000000u), 0, 1 + slot);
        }
        snprintf(text, sizeof(text), "slots %d..%d of %d (page %d of %d)", first, first + per_page < slots ? first + per_page - 1 : slots - 1,
            slots, self->page + 1, pages);
        ToriRSChrome_SetText(ui->chrome, self->label_page, text);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_page_prev, self->page <= 0);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_page_next, self->page + 1 >= pages);
    }
    ToriRSChrome_GraphEnd(ui->chrome, well);
    self->items_dirty = pending;
}

/** The catalog's pack name and wear position of obj `id`, asked once per id
 *  and kept (a miss is kept too: "" and -1). Falls back to the cache's
 *  display name through obj_icon when the catalog has none. */
static const struct AcctName*
acct_obj_name(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    int id)
{
    const struct EditorUiAppServices* services = &ui->app_services;
    struct AcctName* slot;
    char text[16];

    for( int i = 0; i < self->obj_name_count; i++ )
        if( self->obj_names[i].id == id )
            return &self->obj_names[i];
    if( self->obj_name_count < ACCT_NAMES_MAX )
        slot = &self->obj_names[self->obj_name_count++];
    else
    {
        slot = &self->obj_names[self->obj_name_next];
        self->obj_name_next = (self->obj_name_next + 1) % ACCT_NAMES_MAX;
    }
    slot->id = id;
    slot->name[0] = '\0';
    slot->wearpos = -1;
    snprintf(text, sizeof(text), "%d", id);
    if( ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        struct EditorHost_CatalogPage page;
        memset(&page, 0, sizeof(page));
        if( Editor_HostMapEdCatalogQuery(&ui->editor->host, TORIRSMAPED_CATALOG_OBJ, text, TORIRSMAPED_CATALOG_EXACT, 0, 1, &page)
                == EDITOR_HOST_OK
            && page.row_count > 0 && page.rows[0].id == id )
        {
            snprintf(slot->name, sizeof(slot->name), "%s", page.rows[0].name ? page.rows[0].name : "");
            for( int f = 0; f < page.rows[0].field_count; f++ )
                if( strcmp(page.rows[0].fields[f].key, "wearpos") == 0 )
                {
                    char* end = NULL;
                    long value = strtol(page.rows[0].fields[f].value, &end, 10);
                    if( end && end != page.rows[0].fields[f].value && *end == '\0' && value >= 0
                        && value < TORIRSMAPED_ACCT_WORN_SLOTS )
                        slot->wearpos = (int)value;
                }
        }
        Editor_HostCatalogPageFree(&page);
    }
    if( !slot->name[0] && services->obj_icon )
        (void)services->obj_icon(services->user_data, id, 1, slot->name, (int)sizeof(slot->name));
    return slot;
}

/** "abyssal_whip (4151)" / "obj 4151". */
static void
acct_obj_label(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    int id,
    char* out,
    size_t out_size)
{
    const struct AcctName* name = acct_obj_name(ui, self, id);
    if( name->name[0] )
        snprintf(out, out_size, "%s (%d)", name->name, id);
    else
        snprintf(out, out_size, "obj %d", id);
}

/** The selected slot's line: where it is, what is in it (the catalog's
 *  pack name, the id, the count). */
static void
acct_slot_label(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    int const section = acct_container_section(self);
    const struct Editor_AccountItem* item =
        section >= 0 && self->selected_slot >= 0
            ? Editor_AccountsItem(&self->model.view, section, (uint32_t)self->selected_slot)
            : NULL;
    char where[48];
    char what[96];
    char text[TORIRS_CHROME_LABEL_MAX];

    if( self->selected_slot < 0 )
    {
        ToriRSChrome_SetText(ui->chrome, self->label_slot, "click a slot");
        return;
    }
    if( self->container == ACCT_CONT_WORN )
        snprintf(where, sizeof(where), "%s (slot %d)", acct_worn_slot_name(self->selected_slot), self->selected_slot);
    else
        snprintf(where, sizeof(where), "slot %d", self->selected_slot);
    if( item )
    {
        acct_obj_label(ui, self, item->obj, what, sizeof(what));
        snprintf(text, sizeof(text), "%s: %s x%d", where, what, item->count);
    }
    else
        snprintf(text, sizeof(text), "%s: empty", where);
    ToriRSChrome_SetText(ui->chrome, self->label_slot, text);
}

/** Select `slot` of the shown container: the slot line and the finder's
 *  wear chip follow it; Count takes the slot's stack. */
static void
acct_select_slot(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    int slot)
{
    int const section = acct_container_section(self);
    const struct Editor_AccountItem* item =
        section >= 0 ? Editor_AccountsItem(&self->model.view, section, (uint32_t)slot) : NULL;
    char value[32];

    self->selected_slot = slot;
    if( item )
    {
        snprintf(value, sizeof(value), "%d", item->count);
        ToriRSChrome_SetText(ui->chrome, self->input_count, value);
    }
    acct_slot_label(ui, self);
    ToriRSChrome_SetText(ui->chrome, self->label_verify, "");
    self->put_asked[0] = '\0';
    self->items_dirty = 1;
}

/** The selected loadout's worn layout and its line. */
static void
acct_draw_loadout(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    int const well = self->graph_loadout;
    int const row = self->selected_loadout;
    char text[TORIRS_CHROME_LABEL_MAX];

    ToriRSChrome_GraphBegin(ui->chrome, well);
    if( row < 0 || row >= self->model.loadout_count )
    {
        ToriRSChrome_GraphText(ui->chrome, well, 8, 20, 0xFFC0C0C0u, "pick a loadout to see its gear");
        ToriRSChrome_GraphEnd(ui->chrome, well);
        ToriRSChrome_SetText(ui->chrome, self->label_loadout, "Save current gear keeps this account's worn items + backpack");
        self->loadout_dirty = 0;
        return;
    }
    {
        const struct Editor_AccountLoadout* loadout = &self->model.loadouts[row];
        int32_t worn[TORIRSMAPED_ACCT_WORN_SLOTS];
        int32_t counts[TORIRSMAPED_ACCT_WORN_SLOTS];
        int inv = 0;
        int x = ACCT_WORN_X + 176;
        int y = ACCT_WORN_Y + 4;
        int pending;
        for( int i = 0; i < TORIRSMAPED_ACCT_WORN_SLOTS; i++ )
        {
            worn[i] = -1;
            counts[i] = 1;
        }
        for( int i = 0; i < loadout->item_count; i++ )
            if( loadout->items[i].section == EDITOR_ACCOUNTS_SECTION_WORN && loadout->items[i].slot < TORIRSMAPED_ACCT_WORN_SLOTS )
            {
                worn[loadout->items[i].slot] = loadout->items[i].obj;
                counts[loadout->items[i].slot] = loadout->items[i].count;
            }
        pending = acct_draw_worn(ui, well, worn, counts, -1, 0);
        /* The backpack beside it, at half the game's spacing: 4 x 7 of 32. */
        for( int i = 0; i < loadout->item_count; i++ )
        {
            const struct Editor_AccountItem* item = &loadout->items[i];
            int const cx = x + (int)(item->slot % 4) * 34;
            int const cy = y + (int)(item->slot / 4) * 28;
            if( item->section != EDITOR_ACCOUNTS_SECTION_INV || item->slot >= TORIRSMAPED_ACCT_INV_SLOTS )
                continue;
            inv++;
            if( cx + 32 > 360 )
                continue;
            pending |= acct_draw_item(ui, well, cx, cy, item->obj, item->count);
        }
        ToriRSChrome_GraphEnd(ui->chrome, well);
        snprintf(text, sizeof(text), "%s: %d worn, %d in the backpack", loadout->name, loadout->item_count - inv, inv);
        ToriRSChrome_SetText(ui->chrome, self->label_loadout, text);
        self->loadout_dirty = pending;
    }
}

/* ---- the catalog find ------------------------------------------------------------ */

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

/* ---- the Items tab's finder: Put here ------------------------------------------------ */

/** The worn slot the finder narrows to (-1: the shown container is not the
 *  Equipment, no slot is selected, or the wear chip was taken off). */
static int
acct_put_wear_slot(const struct AccountsPanel* self)
{
    if( self->container != ACCT_CONT_WORN || self->selected_slot < 0 || !self->put_wear_filter )
        return -1;
    return self->selected_slot;
}

/** The finder's query text: the typed words (`#<n>` is the id), plus the
 *  wear chip's filter. "" when there is nothing to ask. */
static void
acct_put_filter(
    const struct AccountsPanel* self,
    char* out,
    size_t out_size)
{
    const char* text = self->put_text;
    int const wear = acct_put_wear_slot(self);
    char* end = NULL;
    long id;

    out[0] = '\0';
    while( *text == ' ' )
        text++;
    if( text[0] == '#' )
    {
        id = strtol(text + 1, &end, 10);
        if( end && end != text + 1 && (*end == '\0' || *end == ' ') && id >= 0 )
            snprintf(out, out_size, "id=%ld%s", id, end);
        else
            snprintf(out, out_size, "%s", text + 1);
    }
    else
        snprintf(out, out_size, "%s", text);
    if( wear >= 0 )
    {
        size_t at = strlen(out);
        snprintf(out + at, out_size > at ? out_size - at : 0, "%swearpos=%d", at ? " " : "", wear);
    }
}

/** Ask the catalog for `filter`'s objs: the rows, with their names and wear
 *  positions. */
static void
acct_put_run(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    const char* filter)
{
    struct EditorHost_CatalogPage page;

    self->put_count = 0;
    snprintf(self->put_asked, sizeof(self->put_asked), "%s", filter);
    if( !filter[0] )
        return;
    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        Editor_UiLog(ui, "accounts: the server has no catalog to find objs in");
        return;
    }
    memset(&page, 0, sizeof(page));
    if( Editor_HostMapEdCatalogQuery(&ui->editor->host, TORIRSMAPED_CATALOG_OBJ, filter, 0, 0, ACCT_PUT_MAX, &page)
        == EDITOR_HOST_OK )
    {
        for( int i = 0; i < page.row_count && self->put_count < ACCT_PUT_MAX; i++ )
        {
            const struct EditorHost_CatalogRow* row = &page.rows[i];
            struct AcctPutRow* put;
            if( row->id < 0 )
                continue;
            put = &self->put_rows[self->put_count++];
            put->id = row->id;
            snprintf(put->name, sizeof(put->name), "%s", row->name ? row->name : "");
            snprintf(put->display, sizeof(put->display), "%s", row->display ? row->display : "");
            put->wearpos = -1;
            for( int f = 0; f < row->field_count; f++ )
                if( strcmp(row->fields[f].key, "wearpos") == 0 )
                {
                    char* end = NULL;
                    long value = strtol(row->fields[f].value, &end, 10);
                    if( end && end != row->fields[f].value && *end == '\0' && value >= 0
                        && value < TORIRSMAPED_ACCT_WORN_SLOTS )
                        put->wearpos = (int)value;
                }
        }
    }
    Editor_HostCatalogPageFree(&page);
}

/** The finder's rows on screen: the game's icon, the pack name, the display
 *  name dimmed, the wear slot as a keycap, the id. An icon still loading
 *  leaves put_dirty set so the row is stated again next tick. */
static void
acct_put_rows(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct EditorUiAppServices* services = &ui->app_services;
    int first;
    int shown;

    self->put_dirty = 0;
    ToriRSChrome_VListSetCount(ui->chrome, self->list_put, self->put_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_put, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->put_count; r++ )
    {
        const struct AcctPutRow* row = &self->put_rows[r];
        char sprite[16] = "";
        char text[TORIRS_CHROME_INPUT_MAX];
        if( services->obj_icon )
        {
            int const scene_id = services->obj_icon(services->user_data, row->id, 1, NULL, 0);
            if( scene_id > 0 )
                snprintf(sprite, sizeof(sprite), "%d", scene_id);
            else if( scene_id == 0 )
                self->put_dirty = 1;
        }
        else
            snprintf(sprite, sizeof(sprite), TORIRS_CHROME_CELL_NO_SPRITE);
        snprintf(text, sizeof(text), "%s%c%s%c%s%c%s%c%d", sprite, TORIRS_CHROME_CELL_SEP, row->name, TORIRS_CHROME_CELL_SEP,
            strcmp(row->display, row->name) == 0 ? "" : row->display, TORIRS_CHROME_CELL_SEP,
            row->wearpos >= 0 ? acct_worn_slot_name(row->wearpos) : "", TORIRS_CHROME_CELL_SEP, row->id);
        ToriRSChrome_VListSetRow(ui->chrome, self->list_put, r, text);
    }
}

/** The finder follows its box: a changed text rests ACCT_PUT_SETTLE_FRAMES,
 *  then the catalog is asked once per (text, wear chip). */
static void
acct_put_tick(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const char* typed = ToriRSChrome_Text(ui->chrome, self->input_put);
    char filter[160];

    if( strcmp(typed, self->put_text) != 0 )
    {
        snprintf(self->put_text, sizeof(self->put_text), "%s", typed);
        self->put_rest = 0;
    }
    acct_put_filter(self, filter, sizeof(filter));
    if( strcmp(filter, self->put_asked) != 0 && ++self->put_rest >= ACCT_PUT_SETTLE_FRAMES )
    {
        acct_put_run(ui, self, filter);
        self->put_dirty = 1;
    }
    if( self->put_dirty )
        acct_put_rows(ui, self);
}

/** The chips under Put here: the wear filter (while the Equipment is shown
 *  and a slot is selected) and the Catalog's pick (while it is an obj). */
static void
acct_put_chips(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    char label[EDP_CHIP_LABEL_MAX];
    char tip[EDP_CHIP_TIP_MAX];

    self->chip_wear = -1;
    self->chip_pick = -1;
    edp_chips_begin(&self->put_strip);
    if( self->container == ACCT_CONT_WORN && self->selected_slot >= 0 )
    {
        const char* name = acct_worn_slot_name(self->selected_slot);
        if( self->put_wear_filter )
        {
            snprintf(label, sizeof(label), "wear: %s", name);
            snprintf(tip, sizeof(tip), "only what the %s slot wears; x searches every obj", name);
            self->chip_wear = edp_chips_push(&self->put_strip, label, tip, 1, 1);
        }
        else
        {
            snprintf(tip, sizeof(tip), "every obj; click to show only what the %s slot wears", name);
            self->chip_wear = edp_chips_push(&self->put_strip, "wear: any", tip, 0, 0);
        }
    }
    if( ui->tools.pick_kind == TORIRSMAPED_CATALOG_OBJ && ui->tools.pick_id >= 0 )
    {
        snprintf(label, sizeof(label), "Catalog pick: %.32s", ui->tools.pick_name);
        snprintf(tip, sizeof(tip), "put the Catalog's pick in the selected slot");
        self->chip_pick = edp_chips_push(&self->put_strip, label, tip, 0, 0);
    }
    edp_chips_end(ui, &self->put_strip);
    ToriRSChrome_SetHidden(ui->chrome, self->put_strip.graph, self->put_strip.count == 0);
    edp_chips_hover(ui, &self->put_strip);
}

/**
 * Put obj `obj` (its pack name `name`, "" unknown; its wear position
 * `wearpos`, -1 unknown) in the selected slot, Count deep: one SET with
 * `#<id> <count>`. A wear position that is not the Equipment slot is refused
 * here (the server refuses it too). Remembered for the check once the view
 * lands.
 */
static void
acct_put_obj(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    int obj,
    const char* name,
    int wearpos)
{
    const char* count_text = ToriRSChrome_Text(ui->chrome, self->input_count);
    int const count = count_text[0] && atoi(count_text) > 0 ? atoi(count_text) : 1;
    char field[16];
    char key[64];
    char value[48];
    char what[96];

    if( self->selected_slot < 0 )
    {
        Editor_UiLog(ui, "accounts: click a slot first, then choose what to put there");
        return;
    }
    if( name && name[0] )
        snprintf(what, sizeof(what), "%s (%d)", name, obj);
    else
        acct_obj_label(ui, self, obj, what, sizeof(what));
    if( self->container == ACCT_CONT_WORN && wearpos >= 0 && wearpos != self->selected_slot )
    {
        Editor_UiLog(ui, "accounts: %s is worn in the %s slot; this is %s", what, acct_worn_slot_name(wearpos),
            acct_worn_slot_name(self->selected_slot));
        return;
    }
    snprintf(field, sizeof(field), "%d", self->selected_slot);
    acct_container_key(self, field, key, sizeof(key));
    snprintf(value, sizeof(value), "#%d %d", obj, count);
    if( !acct_set(ui, self, key, value) )
        return;
    self->verify_pending = 1;
    self->verify_section = acct_container_section(self);
    self->verify_slot = self->selected_slot;
    self->verify_obj = obj;
    self->verify_count = count;
    self->verify_view_serial = self->model.view_serial;
    ToriRSChrome_SetText(ui->chrome, self->label_verify, "");
    Editor_UiLog(ui, "accounts: %s slot %d <- %s x%d", key, self->selected_slot, what, count);
}

/** Row `row` of the finder into the selected slot. */
static void
acct_put_row(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    int row)
{
    const struct AcctPutRow* put;

    if( row < 0 || row >= self->put_count )
        return;
    put = &self->put_rows[row];
    acct_put_obj(ui, self, put->id, put->name, put->wearpos);
}

/** Once a view newer than the last Set lands, the slot is compared with what
 *  was asked: a green line when they agree, a red one naming both when not.
 *  A refused Set (the Log says why) ends the check. */
static void
acct_verify_tick(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct Editor_AccountsModel* m = &self->model;
    const struct Editor_AccountItem* item;
    int const have_view = m->view.status == EDITOR_HOST_OK && strcmp(m->view.name, self->account) == 0;
    char asked[96];
    char got[96];
    char text[TORIRS_CHROME_LABEL_MAX];

    if( !self->verify_pending || m->view_serial == self->verify_view_serial || !have_view )
        return;
    self->verify_pending = 0;
    if( self->verify_section < 0 )
        return;
    item = Editor_AccountsItem(&m->view, self->verify_section, (uint32_t)self->verify_slot);
    if( item && item->obj == self->verify_obj && item->count == self->verify_count )
    {
        ToriRSChrome_SetText(ui->chrome, self->label_verify, "view agrees with the last Set");
        ToriRSChrome_SetColor(ui->chrome, self->label_verify, 0xFF80E080u);
        return;
    }
    acct_obj_label(ui, self, self->verify_obj, asked, sizeof(asked));
    if( item )
    {
        acct_obj_label(ui, self, item->obj, got, sizeof(got));
        snprintf(text, sizeof(text), "view shows %s x%d, asked for %s x%d", got, item->count, asked, self->verify_count);
    }
    else
        snprintf(text, sizeof(text), "view shows slot %d empty, asked for %s x%d", self->verify_slot, asked,
            self->verify_count);
    ToriRSChrome_SetText(ui->chrome, self->label_verify, text);
    ToriRSChrome_SetColor(ui->chrome, self->label_verify, 0xFFFF5555u);
    Editor_UiLog(ui, "accounts: %s", text);
}

int
Editor_Accounts_PutObj(
    struct EditorUi* ui,
    int obj)
{
    struct AccountsPanel* self;
    const struct AcctName* name;

    assert(ui);
    assert(obj >= 0);
    self = Editor_UiPanelSelf(ui, "accounts");
    if( !self || !ui->chrome->panels[self->panel].visible
        || ToriRSChrome_PanelActiveTab(ui->chrome, self->panel) != ACCT_TAB_ITEMS || self->selected_slot < 0 )
        return 0;
    name = acct_obj_name(ui, self, obj);
    acct_put_obj(ui, self, obj, name->name, name->wearpos);
    return 1;
}

static void
acct_refresh(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    const struct Editor_AccountsModel* m = &self->model;
    const struct Editor_AccountView* view = &m->view;
    int const tab = ToriRSChrome_PanelActiveTab(ui->chrome, self->panel);
    int const have_view = view->status == EDITOR_HOST_OK && strcmp(view->name, self->account) == 0;
    int const quest_filter = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_quest_filter);
    char text[TORIRS_CHROME_LABEL_MAX];
    int list_changed = self->shown_list_serial != m->list_serial;
    int view_changed = self->shown_view_serial != m->view_serial;
    int quests_changed = self->shown_quests_serial != m->quests_serial;
    int loadouts_changed = self->shown_loadouts_serial != m->loadouts_serial;

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
        ToriRSChrome_SetDisabled(ui->chrome, self->button_loadout_save, !self->account[0]);
        ToriRSChrome_SetTooltip(ui->chrome, self->button_refresh,
            online ? "Ask the server for a checkpoint (the account's live values)" : "Read the save again");
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
        acct_container_options(ui, self);
        acct_slot_label(ui, self);
        self->items_dirty = 1;
        self->shown_view_serial = m->view_serial;
    }
    if( quests_changed || view_changed || list_changed || quest_filter != self->shown_quest_filter )
    {
        self->shown_quest_filter = quest_filter;
        acct_quest_refresh(ui, self);
        self->shown_quests_serial = m->quests_serial;
    }
    if( loadouts_changed )
    {
        if( self->selected_loadout >= m->loadout_count )
            self->selected_loadout = -1;
        acct_set_rows(ui, self->list_loadouts, m->loadout_count, acct_loadout_row, self);
        self->loadout_dirty = 1;
        self->shown_loadouts_serial = m->loadouts_serial;
    }
    if( tab != self->shown_tab )
    {
        self->shown_tab = tab;
        if( tab == ACCT_TAB_QUESTS && !self->quests_asked )
            acct_ask_quests(ui, self);
        if( tab == ACCT_TAB_ITEMS && !self->invs_asked )
            acct_ask_invs(ui, self);
        self->items_dirty = 1;
        self->loadout_dirty = 1;
    }
    if( self->shown_invs_serial != m->invs_serial )
    {
        self->shown_invs_serial = m->invs_serial;
        self->items_dirty = 1;
    }
    if( self->items_dirty && tab == ACCT_TAB_ITEMS )
        acct_draw_items(ui, self);
    if( self->loadout_dirty && tab == ACCT_TAB_LOADOUTS )
        acct_draw_loadout(ui, self);
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
    if( tab == ACCT_TAB_ITEMS )
    {
        acct_put_tick(ui, self);
        acct_put_chips(ui, self);
        acct_verify_tick(ui, self);
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
    if( self->verify_pending && m->last_request == TORIRSMAPED_REQ_ACCOUNT_SET && m->last_status != EDITOR_HOST_OK )
    {
        self->verify_pending = 0;
        ToriRSChrome_SetText(ui->chrome, self->label_verify, "not set: see the Log");
        ToriRSChrome_SetColor(ui->chrome, self->label_verify, 0xFFFF5555u);
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
        acct_quest_refresh(ui, self);
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
        else if( strncmp(at, "container=", 10) == 0 )
            self->container = atoi(at + 10);
        else if( strncmp(at, "loadout=", 8) == 0 )
            snprintf(self->env_loadout, sizeof(self->env_loadout), "%s", at + 8);
    }
    self->env_active = 1;
}

static void
acct_env_dock(
    struct EditorUi* ui,
    struct AccountsPanel* self)
{
    int const w = edp_px(ui, ACCT_PANEL_W);
    int const x = ui->world.x + edp_px(ui, 8);
    int const y = ui->world.y + edp_px(ui, 8);

    /* Over the world's left edge at its own width: the wells want the
     * panel's full width, which the dock column (22% of the window) is not
     * at the common window sizes. */
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
    if( self->env_loadout[0] && self->selected_loadout < 0 )
        for( int i = 0; i < self->model.loadout_count; i++ )
            if( strcmp(self->model.loadouts[i].name, self->env_loadout) == 0 )
            {
                self->selected_loadout = i;
                ToriRSChrome_VListSetSelected(ui->chrome, self->list_loadouts, i);
                ToriRSChrome_SetText(ui->chrome, self->input_loadout_name, self->model.loadouts[i].name);
                self->loadout_dirty = 1;
            }
}

/* ---- the module ------------------------------------------------------------------------ */

static void
acct_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct AccountsPanel* self = opaque;

    self->frame++;
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

/** The wells: a press selects a slot (Items) -- the topmost keyed rect is
 *  the slot's own, its key 1 + slot. */
static int
acct_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct AccountsPanel* self = opaque;

    if( event->widget == self->put_strip.graph )
    {
        int on_x = 0;
        int index;
        if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
            return 1;
        index = edp_chips_key_of(&self->put_strip, event->key, &on_x);
        if( index >= 0 && index == self->chip_wear )
        {
            self->put_wear_filter = !self->put_wear_filter;
            self->put_asked[0] = '\0';
        }
        else if( index >= 0 && index == self->chip_pick && ui->tools.pick_kind == TORIRSMAPED_CATALOG_OBJ
                 && ui->tools.pick_id >= 0 )
        {
            const struct AcctName* name = acct_obj_name(ui, self, ui->tools.pick_id);
            acct_put_obj(ui, self, ui->tools.pick_id, name->name, name->wearpos);
        }
        return 1;
    }
    if( event->widget != self->graph_items && event->widget != self->graph_loadout )
        return 0;
    if( event->widget == self->graph_items && event->kind == TORIRS_CHROME_EVENT_GRAPH_PRESS && event->key > 0 )
        acct_select_slot(ui, self, event->key - 1);
    return 1;
}

/** Open inv `text` (a name or an id) as a container of the shown account:
 *  the save gets its section on the first Set. */
static void
acct_open_inv(
    struct EditorUi* ui,
    struct AccountsPanel* self,
    const char* text)
{
    const struct Editor_AccountInv* found = NULL;
    char* end = NULL;
    long id = strtol(text, &end, 10);

    for( int i = 0; i < self->model.inv_count && !found; i++ )
        if( strcmp(self->model.invs[i].name, text) == 0
            || (end && end != text && *end == '\0' && (long)self->model.invs[i].id == id) )
            found = &self->model.invs[i];
    if( !found )
    {
        Editor_UiLog(ui, "accounts: no inv is named '%s'%s", text,
            self->model.inv_count ? "" : " (the inv list has not arrived yet)");
        return;
    }
    if( (int)found->id == 93 || (int)found->id == 94 || (int)found->id == 95 )
    {
        self->container = found->id == 93 ? ACCT_CONT_INV : found->id == 94 ? ACCT_CONT_WORN : ACCT_CONT_BANK;
        acct_container_options(ui, self);
        self->items_dirty = 1;
        return;
    }
    self->open_inv = (int)found->id;
    snprintf(self->open_inv_name, sizeof(self->open_inv_name), "%s", found->name);
    self->open_inv_size = (int)found->size;
    acct_container_options(ui, self);
    for( int i = 0; i < self->model.view.container_count; i++ )
        if( (int)self->model.view.containers[i].inv == (int)found->id )
            self->container = ACCT_CONT_OTHER + i;
    if( self->open_inv >= 0 )
        self->container = ACCT_CONT_OTHER + self->model.view.container_count;
    self->page = 0;
    self->selected_slot = -1;
    acct_container_options(ui, self);
    acct_slot_label(ui, self);
    self->items_dirty = 1;
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
            self->open_inv = -1;
            self->selected_slot = -1;
            acct_ask_view(ui, self);
            self->shown_view_serial = -1;
        }
        return 1;
    }
    if( widget == self->tabs || widget == self->dropdown_quest_filter )
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
    /* Items */
    if( widget == self->dropdown_container )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( choice >= 0 && choice < self->container_option_count && choice != self->container )
        {
            self->container = choice;
            self->page = 0;
            self->selected_slot = -1;
            acct_slot_label(ui, self);
            self->items_dirty = 1;
        }
        return 1;
    }
    if( widget == self->button_inv_open || widget == self->input_inv )
    {
        const char* text = ToriRSChrome_Text(ui->chrome, self->input_inv);
        if( !text[0] )
        {
            Editor_UiLog(ui, "accounts: type an inv name or id to open (configs/all.inv)");
            return 1;
        }
        if( !self->invs_asked )
            acct_ask_invs(ui, self);
        acct_open_inv(ui, self, text);
        return 1;
    }
    if( widget == self->button_page_prev || widget == self->button_page_next )
    {
        self->page += widget == self->button_page_next ? 1 : -1;
        if( self->page < 0 )
            self->page = 0;
        self->items_dirty = 1;
        return 1;
    }
    if( widget == self->list_put )
    {
        acct_put_row(ui, self, ToriRSChrome_VListSelected(ui->chrome, widget));
        return 1;
    }
    if( widget == self->input_put )
    {
        /* Return: the first row the words match. */
        char filter[160];
        acct_put_filter(self, filter, sizeof(filter));
        if( strcmp(filter, self->put_asked) != 0 )
        {
            acct_put_run(ui, self, filter);
            self->put_dirty = 1;
        }
        if( self->put_count == 0 )
            Editor_UiLog(ui, "accounts: no obj matches '%s'", self->put_text);
        else
            acct_put_row(ui, self, 0);
        return 1;
    }
    if( widget == self->button_item_remove )
    {
        char field[16];
        if( self->selected_slot < 0 )
        {
            Editor_UiLog(ui, "accounts: click a slot first");
            return 1;
        }
        snprintf(field, sizeof(field), "%d", self->selected_slot);
        acct_container_key(self, field, key, sizeof(key));
        acct_set(ui, self, key, "-");
        ToriRSChrome_SetText(ui->chrome, self->label_verify, "");
        return 1;
    }
    if( widget == self->button_clear )
    {
        acct_container_key(self, "clear", key, sizeof(key));
        acct_set(ui, self, key, "");
        return 1;
    }
    /* Loadouts */
    if( widget == self->list_loadouts )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, widget);
        if( row >= 0 && row < self->model.loadout_count )
        {
            self->selected_loadout = row;
            ToriRSChrome_SetText(ui->chrome, self->input_loadout_name, self->model.loadouts[row].name);
            self->loadout_dirty = 1;
        }
        return 1;
    }
    if( widget == self->button_loadout_save )
    {
        const char* name = ToriRSChrome_Text(ui->chrome, self->input_loadout_name);
        if( !name[0] )
        {
            Editor_UiLog(ui, "accounts: type the loadout's Name first");
            return 1;
        }
        if( !self->account[0] )
        {
            Editor_UiLog(ui, "accounts: choose the account whose gear to keep first");
            return 1;
        }
        type = Editor_AccountsEncodeLoadoutSave(&payload, name, self->account);
        acct_send(ui, self, type, &payload);
        return 1;
    }
    if( widget == self->button_loadout_apply || widget == self->button_loadout_delete )
    {
        const char* name = ToriRSChrome_Text(ui->chrome, self->input_loadout_name);
        if( !name[0] )
        {
            Editor_UiLog(ui, "accounts: pick a loadout (or type its Name) first");
            return 1;
        }
        if( widget == self->button_loadout_apply )
            acct_set(ui, self, "loadout", name);
        else
        {
            type = Editor_AccountsEncodeLoadoutDelete(&payload, name);
            acct_send(ui, self, type, &payload);
        }
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
    if( widget == self->list_quests )
    {
        acct_quest_labels(ui, self);
        return 1;
    }
    if( widget == self->button_quest_complete || widget == self->button_quest_mark || widget == self->button_quest_reset )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_quests);
        if( row < 0 || row >= self->quest_row_count )
        {
            Editor_UiLog(ui, "accounts: pick a quest in the list first");
            return 1;
        }
        snprintf(key, sizeof(key), "quests.%s", self->model.quests[self->quest_rows[row]].dbrow);
        acct_set(ui, self, key,
            widget == self->button_quest_complete ? "complete" : widget == self->button_quest_mark ? "mark" : "reset");
        return 1;
    }
    if( widget == self->button_quest_complete_shown || widget == self->button_quest_mark_shown )
    {
        acct_quest_batch(ui, self, widget == self->button_quest_complete_shown ? "complete" : "mark");
        return 1;
    }
    if( widget == self->button_quest_mark_all )
    {
        acct_set(ui, self, "quests.all", "mark");
        return 1;
    }
    if( widget == self->list_queue )
        return 1;
    if( widget == self->list_log || widget == self->input_name || widget == self->input_password
        || widget == self->input_fixture || widget == self->input_x || widget == self->input_z
        || widget == self->input_level || widget == self->input_count || widget == self->input_loadout_name
        || widget == self->put_strip.graph )
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
    self->shown_loadouts_serial = -1;
    self->shown_invs_serial = 0;
    self->shown_tab = -1;
    self->shown_quest_filter = -1;
    self->shown_pick_id = -1;
    self->put_wear_filter = 1;
    self->chip_wear = -1;
    self->chip_pick = -1;
    self->verify_section = -1;
    self->log_follow = 1;
    self->env_tab = -1;
    self->container = ACCT_CONT_WORN;
    self->selected_slot = -1;
    self->open_inv = -1;
    self->selected_loadout = -1;
    self->look_asked_frame = -ACCT_LOOK_ASK_FRAMES;

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, 40), edp_px(ui, ACCT_PANEL_W), "Accounts");
    assert(self->panel >= 0);
    /* Above the tabs: which account, and one line of its state. */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
    self->names[0][0] = '\0';
    self->options[0] = self->names[0];
    self->dropdown_account = ToriRSChrome_Dropdown(chrome, self->panel, "Account", self->options, 0, -1);
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "no account chosen");
    self->tabs = ToriRSChrome_Tabs(chrome, self->panel, k_tab_titles, ACCT_TABS, 0);
    assert(self->tabs >= 0);

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_ACCOUNT);
    self->label_lane = ToriRSChrome_Label(chrome, self->panel, "asking the server for its lane...");
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    self->button_use = ToriRSChrome_Button(chrome, self->panel, "Use for RUN / Play");
    ToriRSChrome_SetTooltip(chrome, self->button_use, "RUN and Play log in as this account (needs its password)");
    self->button_duplicate = ToriRSChrome_Button(chrome, self->panel, "Duplicate");
    ToriRSChrome_SetTooltip(chrome, self->button_duplicate, "A copy of this account under Name with Password");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete");
    ToriRSChrome_SetTooltip(chrome, self->button_delete, "Its save, password record and queue (offline only)");
    ToriRSChrome_Separator(chrome, self->panel);
    self->label_position = ToriRSChrome_Label(chrome, self->panel, "no values yet");
    self->input_x = ToriRSChrome_TextInput(chrome, self->panel, "X", "");
    self->input_z = ToriRSChrome_TextInput(chrome, self->panel, "Z", "");
    self->input_level = ToriRSChrome_TextInput(chrome, self->panel, "Level", "0");
    self->button_move = ToriRSChrome_Button(chrome, self->panel, "Move");
    ToriRSChrome_SetTooltip(chrome, self->button_move, "Online: ::tele; offline: the save's position");
    self->button_to_selection = ToriRSChrome_Button(chrome, self->panel, "To selection");
    ToriRSChrome_SetTooltip(chrome, self->button_to_selection, "The Select tool's tile or loc");
    self->button_place_tool = ToriRSChrome_Button(chrome, self->panel, "Place player tool");
    ToriRSChrome_SetTooltip(chrome, self->button_place_tool,
        "Click a tile to put the PLAY account there (shown in its gear)");
    ToriRSChrome_Separator(chrome, self->panel);
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    self->input_password = ToriRSChrome_TextInput(chrome, self->panel, "Password", "");
    ToriRSChrome_SetTooltip(chrome, self->input_password, "8 to 20 characters with a letter and a digit, not the name");
    self->input_fixture = ToriRSChrome_TextInput(chrome, self->panel, "Fixture", "");
    ToriRSChrome_SetTooltip(chrome, self->input_fixture, "The save a new account starts from (empty: the lane's)");
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New account");

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_ITEMS);
    self->container_names[0][0] = '\0';
    self->container_options[0] = self->container_names[0];
    self->dropdown_container = ToriRSChrome_Dropdown(chrome, self->panel, "Container", self->container_options, 0, -1);
    self->input_inv = ToriRSChrome_TextInput(chrome, self->panel, "Inv", "");
    ToriRSChrome_SetTooltip(chrome, self->input_inv, "Any inv of configs/all.inv by name or id (Enter opens it)");
    self->button_inv_open = ToriRSChrome_Button(chrome, self->panel, "Open inv");
    self->graph_items = ToriRSChrome_Graph(chrome, self->panel, "", ACCT_WELL_H);
    self->label_page = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_page_prev = ToriRSChrome_Button(chrome, self->panel, "Previous page");
    self->button_page_next = ToriRSChrome_Button(chrome, self->panel, "Next page");
    self->label_slot = ToriRSChrome_Label(chrome, self->panel, "click a slot");
    self->label_verify = ToriRSChrome_LabelColored(chrome, self->panel, "", 0xFF80E080u);
    self->input_put = ToriRSChrome_TextInput(chrome, self->panel, "Put here", "");
    ToriRSChrome_SetTooltip(chrome, self->input_put, "An obj by name or #id; a row click sets it (online: ::item)");
    self->input_count = ToriRSChrome_TextInput(chrome, self->panel, "Count", "1");
    edp_chips_init(ui, &self->put_strip, self->panel, 1);
    ToriRSChrome_SetHidden(chrome, self->put_strip.graph, 1);
    self->list_put = ToriRSChrome_VListCells(chrome, self->panel,
        TORIRS_CHROME_CELL_SEP_STR "name" TORIRS_CHROME_CELL_SEP_STR TORIRS_CHROME_CELL_SEP_STR "wear" TORIRS_CHROME_CELL_SEP_STR "id",
        ACCT_PUT_ROWS, 0);
    ToriRSChrome_SetTooltip(chrome, self->list_put, "click a row to put it in the selected slot");
    self->button_item_remove = ToriRSChrome_Button(chrome, self->panel, "Remove");
    self->button_clear = ToriRSChrome_Button(chrome, self->panel, "Clear container");
    ToriRSChrome_SetTooltip(chrome, self->button_clear, "Empty every slot (online: the backpack only, ::clearinv)");

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_LOADOUTS);
    self->list_loadouts = ToriRSChrome_VList(chrome, self->panel, "Loadouts", ACCT_LOADOUT_ROWS, 0);
    self->graph_loadout = ToriRSChrome_Graph(chrome, self->panel, "", ACCT_PREVIEW_H);
    self->label_loadout = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_loadout_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_loadout_name, "Letters, digits and '_' (spaces become '_')");
    self->button_loadout_save = ToriRSChrome_Button(chrome, self->panel, "Save current gear");
    ToriRSChrome_SetTooltip(chrome, self->button_loadout_save,
        "This account's worn items + backpack become the loadout");
    self->button_loadout_apply = ToriRSChrome_Button(chrome, self->panel, "Apply to account");
    ToriRSChrome_SetTooltip(chrome, self->button_loadout_apply,
        "Its worn items + backpack replace this account's");
    self->button_loadout_delete = ToriRSChrome_Button(chrome, self->panel, "Delete loadout");

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
    self->label_quest_summary = ToriRSChrome_Label(chrome, self->panel, "asking for the quest rows...");
    self->input_quest_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    self->dropdown_quest_filter = ToriRSChrome_Dropdown(chrome, self->panel, "Show", k_quest_filters, 4, 0);
    self->list_quests = ToriRSChrome_VList(chrome, self->panel, "Quests", ACCT_QUEST_ROWS, 0);
    self->label_quest_detail = ToriRSChrome_Label(chrome, self->panel, "pick a quest for its var and value");
    self->button_quest_complete = ToriRSChrome_Button(chrome, self->panel, "Complete");
    ToriRSChrome_SetTooltip(chrome, self->button_quest_complete, "::complete: the state and the quest points");
    self->button_quest_mark = ToriRSChrome_Button(chrome, self->panel, "Mark complete");
    ToriRSChrome_SetTooltip(chrome, self->button_quest_mark, "::questmark: the state only");
    self->button_quest_reset = ToriRSChrome_Button(chrome, self->panel, "Reset");
    ToriRSChrome_SetTooltip(chrome, self->button_quest_reset, "The quest's var back to 0 (not started)");
    self->button_quest_complete_shown = ToriRSChrome_Button(chrome, self->panel, "Complete shown");
    ToriRSChrome_SetTooltip(chrome, self->button_quest_complete_shown,
        "::complete each shown quest not yet complete");
    self->button_quest_mark_shown = ToriRSChrome_Button(chrome, self->panel, "Mark shown");
    ToriRSChrome_SetTooltip(chrome, self->button_quest_mark_shown, "::questmark each shown quest not yet complete");
    self->button_quest_mark_all = ToriRSChrome_Button(chrome, self->panel, "Mark all complete");
    self->label_queue = ToriRSChrome_Label(chrome, self->panel, "0 queued cheat(s) for the next login");
    self->list_queue = ToriRSChrome_VList(chrome, self->panel, "Queued", ACCT_QUEUE_ROWS, 0);

    ToriRSChrome_PanelBeginTab(chrome, self->panel, ACCT_TAB_LOG);
    self->list_log = ToriRSChrome_VList(chrome, self->panel, "Log", ACCT_LOG_ROWS, 0);

    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
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
    desc.event = acct_event;
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
Editor_Accounts_PlayPosition(
    struct EditorUi* ui,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct AccountsPanel* self;
    const struct Editor_AccountsModel* m;

    assert(ui);
    assert(out_x);
    assert(out_z);
    assert(out_level);
    self = acct_self(ui);
    if( !self )
        return 0;
    if( ui->editor && self->frame - self->position_asked_frame >= ACCT_LOOK_ASK_FRAMES )
    {
        self->position_asked_frame = self->frame;
        acct_ask_list(ui, self);
    }
    m = &self->model;
    if( !m->play_account[0] )
        return 0;
    for( int i = 0; i < m->row_count; i++ )
        if( strcmp(m->rows[i].name, m->play_account) == 0 )
        {
            /* A save that has never been placed says 0,0. */
            if( m->rows[i].x <= 0 && m->rows[i].z <= 0 )
                return 0;
            *out_x = m->rows[i].x;
            *out_z = m->rows[i].z;
            *out_level = m->rows[i].level;
            return 1;
        }
    return 0;
}

void
Editor_Accounts_RevealPlay(struct EditorUi* ui)
{
    struct AccountsPanel* self;

    assert(ui);
    self = acct_self(ui);
    if( !self )
        return;
    if( self->model.play_account[0] )
        (void)Editor_Accounts_Show(ui, self->model.play_account);
    if( Editor_UiPanelInStrip(ui, "accounts") )
        Editor_UiGroupShow(ui, "accounts");
    else if( !ui->chrome->panels[self->panel].visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
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

int
Editor_Accounts_PlayLook(
    struct EditorUi* ui,
    struct Editor_AccountLook* out,
    int* out_serial)
{
    struct AccountsPanel* self;
    const struct Editor_AccountsModel* m;

    assert(ui);
    assert(out);
    assert(out_serial);
    self = acct_self(ui);
    if( !self )
        return 0;
    m = &self->model;
    if( m->play_look_known && strcmp(m->play_look.name, m->play_account) == 0 )
    {
        *out = m->play_look;
        *out_serial = m->play_look_serial;
        return 1;
    }
    /* Not known yet: ask for the play account's view (every window hears
     * it; a window following another account keeps only the look). */
    if( m->play_account[0]
        && (strcmp(self->look_asked, m->play_account) != 0 || self->frame - self->look_asked_frame >= ACCT_LOOK_ASK_FRAMES) )
    {
        snprintf(self->look_asked, sizeof(self->look_asked), "%s", m->play_account);
        self->look_asked_frame = self->frame;
        acct_ask_view_of(ui, self, m->play_account);
    }
    return 0;
}
