/*
 * The Lanes panel (module "publish", File > Publish..., Ctrl+Shift+U): where
 * this window's edits land and where they go next (owner, 2026-11-02:
 * "Edits should go to a dedicated 'working' lane and then there should be a
 * publish menu that publishes to a target lane or a new lane"), widened by
 * workbench 6.5 (design #lanes, "The Lanes panel (today's Publish panel,
 * widened)") from the Publish panel into the Lanes panel.
 *
 * The editor server serves a LANE (torirsmaped/torirs_maped_lanes.h): the
 * main tree itself, or a named lane -- build/lanes/<name>/content, a
 * copy-on-write mirror of the tree that RUN builds and plays on its own. The
 * panel shows
 *
 *   the lane         "Lane: working | 3 changed", its base;
 *   every lane       a VLIST with its Find box (R3): each lane with its change
 *                    count against its base and, for a tree another editor
 *                    holds, who holds it ("main | the tree | locked: pid N");
 *   the changes      chips M / A / D that filter the change list (a lit chip
 *                    shows its kind), and the list itself;
 *
 * and offers:
 *
 *   Publish to...    every dirty document is saved, then the changes are
 *                    written into the target: the main tree ("main"), another
 *                    lane, or "New lane..." with a name. When the target is
 *                    this lane's base, the lane is clean afterwards (its
 *                    published files are links to the base again) unless
 *                    "Keep the changes here" is ticked.
 *   Scratch lane     (Ctrl+Shift+N) a lane named scratch-<yymmdd><letter>
 *                    made from main, then the session switched to it.
 *   Switch...        the lane picked in the list becomes the session's.
 *   Discard...       every change back to the base (a second press within
 *                    4 s confirms); the server drops its documents, and every
 *                    window reopens the square it shows.
 *   New lane         a lane mirrored from the main tree, nothing published.
 *   Refresh          scan the lanes and the changes again (a save does it).
 *
 * A SWITCH IS STAGED. A running editor server cannot re-point its lane: its
 * tree lock, its documents and their history, the catalog, the live dir and
 * the servers it launched are all the lane's, and every window mirrors them.
 * So Switch (and Scratch lane's switch) writes the lane the NEXT boot serves
 * (LANE_SWITCH: build/lanes/lane.ini, read by the daemon's main and the
 * embedded server), and the status bar and the lane chip say so ("working |
 * 3 changed | next: scratch-261105a").
 *
 * The module also keeps ui->lane, for every window and every module: the
 * lane's name and its change count, which the status bar's lane chip shows
 * (Editor_Lanes_ChipText). It asks LANE_INFO once, and the count alone
 * (LANE_CHANGES with TORIRSMAPED_LANE_CHANGES_COUNT_ONLY, FACT_LANE_COUNT)
 * every EDITOR_UI_LANE_COUNT_MS, after a save and after a publish, whether
 * the panel shows or not; the full lists only while it shows. A server
 * without TORIRSMAPED_CAP_LANES_V2 hears no frame it does not know.
 *
 * A server on the main tree has nothing to publish: the panel says so and
 * how to start on a lane. The requests go through the host's lanes frames
 * (Editor_HostMapEdLanesSend / Take, editor_host.h); every answer to an
 * action is a broadcast, so every window's panel shows the same lane.
 */
#include "edpanel_chips.h"
#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/editor.h"
#include "editor/editor_host.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_buf.h"
#include "torirsmaped/torirs_maped_lanes.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
    PUBLISH_ROWS = 6,
    PUBLISH_LANE_ROWS = 5,
    PUBLISH_CHANGE_MAX = 1024,
    PUBLISH_LANES_MAX = TORIRSMAPED_LANES_MAX,
    /** The dropdown: every lane but this one, then "New lane...". */
    PUBLISH_TARGET_NEW = -1,
    /** Discard... asks twice: the second press within this confirms. */
    PUBLISH_DISCARD_CONFIRM_MS = 4000,
    /** The change chips, in kind order. */
    PUBLISH_CHIP_MODIFIED = 0,
    PUBLISH_CHIP_ADDED = 1,
    PUBLISH_CHIP_DELETED = 2,
    PUBLISH_CHIPS = 3,
};

struct publish_change
{
    uint32_t kind;
    char path[256];
    uint32_t size;
};

struct publish_lane
{
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];
    int is_main;
    int is_this;
    /** Its change count (-1: not counted, an older server) and who holds
     *  its tree ("" nobody). */
    int changes;
    char held[256];
};

struct PublishPanel
{
    int panel;
    int label_lane;
    int label_base;
    int input_find;
    int list_lanes;
    int label_counts;
    struct Edp_ChipStrip chips;
    int list_changes;
    int drop_target;
    int input_new;
    int check_keep;
    int button_publish;
    int button_scratch;
    int button_switch;
    int button_discard;
    int button_create;
    int button_refresh;
    int label_status;

    /* the lane, as the server last said (ui->lane holds the shared half) */
    char lane_content[1024];
    char lane_base[1024];
    int info_known;

    struct publish_lane lanes[PUBLISH_LANES_MAX];
    int lane_count;
    int list_known;
    /** The lane list's rows: indices into `lanes` that match Find. */
    int lane_rows[PUBLISH_LANES_MAX];
    int lane_row_count;
    char shown_find[TORIRS_CHROME_INPUT_MAX];
    /** The dropdown's options: the other lanes' names, then "New lane...". */
    const char* options[PUBLISH_LANES_MAX + 1];
    int option_lane[PUBLISH_LANES_MAX + 1];
    int option_count;

    struct publish_change* changes;
    int change_count;
    int modified;
    int added;
    int deleted;
    int changes_known;
    /** The chips' filter: a kind's changes are listed while its chip is lit. */
    int show_kind[PUBLISH_CHIPS];
    /** The change list's rows: indices into `changes`. */
    int* change_rows;
    int change_row_count;

    /** Asked the server, nothing heard yet (a request per kind). */
    int info_pending;
    int list_pending;
    int changes_pending;
    int count_pending;
    int action_pending;
    /** The panel refreshes when it opens and after a save (the document's
     *  unsaved state falling back to clean). */
    int refresh_wanted;
    int seen_unsaved;
    /** The panel's visibility last tick: opening it (the hotkey, the View
     *  menu) asks the server afresh. */
    int was_visible;
    /** The chrome clock of the last count ask (the chip's period). */
    int64_t count_asked_ms;
    int count_ever_asked;

    /** Scratch lane: waiting for the lane list to pick a free letter, then
     *  for LANE_CREATE's answer to switch to `scratch_name`. */
    int scratch_wanted;
    char scratch_name[TORIRSMAPED_LANE_NAME_MAX + 8];
    /** Discard...'s first press, on the chrome clock (0: not armed). */
    int64_t discard_armed_ms;
    int discard_armed;
    /** The hotkey table's serial the keycaps were worn for. */
    uint32_t chord_serial;
};

/** ToriRSMapEdLanes_NameValid's rule, spelled here: a window without the
 *  embedded server (the web) links no lane module. */
static int
publish_name_valid(const char* name)
{
    size_t const length = strlen(name);
    if( length == 0 || length > TORIRSMAPED_LANE_NAME_MAX || strcmp(name, "main") == 0 )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') )
            return 0;
    }
    return 1;
}

int
Editor_LanesScratchName(
    const char* yymmdd,
    const char* const* taken,
    int count,
    char* out,
    int out_size)
{
    assert(yymmdd);
    assert(taken || count == 0);
    assert(out);
    assert(out_size > 0);
    assert(strlen(yymmdd) == 6);
    for( char letter = 'a'; letter <= 'z'; letter++ )
    {
        int used = 0;
        snprintf(out, (size_t)out_size, "scratch-%s%c", yymmdd, letter);
        for( int i = 0; i < count && !used; i++ )
            used = strcmp(taken[i], out) == 0;
        if( !used )
            return 1;
    }
    out[0] = '\0';
    return 0;
}

static struct PublishPanel*
publish_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "publish");
}

static int
publish_v2(const struct EditorUi* ui)
{
    return ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_LANES_V2) != 0;
}

static int
publish_send(
    struct EditorUi* ui,
    uint32_t type,
    struct ToriRSMapEdBuf* payload)
{
    int sent;
    if( !ui->editor )
    {
        ToriRSMapEd_BufFree(payload);
        return 0;
    }
    sent = Editor_HostMapEdLanesSend(&ui->editor->host, type,
        payload->data ? payload->data + payload->head : NULL, (uint32_t)ToriRSMapEd_BufAvailable(payload));
    ToriRSMapEd_BufFree(payload);
    return sent;
}

/** The count alone, for the chip (a server without the frame: nothing). */
static void
publish_ask_count(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;

    self->count_asked_ms = ui->chrome->now_ms;
    self->count_ever_asked = 1;
    if( !publish_v2(ui) || self->count_pending )
        return;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_LANE_CHANGES_COUNT_ONLY);
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_CHANGES, &payload) )
        self->count_pending = 1;
}

static void
publish_ask_info(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_INFO, &payload) )
        self->info_pending = 1;
}

static void
publish_ask_list(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_LIST, &payload) )
        self->list_pending = 1;
}

static void
publish_ask(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;

    publish_ask_info(ui, self);
    publish_ask_list(ui, self);
    memset(&payload, 0, sizeof(payload));
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_CHANGES, &payload) )
        self->changes_pending = 1;
    self->refresh_wanted = 0;
}

static const char*
publish_kind_letter(uint32_t kind)
{
    return kind == TORIRSMAPED_LANE_CHANGE_MODIFIED ? "M" : kind == TORIRSMAPED_LANE_CHANGE_ADDED ? "A" : "D";
}

static int
publish_contains(
    const char* text,
    const char* filter)
{
    size_t const n = strlen(filter);

    if( n == 0 )
        return 1;
    for( const char* at = text; *at; at++ )
    {
        size_t i = 0;
        while( i < n && at[i] )
        {
            char a = at[i];
            char b = filter[i];
            if( a >= 'A' && a <= 'Z' )
                a = (char)(a - 'A' + 'a');
            if( b >= 'A' && b <= 'Z' )
                b = (char)(b - 'A' + 'a');
            if( a != b )
                break;
            i++;
        }
        if( i == n )
            return 1;
    }
    return 0;
}

/** The shared half: ui->lane changes only when a field does. */
static void
publish_lane_note(
    struct EditorUi* ui,
    const char* name,
    int is_main,
    int writable,
    int changes,
    const char* next_boot)
{
    struct EditorUiLane* lane = &ui->lane;
    struct EditorUiLane before = *lane;

    if( name )
    {
        snprintf(lane->name, sizeof(lane->name), "%s", name);
        lane->is_main = is_main;
        lane->known = 1;
    }
    if( writable >= 0 )
        lane->writable = writable;
    if( changes >= 0 )
    {
        lane->changes = changes;
        lane->changes_known = 1;
    }
    if( next_boot )
    {
        /* The lane it serves now is no switch waiting. */
        snprintf(lane->next_boot, sizeof(lane->next_boot), "%s", strcmp(next_boot, lane->name) == 0 ? "" : next_boot);
    }
    if( memcmp(&before, lane, sizeof(before)) != 0 )
        lane->serial++;
}

void
Editor_Lanes_ChipText(
    const struct EditorUi* ui,
    char* out,
    int out_size)
{
    const struct EditorUiLane* lane;
    int at;

    assert(ui);
    assert(out);
    assert(out_size > 0);
    lane = &ui->lane;
    out[0] = '\0';
    if( !lane->known )
        return;
    if( lane->is_main )
        at = snprintf(out, (size_t)out_size, "%s", lane->name);
    else if( !lane->changes_known )
        at = snprintf(out, (size_t)out_size, "%s", lane->name);
    else if( lane->changes == 0 )
        at = snprintf(out, (size_t)out_size, "%s | clean", lane->name);
    else
        at = snprintf(out, (size_t)out_size, "%s | %d changed", lane->name, lane->changes);
    if( lane->next_boot[0] && at > 0 && at < out_size )
        snprintf(out + at, (size_t)(out_size - at), " | next: %s", lane->next_boot);
}

/* ---- the lists ------------------------------------------------------------------ */

static void
publish_lane_row_text(
    const struct publish_lane* lane,
    char* out,
    size_t out_size)
{
    char count[48];

    if( lane->is_main )
        snprintf(count, sizeof(count), "the tree");
    else if( lane->changes < 0 )
        snprintf(count, sizeof(count), "not counted");
    else if( lane->changes == 0 )
        snprintf(count, sizeof(count), "clean");
    else
        snprintf(count, sizeof(count), "%d changed", lane->changes);
    /* This lane's tree is held by this very server: no news. */
    int const locked = lane->held[0] && !lane->is_this;
    snprintf(out, out_size, "%s%s | %s%s%s", lane->name, lane->is_this ? " (this)" : "", count,
        locked ? " | locked: " : "", locked ? lane->held : "");
}

static void
publish_fill_lanes(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, self->list_lanes, self->lane_row_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_lanes, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < self->lane_row_count; row++ )
    {
        char text[300];
        publish_lane_row_text(&self->lanes[self->lane_rows[row]], text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list_lanes, row, text);
    }
}

static void
publish_filter_lanes(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);

    if( !find )
        find = "";
    self->lane_row_count = 0;
    for( int i = 0; i < self->lane_count; i++ )
        if( publish_contains(self->lanes[i].name, find) )
            self->lane_rows[self->lane_row_count++] = i;
    snprintf(self->shown_find, sizeof(self->shown_find), "%s", find);
    publish_fill_lanes(ui, self);
}

static int
publish_chip_of(uint32_t kind)
{
    return kind == TORIRSMAPED_LANE_CHANGE_MODIFIED ? PUBLISH_CHIP_MODIFIED
           : kind == TORIRSMAPED_LANE_CHANGE_ADDED  ? PUBLISH_CHIP_ADDED
                                                    : PUBLISH_CHIP_DELETED;
}

static void
publish_fill_rows(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, self->list_changes, self->change_row_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_changes, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < self->change_row_count; row++ )
    {
        char text[300];
        const struct publish_change* change = &self->changes[self->change_rows[row]];
        if( change->kind == TORIRSMAPED_LANE_CHANGE_DELETED )
            snprintf(text, sizeof(text), "%s %s", publish_kind_letter(change->kind), change->path);
        else
            snprintf(text, sizeof(text), "%s %s (%u bytes)", publish_kind_letter(change->kind), change->path,
                change->size);
        ToriRSChrome_VListSetRow(ui->chrome, self->list_changes, row, text);
    }
}

/** The chips (M / A / D with their counts) and the rows they let through. */
static void
publish_filter_changes(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    static const char* const k_words[PUBLISH_CHIPS] = { "modified", "added", "deleted" };
    int const counts[PUBLISH_CHIPS] = { self->modified, self->added, self->deleted };

    edp_chips_begin(&self->chips);
    for( int i = 0; i < PUBLISH_CHIPS; i++ )
    {
        char label[EDP_CHIP_LABEL_MAX];
        char tip[EDP_CHIP_TIP_MAX];
        snprintf(label, sizeof(label), "%c %d", "MAD"[i], counts[i]);
        snprintf(tip, sizeof(tip), "%s %s: click to %s them", self->show_kind[i] ? "Showing" : "Hiding", k_words[i],
            self->show_kind[i] ? "hide" : "show");
        (void)edp_chips_push(&self->chips, label, tip, self->show_kind[i], 0);
    }
    edp_chips_end(ui, &self->chips);
    self->change_row_count = 0;
    for( int i = 0; i < self->change_count; i++ )
        if( self->show_kind[publish_chip_of(self->changes[i].kind)] )
            self->change_rows[self->change_row_count++] = i;
    publish_fill_rows(ui, self);
}

static void
publish_chords(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    edp_button_chord(ui, self->button_publish, EDITOR_HK_FILE_PUBLISH);
    edp_button_chord(ui, self->button_scratch, EDITOR_HK_FILE_SCRATCH_LANE);
    self->chord_serial = ui->hotkeys.serial;
}

static void
publish_refresh_widgets(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    char text[1200];
    char chip[96];
    int const on_lane = ui->lane.known && !ui->lane.is_main;
    int selected = ToriRSChrome_DropdownSelected(ui->chrome, self->drop_target);
    int const picked = ToriRSChrome_VListSelected(ui->chrome, self->list_lanes);

    Editor_Lanes_ChipText(ui, chip, (int)sizeof(chip));
    if( !ui->lane.known )
        snprintf(text, sizeof(text), "Lane: asking the editor server...");
    else if( ui->lane.is_main )
        snprintf(text, sizeof(text), "Lane: main -- this editor edits the tree itself (%s)", self->lane_content);
    else
        snprintf(text, sizeof(text), "Lane: %s%s", chip, ui->lane.writable ? "" : " [read-only]");
    ToriRSChrome_SetText(ui->chrome, self->label_lane, text);
    if( on_lane )
        snprintf(text, sizeof(text), "Base: %s", self->lane_base);
    else if( ui->lane.known )
        snprintf(text, sizeof(text), "Start the editor on a lane ([editor:boot] lane=working) to publish.");
    else
        text[0] = '\0';
    ToriRSChrome_SetText(ui->chrome, self->label_base, text);
    if( !self->changes_known )
        snprintf(text, sizeof(text), on_lane ? "Changes: scanning..." : "");
    else if( self->change_count == 0 )
        snprintf(text, sizeof(text), "No changes: the lane is its base.");
    else
        snprintf(text, sizeof(text), "%d modified, %d added, %d deleted%s", self->modified, self->added,
            self->deleted, self->change_count < self->modified + self->added + self->deleted ? " (first 1024 shown)"
                                                                                                : "");
    ToriRSChrome_SetText(ui->chrome, self->label_counts, text);

    /* The targets: every other lane, then a new one. */
    self->option_count = 0;
    for( int i = 0; i < self->lane_count && self->option_count < PUBLISH_LANES_MAX; i++ )
    {
        if( self->lanes[i].is_this )
            continue;
        self->options[self->option_count] = self->lanes[i].name;
        self->option_lane[self->option_count] = i;
        self->option_count++;
    }
    self->options[self->option_count] = "New lane...";
    self->option_lane[self->option_count] = PUBLISH_TARGET_NEW;
    self->option_count++;
    if( selected < 0 || selected >= self->option_count )
        selected = 0;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->drop_target, self->options, self->option_count, selected);
    ToriRSChrome_SetHidden(ui->chrome, self->input_new, self->option_lane[selected] != PUBLISH_TARGET_NEW);

    ToriRSChrome_SetDisabled(ui->chrome, self->button_publish,
        !on_lane || !ui->lane.writable || self->action_pending || self->change_count == 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_discard,
        !on_lane || !ui->lane.writable || self->action_pending || self->change_count == 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_create, !ui->lane.known || self->action_pending);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_scratch, !ui->lane.known || self->action_pending);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_switch,
        !publish_v2(ui) || self->action_pending || picked < 0 || picked >= self->lane_row_count);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_refresh, !ui->lane.known || self->changes_pending);
    publish_filter_lanes(ui, self);
    publish_filter_changes(ui, self);
}

/** Reopen the squares this window shows: the server dropped its documents. */
static void
publish_reopen(struct EditorUi* ui)
{
    struct Editor_Doc* doc;
    int opened = 0;

    if( !ui->editor )
        return;
    doc = &ui->editor->doc;
    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        if( !square->loaded )
            continue;
        Editor_UiOpenSquare(ui, square->map_x, square->map_z, Editor_DocSquareStorage(doc, square), -1, opened > 0);
        opened++;
    }
    if( opened )
        Editor_UiLog(ui, "publish: reopened %d square(s) from the base", opened);
}

/** The scratch lane, once the list said which letters are taken. */
static void
publish_scratch_go(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];

    self->scratch_wanted = 0;
    if( !Editor_Lanes_ScratchNameFor(ui, (long long)time(NULL), name, (int)sizeof(name)) )
    {
        Editor_UiStatus(ui, "Scratch lane refused: today's 26 scratch lanes (a to z) exist; publish or delete one");
        return;
    }
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, name);
    if( !publish_send(ui, TORIRSMAPED_REQ_LANE_CREATE, &payload) )
    {
        Editor_UiStatus(ui, "Scratch lane refused: the editor server connection is gone");
        return;
    }
    snprintf(self->scratch_name, sizeof(self->scratch_name), "%s", name);
    self->action_pending = 1;
    ToriRSChrome_SetText(ui->chrome, self->label_status, "making the scratch lane...");
    Editor_UiStatus(ui, "Scratch lane %s: mirroring main...", name);
}

static void
publish_apply_fact(
    struct EditorUi* ui,
    struct PublishPanel* self,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    if( type == TORIRSMAPED_FACT_LANE_INFO )
    {
        char live[1024];
        char name[TORIRSMAPED_LANE_NAME_MAX + 8];
        char next_boot[TORIRSMAPED_LANE_NAME_MAX + 8];
        int is_main;
        int writable;
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        ToriRSMapEd_GetString(&reader, self->lane_content, sizeof(self->lane_content));
        ToriRSMapEd_GetString(&reader, live, sizeof(live));
        ToriRSMapEd_GetString(&reader, self->lane_base, sizeof(self->lane_base));
        is_main = (int)ToriRSMapEd_GetU32(&reader);
        writable = (int)ToriRSMapEd_GetU32(&reader);
        self->info_pending = 0;
        if( reader.bad )
            return;
        self->info_known = 1;
        /* The workbench tail: what the next boot serves. */
        ToriRSMapEd_GetString(&reader, next_boot, sizeof(next_boot));
        publish_lane_note(ui, name, is_main, writable, is_main ? 0 : -1, reader.bad ? NULL : next_boot);
    }
    else if( type == TORIRSMAPED_FACT_LANE_LIST )
    {
        uint32_t count = ToriRSMapEd_GetU32(&reader);
        uint32_t tail;
        self->lane_count = 0;
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            char content[1024];
            char base[1024];
            struct publish_lane lane;
            memset(&lane, 0, sizeof(lane));
            ToriRSMapEd_GetString(&reader, lane.name, sizeof(lane.name));
            ToriRSMapEd_GetString(&reader, content, sizeof(content));
            ToriRSMapEd_GetString(&reader, base, sizeof(base));
            lane.is_main = (int)ToriRSMapEd_GetU32(&reader);
            lane.is_this = (int)ToriRSMapEd_GetU32(&reader);
            lane.changes = -1;
            if( self->lane_count < PUBLISH_LANES_MAX )
                self->lanes[self->lane_count++] = lane;
        }
        /* The workbench tail: counts and lock holders, row for row. */
        tail = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < tail && !reader.bad; i++ )
        {
            char held[256];
            int32_t const changes = (int32_t)ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetString(&reader, held, sizeof(held));
            if( reader.bad || (int)i >= self->lane_count )
                break;
            self->lanes[i].changes = changes;
            snprintf(self->lanes[i].held, sizeof(self->lanes[i].held), "%s", held);
        }
        self->list_pending = 0;
        self->list_known = 1;
        if( self->scratch_wanted )
            publish_scratch_go(ui, self);
    }
    else if( type == TORIRSMAPED_FACT_LANE_CHANGES )
    {
        uint32_t rows;
        self->modified = (int)ToriRSMapEd_GetU32(&reader);
        self->added = (int)ToriRSMapEd_GetU32(&reader);
        self->deleted = (int)ToriRSMapEd_GetU32(&reader);
        rows = ToriRSMapEd_GetU32(&reader);
        self->change_count = 0;
        for( uint32_t i = 0; i < rows && !reader.bad; i++ )
        {
            struct publish_change change;
            change.kind = ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetString(&reader, change.path, sizeof(change.path));
            change.size = ToriRSMapEd_GetU32(&reader);
            if( self->change_count < PUBLISH_CHANGE_MAX )
                self->changes[self->change_count++] = change;
        }
        self->changes_known = 1;
        self->changes_pending = 0;
        if( ui->lane.known )
            publish_lane_note(ui, NULL, 0, -1, self->modified + self->added + self->deleted, NULL);
    }
    else if( type == TORIRSMAPED_FACT_LANE_COUNT )
    {
        char name[TORIRSMAPED_LANE_NAME_MAX + 8];
        char next_boot[TORIRSMAPED_LANE_NAME_MAX + 8];
        int is_main;
        int changes;
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        is_main = (int)ToriRSMapEd_GetU32(&reader);
        changes = (int)ToriRSMapEd_GetU32(&reader);
        changes += (int)ToriRSMapEd_GetU32(&reader);
        changes += (int)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, next_boot, sizeof(next_boot));
        self->count_pending = 0;
        if( !reader.bad )
            publish_lane_note(ui, name, is_main, -1, changes, next_boot);
    }
    else if( type == TORIRSMAPED_FACT_LANE_PUBLISHED )
    {
        uint32_t kind = ToriRSMapEd_GetU32(&reader);
        uint32_t status = ToriRSMapEd_GetU32(&reader);
        char message[1200];
        char target[64];
        uint32_t reload;
        ToriRSMapEd_GetString(&reader, message, sizeof(message));
        ToriRSMapEd_GetString(&reader, target, sizeof(target));
        (void)ToriRSMapEd_GetU32(&reader);
        (void)ToriRSMapEd_GetU32(&reader);
        reload = ToriRSMapEd_GetU32(&reader);
        self->action_pending = 0;
        ToriRSChrome_SetText(ui->chrome, self->label_status, message);
        if( kind == TORIRSMAPED_LANE_PUBLISHED_CREATE && self->scratch_name[0]
            && strcmp(target, self->scratch_name) == 0 )
        {
            /* Our scratch lane: made, now the session goes to it. */
            if( status != EDITOR_HOST_OK )
                Editor_UiStatus(ui, "Scratch lane refused: %s", message);
            else if( !publish_v2(ui) )
                Editor_UiStatus(ui,
                    "Scratch lane %s made; this editor server cannot switch lanes: boot the editor with lane=%s",
                    target, target);
            else
                (void)Editor_Lanes_Switch(ui, target);
            self->scratch_name[0] = '\0';
        }
        else if( kind == TORIRSMAPED_LANE_PUBLISHED_SWITCH )
        {
            if( status == EDITOR_HOST_OK )
            {
                publish_lane_note(ui, NULL, 0, -1, -1, target);
                if( strcmp(target, ui->lane.name) == 0 )
                    Editor_UiStatus(ui, "Switch: this editor serves %s already, and will from every boot on",
                        target);
                else
                    Editor_UiStatus(ui,
                        "Switched to %s for the next editor boot (build/lanes/lane.ini); this session stays on %s: "
                        "a running server cannot re-point its lane",
                        target, ui->lane.name);
            }
            else
                Editor_UiStatus(ui, "%s", message);
        }
        else
            Editor_UiStatus(ui, "%s%s: %s",
                kind == TORIRSMAPED_LANE_PUBLISHED_PUBLISH   ? "Publish"
                : kind == TORIRSMAPED_LANE_PUBLISHED_DISCARD ? "Discard"
                                                             : "New lane",
                status == EDITOR_HOST_OK ? "" : " refused", message);
        if( reload )
            publish_reopen(ui);
        /* The lane list and the changes moved: ask again. */
        self->refresh_wanted = 1;
        publish_ask_count(ui, self);
    }
}

static void
publish_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct PublishPanel* self = opaque;
    uint32_t type;
    uint8_t* body;
    uint32_t length;
    int heard = 0;
    int visible;

    if( !ui->editor )
        return;
    visible = ui->chrome->panels[self->panel].visible;
    if( visible && !self->was_visible )
        self->refresh_wanted = 1;
    self->was_visible = visible;
    /* The lane's name for every window, from the first frame: one cheap
     * LANE_INFO, whether the panel shows or not. */
    if( !self->info_known && !self->info_pending )
        publish_ask_info(ui, self);
    while( Editor_HostMapEdLanesTake(&ui->editor->host, &type, &body, &length) )
    {
        publish_apply_fact(ui, self, type, body, length);
        ToriMem_Free(ToriMem_Editor(), body);
        heard = 1;
    }
    {
        int const unsaved = Editor_DocHasUnsaved(&ui->editor->doc);
        if( self->seen_unsaved && !unsaved && ui->lane.known && !ui->lane.is_main )
        {
            self->refresh_wanted = 1;
            publish_ask_count(ui, self);
        }
        self->seen_unsaved = unsaved;
    }
    /* The chip's count, on its period (the count alone: no rows). */
    if( ui->lane.known && !ui->lane.is_main
        && (!self->count_ever_asked || ui->chrome->now_ms - self->count_asked_ms >= EDITOR_UI_LANE_COUNT_MS) )
        publish_ask_count(ui, self);
    if( self->refresh_wanted && visible )
        publish_ask(ui, self);
    if( self->discard_armed && ui->chrome->now_ms - self->discard_armed_ms >= PUBLISH_DISCARD_CONFIRM_MS )
    {
        self->discard_armed = 0;
        ToriRSChrome_SetLabel(ui->chrome, self->button_discard, "Discard...");
    }
    if( self->chord_serial != ui->hotkeys.serial )
        publish_chords(ui, self);
    if( !visible )
        return;
    if( heard || self->refresh_wanted )
        publish_refresh_widgets(ui, self);
    else
    {
        const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
        if( find && strcmp(find, self->shown_find) != 0 )
            publish_filter_lanes(ui, self);
        else if( ToriRSChrome_VListNeedsRows(ui->chrome, self->list_lanes) )
            publish_fill_lanes(ui, self);
        if( ToriRSChrome_VListNeedsRows(ui->chrome, self->list_changes) )
            publish_fill_rows(ui, self);
        edp_chips_hover(ui, &self->chips);
    }
}

static int
publish_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct PublishPanel* self = opaque;
    int on_x = 0;
    int index;

    if( event->widget != self->chips.graph )
        return 0;
    if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
        return 1;
    index = edp_chips_key_of(&self->chips, event->key, &on_x);
    if( index >= 0 && index < PUBLISH_CHIPS )
    {
        self->show_kind[index] = !self->show_kind[index];
        publish_filter_changes(ui, self);
    }
    return 1;
}

static int
publish_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct PublishPanel* self = opaque;
    struct ToriRSMapEdBuf payload;
    int selected;

    if( widget == self->drop_target )
    {
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->input_find || widget == self->list_changes || widget == self->input_new )
        return 1;
    if( widget == self->list_lanes )
    {
        /* Enter on a lane: its Switch. */
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_refresh )
    {
        self->changes_known = 0;
        publish_ask(ui, self);
        publish_ask_count(ui, self);
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_scratch )
    {
        (void)Editor_Lanes_Scratch(ui);
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_switch )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_lanes);
        if( row < 0 || row >= self->lane_row_count )
            Editor_UiStatus(ui, "Switch refused: pick a lane in the list first");
        else
            (void)Editor_Lanes_Switch(ui, self->lanes[self->lane_rows[row]].name);
        return 1;
    }
    if( widget == self->button_publish )
    {
        selected = ToriRSChrome_DropdownSelected(ui->chrome, self->drop_target);
        if( selected < 0 || selected >= self->option_count )
            return 1;
        memset(&payload, 0, sizeof(payload));
        if( self->option_lane[selected] == PUBLISH_TARGET_NEW )
        {
            const char* name = ToriRSChrome_Text(ui->chrome, self->input_new);
            if( !name || !publish_name_valid(name) )
            {
                Editor_UiStatus(ui, "Publish refused: name the new lane (a-z, 0-9, _ and -, not main)");
                return 1;
            }
            ToriRSMapEd_PutString(&payload, "");
            ToriRSMapEd_PutString(&payload, name);
        }
        else
        {
            ToriRSMapEd_PutString(&payload, self->lanes[self->option_lane[selected]].name);
            ToriRSMapEd_PutString(&payload, "");
        }
        ToriRSMapEd_PutU32(&payload,
            ToriRSChrome_Checked(ui->chrome, self->check_keep) ? 0u : (uint32_t)TORIRSMAPED_LANE_PUBLISH_RESET);
        if( publish_send(ui, TORIRSMAPED_REQ_LANE_PUBLISH, &payload) )
        {
            self->action_pending = 1;
            ToriRSChrome_SetText(ui->chrome, self->label_status, "publishing...");
        }
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_discard )
    {
        if( !self->discard_armed )
        {
            self->discard_armed = 1;
            self->discard_armed_ms = ui->chrome->now_ms;
            ToriRSChrome_SetLabel(ui->chrome, self->button_discard, "Discard: sure?");
            Editor_UiStatus(ui, "Discard: press again within 4 s to put %d change(s) back to the base, unsaved "
                                "edits too",
                self->modified + self->added + self->deleted);
            return 1;
        }
        self->discard_armed = 0;
        ToriRSChrome_SetLabel(ui->chrome, self->button_discard, "Discard...");
        memset(&payload, 0, sizeof(payload));
        if( publish_send(ui, TORIRSMAPED_REQ_LANE_DISCARD, &payload) )
        {
            self->action_pending = 1;
            ToriRSChrome_SetText(ui->chrome, self->label_status, "discarding...");
        }
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_create )
    {
        const char* name = ToriRSChrome_Text(ui->chrome, self->input_new);
        if( !name || !publish_name_valid(name) )
        {
            Editor_UiStatus(ui, "New lane refused: name it in the box (a-z, 0-9, _ and -, not main)");
            ToriRSChrome_SetHidden(ui->chrome, self->input_new, 0);
            return 1;
        }
        memset(&payload, 0, sizeof(payload));
        ToriRSMapEd_PutString(&payload, name);
        if( publish_send(ui, TORIRSMAPED_REQ_LANE_CREATE, &payload) )
        {
            self->action_pending = 1;
            ToriRSChrome_SetText(ui->chrome, self->label_status, "making the lane...");
        }
        publish_refresh_widgets(ui, self);
        return 1;
    }
    return 0;
}

/** The editor opened or closed: the panel starts (and stays) hidden until
 *  File > Publish... opens it, like the Live panel; TORIRS_EDITOR_PUBLISH=show
 *  (photographs) opens it with the editor. */
static void
publish_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct PublishPanel* self = opaque;
    const char* value = getenv("TORIRS_EDITOR_PUBLISH");
    int const show = visible && value && strcmp(value, "show") == 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, show);
    if( show )
        self->refresh_wanted = 1;
}

static void
publish_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct PublishPanel* self = opaque;
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), self->change_rows);
    ToriMem_Free(ToriMem_Editor(), self->changes);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Publish_Register(struct EditorUi* ui)
{
    struct PublishPanel* self;
    struct EditorUiPanel desc;
    static const char* const k_first_options[] = { "New lane..." };

    assert(ui);
    if( !ui->editor )
        return;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->changes = ToriMem_Calloc(ToriMem_Editor(), PUBLISH_CHANGE_MAX, sizeof(*self->changes));
    assert(self->changes);
    self->change_rows = ToriMem_Calloc(ToriMem_Editor(), PUBLISH_CHANGE_MAX, sizeof(*self->change_rows));
    assert(self->change_rows);
    self->seen_unsaved = Editor_DocHasUnsaved(&ui->editor->doc);
    for( int i = 0; i < PUBLISH_CHIPS; i++ )
        self->show_kind[i] = 1;

    self->panel = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, 300), edp_px(ui, 28), edp_px(ui, 300), "Lanes");
    assert(self->panel >= 0);
    self->label_lane = ToriRSChrome_Label(ui->chrome, self->panel, "Lane: asking the editor server...");
    self->label_base = ToriRSChrome_Label(ui->chrome, self->panel, "");
    self->input_find = ToriRSChrome_TextInput(ui->chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(ui->chrome, self->input_find, "part of a lane's name");
    self->list_lanes = ToriRSChrome_VList(ui->chrome, self->panel, "Lanes", PUBLISH_LANE_ROWS, 0);
    ToriRSChrome_SetTooltip(ui->chrome, self->list_lanes, "Every lane: its change count; Switch... goes to it");
    self->label_counts = ToriRSChrome_Label(ui->chrome, self->panel, "");
    edp_chips_init(ui, &self->chips, self->panel, 1);
    self->list_changes = ToriRSChrome_VList(ui->chrome, self->panel, "Changes", PUBLISH_ROWS, 0);
    ToriRSChrome_Separator(ui->chrome, self->panel);
    self->drop_target = ToriRSChrome_Dropdown(ui->chrome, self->panel, "Publish to", k_first_options, 1, 0);
    self->input_new = ToriRSChrome_TextInput(ui->chrome, self->panel, "New lane name", "");
    self->check_keep = ToriRSChrome_Checkbox(ui->chrome, self->panel, "Keep the changes here after publishing", 0);
    ToriRSChrome_SetTooltip(ui->chrome, self->check_keep,
        "Unticked: publishing into this lane's base leaves the lane clean (its files are the base's again)");
    self->button_publish = ToriRSChrome_Button(ui->chrome, self->panel, "Publish to...");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_publish,
        "Save every dirty document, then write this lane's changes into the target");
    self->button_scratch = ToriRSChrome_Button(ui->chrome, self->panel, "Scratch lane");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_scratch, "A dated lane from main; the next boot serves it");
    self->button_switch = ToriRSChrome_Button(ui->chrome, self->panel, "Switch...");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_switch, "The lane picked above: the next editor boot serves it");
    self->button_discard = ToriRSChrome_Button(ui->chrome, self->panel, "Discard...");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_discard,
        "Every change back to the base, unsaved edits included (press twice)");
    self->button_create = ToriRSChrome_Button(ui->chrome, self->panel, "New lane");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_create, "A lane mirrored from the main tree, nothing published");
    self->button_refresh = ToriRSChrome_Button(ui->chrome, self->panel, "Refresh");
    self->label_status = ToriRSChrome_Label(ui->chrome, self->panel, "");
    publish_chords(ui, self);
    ToriRSChrome_PanelSetResizable(ui->chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "publish";
    desc.title = "Lanes";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = publish_tick;
    desc.activate = publish_activate;
    desc.event = publish_event;
    desc.set_visible = publish_set_visible;
    desc.destroy = publish_destroy;
    Editor_UiAddPanel(ui, &desc);
}

int
Editor_Publish_Open(struct EditorUi* ui)
{
    assert(ui);
    if( !Editor_UiFindPanel(ui, "publish") )
    {
        Editor_UiLog(ui, "publish: this window has no Lanes panel (no editor server)");
        return 0;
    }
    return Editor_UiTogglePanel(ui, "publish");
}

int
Editor_Lanes_Show(struct EditorUi* ui)
{
    struct PublishPanel* self;

    assert(ui);
    self = publish_self(ui);
    if( !self )
    {
        Editor_UiStatus(ui, "Lanes: this window has no Lanes panel (no editor server)");
        return 0;
    }
    if( !ui->chrome->panels[self->panel].visible )
        (void)Editor_UiTogglePanel(ui, "publish");
    return 1;
}

int
Editor_LanesList(
    struct EditorUi* ui,
    struct Editor_LaneList* out)
{
    struct PublishPanel* self;

    assert(ui);
    assert(out);
    out->count = 0;
    self = publish_self(ui);
    if( !self || !self->list_known )
        return 0;
    for( int i = 0; i < self->lane_count && out->count < EDITOR_LANES_LIST_MAX; i++ )
    {
        struct Editor_LaneListRow* row = &out->rows[out->count++];
        snprintf(row->name, sizeof(row->name), "%s", self->lanes[i].name);
        row->is_this = self->lanes[i].is_this;
        publish_lane_row_text(&self->lanes[i], row->summary, sizeof(row->summary));
    }
    return out->count;
}

int
Editor_Lanes_ScratchNameFor(
    struct EditorUi* ui,
    long long when,
    char* out,
    int out_size)
{
    struct PublishPanel* self;
    const char* taken[PUBLISH_LANES_MAX];
    char day[16];
    time_t const seconds = (time_t)when;
    struct tm local;
    int count = 0;

    assert(ui);
    assert(out);
    self = publish_self(ui);
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    snprintf(day, sizeof(day), "%02d%02d%02d", local.tm_year % 100, local.tm_mon + 1, local.tm_mday);
    if( self )
        for( int i = 0; i < self->lane_count; i++ )
            taken[count++] = self->lanes[i].name;
    return Editor_LanesScratchName(day, taken, count, out, out_size);
}

int
Editor_Lanes_Scratch(struct EditorUi* ui)
{
    struct PublishPanel* self;

    assert(ui);
    self = publish_self(ui);
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Scratch lane refused: this window has no editor server");
        return 0;
    }
    if( self->action_pending || self->scratch_wanted )
    {
        Editor_UiStatus(ui, "Scratch lane refused: a lane action is in flight; wait for it");
        return 0;
    }
    /* The letters taken are the list's: ask it afresh, then make the lane. */
    self->scratch_wanted = 1;
    publish_ask_list(ui, self);
    Editor_UiStatus(ui, "Scratch lane: reading the lanes for a free name...");
    return 1;
}

int
Editor_Lanes_Switch(
    struct EditorUi* ui,
    const char* name)
{
    struct PublishPanel* self;
    struct ToriRSMapEdBuf payload;

    assert(ui);
    assert(name);
    self = publish_self(ui);
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Switch refused: this window has no editor server");
        return 0;
    }
    if( !publish_v2(ui) )
    {
        Editor_UiStatus(ui, "Switch refused: this editor server predates lane switching; boot with lane=%s", name);
        return 0;
    }
    if( ui->lane.known && strcmp(name, ui->lane.name) == 0 && !ui->lane.next_boot[0] )
    {
        Editor_UiStatus(ui, "Switch refused: this editor serves %s already", name);
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, name);
    if( !publish_send(ui, TORIRSMAPED_REQ_LANE_SWITCH, &payload) )
    {
        Editor_UiStatus(ui, "Switch refused: the editor server connection is gone");
        return 0;
    }
    self->action_pending = 1;
    ToriRSChrome_SetText(ui->chrome, self->label_status, "switching...");
    return 1;
}
