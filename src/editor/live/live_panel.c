/*
 * The Live panel (live_panel.h). A chrome WINDOW, hidden until the View menu
 * opens it; everything it shows comes from the Editor_LiveModel the
 * transport's facts fill, and every button is one request.
 */

#include "live_panel.h"

#include "editor/editor_ui.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_live.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
/* The page opens the lane's web play URL in a tab of its own (a browser
 * cannot start a native client). 1 when the browser gave a window. */
EM_JS(int, live_web_open, (const char* url), {
    var opened = window.open(UTF8ToString(url), '_blank');
    return opened ? 1 : 0;
});
#endif

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
    /** The RUN bar under the run line, hidden while no RUN is in flight. */
    int progress;
    /** The same bar as a small window at the foot of the MAIN window (a
     *  popped-out panel's window keeps its own bar in the panel): RUN is in
     *  the menu and the toolbar, so its progress must not need the Live
     *  panel open. Not an editor panel: never in the View menu, the dock or
     *  the layout file. */
    int float_panel;
    int float_label;
    int float_progress;
    int shown_progress_serial;
    int progress_shown;
    int label_play;
    int button_play;
    int button_play_stop;
    int input_address;
    int input_ticket;
    int button_attach;
    int button_launch;
    int button_detach;
    /** Detach then Launch: a lane this editor launched, started again. */
    int button_restart;
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
    int shown_play_serial;
    int log_follow;
    /** Web: LIVE_PLAY browser was sent (once per panel). */
    int browser_sent;
    /** Web: the RUN whose play tab was handled (one tab per RUN). */
    int web_run_handled;
    /** This window sent the RUN in flight (Editor_Live_Run): its RUN_DONE
     *  opens the play tab here. */
    int run_requested;
    /** TORIRS_EDITOR_LIVE=show (photographs): 0 unread, 1 show, 2 done. */
    int env_show;
    /** ui->hotkeys.serial RUN's keycap was labelled from. */
    uint32_t chord_serial;

    /* The last RUN's outcome, said once in the console and the status bar
     * when its FACT_RUN_DONE lands (live_run_outcome). */
    /** model.run_serial of the RUN whose outcome was said. */
    int said_run_serial;
    /** enum Editor_LiveRunOutcome and the sentence, "" before any RUN. */
    int outcome_kind;
    char outcome[EDITOR_LIVE_OUTCOME_MAX];
    /** When the RUN in flight was first seen (or sent from here), on the
     *  chrome's clock; `run_timed` 0 when nobody saw it start. */
    int run_timed;
    int64_t run_started_ms;
    /** The outcome was said while the model still showed the RUN's last
     *  step: cleared by the first tick that sees it idle. */
    int run_ended;
    /** model.build_serial at the last tick no RUN was in flight: a RUN that
     *  ends on another value ran a build, and its lines are the log's last. */
    int idle_build_serial;

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
    /* A row's text holds TORIRS_CHROME_INPUT_MAX bytes: every line is
     * composed to fit it. A longer one was cut by the chrome, which says so
     * in the Output console -- the only line a failed RUN used to leave
     * there. The Log list below keeps every message whole. */
    char text[TORIRS_CHROME_INPUT_MAX];

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
            /* The sentence the console and the status bar were given. */
            snprintf(text, sizeof(text), "%s", self->outcome);
        else
            snprintf(text, sizeof(text), "pending: %u square(s), %u spawn file(s), %u archive(s)",
                m->pending_squares, m->pending_spawns, m->pending_archives);
        ToriRSChrome_SetText(ui->chrome, self->label_run, text);

        ToriRSChrome_SetDisabled(ui->chrome, self->button_attach, running || connecting);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_launch, running || connecting);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_detach, !(attached || connecting));
        /* Only a lane this editor launched: a server it merely dialled is
         * somebody else's to stop, and launching beside it would fight it
         * for the lane's ports. */
        ToriRSChrome_SetDisabled(ui->chrome, self->button_restart,
            !((attached || connecting) && m->launched) || running || m->building);
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
    if( self->shown_progress_serial != m->progress_serial || self->shown_run_serial != m->run_serial
        || running != self->progress_shown )
    {
        Editor_LiveProgressLine(m, text, sizeof(text));
        if( running && text[0] )
        {
            char caption[TORIRS_CHROME_INPUT_MAX];
            snprintf(caption, sizeof(caption), "RUN %u%%: %s", m->progress_percent, text);
            ToriRSChrome_SetText(ui->chrome, self->label_run, caption);
            ToriRSChrome_SetText(ui->chrome, self->float_label, caption);
            ToriRSChrome_SetProgress(ui->chrome, self->progress, (int)m->progress_percent);
            ToriRSChrome_SetProgress(ui->chrome, self->float_progress, (int)m->progress_percent);
        }
        ToriRSChrome_SetHidden(ui->chrome, self->progress, !running);
        self->progress_shown = running;
        ToriRSChrome_PanelSetVisible(ui->chrome, self->float_panel, running && ui->solo_panel < 0);
        self->shown_progress_serial = m->progress_serial;
    }
    if( running && ui->solo_panel < 0 )
    {
        /* Bottom centre of the window, above nothing: the dock's columns
         * leave the middle of the foot free. */
        struct ToriRSChromeRect const rect = ToriRSChrome_PanelRect(ui->chrome, self->float_panel);
        int const w = rect.w > 0 ? rect.w : edp_px(ui, 420);
        int const h = rect.h > 0 ? rect.h : edp_px(ui, 56);
        ToriRSChrome_PanelMove(ui->chrome, self->float_panel, (ui->chrome->surface_w - w) / 2,
            ui->chrome->surface_h - h - edp_px(ui, 12));
    }
    if( self->shown_play_serial != m->play_serial || self->shown_run_serial != m->run_serial )
    {
        int const starting = m->play_state == TORIRSMAPED_PLAY_STARTING;
        int const stoppable = m->play_state == TORIRSMAPED_PLAY_STARTING || m->play_state == TORIRSMAPED_PLAY_ONLINE
                              || m->play_state == TORIRSMAPED_PLAY_LOGGED_OUT;
        Editor_LivePlayLine(m, text, sizeof(text));
        ToriRSChrome_SetText(ui->chrome, self->label_play, text);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_play, starting && !m->play_browser);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_play_stop, !stoppable || m->play_browser);
        self->shown_play_serial = m->play_serial;
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

/**
 * The FIRST error the last build printed, as "fenwick.rs2:7: unknown
 * variable '%'": the file by its name alone (the Problems panel holds the
 * path, and the sentence this goes in is one console line). 0 when none of
 * its lines reads as an error.
 *
 * The log is newest first: the build's DONE row, then its lines back to the
 * DONE row of the build before it. Rows with no build mark between them are
 * live lines.
 */
static int
live_first_error(
    const struct Editor_LiveModel* m,
    char* out,
    size_t out_size)
{
    int in_build = 0;
    int found = 0;

    out[0] = '\0';
    for( int i = 0; i < m->log_count; i++ )
    {
        uint32_t const mark = Editor_LiveLogWhat(m, i);
        struct Editor_ProblemRow row;
        const char* name;

        if( mark & EDITOR_LIVE_LOG_DONE )
        {
            if( in_build )
                break;
            in_build = 1;
            continue;
        }
        if( !in_build || mark == 0 )
            continue;
        if( !Editor_ProblemsParseLine(Editor_LiveLogLine(m, i), &row) || row.severity != EDITOR_PROBLEM_ERROR )
            continue;
        /* Older rows come later in this walk: the last one kept is the
         * first the build printed. */
        name = row.file;
        for( const char* c = row.file; *c; c++ )
            if( *c == '/' || *c == '\\' )
                name = c + 1;
        if( name[0] )
            snprintf(out, out_size, "%s:%d: %s", name, row.line, row.text);
        else
            snprintf(out, out_size, "%s", row.text);
        found = 1;
    }
    return found;
}

/** Bring the Problems panel to the front of its group (the bottom strip's),
 *  in a window that lays its panels out. */
static void
live_show_problems(struct EditorUi* ui)
{
    struct EditorUiPanel* problems;

    if( !ui->visible || ui->panels_hidden || ui->solo_panel >= 0 )
        return;
    problems = Editor_UiFindPanel(ui, "problems");
    if( !problems || problems->chrome_panel < 0 )
        return;
    if( Editor_UiGroupOf(ui, "problems") >= 0 )
        Editor_UiGroupShow(ui, "problems");
    else
        ToriRSChrome_PanelSetVisible(ui->chrome, problems->chrome_panel, 1);
}

/**
 * A RUN ended since the last tick: say how, once, where the user is looking
 * -- one line in the Output console and the status bar's sentence
 * (Editor_UiStatus does both), whether or not this panel is open. RUN is in
 * the File menu and the tool rail, so its outcome must not need the Live
 * panel either (2026-11-09: "RUN done" was all the main window said, for a
 * RUN whose new var needed a restart as much as for one that failed).
 */
static void
live_run_outcome(
    struct EditorUi* ui,
    struct LivePanel* self)
{
    const struct Editor_LiveModel* m = &self->model;
    int const running = m->run_step != TORIRSMAPED_RUN_STEP_IDLE;
    /* Refused before it began: the RUN it collided with is still in flight
     * and still timed. */
    int const refused = m->run_status != 0 && m->run_last_step == TORIRSMAPED_RUN_STEP_IDLE;
    char reason[112];
    int seconds = -1;
    int built;

    if( !running )
        self->run_ended = 0;
    /* A RUN another window (or torirsmapedctl) started is timed from the
     * tick that first sees it in flight. */
    if( running && !self->run_timed && !self->run_ended && ui->chrome->now_ms > 0 )
    {
        self->run_timed = 1;
        self->run_started_ms = ui->chrome->now_ms;
    }
    if( m->run_serial == self->said_run_serial )
    {
        if( !running )
            self->idle_build_serial = m->build_serial;
        return;
    }
    self->said_run_serial = m->run_serial;
    built = m->build_serial != self->idle_build_serial;
    if( self->run_timed && !refused )
        seconds = (int)((ui->chrome->now_ms - self->run_started_ms + 500) / 1000);

    reason[0] = '\0';
    /* The failed build's first diagnostic, when this RUN's build is the one
     * that failed (a RUN stopped before its build must not quote an older
     * build's errors). */
    if( m->run_status != 0 && built && m->last_build_status != 0 )
        (void)live_first_error(m, reason, sizeof(reason));
    self->outcome_kind = (int)Editor_LiveRunOutcomeLine(m, seconds, reason, self->outcome, sizeof(self->outcome));
    Editor_UiStatus(ui, "%s", self->outcome);

    /* The Problems list says why a RUN stopped until the next one ends, and
     * comes to the front: the build's diagnostics are its rows already. */
    if( refused )
        return;
    (void)Editor_Problems_Clear(ui, "RUN");
    if( self->outcome_kind == EDITOR_LIVE_RUN_STOPPED )
    {
        Editor_Problems_Report(ui, "RUN", "", 0, EDITOR_PROBLEM_ERROR,
            m->run_message[0] ? m->run_message : self->outcome);
        live_show_problems(ui);
    }
    /* This RUN is over. Its status (run_step back to IDLE) may land a tick
     * after its RUN_DONE: until it does, the step still showing is not a new
     * RUN to time. */
    self->run_timed = 0;
    self->run_ended = running;
    self->idle_build_serial = m->build_serial;
}

static void
live_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct LivePanel* self = opaque;

    /* RUN wears file.run's chord (design section 15 surface 3), relabelled
     * on the frame after a rebind. */
    if( self->chord_serial != ui->hotkeys.serial )
    {
        edp_button_chord(ui, self->button_run, EDITOR_HK_FILE_RUN);
        self->chord_serial = ui->hotkeys.serial;
    }
    Editor_LiveDrain(&self->transport, &self->model);
    live_run_outcome(ui, self);
#if defined(__EMSCRIPTEN__)
    /* A browser window plays the lane in a tab: tell the editor server once,
     * so its RUN never starts a native client for us. */
    if( !self->browser_sent )
    {
        struct ToriRSMapEdBuf payload;
        uint32_t type;
        memset(&payload, 0, sizeof(payload));
        type = Editor_LiveEncodePlay(&payload, TORIRSMAPED_PLAY_BROWSER);
        self->browser_sent = Editor_LiveSend(&self->transport, type, &payload);
    }
    /* After a RUN, a lane whose account is not on the server gets its play
     * tab (the RUN_DONE message said "open the play tab"). */
    if( self->model.run_serial != self->web_run_handled && self->model.run_step == TORIRSMAPED_RUN_STEP_IDLE )
    {
        int const mine = self->run_requested || ui->solo_panel < 0;
        self->web_run_handled = self->model.run_serial;
        /* One tab per RUN: every window of the Client hears RUN_DONE, so a
         * popped-out panel's tab opens none unless it pressed RUN itself
         * (two opened, 2026-11-02); a RUN from elsewhere (torirsmapedctl)
         * opens it in the main window. */
        self->run_requested = 0;
        if( mine && self->model.run_status == 0 && self->model.play_state != TORIRSMAPED_PLAY_ONLINE
            && self->model.play_web_url[0] )
        {
            if( live_web_open(self->model.play_web_url) )
                Editor_UiLog(ui, "play: opened the play tab");
            else
                Editor_UiLog(ui, "play: the page could not open a tab (allow popups for this site): %s",
                    self->model.play_web_url);
        }
    }
#endif
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
    if( widget == self->button_restart )
    {
        /* Detach, then Launch: the two requests those buttons send, in that
         * order on one connection. The editor server answers them in order,
         * and its Detach has stopped the lane's servers and its play client
         * (the link's close waits for them) before it reads the Launch. For
         * what a reload cannot swap into a running server (a new var). */
        type = Editor_LiveEncodeDetach(&payload);
        if( !live_send(ui, self, type, &payload) )
            return 1;
        type = Editor_LiveEncodeAttach(&payload, 1, "", ToriRSChrome_Text(ui->chrome, self->input_ticket));
        if( live_send(ui, self, type, &payload) )
            Editor_UiStatus(ui,
                "Restarting the lane's servers on the last build. RUN (or Play) starts the play client again");
        return 1;
    }
    if( widget == self->button_run )
    {
        (void)Editor_Live_Run(ui);
        return 1;
    }
    if( widget == self->button_play || widget == self->button_play_stop )
    {
#if defined(__EMSCRIPTEN__)
        if( widget == self->button_play )
        {
            if( self->model.play_state == TORIRSMAPED_PLAY_ONLINE )
                Editor_UiLog(ui, "play: already logged in (its tab is open)");
            else if( !self->model.play_web_url[0] )
                Editor_UiLog(ui, "play: the lane serves no web page (it launches no io_server)");
            else if( !live_web_open(self->model.play_web_url) )
                Editor_UiLog(ui, "play: the page could not open a tab (allow popups for this site)");
            return 1;
        }
#endif
        type = Editor_LiveEncodePlay(
            &payload, widget == self->button_play ? TORIRSMAPED_PLAY_START : TORIRSMAPED_PLAY_STOP);
        live_send(ui, self, type, &payload);
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
    if( self->env_show == 0 )
    {
        const char* value = getenv("TORIRS_EDITOR_LIVE");
        self->env_show = value && strcmp(value, "show") == 0 ? 1 : 2;
    }
    /* TORIRS_EDITOR_LIVE=show (photographs): open with the editor. */
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, visible && self->env_show == 1);
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
    self->shown_play_serial = -1;
    self->log_follow = 1;

    self->panel = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, 260), edp_px(ui, 28), edp_px(ui, 300), "Live");
    assert(self->panel >= 0);
    self->label_status = ToriRSChrome_Label(ui->chrome, self->panel, "detached");
    self->label_run = ToriRSChrome_Label(ui->chrome, self->panel, "");
    self->progress = ToriRSChrome_Progress(ui->chrome, self->panel, "RUN", "", 0);
    ToriRSChrome_SetHidden(ui->chrome, self->progress, 1);
    self->input_address = ToriRSChrome_TextInput(ui->chrome, self->panel, "Address", "127.0.0.1:43594");
    self->input_ticket = ToriRSChrome_TextInput(ui->chrome, self->panel, "Ticket", "");
    self->button_attach = ToriRSChrome_Button(ui->chrome, self->panel, "Attach");
    self->button_launch = ToriRSChrome_Button(ui->chrome, self->panel, "Launch");
    self->button_detach = ToriRSChrome_Button(ui->chrome, self->panel, "Detach");
    self->button_restart = ToriRSChrome_Button(ui->chrome, self->panel, "Restart lane");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_attach, "Connect to a running game server at the address");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_launch,
        "Start the lane's game server, JS5 and io servers and connect");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_restart,
        "Stop the lane's servers and start them on the last build");
    /* Enabled by the first status that says this editor launched the lane. */
    ToriRSChrome_SetDisabled(ui->chrome, self->button_restart, 1);
    ToriRSChrome_Separator(ui->chrome, self->panel);
    self->button_run = ToriRSChrome_Button(ui->chrome, self->panel, "RUN");
    /* Tips are 63 bytes (TORIRS_CHROME_LABEL_MAX); a longer one is cut. */
    ToriRSChrome_SetTooltip(ui->chrome, self->button_run, "Save all, build, reload the server, open the play client");
    edp_button_chord(ui, self->button_run, EDITOR_HK_FILE_RUN);
    self->chord_serial = ui->hotkeys.serial;
    self->button_packs = ToriRSChrome_Button(ui->chrome, self->panel, "Build packs");
    self->button_client = ToriRSChrome_Button(ui->chrome, self->panel, "Build client cache");
    ToriRSChrome_Separator(ui->chrome, self->panel);
    self->label_play = ToriRSChrome_Label(ui->chrome, self->panel, "play: not started");
    self->button_play = ToriRSChrome_Button(ui->chrome, self->panel, "Play");
    self->button_play_stop = ToriRSChrome_Button(ui->chrome, self->panel, "Stop");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_play, "Start (or restart) the play client, logged into the server");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_play_stop, "Stop the lane's play client");
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

    self->float_panel = ToriRSChrome_PanelAdd(ui->chrome, TORIRS_CHROME_PANEL_WINDOW, 0, 0, edp_px(ui, 420), "RUN");
    assert(self->float_panel >= 0);
    self->float_label = ToriRSChrome_Label(ui->chrome, self->float_panel, "");
    self->float_progress = ToriRSChrome_Progress(ui->chrome, self->float_panel, "", "", 0);
    ToriRSChrome_PanelSetClosable(ui->chrome, self->float_panel, 0);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->float_panel, 0);
    self->shown_progress_serial = -1;

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

int
Editor_Live_Busy(struct EditorUi* ui)
{
    struct LivePanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "live");
    if( !self )
        return -1;
    return self->model.run_step != TORIRSMAPED_RUN_STEP_IDLE || self->model.building;
}

int
Editor_Live_Cheat(
    struct EditorUi* ui,
    const char* player,
    const char* line)
{
    struct LivePanel* self;
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    assert(ui);
    assert(player);
    assert(line);
    self = Editor_UiPanelSelf(ui, "live");
    if( !self )
        return -1;
    if( self->model.state != TORIRSMAPED_LIVE_ATTACHED )
    {
        Editor_UiLog(ui, "live: ::%s for %s refused: the live lane is not attached", line, player);
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    type = Editor_LiveEncodeCheat(&payload, player, line);
    if( !live_send(ui, self, type, &payload) )
        return 0;
    Editor_UiLog(ui, "live: ::%s for %s", line, player);
    return 1;
}

int
Editor_Live_Teleport(
    struct EditorUi* ui,
    const char* player,
    int x,
    int z,
    int level)
{
    char coord[64];
    char line[96];

    assert(ui);
    assert(player);
    if( !Editor_UiPanelSelf(ui, "live") )
        return -1;
    Editor_LiveTeleCoord(x, z, level < 0 ? 0 : level, coord, sizeof(coord));
    snprintf(line, sizeof(line), "tele %s", coord);
    return Editor_Live_Cheat(ui, player, line);
}

int
Editor_Live_Run(struct EditorUi* ui)
{
    struct LivePanel* self;
    struct ToriRSMapEdBuf payload;
    uint32_t type;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "live");
    if( !self )
    {
        Editor_UiLog(ui, "RUN: this window has no Live panel (no editor server)");
        return -1;
    }
    if( self->model.run_step != TORIRSMAPED_RUN_STEP_IDLE || self->model.building )
    {
        Editor_UiLog(ui, "RUN: a %s is in flight; wait for it", self->model.building ? "build" : "RUN");
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    type = Editor_LiveEncodeRun(&payload);
    if( !live_send(ui, self, type, &payload) )
        return 0;
    self->run_requested = 1;
    /* Timed from the press: the outcome says how long it took. */
    if( ui->chrome->now_ms > 0 )
    {
        self->run_timed = 1;
        self->run_started_ms = ui->chrome->now_ms;
    }
    Editor_UiLog(ui, "RUN: save, build packs, build the client cache, reload the live server");
    return 1;
}

const char*
Editor_Live_RunOutcome(
    struct EditorUi* ui,
    int* out_kind)
{
    struct LivePanel* self;

    assert(ui);
    assert(out_kind);
    self = Editor_UiPanelSelf(ui, "live");
    *out_kind = self ? self->outcome_kind : EDITOR_LIVE_RUN_NONE;
    return self ? self->outcome : "";
}
