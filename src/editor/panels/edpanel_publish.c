/*
 * The Publish panel (module "publish", File > Publish..., Ctrl+Shift+U):
 * where this window's edits land and where they go next (owner, 2026-11-02:
 * "Edits should go to a dedicated 'working' lane and then there should be a
 * publish menu that publishes to a target lane or a new lane").
 *
 * The editor server serves a LANE (torirsmaped/torirs_maped_lanes.h): the
 * main tree itself, or a named lane -- build/lanes/<name>/content, a
 * copy-on-write mirror of the tree that RUN builds and plays on its own. The
 * panel shows the lane, its base and its changes against the base (modified
 * / added / deleted paths), and offers:
 *
 *   Publish          every dirty document is saved, then the changes are
 *                    written into the target: the main tree ("main"), another
 *                    lane, or "New lane..." with a name. When the target is
 *                    this lane's base, the lane is clean afterwards (its
 *                    published files are links to the base again) unless
 *                    "Keep the changes here" is ticked.
 *   Discard changes  every change back to the base; the server drops its
 *                    documents, and every window reopens the square it shows.
 *   New lane         a lane mirrored from the main tree, nothing published.
 *   Refresh          scan the changes again (a save does it too).
 *
 * A server on the main tree has nothing to publish: the panel says so and
 * how to start on a lane. The requests go through the host's lanes frames
 * (Editor_HostMapEdLanesSend / Take, editor_host.h); every answer is a
 * broadcast, so every window's panel shows the same lane.
 */
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

enum
{
    PUBLISH_ROWS = 6,
    PUBLISH_CHANGE_MAX = 1024,
    PUBLISH_LANES_MAX = TORIRSMAPED_LANES_MAX,
    /** The dropdown: every lane but this one, then "New lane...". */
    PUBLISH_TARGET_NEW = -1,
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
};

struct PublishPanel
{
    int panel;
    int label_lane;
    int label_base;
    int label_counts;
    int list_changes;
    int drop_target;
    int input_new;
    int check_keep;
    int button_publish;
    int button_discard;
    int button_create;
    int button_refresh;
    int label_status;

    /* the lane, as the server last said */
    char lane_name[TORIRSMAPED_LANE_NAME_MAX + 8];
    char lane_content[1024];
    char lane_base[1024];
    int lane_is_main;
    int lane_writable;
    int info_known;

    struct publish_lane lanes[PUBLISH_LANES_MAX];
    int lane_count;
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

    /** Asked the server, nothing heard yet (a request per kind). */
    int info_pending;
    int list_pending;
    int changes_pending;
    int action_pending;
    /** The panel refreshes when it opens and after a save (the document's
     *  unsaved state falling back to clean). */
    int refresh_wanted;
    int seen_unsaved;
    /** The panel's visibility last tick: opening it (the hotkey, the View
     *  menu) asks the server afresh. */
    int was_visible;
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

static void
publish_ask(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_INFO, &payload) )
        self->info_pending = 1;
    memset(&payload, 0, sizeof(payload));
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_LIST, &payload) )
        self->list_pending = 1;
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

static void
publish_fill_rows(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, self->list_changes, self->change_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_changes, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < self->change_count; row++ )
    {
        char text[300];
        const struct publish_change* change = &self->changes[row];
        if( change->kind == TORIRSMAPED_LANE_CHANGE_DELETED )
            snprintf(text, sizeof(text), "%s %s", publish_kind_letter(change->kind), change->path);
        else
            snprintf(text, sizeof(text), "%s %s (%u bytes)", publish_kind_letter(change->kind), change->path,
                change->size);
        ToriRSChrome_VListSetRow(ui->chrome, self->list_changes, row, text);
    }
}

static void
publish_refresh_widgets(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    char text[1200];
    int const on_lane = self->info_known && !self->lane_is_main;
    int selected = ToriRSChrome_DropdownSelected(ui->chrome, self->drop_target);

    if( !self->info_known )
        snprintf(text, sizeof(text), "Lane: asking the editor server...");
    else if( self->lane_is_main )
        snprintf(text, sizeof(text), "Lane: main -- this editor edits the tree itself (%s)", self->lane_content);
    else
        snprintf(text, sizeof(text), "Lane: %s%s", self->lane_name, self->lane_writable ? "" : " [read-only]");
    ToriRSChrome_SetText(ui->chrome, self->label_lane, text);
    if( on_lane )
        snprintf(text, sizeof(text), "Base: %s", self->lane_base);
    else if( self->info_known )
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
        !on_lane || !self->lane_writable || self->action_pending || self->change_count == 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_discard,
        !on_lane || !self->lane_writable || self->action_pending || self->change_count == 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_create, !self->info_known || self->action_pending);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_refresh, !self->info_known || self->changes_pending);
    publish_fill_rows(ui, self);
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
        ToriRSMapEd_GetString(&reader, self->lane_name, sizeof(self->lane_name));
        ToriRSMapEd_GetString(&reader, self->lane_content, sizeof(self->lane_content));
        ToriRSMapEd_GetString(&reader, live, sizeof(live));
        ToriRSMapEd_GetString(&reader, self->lane_base, sizeof(self->lane_base));
        self->lane_is_main = (int)ToriRSMapEd_GetU32(&reader);
        self->lane_writable = (int)ToriRSMapEd_GetU32(&reader);
        self->info_known = !reader.bad;
        self->info_pending = 0;
    }
    else if( type == TORIRSMAPED_FACT_LANE_LIST )
    {
        uint32_t count = ToriRSMapEd_GetU32(&reader);
        self->lane_count = 0;
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            char content[1024];
            char base[1024];
            struct publish_lane lane;
            ToriRSMapEd_GetString(&reader, lane.name, sizeof(lane.name));
            ToriRSMapEd_GetString(&reader, content, sizeof(content));
            ToriRSMapEd_GetString(&reader, base, sizeof(base));
            lane.is_main = (int)ToriRSMapEd_GetU32(&reader);
            lane.is_this = (int)ToriRSMapEd_GetU32(&reader);
            if( self->lane_count < PUBLISH_LANES_MAX )
                self->lanes[self->lane_count++] = lane;
        }
        self->list_pending = 0;
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
        Editor_UiLog(ui, "%s%s: %s",
            kind == TORIRSMAPED_LANE_PUBLISHED_PUBLISH   ? "publish"
            : kind == TORIRSMAPED_LANE_PUBLISHED_DISCARD ? "discard"
                                                         : "new lane",
            status == EDITOR_HOST_OK ? "" : " FAILED", message);
        ToriRSChrome_SetText(ui->chrome, self->label_status, message);
        if( reload )
            publish_reopen(ui);
        /* The lane list and the changes moved: ask again. */
        self->refresh_wanted = 1;
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

    if( !ui->editor )
        return;
    {
        int const visible = ui->chrome->panels[self->panel].visible;
        if( visible && !self->was_visible )
            self->refresh_wanted = 1;
        self->was_visible = visible;
    }
    while( Editor_HostMapEdLanesTake(&ui->editor->host, &type, &body, &length) )
    {
        publish_apply_fact(ui, self, type, body, length);
        ToriMem_Free(ToriMem_Editor(), body);
        heard = 1;
    }
    {
        int const unsaved = Editor_DocHasUnsaved(&ui->editor->doc);
        if( self->seen_unsaved && !unsaved && self->info_known && !self->lane_is_main )
            self->refresh_wanted = 1;
        self->seen_unsaved = unsaved;
    }
    if( self->refresh_wanted && ui->chrome->panels[self->panel].visible )
        publish_ask(ui, self);
    if( heard || self->refresh_wanted )
        publish_refresh_widgets(ui, self);
    else if( ToriRSChrome_VListNeedsRows(ui->chrome, self->list_changes) )
        publish_fill_rows(ui, self);
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
    if( widget == self->button_refresh )
    {
        self->changes_known = 0;
        publish_ask(ui, self);
        publish_refresh_widgets(ui, self);
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
                Editor_UiLog(ui, "publish: name the new lane (a-z, 0-9, _ and -, not main)");
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
            Editor_UiLog(ui, "publish: name the new lane in the box (a-z, 0-9, _ and -, not main)");
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
    ToriMem_Free(ToriMem_Editor(), self->changes);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Publish_Register(struct EditorUi* ui)
{
    struct PublishPanel* self;
    struct EditorUiPanel desc;
    int const scale = ToriRSChrome_Scale(ui->chrome);
    static const char* const k_first_options[] = { "New lane..." };

    assert(ui);
    if( !ui->editor )
        return;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->changes = ToriMem_Calloc(ToriMem_Editor(), PUBLISH_CHANGE_MAX, sizeof(*self->changes));
    assert(self->changes);
    self->seen_unsaved = Editor_DocHasUnsaved(&ui->editor->doc);

    self->panel = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_WINDOW, 300 * scale, 28 * scale, 300 * scale, "Publish");
    assert(self->panel >= 0);
    self->label_lane = ToriRSChrome_Label(ui->chrome, self->panel, "Lane: asking the editor server...");
    self->label_base = ToriRSChrome_Label(ui->chrome, self->panel, "");
    self->label_counts = ToriRSChrome_Label(ui->chrome, self->panel, "");
    self->list_changes = ToriRSChrome_VList(ui->chrome, self->panel, "Changes", PUBLISH_ROWS, 0);
    ToriRSChrome_Separator(ui->chrome, self->panel);
    self->drop_target = ToriRSChrome_Dropdown(ui->chrome, self->panel, "Publish to", k_first_options, 1, 0);
    self->input_new = ToriRSChrome_TextInput(ui->chrome, self->panel, "New lane name", "");
    self->check_keep = ToriRSChrome_Checkbox(ui->chrome, self->panel, "Keep the changes here after publishing", 0);
    ToriRSChrome_SetTooltip(ui->chrome, self->check_keep,
        "Unticked: publishing into this lane's base leaves the lane clean (its files are the base's again)");
    self->button_publish = ToriRSChrome_Button(ui->chrome, self->panel, "Publish");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_publish,
        "Save every dirty document, then write this lane's changes into the target");
    self->button_discard = ToriRSChrome_Button(ui->chrome, self->panel, "Discard changes");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_discard,
        "Every change back to the base, unsaved edits included; every window reopens its square");
    self->button_create = ToriRSChrome_Button(ui->chrome, self->panel, "New lane");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_create, "A lane mirrored from the main tree, nothing published");
    self->button_refresh = ToriRSChrome_Button(ui->chrome, self->panel, "Refresh");
    self->label_status = ToriRSChrome_Label(ui->chrome, self->panel, "");
    ToriRSChrome_PanelSetResizable(ui->chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "publish";
    desc.title = "Publish";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = publish_tick;
    desc.activate = publish_activate;
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
        Editor_UiLog(ui, "publish: this window has no Publish panel (no editor server)");
        return 0;
    }
    return Editor_UiTogglePanel(ui, "publish");
}
