/*
 * The Live panel (live_panel.h). A chrome WINDOW, hidden until the View menu
 * opens it; everything it shows comes from the Editor_LiveModel the
 * transport's facts fill, and every button is one request.
 */

#include "live_panel.h"

#include "editor/editor_ui.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_live.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum
{
    LIVE_VAR_ROWS = 6,
    LIVE_STALE_ROWS = 4,
    LIVE_LOG_ROWS = 8,
    LIVE_FOLLOW_VARPS_MAX = 64,
};

struct LivePanel
{
    struct Editor_LiveTransport transport;
    struct Editor_LiveModel model;

    int panel;
    int label_status;
    int label_run;
    int input_address;
    int input_ticket;
    int button_attach;
    int button_launch;
    int button_detach;
    int button_run;
    int button_packs;
    int button_client;
    int input_player;
    int input_varps;
    int button_follow;
    int button_unfollow;
    int list_vars;
    int input_cheat;
    int button_cheat;
    int button_tele;
    int list_stale;
    int list_log;

    int shown_status_serial;
    int shown_vars_serial;
    int shown_log_serial;
    int shown_run_serial;
    int shown_build_serial;
    int log_follow;

    /** Who this window follows ("" nobody), and the varps it asked for. */
    char following[64];
};

static void
live_set_rows(
    struct EditorUi* ui,
    int list,
    int count,
    const char* (*row_text)(struct LivePanel*, int, char*, size_t),
    struct LivePanel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, list, count);
    if( !ToriRSChrome_VListWindow(ui->chrome, list, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < count; row++ )
    {
        char text[EDITOR_LIVE_LOG_LINE_MAX];
        ToriRSChrome_VListSetRow(ui->chrome, list, row, row_text(self, row, text, sizeof(text)));
    }
}

static const char*
live_var_row(
    struct LivePanel* self,
    int row,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "varp %u = %d", self->model.vars[row].varp, (int)self->model.vars[row].value);
    return out;
}

static const char*
live_stale_row(
    struct LivePanel* self,
    int row,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s", self->model.stale[row]);
    return out;
}

static const char*
live_log_row(
    struct LivePanel* self,
    int row,
    char* out,
    size_t out_size)
{
    /* Row 0 is the oldest kept. */
    const char* line = Editor_LiveLogLine(&self->model, self->model.log_count - 1 - row);
    snprintf(out, out_size, "%s", line ? line : "");
    return out;
}

static void
live_refresh(
    struct EditorUi* ui,
    struct LivePanel* self)
{
    const struct Editor_LiveModel* m = &self->model;
    int const attached = m->state == TORIRSMAPED_LIVE_ATTACHED;
    int const connecting = m->state == TORIRSMAPED_LIVE_CONNECTING;
    int const running = m->run_step != TORIRSMAPED_RUN_STEP_IDLE;
    char text[512];

    if( self->shown_status_serial != m->status_serial || self->shown_run_serial != m->run_serial
        || self->shown_build_serial != m->build_serial )
    {
        if( attached )
            snprintf(text, sizeof(text), "%s %s: %s, %u player(s), tick %u%s",
                m->launched ? "launched" : "attached", m->address, m->server_name, m->players, m->tick,
                m->stale_count ? ", stale units" : "");
        else if( connecting )
            snprintf(text, sizeof(text), "connecting to %s", m->address);
        else
            snprintf(text, sizeof(text), "%s%s%s", ToriRSMapEd_LiveStateName(m->state),
                m->message[0] ? ": " : "", m->message);
        ToriRSChrome_SetText(ui->chrome, self->label_status, text);

        if( running )
            snprintf(text, sizeof(text), "RUN: %s ...", ToriRSMapEd_RunStepName(m->run_step));
        else if( m->run_serial > 0 )
            snprintf(text, sizeof(text), "RUN %s: %s", m->run_status == 0 ? "done" : "stopped",
                m->run_message);
        else
            snprintf(text, sizeof(text), "pending: %u square(s), %u spawn file(s), %u archive(s)",
                m->pending_squares, m->pending_spawns, m->pending_archives);
        ToriRSChrome_SetText(ui->chrome, self->label_run, text);

        ToriRSChrome_SetDisabled(ui->chrome, self->button_attach, running || connecting);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_launch, running || connecting);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_detach, !(attached || connecting));
        ToriRSChrome_SetDisabled(ui->chrome, self->button_run, running || m->building);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_packs, running || m->building);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_client, running || m->building);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_cheat, !attached);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_tele, !attached);
        live_set_rows(ui, self->list_stale, m->stale_count, live_stale_row, self);
        self->shown_status_serial = m->status_serial;
        self->shown_run_serial = m->run_serial;
        self->shown_build_serial = m->build_serial;
    }
    if( self->shown_vars_serial != m->vars_serial )
    {
        live_set_rows(ui, self->list_vars, m->var_count, live_var_row, self);
        self->shown_vars_serial = m->vars_serial;
    }
    if( self->shown_log_serial != m->log_serial )
    {
        int first;
        int shown;
        ToriRSChrome_VListSetCount(ui->chrome, self->list_log, m->log_count);
        if( self->log_follow )
            ToriRSChrome_VListSetScroll(ui->chrome, self->list_log, m->log_count - LIVE_LOG_ROWS);
        live_set_rows(ui, self->list_log, m->log_count, live_log_row, self);
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_log, &first, &shown) )
            self->log_follow = first + shown >= m->log_count;
        self->shown_log_serial = m->log_serial;
    }
}

static void
live_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct LivePanel* self = opaque;

    Editor_LiveDrain(&self->transport, &self->model);
    live_refresh(ui, self);
}

static int
live_send(
    struct EditorUi* ui,
    struct LivePanel* self,
    uint32_t type,
    struct ToriRSMapEdBuf* payload)
{
    if( Editor_LiveSend(&self->transport, type, payload) )
        return 1;
    Editor_UiLog(ui, "live: the editor server connection is gone");
    return 0;
}

static int
live_publish_focus(
    struct EditorUi* ui,
    struct LivePanel* self,
    const char* name)
{
    int32_t values[TORIRSMAPED_LIVE_FOCUS_VALUES];

    ToriRSMapEd_LiveFocusPack(name, -1, -1, -1, values);
    if( self->transport.publish )
        return self->transport.publish(
            self->transport.user, TORIRSMAPED_STATE_LIVE_FOCUS, values, TORIRSMAPED_LIVE_FOCUS_VALUES);
    return Editor_UiPublish(ui, TORIRSMAPED_STATE_LIVE_FOCUS, values, TORIRSMAPED_LIVE_FOCUS_VALUES);
}

/** The selection as an absolute tile: the first selected loc, else the
 *  latched tile, else the rectangle's centre. 0 when nothing is selected. */
static int
live_selection_tile(
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
live_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct LivePanel* self = opaque;
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    memset(&payload, 0, sizeof(payload));
    if( widget == self->button_attach || widget == self->button_launch )
    {
        int const launch = widget == self->button_launch;
        const char* address = ToriRSChrome_Text(ui->chrome, self->input_address);
        if( !launch && !strchr(address, ':') )
        {
            Editor_UiLog(ui, "live: attach needs host:port");
            return 1;
        }
        type = Editor_LiveEncodeAttach(
            &payload, launch, address, ToriRSChrome_Text(ui->chrome, self->input_ticket));
        live_send(ui, self, type, &payload);
        return 1;
    }
    if( widget == self->button_detach )
    {
        type = Editor_LiveEncodeDetach(&payload);
        live_send(ui, self, type, &payload);
        return 1;
    }
    if( widget == self->button_run )
    {
        type = Editor_LiveEncodeRun(&payload);
        if( live_send(ui, self, type, &payload) )
            Editor_UiLog(ui, "RUN: save, build packs, build the client cache, reload the live server");
        return 1;
    }
    if( widget == self->button_packs || widget == self->button_client )
    {
        type = Editor_LiveEncodeBuild(&payload,
            widget == self->button_packs ? TORIRSMAPED_BUILD_PACKS : TORIRSMAPED_BUILD_CLIENT_CACHE);
        live_send(ui, self, type, &payload);
        return 1;
    }
    if( widget == self->button_follow )
    {
        const char* player = ToriRSChrome_Text(ui->chrome, self->input_player);
        uint32_t varps[LIVE_FOLLOW_VARPS_MAX];
        int n = Editor_LiveParseVarps(
            ToriRSChrome_Text(ui->chrome, self->input_varps), varps, LIVE_FOLLOW_VARPS_MAX);
        if( !player[0] )
        {
            Editor_UiLog(ui, "live: name the player to follow");
            return 1;
        }
        if( n < 0 )
        {
            Editor_UiLog(ui, "live: varps are ids separated by commas (\"281, 1048\")");
            return 1;
        }
        snprintf(self->following, sizeof(self->following), "%s", player);
        type = Editor_LiveEncodeSubscribe(&payload, player, varps, n);
        live_send(ui, self, type, &payload);
        live_publish_focus(ui, self, player);
        Editor_UiLog(ui, "live: following %s (%d varp(s))", player, n);
        return 1;
    }
    if( widget == self->button_unfollow )
    {
        type = Editor_LiveEncodeSubscribe(&payload, self->following, NULL, 0);
        live_send(ui, self, type, &payload);
        self->following[0] = '\0';
        live_publish_focus(ui, self, "");
        return 1;
    }
    if( widget == self->button_cheat || widget == self->input_cheat )
    {
        const char* line = ToriRSChrome_Text(ui->chrome, self->input_cheat);
        const char* player = ToriRSChrome_Text(ui->chrome, self->input_player);
        if( !line[0] )
            return 1;
        type = Editor_LiveEncodeCheat(&payload, player, line);
        if( live_send(ui, self, type, &payload) )
            ToriRSChrome_SetText(ui->chrome, self->input_cheat, "");
        return 1;
    }
    if( widget == self->button_tele )
    {
        const char* player = ToriRSChrome_Text(ui->chrome, self->input_player);
        int x;
        int z;
        int level;
        char coord[64];
        char line[96];
        if( !player[0] )
        {
            Editor_UiLog(ui, "live: name the player to teleport");
            return 1;
        }
        if( !live_selection_tile(ui, &x, &z, &level) )
        {
            Editor_UiLog(ui, "live: select a tile or a loc first");
            return 1;
        }
        Editor_LiveTeleCoord(x, z, level < 0 ? 0 : level, coord, sizeof(coord));
        snprintf(line, sizeof(line), "tele %s", coord);
        type = Editor_LiveEncodeCheat(&payload, player, line);
        if( live_send(ui, self, type, &payload) )
            Editor_UiLog(ui, "live: ::%s for %s", line, player);
        return 1;
    }
    if( widget == self->list_vars || widget == self->list_stale || widget == self->list_log
        || widget == self->input_address || widget == self->input_ticket
        || widget == self->input_player || widget == self->input_varps )
        return 1;
    return 0;
}

static void
live_on_state(
    struct EditorUi* ui,
    void* opaque,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct LivePanel* self = opaque;
    char name[TORIRSMAPED_LIVE_FOCUS_NAME_MAX + 1];

    if( key != TORIRSMAPED_STATE_LIVE_FOCUS )
        return;
    if( !ToriRSMapEd_LiveFocusName(values, count, name, sizeof(name)) )
        return;
    /* Another window of this Client followed someone: show whom. */
    snprintf(self->following, sizeof(self->following), "%s", name);
    if( strcmp(ToriRSChrome_Text(ui->chrome, self->input_player), name) != 0 )
        ToriRSChrome_SetText(ui->chrome, self->input_player, name);
}

/** The editor opened or closed: the Live panel starts (and stays) hidden
 *  until the View menu opens it, like the other on-demand panels. */
static void
live_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct LivePanel* self = opaque;
    (void)visible;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
}

static void
live_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Live_RegisterWith(
    struct EditorUi* ui,
    const struct Editor_LiveTransport* transport)
{
    struct LivePanel* self;
    struct EditorUiPanel desc;
    int const scale = ToriRSChrome_Scale(ui->chrome);

    assert(ui);
    assert(transport);
    assert(transport->send);
    assert(transport->take);

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->transport = *transport;
    Editor_LiveModelInit(&self->model);
    self->shown_status_serial = -1;
    self->shown_vars_serial = -1;
    self->shown_log_serial = -1;
    self->shown_run_serial = -1;
    self->shown_build_serial = -1;
    self->log_follow = 1;

    self->panel = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_WINDOW, 260 * scale, 28 * scale, 300 * scale, "Live");
    assert(self->panel >= 0);
    self->label_status = ToriRSChrome_Label(ui->chrome, self->panel, "detached");
    self->label_run = ToriRSChrome_Label(ui->chrome, self->panel, "");
    self->input_address = ToriRSChrome_TextInput(ui->chrome, self->panel, "Address", "127.0.0.1:43594");
    self->input_ticket = ToriRSChrome_TextInput(ui->chrome, self->panel, "Ticket", "");
    self->button_attach = ToriRSChrome_Button(ui->chrome, self->panel, "Attach");
    self->button_launch = ToriRSChrome_Button(ui->chrome, self->panel, "Launch");
    self->button_detach = ToriRSChrome_Button(ui->chrome, self->panel, "Detach");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_attach, "Connect to a running game server at the address");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_launch,
        "Start the lane's game server, JS5 and io servers and connect");
    ToriRSChrome_Separator(ui->chrome, self->panel);
    self->button_run = ToriRSChrome_Button(ui->chrome, self->panel, "RUN");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_run,
        "Save all, build packs and client cache, reload the live server");
    self->button_packs = ToriRSChrome_Button(ui->chrome, self->panel, "Build packs");
    self->button_client = ToriRSChrome_Button(ui->chrome, self->panel, "Build client cache");
    ToriRSChrome_Separator(ui->chrome, self->panel);
    self->input_player = ToriRSChrome_TextInput(ui->chrome, self->panel, "Player", "");
    self->input_varps = ToriRSChrome_TextInput(ui->chrome, self->panel, "Varps", "");
    self->button_follow = ToriRSChrome_Button(ui->chrome, self->panel, "Follow");
    self->button_unfollow = ToriRSChrome_Button(ui->chrome, self->panel, "Stop following");
    self->list_vars = ToriRSChrome_VList(ui->chrome, self->panel, "Vars", LIVE_VAR_ROWS, 0);
    self->input_cheat = ToriRSChrome_TextInput(ui->chrome, self->panel, "Cheat", "");
    self->button_cheat = ToriRSChrome_Button(ui->chrome, self->panel, "Send");
    self->button_tele = ToriRSChrome_Button(ui->chrome, self->panel, "Teleport to selection");
    self->list_stale = ToriRSChrome_VList(ui->chrome, self->panel, "Stale", LIVE_STALE_ROWS, 0);
    self->list_log = ToriRSChrome_VList(ui->chrome, self->panel, "Log", LIVE_LOG_ROWS, 0);
    ToriRSChrome_PanelSetResizable(ui->chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "live";
    desc.title = "Live";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = live_tick;
    desc.activate = live_activate;
    desc.on_state = live_on_state;
    desc.set_visible = live_set_visible;
    desc.destroy = live_destroy;
    Editor_UiAddPanel(ui, &desc);
}

const struct Editor_LiveModel*
Editor_LivePanelModel(struct EditorUi* ui)
{
    struct LivePanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "live");
    return self ? &self->model : NULL;
}
