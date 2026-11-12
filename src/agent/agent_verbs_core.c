/*
 * The agent server's core verbs (docs/AGENT_API.md): hello, help, wait, the
 * pointer and the keyboard (mouse, drag, wheel, key, type), shot, the chrome
 * as data (ui) and by name (ui_click, ui_set), and log.
 *
 * The chrome is struct ToriRSChrome (ui/uitree_debug_overlay.h), a POD: every
 * panel and widget carries the box the last Build resolved for it, so `ui` is
 * a read of the model and never a second layout. Three things in the model
 * are not widgets and have no box of their own -- a tab of a strip, a row of
 * a list, a row of the open dropdown -- and each is located here from what
 * the chrome publishes (the metrics header, the display list), then CHECKED
 * against the chrome's own hover before a press goes down, so a box this
 * file got wrong is a refused click and never a click on the wrong thing.
 */

#include "agent/agent_verbs.h"

#if !defined(__EMSCRIPTEN__)

#include "app.h"
#include "bmp.h"
#include "cmd/cmdbus.h"
#include "editor/editor.h"
#include "editor/editor_doc.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/live/live_panel.h"
#include "editor/panels/edpanels.h"
#include "input/torirs_input.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "miniz.h"
#include "platform/platform_window.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/torirs_chrome_metrics.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_minimenu.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define agent_pid() ((long)_getpid())
#define agent_mkdir(path) _mkdir(path)
#else
#include <unistd.h>
#define agent_pid() ((long)getpid())
#define agent_mkdir(path) mkdir(path, 0777)
#endif

/* ---- text ------------------------------------------------------------------------ */

static int
agent_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/** Equal, ASCII case aside. */
static int
agent_text_equal(
    const char* a,
    const char* b)
{
    for( ;; a++, b++ )
    {
        if( agent_lower((unsigned char)*a) != agent_lower((unsigned char)*b) )
            return 0;
        if( !*a )
            return 1;
    }
}

/** `needle` somewhere in `hay`, ASCII case aside. An empty needle is in
 *  everything. */
static int
agent_text_contains(
    const char* hay,
    const char* needle)
{
    size_t const length = strlen(needle);

    if( length == 0 )
        return 1;
    for( ; *hay; hay++ )
    {
        size_t i = 0;
        while( i < length && hay[i] && agent_lower((unsigned char)hay[i]) == agent_lower((unsigned char)needle[i]) )
            i++;
        if( i == length )
            return 1;
    }
    return 0;
}

/** `text` without its part after a tab ("Undo\tCtrl+Z" is the row "Undo" and
 *  the chord "Ctrl+Z"). Returns the chord, or NULL when there is no tab. */
static const char*
agent_text_split_tab(
    const char* text,
    char* out_left,
    size_t left_size)
{
    const char* tab = strchr(text, '\t');
    size_t length = tab ? (size_t)(tab - text) : strlen(text);

    if( length >= left_size )
        length = left_size - 1;
    memcpy(out_left, text, length);
    out_left[length] = '\0';
    return tab ? tab + 1 : NULL;
}

/** A menu row as a person reads it: the colour tags (`<col=ffff00>`, `@yel@`)
 *  taken out. */
static void
agent_text_plain(
    const char* text,
    char* out,
    size_t out_size)
{
    size_t used = 0;

    for( const char* c = text; *c && used + 1 < out_size; )
    {
        if( *c == '<' )
        {
            const char* close = strchr(c, '>');
            if( close )
            {
                c = close + 1;
                continue;
            }
        }
        if( *c == '@' && c[1] && c[2] && c[3] && c[4] == '@' )
        {
            c += 5;
            continue;
        }
        out[used++] = *c++;
    }
    out[used] = '\0';
}

/* ---- hello, help, wait ------------------------------------------------------------ */

static enum AgentVerbResult
verb_hello(struct AgentCtx* ctx)
{
    struct AgentJsonWriter* w = Agent_ReplyBegin(ctx);
    int width;
    int height;

    Agent_SurfaceSize(&width, &height);
    AgentJsonW_KInt(w, "version", AGENT_API_VERSION);
    AgentJsonW_KStr(w, "mode", ctx->app->editor ? "editor" : "game");
    /* Booted: the interface tree is up. The editor's panels come a little
     * later; `editor` says when. */
    AgentJsonW_KBool(w, "ready", ctx->app->app_state == APP_STATE_READY);
    AgentJsonW_KBool(w, "editor", Agent_EditorUi(ctx) != NULL);
    AgentJsonW_KInt(w, "frame", ctx->frame);
    AgentJsonW_KInt(w, "pid", agent_pid());
    AgentJsonW_Key(w, "surface");
    AgentJsonW_ObjBegin(w);
    AgentJsonW_KInt(w, "w", width);
    AgentJsonW_KInt(w, "h", height);
    AgentJsonW_ObjEnd(w);
    AgentJsonW_KDbl(w, "chrome_scale", (double)ToriRSChrome_ScaleQ(&ctx->app->dbg_ui) / 4.0);
    AgentJsonW_Key(w, "verbs");
    AgentJsonW_ArrBegin(w);
    for( int i = 0;; i++ )
    {
        const struct AgentVerb* verb = Agent_VerbAt(i);
        if( !verb )
            break;
        AgentJsonW_Str(w, verb->name);
    }
    AgentJsonW_ArrEnd(w);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

static enum AgentVerbResult
verb_help(struct AgentCtx* ctx)
{
    struct AgentJsonWriter* w = Agent_ReplyBegin(ctx);

    AgentJsonW_Key(w, "verbs");
    AgentJsonW_ArrBegin(w);
    for( int i = 0;; i++ )
    {
        const struct AgentVerb* verb = Agent_VerbAt(i);
        if( !verb )
            break;
        AgentJsonW_ObjBegin(w);
        AgentJsonW_KStr(w, "name", verb->name);
        AgentJsonW_KStr(w, "args", verb->args);
        AgentJsonW_KStr(w, "about", verb->about);
        if( verb->flags & AGENT_VERB_EDITOR )
            AgentJsonW_KBool(w, "editor_only", 1);
        AgentJsonW_ObjEnd(w);
    }
    AgentJsonW_ArrEnd(w);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

/** Frames between two questions to the catalog while a `wait` is held. */
#define AGENT_INDEX_POLL_FRAMES 10

enum
{
    /** wait's op->ints: quiet frames in a row, the timeout, then the index
     *  poll's frame and its last answer. */
    WAIT_INT_QUIET = 0,
    WAIT_INT_TIMEOUT,
    WAIT_INT_INDEX_FRAME,
    WAIT_INT_INDEX_READING,
};

/**
 * 1 while the lane's catalog index is still being read, with the clause in
 * op->text ("the lane's catalog index is still being read (37%): ...").
 *
 * The editor's server reads the lane's whole tree once after it starts, a
 * minute or two. Until then the
 * Catalog's lists are empty or partial, the Square map has no atlas and Save
 * cannot place a new square -- and nothing else is busy, so `wait until=idle`
 * said idle and the next step failed (2026-11-11). The server answers a
 * progress query at once from its partial index (TORIRSMAPED_CATALOG_PROGRESS);
 * asked every AGENT_INDEX_POLL_FRAMES frames, not every frame.
 */
static int
agent_index_reading(
    struct AgentCtx* ctx,
    struct EditorUi* eui,
    struct AgentOp* op)
{
    struct EditorHost_CatalogPage page;

    if( !eui->editor || !eui->editor->host_open || !(eui->editor->capabilities & TORIRSMAPED_CAP_CATALOG_PROGRESS) )
        return 0;
    if( op->ints[WAIT_INT_INDEX_FRAME] != 0 && ctx->frame - op->ints[WAIT_INT_INDEX_FRAME] < AGENT_INDEX_POLL_FRAMES )
        return op->ints[WAIT_INT_INDEX_READING];
    op->ints[WAIT_INT_INDEX_FRAME] = (int)ctx->frame;
    op->ints[WAIT_INT_INDEX_READING] = 0;
    memset(&page, 0, sizeof(page));
    if( Editor_HostMapEdCatalogQuery(&eui->editor->host, TORIRSMAPED_CATALOG_LOC, "", TORIRSMAPED_CATALOG_PROGRESS, 0,
            1, &page)
            == EDITOR_HOST_OK
        && (page.page_flags & TORIRSMAPED_CATALOG_PAGE_PARTIAL) )
    {
        op->ints[WAIT_INT_INDEX_READING] = 1;
        snprintf(op->text, sizeof(op->text),
            "the lane's catalog index is still being read (%u%%): until it is, lists are partial and Save cannot "
            "place a new square (it takes a minute or two after the editor starts)",
            page.progress_total > 0 ? (unsigned)((uint64_t)page.progress_done * 100u / page.progress_total) : 0u);
    }
    Editor_HostCatalogPageFree(&page);
    return op->ints[WAIT_INT_INDEX_READING];
}

/**
 * What is keeping the client from being idle, as a clause for a sentence, or
 * NULL when nothing is: the boot, loads in flight, the world loading, a RUN
 * or a build, a Check of the scripts, the catalog index's first read, a
 * chrome panel waiting on its layout. `op` is the wait's (the index poll keeps its answer there).
 */
static const char*
agent_busy_with(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct App* app = ctx->app;
    struct EditorUi* eui = Agent_EditorUi(ctx);
    struct ToriRSChrome const* chrome = &app->dbg_ui;

    if( app->app_state != APP_STATE_READY )
        return "the client is still booting";
    if( App_AsyncPending(app) )
        return "the client has loads in flight";
    if( eui && eui->app_services.world_busy && eui->app_services.world_busy(eui->app_services.user_data) )
        return "the world is loading";
    if( eui && Editor_Live_Busy(eui) == 1 )
        return "a RUN or a build is in flight";
    /* Check scripts answers a minute later in the console; a script that
     * waited for idle and read the log found no answer there. */
    if( eui && Editor_Triggers_CheckRunning(eui) == 1 )
        return "a Check of the scripts is in flight";
    if( eui && agent_index_reading(ctx, eui, op) )
        return op->text;
    if( chrome->dirty )
        return "the chrome is waiting on its layout";
    for( int i = 0; i < chrome->panel_count; i++ )
        if( chrome->panels[i].visible && chrome->panels[i].dirty )
            return "a chrome panel is waiting on its layout";
    return NULL;
}

/** Frames in a row with nothing busy before `wait` calls the client idle: a
 *  single quiet frame between two loads is not idle. */
#define AGENT_IDLE_FRAMES 3

enum
{
    WAIT_FRAMES = 0,
    WAIT_IDLE,
    WAIT_READY,
};

static int
wait_step(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct AgentJsonWriter* w;
    const char* busy = NULL;

    if( op->phase == WAIT_FRAMES )
    {
        if( ctx->frame < op->until_frame )
            return 0;
    }
    else
    {
        if( op->phase == WAIT_READY )
        {
            if( ctx->app->app_state != APP_STATE_READY )
                busy = "the client is still booting";
            else if( ctx->app->editor && !Agent_EditorUi(ctx) )
                busy = "the editor's panels are not up yet";
        }
        else
            busy = agent_busy_with(ctx, op);
        if( busy )
            op->ints[0] = 0;
        else
            op->ints[0]++;
        if( op->ints[0] < (op->phase == WAIT_IDLE ? AGENT_IDLE_FRAMES : 1) )
        {
            if( ctx->now_ms < op->deadline_ms )
                return 0;
            Agent_ReplyError(ctx, "still waiting after %d ms: %s", op->ints[1],
                busy ? busy : "it has only just gone quiet; ask again");
            return 1;
        }
    }
    w = Agent_ReplyBegin(ctx);
    AgentJsonW_KInt(w, "frames", ctx->frame - op->start_frame);
    AgentJsonW_KInt(w, "ms", (long long)(ctx->now_ms - op->start_ms));
    AgentJsonW_KInt(w, "frame", ctx->frame);
    Agent_ReplyEnd(ctx);
    return 1;
}

static enum AgentVerbResult
verb_wait(struct AgentCtx* ctx)
{
    struct AgentOp* op;
    const char* until = NULL;
    int frames = 0;
    int timeout_ms = 10000;
    int const has_frames = Agent_ArgInt(ctx, "frames", &frames);
    int has_until;

    if( has_frames < 0 )
        return AGENT_VERB_DONE;
    has_until = Agent_ArgString(ctx, "until", &until);
    if( has_until < 0 || Agent_ArgInt(ctx, "timeout_ms", &timeout_ms) < 0 )
        return AGENT_VERB_DONE;
    if( has_frames == has_until )
    {
        Agent_ReplyError(ctx, "wait takes one of \"frames\": N and \"until\": \"idle\" | \"ready\"");
        return AGENT_VERB_DONE;
    }
    if( has_frames && (frames < 1 || frames > 100000) )
    {
        Agent_ReplyError(ctx, "\"frames\" is 1..100000, not %d", frames);
        return AGENT_VERB_DONE;
    }
    if( has_until && strcmp(until, "idle") != 0 && strcmp(until, "ready") != 0 )
    {
        Agent_ReplyError(ctx, "\"until\" is \"idle\" or \"ready\", not \"%s\"", until);
        return AGENT_VERB_DONE;
    }
    if( timeout_ms < 0 || timeout_ms > 3600000 )
    {
        Agent_ReplyError(ctx, "\"timeout_ms\" is 0..3600000, not %d", timeout_ms);
        return AGENT_VERB_DONE;
    }
    op = Agent_OpArm(ctx, wait_step, 0);
    if( has_frames )
    {
        op->phase = WAIT_FRAMES;
        op->until_frame = ctx->frame + frames;
    }
    else
    {
        op->phase = strcmp(until, "idle") == 0 ? WAIT_IDLE : WAIT_READY;
        op->deadline_ms = ctx->now_ms + (uint64_t)timeout_ms;
        op->ints[1] = timeout_ms;
    }
    return AGENT_VERB_PENDING;
}

/* ---- quit ---------------------------------------------------------------------------- */

static enum AgentVerbResult
verb_quit(struct AgentCtx* ctx)
{
    struct AgentJsonWriter* w;
    int discard = 0;

    if( Agent_ArgBool(ctx, "discard", &discard) < 0 )
        return AGENT_VERB_DONE;
    /* The window's close box asks nothing; an agent that quits by mistake
     * should not lose a map to it. */
    if( !discard && ctx->app->editor )
    {
        int const unsaved = Editor_DocUnsavedCount(&ctx->app->editor->doc);
        if( unsaved > 0 )
        {
            Agent_ReplyError(ctx,
                "%d open document%s unsaved edits: save first (action file.save_all), or pass \"discard\": true to "
                "quit without them",
                unsaved, unsaved == 1 ? " has" : "s have");
            return AGENT_VERB_DONE;
        }
    }
    /* The frame loop ends on its next iteration, through the same teardown a
     * closed window gets (editor/editor_view.h): the layout, the hotkeys and
     * the preferences are flushed. This reply is sent first. */
    EditorView_RequestExit();
    w = Agent_ReplyBegin(ctx);
    AgentJsonW_KBool(w, "quitting", 1);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

/* ---- the pointer ------------------------------------------------------------------- */

/** A point a pointer event can carry at all (the bus's int16). */
static int
agent_need_point(
    struct AgentCtx* ctx,
    int x,
    int y)
{
    if( x >= -32768 && x <= 32767 && y >= -32768 && y <= 32767 )
        return 1;
    Agent_ReplyError(ctx, "(%d, %d) is not a point a pointer event can carry", x, y);
    return 0;
}

static void
pointer_finish(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct AgentJsonWriter* w = Agent_ReplyBegin(ctx);
    int x;
    int y;

    (void)op;
    Agent_PointerAt(ctx, &x, &y);
    AgentJsonW_KInt(w, "x", x);
    AgentJsonW_KInt(w, "y", y);
    AgentJsonW_KInt(w, "frame", ctx->frame);
    Agent_ReplyEnd(ctx);
}

static enum AgentVerbResult
verb_mouse(struct AgentCtx* ctx)
{
    struct AgentOp* op;
    const char* action = "click";
    unsigned mods = 0;
    int button = TORIRSM_LEFT;
    int x = 0;
    int y = 0;

    if( !Agent_NeedInt(ctx, "x", &x) || !Agent_NeedInt(ctx, "y", &y) )
        return AGENT_VERB_DONE;
    if( Agent_ArgString(ctx, "action", &action) < 0 || Agent_ArgButton(ctx, &button) < 0
        || Agent_ArgMods(ctx, &mods) < 0 )
        return AGENT_VERB_DONE;
    if( strcmp(action, "move") != 0 && strcmp(action, "down") != 0 && strcmp(action, "up") != 0
        && strcmp(action, "click") != 0 && strcmp(action, "double") != 0 )
    {
        Agent_ReplyError(ctx, "\"action\" is \"move\", \"down\", \"up\", \"click\" or \"double\", not \"%s\"",
            action);
        return AGENT_VERB_DONE;
    }
    /* A press lands on something on the surface; a move or a release may
     * end off it (a panel dragged past the edge). */
    if( strcmp(action, "move") == 0 || strcmp(action, "up") == 0 )
    {
        if( !agent_need_point(ctx, x, y) )
            return AGENT_VERB_DONE;
    }
    else if( !Agent_NeedOnSurface(ctx, x, y) )
        return AGENT_VERB_DONE;

    op = Agent_ScriptArm(ctx, NULL, pointer_finish);
    Agent_ScriptModsDown(ctx, op, mods);
    if( strcmp(action, "move") == 0 )
        Agent_ScriptMove(op, x, y);
    else if( strcmp(action, "down") == 0 )
    {
        Agent_ScriptMove(op, x, y);
        Agent_ScriptFrames(op, 1);
        Agent_ScriptHoldDown(op, button, x, y);
    }
    else if( strcmp(action, "up") == 0 )
    {
        Agent_ScriptMove(op, x, y);
        Agent_ScriptFrames(op, 1);
        Agent_ScriptUp(op, button, x, y);
    }
    else
    {
        Agent_ScriptClick(op, button, x, y);
        if( strcmp(action, "double") == 0 )
        {
            Agent_ScriptFrames(op, 1);
            Agent_ScriptDown(op, button, x, y);
            Agent_ScriptFrames(op, 1);
            Agent_ScriptUp(op, button, x, y);
        }
    }
    Agent_ScriptModsUp(ctx, op, mods);
    return AGENT_VERB_PENDING;
}

static enum AgentVerbResult
verb_drag(struct AgentCtx* ctx)
{
    struct AgentOp* op;
    unsigned mods = 0;
    int button = TORIRSM_LEFT;
    int from[2];
    int to[2];
    int steps = 12;

    if( !Agent_NeedInts(ctx, "from", from, 2) || !Agent_NeedInts(ctx, "to", to, 2) )
        return AGENT_VERB_DONE;
    if( Agent_ArgInt(ctx, "steps", &steps) < 0 || Agent_ArgButton(ctx, &button) < 0
        || Agent_ArgMods(ctx, &mods) < 0 )
        return AGENT_VERB_DONE;
    if( steps < 1 || steps > 600 )
    {
        Agent_ReplyError(ctx, "\"steps\" is 1..600 (one pointer move a frame), not %d", steps);
        return AGENT_VERB_DONE;
    }
    if( !Agent_NeedOnSurface(ctx, from[0], from[1]) || !agent_need_point(ctx, to[0], to[1]) )
        return AGENT_VERB_DONE;

    op = Agent_ScriptArm(ctx, NULL, pointer_finish);
    Agent_ScriptModsDown(ctx, op, mods);
    Agent_ScriptMove(op, from[0], from[1]);
    Agent_ScriptFrames(op, 1);
    Agent_ScriptDown(op, button, from[0], from[1]);
    Agent_ScriptFrames(op, 1);
    for( int step = 1; step <= steps; step++ )
    {
        Agent_ScriptMove(op, from[0] + (to[0] - from[0]) * step / steps, from[1] + (to[1] - from[1]) * step / steps);
        Agent_ScriptFrames(op, 1);
    }
    /* One more frame at rest before letting go: what is dragged has caught
     * up with the pointer when the release names where it lands. */
    Agent_ScriptFrames(op, 1);
    Agent_ScriptUp(op, button, to[0], to[1]);
    Agent_ScriptModsUp(ctx, op, mods);
    return AGENT_VERB_PENDING;
}

static enum AgentVerbResult
verb_wheel(struct AgentCtx* ctx)
{
    struct AgentOp* op;
    unsigned mods = 0;
    int x = 0;
    int y = 0;
    int notches = 0;

    if( !Agent_NeedInt(ctx, "x", &x) || !Agent_NeedInt(ctx, "y", &y) || !Agent_NeedInt(ctx, "notches", &notches) )
        return AGENT_VERB_DONE;
    if( Agent_ArgMods(ctx, &mods) < 0 )
        return AGENT_VERB_DONE;
    if( notches == 0 || notches < -200 || notches > 200 )
    {
        Agent_ReplyError(ctx, "\"notches\" is -200..200 and not 0 (positive scrolls up, away from the user), not %d",
            notches);
        return AGENT_VERB_DONE;
    }
    if( !Agent_NeedOnSurface(ctx, x, y) )
        return AGENT_VERB_DONE;

    op = Agent_ScriptArm(ctx, NULL, pointer_finish);
    Agent_ScriptModsDown(ctx, op, mods);
    Agent_ScriptMove(op, x, y);
    for( int i = 0; i < abs(notches); i++ )
    {
        Agent_ScriptFrames(op, 1);
        Agent_ScriptWheel(op, notches > 0 ? 1 : -1);
    }
    Agent_ScriptModsUp(ctx, op, mods);
    return AGENT_VERB_PENDING;
}

/* ---- the keyboard ------------------------------------------------------------------- */

/** EDITOR_HK_MOD_* and AGENT_MOD_* are the same three bits. */
_Static_assert(EDITOR_HK_MOD_CTRL == AGENT_MOD_CTRL, "the chord's Ctrl bit");
_Static_assert(EDITOR_HK_MOD_SHIFT == AGENT_MOD_SHIFT, "the chord's Shift bit");
_Static_assert(EDITOR_HK_MOD_ALT == AGENT_MOD_ALT, "the chord's Alt bit");

static const struct
{
    unsigned bit;
    int key;
} k_chord_mods[] = {
    { AGENT_MOD_CTRL, TORIRSK_CTRL },
    { AGENT_MOD_SHIFT, TORIRSK_SHIFT },
    { AGENT_MOD_ALT, TORIRSK_ALT },
};

static enum AgentVerbResult
verb_key(struct AgentCtx* ctx)
{
    struct AgentOp* op;
    struct Editor_Chord chord;
    const char* text = NULL;
    const char* action = "tap";
    int hold = 2;

    if( !Agent_NeedString(ctx, "chord", &text) )
        return AGENT_VERB_DONE;
    if( Agent_ArgString(ctx, "action", &action) < 0 || Agent_ArgInt(ctx, "hold", &hold) < 0 )
        return AGENT_VERB_DONE;
    if( strcmp(action, "tap") != 0 && strcmp(action, "down") != 0 && strcmp(action, "up") != 0 )
    {
        Agent_ReplyError(ctx, "\"action\" is \"tap\", \"down\" or \"up\", not \"%s\"", action);
        return AGENT_VERB_DONE;
    }
    if( hold < 1 || hold > 600 )
    {
        Agent_ReplyError(ctx, "\"hold\" is 1..600 frames, not %d", hold);
        return AGENT_VERB_DONE;
    }
    /* The editor's own chord spelling (editor_hotkeys.h): the key names are
     * LibToriRS_KeyCodeName's, as TORIRS_SIM_HOTKEY's are. That one's parser
     * (main.c) stops the process on a bad chord; this must answer instead. */
    memset(&chord, 0, sizeof(chord));
    if( !Editor_ChordParse(text, &chord) || chord.key == TORIRSK_UNKNOWN )
    {
        Agent_ReplyError(ctx,
            "\"%s\" is not a chord: [ctrl+][shift+][alt+]<key>, the key one of a..z 0..9 f1..f12 escape return "
            "backspace delete tab space left right up down pageup pagedown home end insert comma period minus "
            "equals lbracket rbracket semicolon apostrophe grave slash backslash",
            text);
        return AGENT_VERB_DONE;
    }

    op = Agent_ScriptArm(ctx, NULL, NULL);
    if( strcmp(action, "tap") == 0 )
    {
        Agent_ScriptModsDown(ctx, op, chord.mods);
        Agent_ScriptKeyDown(op, chord.key, 0);
        /* Down for `hold` frames: the press edge is seen on its own frame,
         * the held state from then until the release (TORIRS_SIM_HOTKEY's
         * default of two). */
        Agent_ScriptFrames(op, hold);
        Agent_ScriptKeyUp(op, chord.key, 0);
        Agent_ScriptModsUp(ctx, op, chord.mods);
    }
    else if( strcmp(action, "down") == 0 )
    {
        int any = 0;
        for( size_t m = 0; m < sizeof(k_chord_mods) / sizeof(k_chord_mods[0]); m++ )
            if( chord.mods & k_chord_mods[m].bit )
            {
                Agent_ScriptKeyDown(op, k_chord_mods[m].key, 1);
                any = 1;
            }
        if( any )
            Agent_ScriptFrames(op, 1);
        Agent_ScriptKeyDown(op, chord.key, 1);
    }
    else
    {
        Agent_ScriptKeyUp(op, chord.key, 1);
        for( size_t m = 0; m < sizeof(k_chord_mods) / sizeof(k_chord_mods[0]); m++ )
            if( chord.mods & k_chord_mods[m].bit )
                Agent_ScriptKeyUp(op, k_chord_mods[m].key, 1);
    }
    return AGENT_VERB_PENDING;
}

/**
 * `text` as typed input, `per_frame` characters a frame: each a key event
 * carrying the character and no key, which is what a field reads. A newline
 * is the Return key, a tab the Tab key.
 *
 * With `op` NULL nothing is appended: the text is only checked, so a verb can
 * refuse it before it arms anything. Returns 1, or 0 with `*bad` the first
 * code point a field cannot take (past U+00FF, or a control character).
 */
static int
agent_script_text(
    struct AgentOp* op,
    const char* text,
    int per_frame,
    unsigned* bad)
{
    const unsigned char* at = (const unsigned char*)text;
    int in_frame = 0;

    assert(text);
    assert(bad);
    while( *at )
    {
        unsigned code = *at;
        int length = 1;

        if( code >= 0xC0 && code <= 0xDF && (at[1] & 0xC0) == 0x80 )
        {
            code = ((code & 0x1Fu) << 6) | (at[1] & 0x3Fu);
            length = 2;
        }
        else if( code >= 0xE0 && code <= 0xEF && (at[1] & 0xC0) == 0x80 && (at[2] & 0xC0) == 0x80 )
        {
            code = ((code & 0x0Fu) << 12) | ((at[1] & 0x3Fu) << 6) | (at[2] & 0x3Fu);
            length = 3;
        }
        else if( code >= 0xF0 )
        {
            *bad = 0x10000u;
            return 0;
        }
        at += length;

        if( code == '\r' )
            continue;
        if( code != '\n' && code != '\t' && (code < 32 || code > 255) )
        {
            *bad = code;
            return 0;
        }
        if( !op )
            continue;
        if( in_frame >= per_frame )
        {
            Agent_ScriptFrames(op, 1);
            in_frame = 0;
        }
        if( code == '\n' || code == '\t' )
        {
            int const key = code == '\n' ? TORIRSK_RETURN : TORIRSK_TAB;
            /* A key of its own, on frames of its own. */
            if( in_frame > 0 )
                Agent_ScriptFrames(op, 1);
            Agent_ScriptKeyDown(op, key, 0);
            Agent_ScriptFrames(op, 1);
            Agent_ScriptKeyUp(op, key, 0);
            Agent_ScriptFrames(op, 1);
            in_frame = 0;
            continue;
        }
        Agent_ScriptChar(op, (int)code);
        in_frame++;
    }
    return 1;
}

static void
type_finish(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct AgentJsonWriter* w = Agent_ReplyBegin(ctx);

    AgentJsonW_KInt(w, "characters", op->ints[0]);
    /* The field that had the keyboard: an agent that typed at nothing reads
     * -1 here instead of a screenshot. */
    AgentJsonW_KInt(w, "focus", ctx->app->dbg_ui.focus);
    Agent_ReplyEnd(ctx);
}

static enum AgentVerbResult
verb_type(struct AgentCtx* ctx)
{
    struct AgentOp* op;
    const char* text = NULL;
    unsigned bad = 0;

    if( !Agent_NeedString(ctx, "text", &text) )
        return AGENT_VERB_DONE;
    if( !text[0] )
    {
        Agent_ReplyError(ctx, "\"text\" is empty: there is nothing to type");
        return AGENT_VERB_DONE;
    }
    if( !agent_script_text(NULL, text, 1, &bad) )
    {
        Agent_ReplyError(ctx,
            "\"text\" holds U+%04X, which a field here cannot take: the client's fields hold the characters "
            "U+0020..U+00FF, a newline (Return) and a tab",
            bad);
        return AGENT_VERB_DONE;
    }
    op = Agent_ScriptArm(ctx, NULL, type_finish);
    (void)agent_script_text(op, text, 1, &bad);
    /* Characters, not bytes: a UTF-8 continuation byte starts none. */
    for( const char* c = text; *c; c++ )
        if( ((unsigned char)*c & 0xC0) != 0x80 )
            op->ints[0]++;
    return AGENT_VERB_PENDING;
}

/* ---- shot ------------------------------------------------------------------------------ */

enum
{
    SHOT_SETTLE = 0,
    SHOT_WAIT_FILE,
};

/** How long a present may take to come before `shot` gives up. */
#define AGENT_SHOT_TIMEOUT_MS 5000

/**
 * The BMP at `path` as bmp_write_file writes one (54-byte header, 32 bits a
 * pixel, bottom-up): its size, and -- with `out_pixels` -- its bytes. 0 when
 * the file is not there or not whole yet.
 */
static int
agent_bmp_read(
    const char* path,
    int* out_w,
    int* out_h,
    unsigned char** out_pixels)
{
    unsigned char header[54];
    FILE* file = fopen(path, "rb");
    long width;
    long height;
    size_t bytes;

    if( !file )
        return 0;
    if( fread(header, 1, sizeof(header), file) != sizeof(header) || header[0] != 'B' || header[1] != 'M'
        || header[28] != 32 )
    {
        fclose(file);
        return 0;
    }
    width = (long)header[18] | ((long)header[19] << 8) | ((long)header[20] << 16) | ((long)header[21] << 24);
    height = (long)header[22] | ((long)header[23] << 8) | ((long)header[24] << 16) | ((long)header[25] << 24);
    if( width <= 0 || height <= 0 || width > 16384 || height > 16384 )
    {
        fclose(file);
        return 0;
    }
    bytes = (size_t)width * (size_t)height * 4;
    if( out_pixels )
    {
        unsigned char* pixels = ToriMem_Alloc(ToriMem_Net(), bytes);
        assert(pixels);
        if( fread(pixels, 1, bytes, file) != bytes )
        {
            ToriMem_Free(ToriMem_Net(), pixels);
            fclose(file);
            return 0;
        }
        *out_pixels = pixels;
    }
    else
    {
        /* Whole: the last pixel is there. */
        if( fseek(file, (long)(sizeof(header) + bytes - 1), SEEK_SET) != 0 || fgetc(file) == EOF )
        {
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    *out_w = (int)width;
    *out_h = (int)height;
    return 1;
}

/**
 * Turn the captured BMP into the file that was asked for: the `rect` cut out
 * of it, as PNG when the path says so. Returns 1 with the picture's size, or
 * 0 with `why` saying what stopped it.
 */
static int
agent_shot_write(
    struct AgentOp* op,
    int* out_w,
    int* out_h,
    char* why,
    size_t why_size)
{
    const char* capture = op->text;
    const char* path = op->path;
    int const png = op->ints[0];
    int const cropped = op->ints[1];
    unsigned char* pixels = NULL;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    int w;
    int h;

    if( !agent_bmp_read(capture, &width, &height, &pixels) )
    {
        snprintf(why, why_size, "the captured frame at %s could not be read back", capture);
        return 0;
    }
    w = width;
    h = height;
    if( cropped )
    {
        x = op->ints[2];
        y = op->ints[3];
        w = op->ints[4];
        h = op->ints[5];
        if( x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > width || y + h > height )
        {
            ToriMem_Free(ToriMem_Net(), pixels);
            snprintf(why, why_size, "\"rect\" [%d, %d, %d, %d] is not inside the %d x %d picture", x, y, w, h, width,
                height);
            return 0;
        }
    }

    if( png )
    {
        /* Three channels: the frame has no alpha to carry. Level 1, as the
         * plugin screenshots are written (app_plugin_assets.c): the encode
         * runs inside a frame. */
        unsigned char* rgb = ToriMem_Alloc(ToriMem_Net(), (size_t)w * (size_t)h * 3);
        size_t png_size = 0;
        void* encoded;
        FILE* file;
        int written = 0;

        assert(rgb);
        for( int row = 0; row < h; row++ )
        {
            /* The BMP's rows run bottom-up. */
            const unsigned char* src = pixels + ((size_t)(height - 1 - (y + row)) * (size_t)width + (size_t)x) * 4;
            unsigned char* dst = rgb + (size_t)row * (size_t)w * 3;
            for( int col = 0; col < w; col++ )
            {
                dst[col * 3 + 0] = src[col * 4 + 2];
                dst[col * 3 + 1] = src[col * 4 + 1];
                dst[col * 3 + 2] = src[col * 4 + 0];
            }
        }
        encoded = tdefl_write_image_to_png_file_in_memory_ex(rgb, w, h, 3, &png_size, 1, MZ_FALSE);
        ToriMem_Free(ToriMem_Net(), rgb);
        assert(encoded);
        file = fopen(path, "wb");
        if( file )
        {
            written = fwrite(encoded, 1, png_size, file) == png_size;
            written = fclose(file) == 0 && written;
        }
        mz_free(encoded);
        if( !written )
        {
            ToriMem_Free(ToriMem_Net(), pixels);
            snprintf(why, why_size, "%s could not be written (does its folder exist?)", path);
            return 0;
        }
    }
    else if( cropped )
    {
        int* argb = ToriMem_Alloc(ToriMem_Net(), (size_t)w * (size_t)h * sizeof(int));

        assert(argb);
        for( int row = 0; row < h; row++ )
        {
            const unsigned char* src = pixels + ((size_t)(height - 1 - (y + row)) * (size_t)width + (size_t)x) * 4;
            for( int col = 0; col < w; col++ )
                argb[(size_t)row * (size_t)w + (size_t)col] = (int)(((unsigned)src[col * 4 + 3] << 24)
                                                                    | ((unsigned)src[col * 4 + 2] << 16)
                                                                    | ((unsigned)src[col * 4 + 1] << 8)
                                                                    | (unsigned)src[col * 4 + 0]);
        }
        bmp_write_file(path, argb, w, h);
        ToriMem_Free(ToriMem_Net(), argb);
    }
    /* else: the capture went straight to `path`. */
    ToriMem_Free(ToriMem_Net(), pixels);
    if( strcmp(capture, path) != 0 )
        remove(capture);
    *out_w = w;
    *out_h = h;
    return 1;
}

static int
shot_step(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    int width = 0;
    int height = 0;

    /* A present must come for there to be a frame to capture, and the
     * picture should be of what the last request caused: keep the client
     * drawing until the file is there. */
    ctx->app->need_redraw = 1;

    if( op->phase == SHOT_SETTLE )
    {
        if( ctx->frame < op->until_frame )
            return 0;
        /* A file left from an earlier shot must not be mistaken for this one. */
        remove(op->path);
        remove(op->text);
#if defined(TORIRS_PLATFORM_ANDROID)
        Agent_ReplyError(ctx, "shot is not built on Android: its window has no capture");
        return 1;
#else
        if( !PlatformWindow_RequestCapture(ctx->platform, op->text) )
        {
            Agent_ReplyError(ctx,
                "this window cannot capture what it presents (an SDL window presenting through OpenGL, or a window "
                "with no game area): run a software renderer (--soft3d) for shot");
            return 1;
        }
#endif
        op->phase = SHOT_WAIT_FILE;
        op->deadline_ms = ctx->now_ms + AGENT_SHOT_TIMEOUT_MS;
    }

    if( !agent_bmp_read(op->text, &width, &height, NULL) )
    {
        if( ctx->now_ms < op->deadline_ms )
            return 0;
        Agent_ReplyError(ctx,
            "no picture reached %s within %d ms: nothing was presented (a minimised window presents nothing), or the "
            "file could not be written there",
            op->text, AGENT_SHOT_TIMEOUT_MS);
        return 1;
    }
    {
        char why[AGENT_OP_TEXT + 128];
        struct AgentJsonWriter* w;
        int surface_w;
        int surface_h;

        if( !agent_shot_write(op, &width, &height, why, sizeof(why)) )
        {
            if( strcmp(op->text, op->path) != 0 )
                remove(op->text);
            Agent_ReplyError(ctx, "%s", why);
            return 1;
        }
        Agent_SurfaceSize(&surface_w, &surface_h);
        w = Agent_ReplyBegin(ctx);
        AgentJsonW_KStr(w, "path", op->path);
        AgentJsonW_KInt(w, "w", width);
        AgentJsonW_KInt(w, "h", height);
        AgentJsonW_KStr(w, "format", op->ints[0] ? "png" : "bmp");
        /* The space every x and y of the API is in. When the picture is this
         * size (and was not cut), a pixel of it is a point of that space. */
        AgentJsonW_Key(w, "surface");
        AgentJsonW_ObjBegin(w);
        AgentJsonW_KInt(w, "w", surface_w);
        AgentJsonW_KInt(w, "h", surface_h);
        AgentJsonW_ObjEnd(w);
        AgentJsonW_KInt(w, "frame", ctx->frame);
        Agent_ReplyEnd(ctx);
    }
    return 1;
}

/**
 * Make the folder `path`'s file goes in, and the folders above it that are
 * missing. 1 when it is there afterwards; 0 with the folder that could not be
 * made in `out_folder` and errno's reason in `out_why`.
 *
 * A picture asked into a folder that did not exist used to wait out the
 * capture's five seconds and then guess ("either nothing was presented, or
 * the folder cannot be written"); the folder is the caller's to name and
 * nobody's to make by hand.
 */
static int
agent_make_parent_dirs(
    const char* path,
    char* out_folder,
    size_t folder_size,
    const char** out_why)
{
    char folder[AGENT_OP_TEXT];
    size_t const length = strlen(path);
    size_t last = 0;
    struct stat info;

    assert(length < sizeof(folder));
    for( size_t i = 0; i < length; i++ )
        if( path[i] == '/' || path[i] == '\\' )
            last = i;
    /* No folder part ("shot.png"), or the root's own ("/shot.png"). */
    if( last == 0 )
        return 1;
    for( size_t i = 1; i <= last; i++ )
    {
        if( i < last && path[i] != '/' && path[i] != '\\' )
            continue;
        /* "C:" is a drive, not a folder to make; "//" makes nothing. */
        if( path[i - 1] == ':' || path[i - 1] == '/' || path[i - 1] == '\\' )
            continue;
        memcpy(folder, path, i);
        folder[i] = '\0';
        if( stat(folder, &info) == 0 )
            continue;
        if( agent_mkdir(folder) != 0 && errno != EEXIST )
        {
            *out_why = strerror(errno);
            snprintf(out_folder, folder_size, "%s", folder);
            return 0;
        }
    }
    return 1;
}

static enum AgentVerbResult
verb_shot(struct AgentCtx* ctx)
{
    struct AgentOp* op;
    const char* path = NULL;
    const char* dot;
    const char* why = NULL;
    char folder[AGENT_OP_TEXT];
    int rect[4] = { 0, 0, 0, 0 };
    int has_rect;
    int png;

    if( !Agent_NeedString(ctx, "path", &path) )
        return AGENT_VERB_DONE;
    has_rect = Agent_ArgInts(ctx, "rect", rect, 4);
    if( has_rect < 0 )
        return AGENT_VERB_DONE;
    dot = strrchr(path, '.');
    png = dot && agent_text_equal(dot, ".png");
    if( !png && !(dot && agent_text_equal(dot, ".bmp")) )
    {
        Agent_ReplyError(ctx, "\"path\" ends in .png or .bmp (the two formats shot writes), not \"%s\"", path);
        return AGENT_VERB_DONE;
    }
    if( strlen(path) + 16 >= AGENT_OP_TEXT )
    {
        Agent_ReplyError(ctx, "\"path\" is over %d bytes", AGENT_OP_TEXT - 17);
        return AGENT_VERB_DONE;
    }
    /* The folder is made when it is missing; one that cannot be made is
     * refused now, by name, not after the capture's wait. */
    if( !agent_make_parent_dirs(path, folder, sizeof(folder), &why) )
    {
        Agent_ReplyError(ctx, "the folder \"%s\" does not exist and could not be made (%s): give a \"path\" in a "
                              "folder this client can write",
            folder, why);
        return AGENT_VERB_DONE;
    }

    op = Agent_OpArm(ctx, shot_step, 0);
    snprintf(op->path, sizeof(op->path), "%s", path);
    /* The window writes a whole-frame BMP; a PNG or a cut is made from it. */
    if( png || has_rect )
        snprintf(op->text, sizeof(op->text), "%s.capture.bmp", path);
    else
        snprintf(op->text, sizeof(op->text), "%s", path);
    op->ints[0] = png;
    op->ints[1] = has_rect;
    op->ints[2] = rect[0];
    op->ints[3] = rect[1];
    op->ints[4] = rect[2];
    op->ints[5] = rect[3];
    op->phase = SHOT_SETTLE;
    /* Two frames first, so what the request before this one caused has been
     * drawn and presented. */
    op->until_frame = ctx->frame + 2;
    ctx->app->need_redraw = 1;
    return AGENT_VERB_PENDING;
}

/* ---- log ------------------------------------------------------------------------------ */

static enum AgentVerbResult
verb_log(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_EditorUi(ctx);
    struct AgentJsonWriter* w;
    const char* source = NULL;
    int since = -1;
    int limit = 200;
    int has_since;

    has_since = Agent_ArgInt(ctx, "since", &since);
    if( has_since < 0 || Agent_ArgInt(ctx, "limit", &limit) < 0 || Agent_ArgString(ctx, "source", &source) < 0 )
        return AGENT_VERB_DONE;
    if( limit < 1 || limit > 2000 )
    {
        Agent_ReplyError(ctx, "\"limit\" is 1..2000 lines, not %d", limit);
        return AGENT_VERB_DONE;
    }
    if( !source )
        source = eui ? "console" : "chat";
    if( strcmp(source, "console") != 0 && strcmp(source, "live") != 0 && strcmp(source, "chat") != 0 )
    {
        Agent_ReplyError(ctx, "\"source\" is \"console\", \"live\" or \"chat\", not \"%s\"", source);
        return AGENT_VERB_DONE;
    }

    if( strcmp(source, "console") == 0 )
    {
        int fresh;
        int dropped = 0;

        if( !eui )
        {
            Agent_ReplyError(ctx, "this window has no editor console: ask for \"source\": \"chat\"");
            return AGENT_VERB_DONE;
        }
        /* console_serial counts every line ever said, so it is the cursor:
         * the lines since `since` are the newest (serial - since). */
        fresh = !has_since || since > eui->console_serial || since < 0 ? eui->console_count
                                                                       : eui->console_serial - since;
        if( fresh > eui->console_count )
        {
            dropped = fresh - eui->console_count;
            fresh = eui->console_count;
        }
        if( fresh > limit )
        {
            dropped += fresh - limit;
            fresh = limit;
        }
        w = Agent_ReplyBegin(ctx);
        AgentJsonW_KStr(w, "source", source);
        AgentJsonW_Key(w, "lines");
        AgentJsonW_ArrBegin(w);
        for( int i = fresh - 1; i >= 0; i-- )
            AgentJsonW_Str(w, Editor_UiConsoleLine(eui, i));
        AgentJsonW_ArrEnd(w);
        AgentJsonW_KInt(w, "cursor", eui->console_serial);
        /* Lines said since `since` that are not in `lines`: older than the
         * console keeps, or past `limit`. */
        if( dropped > 0 )
            AgentJsonW_KInt(w, "dropped", dropped);
        Agent_ReplyEnd(ctx);
        return AGENT_VERB_DONE;
    }

    if( strcmp(source, "live") == 0 )
    {
        const struct Editor_LiveModel* model = eui ? Editor_LivePanelModel(eui) : NULL;
        int count;

        if( !model )
        {
            Agent_ReplyError(ctx, "this window has no Live panel, so no build and live lines");
            return AGENT_VERB_DONE;
        }
        /* The live log's serial moves once per message, which may be several
         * lines, so it says only WHETHER something is new: an unchanged
         * cursor gets no lines, a changed one the newest `limit`. */
        count = has_since && since == model->log_serial ? 0 : model->log_count;
        if( count > limit )
            count = limit;
        w = Agent_ReplyBegin(ctx);
        AgentJsonW_KStr(w, "source", source);
        AgentJsonW_Key(w, "lines");
        AgentJsonW_ArrBegin(w);
        for( int i = count - 1; i >= 0; i-- )
            AgentJsonW_Str(w, Editor_LiveLogLine(model, i));
        AgentJsonW_ArrEnd(w);
        AgentJsonW_KInt(w, "cursor", model->log_serial);
        Agent_ReplyEnd(ctx);
        return AGENT_VERB_DONE;
    }

    {
        /* The chat box: messages[0] is the newest, each with a serial that
         * only grows (what the quest driver's DriveState_Messages reads). */
        struct RS_Chat const* chat = &ctx->app->chat;
        int newest = chat->message_count > 0 ? chat->messages[0].serial : 0;
        int count = 0;

        while( count < chat->message_count && count < limit && (!has_since || chat->messages[count].serial > since) )
            count++;
        w = Agent_ReplyBegin(ctx);
        AgentJsonW_KStr(w, "source", source);
        AgentJsonW_Key(w, "lines");
        AgentJsonW_ArrBegin(w);
        for( int i = count - 1; i >= 0; i-- )
        {
            char line[RS_CHAT_SENDER_LEN + RS_CHAT_TEXT_LEN + 4];
            if( chat->messages[i].sender[0] )
                snprintf(line, sizeof(line), "%s: %s", chat->messages[i].sender, chat->messages[i].text);
            else
                snprintf(line, sizeof(line), "%s", chat->messages[i].text);
            AgentJsonW_Str(w, line);
        }
        AgentJsonW_ArrEnd(w);
        AgentJsonW_KInt(w, "cursor", newest);
        Agent_ReplyEnd(ctx);
    }
    return AGENT_VERB_DONE;
}

/* ---- the chrome as data ------------------------------------------------------------------ */

static const char*
agent_panel_style_name(int style)
{
    if( style == TORIRS_CHROME_PANEL_MENUBAR )
        return "menubar";
    if( style == TORIRS_CHROME_PANEL_MENU )
        return "menu";
    if( style == TORIRS_CHROME_PANEL_TOOLBAR )
        return "toolbar";
    return "window";
}

static const char*
agent_widget_kind_name(const struct ToriRSChromeWidget* w)
{
    static const char* const k_names[] = {
        "label",      "checkbox", "textinput", "separator", "menuitem", "dropdown", "modelview",
        "button",     "tabstrip", "listrow",   "colorpick", "textarea", "custom",   "tree",
        "vlist",      "iconbutton", "slider",  "radio",     "code",     "graph",
    };

    _Static_assert(TORIRS_CHROME_W_GRAPH == 19, "the widget kinds, in enum ToriRSChromeWidgetKind's order");
    _Static_assert(TORIRS_CHROME_W_FREE == 20, "a widget kind was added: name it in k_names");
    if( w->kind == TORIRS_CHROME_W_DROPDOWN && w->menu_mode )
        return "menu";
    if( w->kind < 0 || w->kind > TORIRS_CHROME_W_GRAPH )
        return "unknown";
    return k_names[w->kind];
}

/** The registry module whose window chrome panel `panel` is, or NULL: what
 *  the View menu and the hotkey table call it ("catalog"). */
static const char*
agent_panel_module(
    struct EditorUi* eui,
    int panel)
{
    if( !eui )
        return NULL;
    for( int i = 0; i < eui->panel_count; i++ )
        if( eui->panels[i].chrome_panel == panel )
            return eui->panels[i].name;
    return NULL;
}

/** The panel group whose frame chrome panel `panel` is, or NULL. */
static const struct EditorUiGroup*
agent_panel_group(
    struct EditorUi* eui,
    int panel)
{
    if( !eui )
        return NULL;
    for( int i = 0; i < eui->group_count; i++ )
        if( eui->groups[i].chrome_panel == panel )
            return &eui->groups[i];
    return NULL;
}

/** What a person calls the panel: its title bar, else (a group's member
 *  wears the group's frame and has no bar of its own) the registry's title. */
static const char*
agent_panel_title(
    struct AgentCtx* ctx,
    int panel)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    struct EditorUi* eui = Agent_EditorUi(ctx);

    if( chrome->panels[panel].title[0] )
        return chrome->panels[panel].title;
    if( eui )
        for( int i = 0; i < eui->panel_count; i++ )
            if( eui->panels[i].chrome_panel == panel && eui->panels[i].title )
                return eui->panels[i].title;
    return "";
}

/** Does `filter` name this panel: its title or its module (or its group),
 *  whole, else a part of its title? */
static int
agent_panel_matches(
    struct AgentCtx* ctx,
    int panel,
    const char* filter)
{
    struct EditorUi* eui = Agent_EditorUi(ctx);
    const char* title = agent_panel_title(ctx, panel);
    const char* module = agent_panel_module(eui, panel);
    const struct EditorUiGroup* group = agent_panel_group(eui, panel);

    if( agent_text_equal(title, filter) )
        return 1;
    if( module && agent_text_equal(module, filter) )
        return 1;
    if( group && agent_text_equal(group->name, filter) )
        return 1;
    return title[0] && agent_text_contains(title, filter);
}

/** Label `index` of a dropdown's options or a strip's titles, as the chrome
 *  reads it (its structured pool, else the borrowed array). */
static const char*
agent_option_label(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w,
    int index)
{
    if( index < 0 || index >= w->option_count )
        return "";
    if( w->structured_options )
    {
        int const at = w->structured_option_first + index;
        if( w->structured_option_first >= 0 && at >= 0 && at < chrome->select_option_count )
            return chrome->select_options[at].label;
        return "";
    }
    if( w->options && w->options[index] )
        return w->options[index];
    return "";
}

static int
agent_option_enabled(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w,
    int index)
{
    if( w->structured_options )
    {
        int const at = w->structured_option_first + index;
        if( w->structured_option_first >= 0 && at >= 0 && at < chrome->select_option_count )
            return chrome->select_options[at].enabled;
    }
    return 1;
}

/** Is the widget one its panel lays out at all (not hidden by its owner, not
 *  on another tab)? The chrome's own rule (dbg_widget_shown). */
static int
agent_widget_shown(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w)
{
    if( w->hidden || w->kind == TORIRS_CHROME_W_FREE )
        return 0;
    if( w->tab >= 0 && w->tab != chrome->panels[w->panel].active_tab )
        return 0;
    return 1;
}

/** The widget's middle is inside the window (when its size is known). A
 *  dialog taller than the window draws its foot past the bottom edge, where
 *  the agent's press still lands and a person's cannot: New instance's Create
 *  was pressed at y 958 of a 900-pixel window (2026-11-11). */
static int
agent_widget_inside_window(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w)
{
    if( chrome->surface_h > 0 && (w->y + w->h / 2 < 0 || w->y + w->h / 2 >= chrome->surface_h) )
        return 0;
    if( chrome->surface_w > 0 && (w->x + w->w / 2 < 0 || w->x + w->w / 2 >= chrome->surface_w) )
        return 0;
    return 1;
}

/** On screen: shown, in an open panel that is not collapsed, with a box,
 *  inside the window. */
static int
agent_widget_on_screen(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w)
{
    const struct ToriRSChromePanel* p = &chrome->panels[w->panel];
    return p->visible && !p->collapsed && agent_widget_shown(chrome, w) && w->w > 0 && w->h > 0
           && agent_widget_inside_window(chrome, w);
}

/**
 * On screen and whole: in a panel that scrolls, inside its scroll window
 * rather than half under its header or its foot, where a press on the row's
 * middle would miss it. A row taller than the window counts when its top is
 * in it; a tab strip is pinned above the window and always counts.
 */
static int
agent_widget_in_view(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w)
{
    const struct ToriRSChromePanel* p = &chrome->panels[w->panel];
    int const top = p->content_y;
    int const foot = p->content_y + p->view_h;

    if( !agent_widget_on_screen(chrome, w) )
        return 0;
    if( w->kind == TORIRS_CHROME_W_TABSTRIP || !(p->scrollable && p->content_h > p->view_h) )
        return 1;
    if( w->h > p->view_h )
        return w->y >= top && w->y < foot;
    return w->y >= top && w->y + w->h <= foot;
}

/** Why a widget is not on screen, for `ui` and for a refused click. */
static const char*
agent_widget_hidden_why(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w)
{
    const struct ToriRSChromePanel* p = &chrome->panels[w->panel];

    if( !p->visible )
        return "panel_closed";
    if( p->collapsed )
        return "panel_collapsed";
    if( w->hidden )
        return "hidden";
    if( w->tab >= 0 && w->tab != p->active_tab )
        return "other_tab";
    if( p->scrollable && p->content_h > p->view_h )
        return "scrolled_out";
    if( w->w > 0 && w->h > 0 && !agent_widget_inside_window(chrome, w) )
        return "off_window";
    return "cut_off";
}

/** The caption a person names a widget by: its label, else (a button, a menu
 *  row, a plain label keep theirs in `text`) its text -- up to a tab, which
 *  starts a chord. Value kinds never lend their value as a name. */
static void
agent_widget_caption(
    const struct ToriRSChromeWidget* w,
    char* out,
    size_t out_size)
{
    const char* source = w->label;

    /* A cells list's caption is its column heads, 0x1F between them: the
     * same row of fields its rows are, spelled the way `ui` spells a row. */
    if( (w->kind == TORIRS_CHROME_W_VLIST || w->kind == TORIRS_CHROME_W_CODE) && strchr(source, TORIRS_CHROME_CELL_SEP) )
    {
        size_t used = 0;
        for( const char* c = source; *c && used + 1 < out_size; c++ )
            out[used++] = *c == TORIRS_CHROME_CELL_SEP ? '\t' : *c;
        out[used] = '\0';
        return;
    }
    if( !source[0] && w->kind != TORIRS_CHROME_W_TEXTINPUT && w->kind != TORIRS_CHROME_W_TEXTAREA
        && w->kind != TORIRS_CHROME_W_COLORPICK )
        source = w->text;
    (void)agent_text_split_tab(source, out, out_size);
}

/**
 * The box of tab `index` of a strip: the chrome's prefix-sum layout
 * (dbg_tab_rect), from the same public metric and text measure it uses. 0
 * for a strip with no room.
 */
static int
agent_tab_rect(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w,
    int index,
    struct ToriRSChromeRect* out)
{
    int const q = ToriRSChrome_ScaleQ(chrome);
    int const pad = ToriRSChrome_ScalePx(q, TORIRS_CHROME_M_TAB_PAD_X);
    int const least = ToriRSChrome_ScalePx(q, 12);
    int total = 0;
    int before = 0;
    int own = 0;
    int x0;
    int x1;

    if( index < 0 || index >= w->option_count || w->w <= 0 || w->h <= 0 )
        return 0;
    for( int i = 0; i < w->option_count; i++ )
    {
        int const natural =
            ToriRSChrome_MeasureTextQ(chrome->theme.font_row, q, agent_option_label(chrome, w, i)) + 2 * pad;
        if( i < index )
            before += natural;
        if( i == index )
            own = natural;
        total += natural;
    }
    if( total <= 0 )
        return 0;
    if( total <= w->w )
    {
        x0 = before;
        x1 = before + own;
    }
    else
    {
        x0 = before * w->w / total;
        x1 = (before + own) * w->w / total;
        if( x1 - x0 < least )
            x1 = x0 + least;
    }
    out->x = w->x + x0;
    out->y = w->y;
    out->w = x1 - x0;
    out->h = w->h;
    return out->w > 0;
}

/**
 * The box of absolute row `row` of a VLIST, as the chrome laid it out
 * (ToriRSChrome_VListRowRect: this file's own arithmetic, from the widget's
 * foot, missed every list with a header or a caption). 0 for a row outside
 * the window or a list off screen.
 */
static int
agent_vlist_row_rect(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w,
    int row,
    struct ToriRSChromeRect* out)
{
    if( w->kind != TORIRS_CHROME_W_VLIST || w->w <= 0 || w->h <= 0 )
        return 0;
    return ToriRSChrome_VListRowRect(chrome, (int)(w - chrome->widgets), row, out);
}

/** The text the host filled row `row` of a VLIST or CODE with, or NULL when
 *  it has not (the pool is a ring over the widget's `rows` slots). */
static const char*
agent_vlist_row_text(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w,
    int row)
{
    const struct ToriRSChromeVRow* slot;

    if( w->vrow_first < 0 || w->rows <= 0 || row < 0 )
        return NULL;
    slot = &chrome->vrows[w->vrow_first + row % w->rows];
    return slot->index == row ? slot->text : NULL;
}

/** Rows the open dropdown shows at once. */
static int
agent_dropdown_rows(const struct ToriRSChromeWidget* w)
{
    return w->option_count < TORIRS_CHROME_DROPDOWN_ROWS ? w->option_count : TORIRS_CHROME_DROPDOWN_ROWS;
}

/**
 * The boxes of the open dropdown's visible rows, read off the display list.
 *
 * The list's rows are not widgets and the chrome exports no box for them, but
 * it draws each as a TEXT primitive that BORROWS the option's own string, so
 * the primitive whose text points into option i's label is row i's, and its
 * clip is the list's row column. The list is built after every panel, so the
 * rows are found from the end of the display list backwards, last row first,
 * and a primitive with another clip (the closed box, which borrows the
 * selected option too) ends the search. Rows not found get w 0.
 *
 * Returns how many rows were located.
 */
static int
agent_dropdown_row_rects(
    const struct ToriRSChrome* chrome,
    struct ToriRSChromeRect out[TORIRS_CHROME_DROPDOWN_ROWS])
{
    const struct ToriRSChromeWidget* w;
    const struct ToriRSChromePrim* prims;
    struct ToriRSChromeRect clip = { 0, 0, 0, 0 };
    int count = 0;
    int rows;
    int at;
    int found = 0;

    memset(out, 0, sizeof(out[0]) * TORIRS_CHROME_DROPDOWN_ROWS);
    if( chrome->dropdown_open < 0 )
        return 0;
    w = &chrome->widgets[chrome->dropdown_open];
    rows = agent_dropdown_rows(w);
    prims = ToriRSChrome_Prims(chrome, &count);
    at = count - 1;
    for( int row = rows - 1; row >= 0 && at >= 0; row-- )
    {
        const char* label = agent_option_label(chrome, w, w->scroll + row);
        const char* label_end = label + strlen(label);

        /* From where the row below was found. A row that is not found (an
         * option with no text draws nothing) leaves that place as it is. */
        for( int scan = at; scan >= 0; scan-- )
        {
            const struct ToriRSChromePrim* prim = &prims[scan];
            int line_h;
            int line_box;

            if( prim->kind != TORIRS_CHROME_PRIM_TEXT || !prim->text || prim->text < label || prim->text > label_end )
                continue;
            /* Every row shares the list's clip; the first one found sets it,
             * and a match under another clip is outside the list. */
            if( found > 0 && (prim->clip.x != clip.x || prim->clip.y != clip.y || prim->clip.h != clip.h) )
                break;
            clip = prim->clip;
            line_h = ToriRSChrome_FontLineHeightQ(prim->font_slot, ToriRSChrome_ScaleQ(chrome));
            line_box = ToriRSChrome_FontLineBoxQ(prim->font_slot, ToriRSChrome_ScaleQ(chrome));
            /* The text's own line box: inside the row's band whatever the
             * band's padding is. */
            out[row].x = clip.x;
            out[row].y = prim->y - line_h;
            out[row].w = clip.w;
            out[row].h = line_box;
            found++;
            at = scan - 1;
            break;
        }
    }
    return found;
}

struct AgentUiFilter
{
    const char* panel;
    const char* text;
    int visible_only;
};

/** Does anything a person can read on the widget hold `text`? */
static int
agent_widget_holds_text(
    const struct ToriRSChrome* chrome,
    const struct ToriRSChromeWidget* w,
    const char* text)
{
    if( agent_text_contains(w->label, text) || agent_text_contains(w->text, text)
        || agent_text_contains(w->tooltip, text) )
        return 1;
    if( w->kind == TORIRS_CHROME_W_DROPDOWN || w->kind == TORIRS_CHROME_W_TABSTRIP )
        for( int i = 0; i < w->option_count; i++ )
            if( agent_text_contains(agent_option_label(chrome, w, i), text) )
                return 1;
    if( w->kind == TORIRS_CHROME_W_VLIST || w->kind == TORIRS_CHROME_W_CODE )
        for( int row = w->imin; row < w->imin + w->rows && row < w->imax; row++ )
        {
            const char* line = agent_vlist_row_text(chrome, w, row);
            if( line && agent_text_contains(line, text) )
                return 1;
        }
    return 0;
}

/** Options one widget lists in `ui` before the rest are only counted: a
 *  palette of every underlay in the cache is hundreds. */
#define AGENT_UI_OPTIONS_MAX 64

static void
agent_ui_write_widget(
    struct AgentJsonWriter* out,
    const struct ToriRSChrome* chrome,
    int id)
{
    const struct ToriRSChromeWidget* w = &chrome->widgets[id];
    const struct ToriRSChromePanel* p = &chrome->panels[w->panel];
    int const on_screen = agent_widget_on_screen(chrome, w);
    char caption[TORIRS_CHROME_INPUT_MAX];
    const char* chord;

    AgentJsonW_ObjBegin(out);
    AgentJsonW_KInt(out, "id", id);
    AgentJsonW_KStr(out, "kind", agent_widget_kind_name(w));
    if( w->kind == TORIRS_CHROME_W_LABEL && w->label_style != TORIRS_CHROME_LABEL_PLAIN )
    {
        static const char* const k_styles[] = { "plain", "heading", "paragraph", "key_value", "progress", "error" };
        if( w->label_style > 0 && w->label_style < TORIRS_CHROME_LABEL_STYLE_COUNT )
            AgentJsonW_KStr(out, "style", k_styles[w->label_style]);
    }

    agent_widget_caption(w, caption, sizeof(caption));
    if( caption[0] )
        AgentJsonW_KStr(out, "label", caption);
    /* The value, where the widget has one that is not its name. A dropdown's
     * is its chosen option, written with its options below. */
    if( w->text[0] && w->kind != TORIRS_CHROME_W_DROPDOWN && w->kind != TORIRS_CHROME_W_TABSTRIP
        && (w->label[0] || w->kind == TORIRS_CHROME_W_TEXTINPUT || w->kind == TORIRS_CHROME_W_TEXTAREA
            || w->kind == TORIRS_CHROME_W_COLORPICK) )
        AgentJsonW_KStr(out, "text", w->text);
    /* A menu row's or a button's chord: after a tab in its caption, or the
     * keycap it wears. */
    chord = w->label[0] ? strchr(w->label, '\t') : NULL;
    if( !chord && !w->label[0] && w->kind != TORIRS_CHROME_W_TEXTINPUT && w->kind != TORIRS_CHROME_W_TEXTAREA
        && w->kind != TORIRS_CHROME_W_COLORPICK )
        chord = strchr(w->text, '\t');
    if( chord && chord[1] )
        AgentJsonW_KStr(out, "chord", chord + 1);
    else if( w->chord[0] )
        AgentJsonW_KStr(out, "chord", w->chord);

    if( w->kind == TORIRS_CHROME_W_CHECKBOX || w->kind == TORIRS_CHROME_W_RADIO || w->kind == TORIRS_CHROME_W_ICONBUTTON
        || w->kind == TORIRS_CHROME_W_TREE || (w->kind == TORIRS_CHROME_W_LISTROW && !w->row_locked) )
        AgentJsonW_KBool(out, "checked", w->checked != 0);
    if( w->disabled )
        AgentJsonW_KBool(out, "disabled", 1);
    if( chrome->focus == id )
        AgentJsonW_KBool(out, "focused", 1);

    if( w->kind == TORIRS_CHROME_W_TABSTRIP )
    {
        /* One widget, several things to press: each tab with its own box. */
        AgentJsonW_KInt(out, "value", p->active_tab);
        AgentJsonW_Key(out, "tabs");
        AgentJsonW_ArrBegin(out);
        for( int i = 0; i < w->option_count; i++ )
        {
            struct ToriRSChromeRect box;

            AgentJsonW_ObjBegin(out);
            AgentJsonW_KInt(out, "tab", i);
            AgentJsonW_KStr(out, "label", agent_option_label(chrome, w, i));
            if( on_screen && agent_tab_rect(chrome, w, i, &box) )
                AgentJsonW_KRect(out, "rect", box.x, box.y, box.w, box.h);
            AgentJsonW_ObjEnd(out);
        }
        AgentJsonW_ArrEnd(out);
    }
    else if( w->kind == TORIRS_CHROME_W_DROPDOWN )
    {
        int const listed = w->option_count < AGENT_UI_OPTIONS_MAX ? w->option_count : AGENT_UI_OPTIONS_MAX;

        /* A menu has no value: each of its rows is a command. */
        if( !w->menu_mode )
        {
            AgentJsonW_KInt(out, "value", w->selected);
            if( w->selected >= 0 && w->selected < w->option_count )
                AgentJsonW_KStr(out, "text", agent_option_label(chrome, w, w->selected));
        }
        if( chrome->dropdown_open == id )
            AgentJsonW_KBool(out, "open", 1);
        AgentJsonW_Key(out, "options");
        AgentJsonW_ArrBegin(out);
        for( int i = 0; i < listed; i++ )
        {
            char left[TORIRS_CHROME_INPUT_MAX];
            (void)agent_text_split_tab(agent_option_label(chrome, w, i), left, sizeof(left));
            AgentJsonW_Str(out, left);
        }
        AgentJsonW_ArrEnd(out);
        /* Stated when `options` is not all of them. */
        if( w->option_count > listed )
            AgentJsonW_KInt(out, "option_count", w->option_count);
    }
    else if( w->kind == TORIRS_CHROME_W_SLIDER )
    {
        AgentJsonW_KInt(out, "value", w->ivalue);
        AgentJsonW_KInt(out, "min", w->imin);
        AgentJsonW_KInt(out, "max", w->imax);
        AgentJsonW_KInt(out, "step", w->istep);
    }
    else if( w->kind == TORIRS_CHROME_W_COLORPICK )
        AgentJsonW_KInt(out, "value", w->selected);
    else if( w->kind == TORIRS_CHROME_W_TREE )
    {
        AgentJsonW_KInt(out, "depth", w->iaux);
        AgentJsonW_KStr(out, "expander",
            w->ivalue == TORIRS_CHROME_TREE_OPEN ? "open" : w->ivalue == TORIRS_CHROME_TREE_CLOSED ? "closed" : "leaf");
    }
    else if( w->kind == TORIRS_CHROME_W_VLIST || w->kind == TORIRS_CHROME_W_CODE )
    {
        /* The list holds only its window: `row_count` rows exist, `first` is
         * the top one shown, and `rows` are the ones the host has filled. */
        AgentJsonW_KInt(out, "value", w->ivalue);
        AgentJsonW_KInt(out, "row_count", w->imax);
        AgentJsonW_KInt(out, "first", w->imin);
        AgentJsonW_KInt(out, "window", w->rows);
        AgentJsonW_Key(out, "rows");
        AgentJsonW_ArrBegin(out);
        for( int row = w->imin; row < w->imin + w->rows && row < w->imax; row++ )
        {
            const char* line = agent_vlist_row_text(chrome, w, row);
            struct ToriRSChromeRect box;
            char cells[TORIRS_CHROME_INPUT_MAX];
            size_t used = 0;

            if( !line )
                continue;
            /* A cells row separates its fields with 0x1F; a tab reads the
             * same and is a character JSON has a spelling for. */
            for( const char* c = line; *c && used + 1 < sizeof(cells); c++ )
                cells[used++] = *c == TORIRS_CHROME_CELL_SEP ? '\t' : *c;
            cells[used] = '\0';
            AgentJsonW_ObjBegin(out);
            AgentJsonW_KInt(out, "row", row);
            AgentJsonW_KStr(out, "text", cells);
            if( on_screen && agent_vlist_row_rect(chrome, w, row, &box) )
                AgentJsonW_KRect(out, "rect", box.x, box.y, box.w, box.h);
            AgentJsonW_ObjEnd(out);
        }
        AgentJsonW_ArrEnd(out);
    }

    if( w->tooltip[0] )
        AgentJsonW_KStr(out, "tooltip", w->tooltip);
    if( on_screen )
        AgentJsonW_KRect(out, "rect", w->x, w->y, w->w, w->h);
    else
    {
        AgentJsonW_KBool(out, "visible", 0);
        AgentJsonW_KStr(out, "why", agent_widget_hidden_why(chrome, w));
        if( w->tab >= 0 )
            AgentJsonW_KInt(out, "tab", w->tab);
    }
    AgentJsonW_ObjEnd(out);
}

/* The right-click menu (the game's, and the editor's Select / Delete rows):
 * its entries are options, drawn bottom to top. */
void
Agent_WriteMinimenu(
    struct AgentCtx* ctx,
    struct AgentJsonWriter* out)
{
    const struct UIMinimenu* menu;

    assert(ctx);
    assert(out);
    menu = ctx->app->frame_view ? ctx->app->frame_view->minimenu : NULL;
    AgentJsonW_ObjBegin(out);
    AgentJsonW_KBool(out, "visible", menu && menu->visible);
    if( menu && menu->visible )
    {
        AgentJsonW_KRect(out, "rect", menu->x, menu->y, menu->width, menu->height);
        AgentJsonW_Key(out, "entries");
        AgentJsonW_ArrBegin(out);
        for( int i = 0; i < menu->option_count; i++ )
        {
            int const y = UIMinimenu_OptionY(menu, i);
            char plain[UITREE_MINIMENU_OPTION_LEN];

            agent_text_plain(menu->options[i].text, plain, sizeof(plain));
            AgentJsonW_ObjBegin(out);
            AgentJsonW_KInt(out, "index", i);
            AgentJsonW_KStr(out, "text", plain);
            AgentJsonW_KRect(out, "rect", menu->x, y - menu->layout.hover_above, menu->width,
                menu->layout.hover_above + menu->layout.hover_below);
            AgentJsonW_ObjEnd(out);
        }
        AgentJsonW_ArrEnd(out);
    }
    AgentJsonW_ObjEnd(out);
}

static void
agent_ui_write_dropdown(
    struct AgentJsonWriter* out,
    const struct ToriRSChrome* chrome)
{
    const struct ToriRSChromeWidget* w = &chrome->widgets[chrome->dropdown_open];
    struct ToriRSChromeRect boxes[TORIRS_CHROME_DROPDOWN_ROWS];
    int const rows = agent_dropdown_rows(w);
    char caption[TORIRS_CHROME_INPUT_MAX];

    (void)agent_dropdown_row_rects(chrome, boxes);
    agent_widget_caption(w, caption, sizeof(caption));
    AgentJsonW_ObjBegin(out);
    AgentJsonW_KInt(out, "widget", chrome->dropdown_open);
    AgentJsonW_KStr(out, "label", caption);
    AgentJsonW_KInt(out, "option_count", w->option_count);
    /* The list shows `rows` options from `first`; the wheel over it moves it. */
    AgentJsonW_KInt(out, "first", w->scroll);
    AgentJsonW_Key(out, "rows");
    AgentJsonW_ArrBegin(out);
    for( int row = 0; row < rows; row++ )
    {
        int const index = w->scroll + row;
        const char* label = agent_option_label(chrome, w, index);
        char left[TORIRS_CHROME_INPUT_MAX];
        const char* chord = agent_text_split_tab(label, left, sizeof(left));

        AgentJsonW_ObjBegin(out);
        AgentJsonW_KInt(out, "option", index);
        AgentJsonW_KStr(out, "label", left);
        if( chord && chord[0] )
            AgentJsonW_KStr(out, "chord", chord);
        if( !agent_option_enabled(chrome, w, index) )
            AgentJsonW_KBool(out, "disabled", 1);
        if( boxes[row].w > 0 )
            AgentJsonW_KRect(out, "rect", boxes[row].x, boxes[row].y, boxes[row].w, boxes[row].h);
        AgentJsonW_ObjEnd(out);
    }
    AgentJsonW_ArrEnd(out);
    AgentJsonW_ObjEnd(out);
}

static enum AgentVerbResult
verb_ui(struct AgentCtx* ctx)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    struct EditorUi* eui = Agent_EditorUi(ctx);
    struct AgentUiFilter filter;
    struct AgentJsonWriter* out;
    int surface_w;
    int surface_h;
    int matched = 0;

    memset(&filter, 0, sizeof(filter));
    if( Agent_ArgString(ctx, "panel", &filter.panel) < 0 || Agent_ArgString(ctx, "text", &filter.text) < 0
        || Agent_ArgBool(ctx, "visible_only", &filter.visible_only) < 0 )
        return AGENT_VERB_DONE;
    if( filter.panel )
    {
        for( int i = 0; i < chrome->panel_count && !matched; i++ )
            matched = agent_panel_matches(ctx, i, filter.panel);
        if( !matched )
        {
            Agent_ReplyError(ctx, "there is no panel \"%s\": ui with no \"panel\" lists every panel's title and module",
                filter.panel);
            return AGENT_VERB_DONE;
        }
    }

    Agent_SurfaceSize(&surface_w, &surface_h);
    out = Agent_ReplyBegin(ctx);
    AgentJsonW_KInt(out, "frame", ctx->frame);
    AgentJsonW_Key(out, "surface");
    AgentJsonW_ObjBegin(out);
    AgentJsonW_KInt(out, "w", surface_w);
    AgentJsonW_KInt(out, "h", surface_h);
    AgentJsonW_ObjEnd(out);
    /* What has the keyboard, what is in the way, where the pointer is. */
    AgentJsonW_KInt(out, "focus", chrome->focus);
    AgentJsonW_KInt(out, "modal", ToriRSChrome_ModalPanel(chrome));
    {
        int const pointer[2] = { chrome->hover_x, chrome->hover_y };
        AgentJsonW_KInts(out, "pointer", pointer, 2);
    }
    /* The boxes below are the last Build's. While this is true a panel has
     * changed since and its boxes move on the next frame. */
    if( chrome->dirty )
        AgentJsonW_KBool(out, "stale", 1);
    if( chrome->dropdown_open >= 0 )
    {
        AgentJsonW_Key(out, "dropdown");
        agent_ui_write_dropdown(out, chrome);
    }
    if( ctx->app->frame_view && ctx->app->frame_view->minimenu && ctx->app->frame_view->minimenu->visible )
    {
        AgentJsonW_Key(out, "minimenu");
        Agent_WriteMinimenu(ctx, out);
    }

    AgentJsonW_Key(out, "panels");
    AgentJsonW_ArrBegin(out);
    for( int i = 0; i < chrome->panel_count; i++ )
    {
        const struct ToriRSChromePanel* p = &chrome->panels[i];
        const char* module = agent_panel_module(eui, i);
        const struct EditorUiGroup* group = agent_panel_group(eui, i);
        int any_text = 0;

        if( filter.panel && !agent_panel_matches(ctx, i, filter.panel) )
            continue;
        if( filter.visible_only && !p->visible )
            continue;
        if( filter.text )
        {
            for( int id = p->first_widget; id >= 0 && !any_text; id = chrome->widgets[id].next )
                any_text = agent_widget_holds_text(chrome, &chrome->widgets[id], filter.text);
            if( !any_text )
                continue;
        }

        AgentJsonW_ObjBegin(out);
        AgentJsonW_KInt(out, "index", i);
        AgentJsonW_KStr(out, "title", agent_panel_title(ctx, i));
        if( module )
            AgentJsonW_KStr(out, "module", module);
        if( group )
            AgentJsonW_KStr(out, "group", group->name);
        AgentJsonW_KStr(out, "style", agent_panel_style_name(p->style));
        AgentJsonW_KBool(out, "visible", p->visible != 0);
        if( p->visible )
        {
            AgentJsonW_KRect(out, "rect", p->last_rect.x, p->last_rect.y, p->last_rect.w, p->last_rect.h);
            if( p->collapsed )
                AgentJsonW_KBool(out, "collapsed", 1);
            if( p->modal )
                AgentJsonW_KBool(out, "modal", 1);
            if( p->maximised )
                AgentJsonW_KBool(out, "maximised", 1);
            if( p->scrollable && p->content_h > p->view_h )
            {
                AgentJsonW_KInt(out, "scroll", p->scroll_y);
                AgentJsonW_KInt(out, "scroll_max", p->content_h - p->view_h);
            }
        }
        /* A closed panel's rows are listed only when it was asked for by
         * name: sixty closed panels of rows nobody can see drown the ones
         * that are on screen. */
        if( p->visible || filter.panel || filter.text )
        {
            int strip = -1;

            for( int id = p->first_widget; id >= 0 && strip < 0; id = chrome->widgets[id].next )
                if( chrome->widgets[id].kind == TORIRS_CHROME_W_TABSTRIP )
                    strip = id;
            if( strip >= 0 )
            {
                const struct ToriRSChromeWidget* tabs = &chrome->widgets[strip];
                AgentJsonW_Key(out, "tabs");
                AgentJsonW_ArrBegin(out);
                for( int t = 0; t < tabs->option_count; t++ )
                    AgentJsonW_Str(out, agent_option_label(chrome, tabs, t));
                AgentJsonW_ArrEnd(out);
                AgentJsonW_KInt(out, "active_tab", p->active_tab);
            }
            AgentJsonW_Key(out, "widgets");
            AgentJsonW_ArrBegin(out);
            for( int id = p->first_widget; id >= 0; id = chrome->widgets[id].next )
            {
                const struct ToriRSChromeWidget* w = &chrome->widgets[id];

                if( w->kind == TORIRS_CHROME_W_SEPARATOR || w->kind == TORIRS_CHROME_W_FREE )
                    continue;
                if( filter.visible_only && !agent_widget_on_screen(chrome, w) )
                    continue;
                if( filter.text && !agent_widget_holds_text(chrome, w, filter.text) )
                    continue;
                agent_ui_write_widget(out, chrome, id);
            }
            AgentJsonW_ArrEnd(out);
        }
        AgentJsonW_ObjEnd(out);
    }
    AgentJsonW_ArrEnd(out);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

/* ---- the chrome by name: ui_click, ui_set ------------------------------------------------
 *
 * A TARGET is what a click lands on: a widget, one tab of a strip, one row of
 * a list, one row of the open dropdown, one row of the right-click menu.
 */

enum AgentTargetKind
{
    AGENT_TARGET_WIDGET = 1,
    AGENT_TARGET_TAB,
    AGENT_TARGET_ROW,
    AGENT_TARGET_OPTION,
    AGENT_TARGET_MENU,
};

struct AgentTarget
{
    int kind;
    /** WIDGET, TAB, ROW: the widget; OPTION: the open dropdown. */
    int widget;
    /** TAB: the tab; ROW: the absolute row; OPTION: the option; MENU: the row. */
    int index;
};

/** Slots of AgentOp.ints the two verbs keep their target and click in. */
enum
{
    UC_KIND = 0,
    UC_WIDGET,
    UC_INDEX,
    UC_SERIAL,
    UC_BUTTON,
    UC_MODS,
    UC_DOUBLE,
    UC_TRIES,
    UC_X,
    UC_Y,
    UC_ENTER,
    UC_TYPED,
    /** The chrome's build serial + 1 when this op last moved a scroll (0:
     *  it has not): the boxes are stale until the serial moves on. */
    UC_BUILD,
    /** Frames the check in progress has waited for the client to catch up. */
    UC_WAITS,
    /** The widget's kind when the op was aimed (its caption is AgentOp.path). */
    UC_WKIND,
};

/** Frames a check waits for what it checks before it calls it a failure: an
 *  input frame the client did not consume (a load, a settling interface) is
 *  held over to the next, so a move pushed one frame ago may not have landed. */
#define AGENT_UI_SETTLE_FRAMES 8

/** 1 while the check may still wait a frame (and counts it); 0 when its
 *  frames are spent. */
static int
agent_check_may_wait(struct AgentOp* op)
{
    if( op->ints[UC_WAITS] >= AGENT_UI_SETTLE_FRAMES )
        return 0;
    op->ints[UC_WAITS]++;
    return 1;
}

/** The script's CALL values. */
enum
{
    UC_CALL_PREPARE = 1,
    UC_CALL_VERIFY,
    UC_CALL_FOCUSED,
    UC_CALL_TYPED,
};

/** Frames a target may take to come on screen after its panel or its list
 *  was scrolled to it. */
#define AGENT_UI_SCROLL_TRIES 6

static const struct UIMinimenu*
agent_minimenu(struct AgentCtx* ctx)
{
    return ctx->app->frame_view ? ctx->app->frame_view->minimenu : NULL;
}

/** "button 57 \"Copy to new...\" in Catalog": the target for a sentence. */
static void
agent_target_describe(
    struct AgentCtx* ctx,
    const struct AgentTarget* target,
    char* out,
    size_t out_size)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    char caption[TORIRS_CHROME_INPUT_MAX];

    if( target->kind == AGENT_TARGET_MENU )
    {
        const struct UIMinimenu* menu = agent_minimenu(ctx);
        agent_text_plain(menu && target->index < menu->option_count ? menu->options[target->index].text : "", caption,
            sizeof(caption));
        snprintf(out, out_size, "menu row %d \"%s\"", target->index, caption);
        return;
    }
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[target->widget];
        const char* panel = agent_panel_title(ctx, w->panel);

        if( target->kind == AGENT_TARGET_OPTION )
        {
            (void)agent_text_split_tab(agent_option_label(chrome, w, target->index), caption, sizeof(caption));
            snprintf(out, out_size, "option %d \"%s\" of the open list", target->index, caption);
        }
        else if( target->kind == AGENT_TARGET_TAB )
            snprintf(out, out_size, "tab %d \"%s\" of widget %d in %s", target->index,
                agent_option_label(chrome, w, target->index), target->widget, panel[0] ? panel : "an untitled panel");
        else if( target->kind == AGENT_TARGET_ROW )
            snprintf(out, out_size, "row %d of list %d in %s", target->index, target->widget,
                panel[0] ? panel : "an untitled panel");
        else
        {
            agent_widget_caption(w, caption, sizeof(caption));
            snprintf(out, out_size, "%s %d \"%s\" in %s", agent_widget_kind_name(w), target->widget, caption,
                panel[0] ? panel : "an untitled panel");
        }
    }
}

/** A tab's title with the chord a group's strip adds to it taken off:
 *  "Catalog (F8)" is the tab a person calls Catalog. */
static void
agent_tab_plain(
    const char* title,
    char* out,
    size_t out_size)
{
    size_t length = strlen(title);

    if( length >= out_size )
        length = out_size - 1;
    memcpy(out, title, length);
    out[length] = '\0';
    if( length > 3 && out[length - 1] == ')' )
    {
        char* open = strrchr(out, '(');
        if( open && open > out && open[-1] == ' ' )
            open[-1] = '\0';
    }
}

static int
agent_caption_matches(
    const char* caption,
    const char* wanted,
    int exact)
{
    if( !caption[0] )
        return 0;
    return exact ? agent_text_equal(caption, wanted) : agent_text_contains(caption, wanted);
}

#define AGENT_FIND_MAX 6

struct AgentFind
{
    struct AgentTarget hits[AGENT_FIND_MAX];
    /** Every match, of which `hits` holds the first AGENT_FIND_MAX. */
    int count;
};

static void
agent_find_add(
    struct AgentFind* find,
    int kind,
    int widget,
    int index)
{
    if( find->count < AGENT_FIND_MAX )
    {
        find->hits[find->count].kind = kind;
        find->hits[find->count].widget = widget;
        find->hits[find->count].index = index;
    }
    find->count++;
}

/**
 * Every target whose caption is `label` (exact), or holds it (not exact).
 *
 * In layers, the way a click would meet them: the open dropdown's rows, then
 * the right-click menu's, then the tabs and widgets of the open panels
 * (`panel` narrows those to one panel). A layer with a match ends the search:
 * an open list takes the next click whatever lies under it. `shown` 1 looks at
 * widgets their panels lay out; 0 at the ones they do not (to say why not).
 * `inputs_only` keeps only the widgets that hold text.
 */
static void
agent_find_label(
    struct AgentCtx* ctx,
    const char* label,
    const char* panel,
    int exact,
    int shown,
    int inputs_only,
    struct AgentFind* find)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    const struct UIMinimenu* menu = agent_minimenu(ctx);
    char caption[TORIRS_CHROME_INPUT_MAX];

    memset(find, 0, sizeof(*find));
    if( !inputs_only && shown && chrome->dropdown_open >= 0 )
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[chrome->dropdown_open];
        for( int i = 0; i < w->option_count; i++ )
        {
            (void)agent_text_split_tab(agent_option_label(chrome, w, i), caption, sizeof(caption));
            if( agent_caption_matches(caption, label, exact) )
                agent_find_add(find, AGENT_TARGET_OPTION, chrome->dropdown_open, i);
        }
        if( find->count > 0 )
            return;
    }
    if( !inputs_only && shown && menu && menu->visible )
    {
        for( int i = 0; i < menu->option_count; i++ )
        {
            agent_text_plain(menu->options[i].text, caption, sizeof(caption));
            if( agent_caption_matches(caption, label, exact) )
                agent_find_add(find, AGENT_TARGET_MENU, -1, i);
        }
        if( find->count > 0 )
            return;
    }
    for( int p = 0; p < chrome->panel_count; p++ )
    {
        const struct ToriRSChromePanel* pan = &chrome->panels[p];

        if( panel && !agent_panel_matches(ctx, p, panel) )
            continue;
        /* Shown widgets live in open panels; the rest are everything else. */
        if( shown && (!pan->visible || pan->collapsed) )
            continue;
        for( int id = pan->first_widget; id >= 0; id = chrome->widgets[id].next )
        {
            const struct ToriRSChromeWidget* w = &chrome->widgets[id];
            int const is_shown = pan->visible && !pan->collapsed && agent_widget_shown(chrome, w);
            int const is_input = w->kind == TORIRS_CHROME_W_TEXTINPUT || w->kind == TORIRS_CHROME_W_TEXTAREA
                                 || w->kind == TORIRS_CHROME_W_COLORPICK || w->kind == TORIRS_CHROME_W_SLIDER;

            if( w->kind == TORIRS_CHROME_W_FREE || w->kind == TORIRS_CHROME_W_SEPARATOR || is_shown != shown )
                continue;
            if( inputs_only && !is_input )
                continue;
            if( w->kind == TORIRS_CHROME_W_TABSTRIP )
            {
                for( int t = 0; t < w->option_count; t++ )
                {
                    const char* title = agent_option_label(chrome, w, t);
                    agent_tab_plain(title, caption, sizeof(caption));
                    if( agent_caption_matches(title, label, exact) || agent_caption_matches(caption, label, exact) )
                        agent_find_add(find, AGENT_TARGET_TAB, id, t);
                }
                continue;
            }
            /* A label is read, not pressed. */
            if( w->kind == TORIRS_CHROME_W_LABEL )
                continue;
            agent_widget_caption(w, caption, sizeof(caption));
            if( agent_caption_matches(caption, label, exact) )
                agent_find_add(find, AGENT_TARGET_WIDGET, id, 0);
        }
    }
}

/**
 * Resolve `label` (and `panel`) to exactly one target: 1 with it in `out`, or
 * 0 with the error reply written -- nothing matches (and why, when a match is
 * closed or on another tab), or several do (naming them).
 */
static int
agent_resolve_label(
    struct AgentCtx* ctx,
    const char* label,
    const char* panel,
    int inputs_only,
    struct AgentTarget* out)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    struct AgentFind find;
    char names[640];
    size_t used = 0;

    if( panel )
    {
        int any = 0;
        for( int i = 0; i < chrome->panel_count && !any; i++ )
            any = agent_panel_matches(ctx, i, panel);
        if( !any )
        {
            Agent_ReplyError(ctx, "there is no panel \"%s\": ui lists every panel's title and module", panel);
            return 0;
        }
    }
    agent_find_label(ctx, label, panel, 1, 1, inputs_only, &find);
    if( find.count == 0 )
        agent_find_label(ctx, label, panel, 0, 1, inputs_only, &find);
    if( find.count == 1 )
    {
        *out = find.hits[0];
        return 1;
    }
    if( find.count == 0 )
    {
        /* Not on screen. Is it somewhere a person would have to open first? */
        agent_find_label(ctx, label, panel, 1, 0, inputs_only, &find);
        if( find.count == 0 )
            agent_find_label(ctx, label, panel, 0, 0, inputs_only, &find);
        if( find.count > 0 && find.hits[0].kind != AGENT_TARGET_MENU )
        {
            const struct ToriRSChromeWidget* w = &chrome->widgets[find.hits[0].widget];
            const struct ToriRSChromePanel* pan = &chrome->panels[w->panel];
            const char* title = agent_panel_title(ctx, w->panel);
            const char* why = agent_widget_hidden_why(chrome, w);

            if( strcmp(why, "panel_closed") == 0 )
                Agent_ReplyError(ctx, "\"%s\" is in the panel \"%s\", which is closed: open it first (the View menu, "
                                      "or its view.* action)",
                    label, title);
            else if( strcmp(why, "panel_collapsed") == 0 )
                Agent_ReplyError(ctx, "\"%s\" is in the panel \"%s\", which is collapsed to its title bar: click the "
                                      "title to expand it",
                    label, title);
            else if( strcmp(why, "other_tab") == 0 )
            {
                const struct ToriRSChromeWidget* strip = NULL;
                for( int id = pan->first_widget; id >= 0 && !strip; id = chrome->widgets[id].next )
                    if( chrome->widgets[id].kind == TORIRS_CHROME_W_TABSTRIP )
                        strip = &chrome->widgets[id];
                Agent_ReplyError(ctx, "\"%s\" is on the tab \"%s\" of the panel \"%s\", which is not the tab showing: "
                                      "click that tab first",
                    label, strip ? agent_option_label(chrome, strip, w->tab) : "?", title);
            }
            else if( strcmp(why, "off_window") == 0 )
                Agent_ReplyError(ctx, "\"%s\" in the panel \"%s\" is past the window's edge (at %d,%d; the window is "
                                      "%dx%d): a person cannot press it there; the panel must fit the window",
                    label, title, w->x + w->w / 2, w->y + w->h / 2, chrome->surface_w, chrome->surface_h);
            else
                Agent_ReplyError(ctx, "\"%s\" is in the panel \"%s\" but hidden there right now: the panel does not "
                                      "offer it in its present state",
                    label, title);
            return 0;
        }
        Agent_ReplyError(ctx, "nothing %s is labelled \"%s\"%s%s: ui lists what is there",
            inputs_only ? "that holds text" : "that can be pressed", label, panel ? " in the panel " : "",
            panel ? panel : "");
        return 0;
    }
    names[0] = '\0';
    for( int i = 0; i < find.count && i < AGENT_FIND_MAX && used < sizeof(names); i++ )
    {
        char one[200];
        agent_target_describe(ctx, &find.hits[i], one, sizeof(one));
        used += (size_t)snprintf(names + used, sizeof(names) - used, "%s%s", i ? "; " : "", one);
    }
    Agent_ReplyError(ctx, "%d things are labelled \"%s\" (%s%s): name one by \"widget\", or narrow it with \"panel\"",
        find.count, label, names, find.count > AGENT_FIND_MAX ? "; ..." : "");
    return 0;
}

/**
 * Where a click on the target goes, when it is on screen: 1 with the point,
 * 0 when it has no box now (off screen, or a row outside its list's window).
 */
static int
agent_target_point(
    struct AgentCtx* ctx,
    const struct AgentTarget* target,
    int* out_x,
    int* out_y)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    struct ToriRSChromeRect box;

    if( target->kind == AGENT_TARGET_MENU )
    {
        const struct UIMinimenu* menu = agent_minimenu(ctx);
        if( !menu || !menu->visible || target->index >= menu->option_count )
            return 0;
        *out_x = menu->x + menu->width / 2;
        *out_y = UIMinimenu_OptionY(menu, target->index);
        return 1;
    }
    if( target->kind == AGENT_TARGET_OPTION )
    {
        struct ToriRSChromeRect boxes[TORIRS_CHROME_DROPDOWN_ROWS];
        const struct ToriRSChromeWidget* w;
        int row;

        if( chrome->dropdown_open != target->widget )
            return 0;
        w = &chrome->widgets[target->widget];
        row = target->index - w->scroll;
        if( row < 0 || row >= agent_dropdown_rows(w) )
            return 0;
        (void)agent_dropdown_row_rects(chrome, boxes);
        if( boxes[row].w <= 0 )
            return 0;
        *out_x = boxes[row].x + boxes[row].w / 2;
        *out_y = boxes[row].y + boxes[row].h / 2;
        return 1;
    }
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[target->widget];

        if( !agent_widget_in_view(chrome, w) )
            return 0;
        if( target->kind == AGENT_TARGET_TAB )
        {
            if( !agent_tab_rect(chrome, w, target->index, &box) )
                return 0;
            *out_x = box.x + box.w / 2;
            *out_y = box.y + box.h / 2;
            return 1;
        }
        if( target->kind == AGENT_TARGET_ROW )
        {
            if( !agent_vlist_row_rect(chrome, w, target->index, &box) )
                return 0;
            /* A third of the way in: the row's name, left of the keycaps a
             * cells row carries at its right end. */
            *out_x = box.x + box.w / 3;
            *out_y = box.y + box.h / 2;
            return 1;
        }
        /* A field's box and a dropdown's are the right part of their row,
         * past the label column: three quarters in is inside it. */
        if( w->kind == TORIRS_CHROME_W_TEXTINPUT || w->kind == TORIRS_CHROME_W_COLORPICK
            || (w->kind == TORIRS_CHROME_W_DROPDOWN && !w->menu_mode) )
            *out_x = w->x + w->w * 3 / 4;
        else
            *out_x = w->x + w->w / 2;
        *out_y = w->y + w->h / 2;
    }
    return 1;
}

/**
 * The target cannot be clicked where it is: move its panel's scroll, or its
 * list's, so the next Build puts it on screen. 1 when something was moved
 * (look again in a frame or two), 0 when nothing can bring it.
 */
static int
agent_target_scroll_to(
    struct AgentCtx* ctx,
    const struct AgentTarget* target)
{
    struct ToriRSChrome* chrome = &ctx->app->dbg_ui;
    struct ToriRSChromeWidget* w;
    struct ToriRSChromePanel* p;

    if( target->kind == AGENT_TARGET_MENU || target->kind == AGENT_TARGET_OPTION )
        return 0;
    w = &chrome->widgets[target->widget];
    p = &chrome->panels[w->panel];
    if( !p->visible || p->collapsed || !agent_widget_shown(chrome, w) )
        return 0;
    if( target->kind == AGENT_TARGET_ROW && (target->index < w->imin || target->index >= w->imin + w->rows) )
    {
        /* The row is outside its list's window: the list to it, the row in
         * the middle of the window (the chrome clamps the ends). */
        int const first = target->index - w->rows / 2;
        ToriRSChrome_VListSetScroll(chrome, target->widget, first < 0 ? 0 : first);
        ctx->app->need_redraw = 1;
        return 1;
    }
    if( !(p->scrollable && p->content_h > p->view_h) || agent_widget_in_view(chrome, w) )
        return 0;
    {
        /* A row scrolled out keeps the y it would be drawn at (and no
         * height); one half in view has its whole box. Either way its top
         * goes to the top of the panel's scroll window. */
        int scroll = p->scroll_y + (w->y - p->content_y);
        if( scroll < 0 )
            scroll = 0;
        if( scroll == p->scroll_y )
            return 0;
        ToriRSChrome_PanelSetScroll(chrome, w->panel, scroll);
    }
    ctx->app->need_redraw = 1;
    return 1;
}

/**
 * Is the pointer, now at the click's point, on the target as the CHROME sees
 * it? 1, or 0 with `why` a clause saying what is there instead.
 */
static int
agent_target_under_pointer(
    struct AgentCtx* ctx,
    const struct AgentTarget* target,
    int x,
    int y,
    char* why,
    size_t why_size)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    int hit;

    if( target->kind == AGENT_TARGET_MENU )
    {
        const struct UIMinimenu* menu = agent_minimenu(ctx);
        if( menu && menu->visible && UIMinimenu_HitOption(menu, x, y) == target->index )
            return 1;
        snprintf(why, why_size, "the menu closed or its rows moved");
        return 0;
    }
    if( target->kind == AGENT_TARGET_OPTION )
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[target->widget];
        if( chrome->dropdown_open == target->widget && chrome->dropdown_hover_row >= 0
            && w->scroll + chrome->dropdown_hover_row == target->index )
            return 1;
        snprintf(why, why_size, "the open list %s",
            chrome->dropdown_open != target->widget ? "closed" : "has another row under that point");
        return 0;
    }
    if( target->kind == AGENT_TARGET_ROW )
    {
        if( ToriRSChrome_VListHoverRow(chrome, target->widget, NULL) == target->index )
            return 1;
        snprintf(why, why_size, "the list has another row (or none) under that point");
        return 0;
    }
    /* An open list or a dialog takes the press before any panel does. */
    if( chrome->dropdown_open >= 0 && chrome->dropdown_open != target->widget )
    {
        snprintf(why, why_size, "a dropdown list is open and would take the press (press escape, or pick from it)");
        return 0;
    }
    hit = ToriRSChrome_HitTest(chrome, x, y);
    if( hit == target->widget )
        return 1;
    if( hit >= 0 )
    {
        char caption[TORIRS_CHROME_INPUT_MAX];
        agent_widget_caption(&chrome->widgets[hit], caption, sizeof(caption));
        snprintf(why, why_size, "%s %d \"%s\" in %s is under that point", agent_widget_kind_name(&chrome->widgets[hit]),
            hit, caption, agent_panel_title(ctx, chrome->widgets[hit].panel));
    }
    else if( ToriRSChrome_ModalPanel(chrome) >= 0 )
        snprintf(why, why_size, "the dialog \"%s\" is open and takes every press until it is answered",
            agent_panel_title(ctx, ToriRSChrome_ModalPanel(chrome)));
    else
        snprintf(why, why_size, "another panel covers that point, or the row is cut off at its panel's edge");
    return 0;
}

static void
agent_target_load(
    const struct AgentOp* op,
    struct AgentTarget* target)
{
    target->kind = op->ints[UC_KIND];
    target->widget = op->ints[UC_WIDGET];
    target->index = op->ints[UC_INDEX];
}

/**
 * The widget the op was aimed at is still that widget.
 *
 * A handle is not an identity: a panel that rebuilds its rows recycles them,
 * and the serial is what tells. But a panel that rebuilt the SAME row in the
 * same slot -- a list that restates itself when its Find box changes, the box
 * included -- has only renumbered it: same handle, same kind, same caption.
 * That is taken as the same widget (and its new serial adopted), so typing
 * into such a box is not reported as a failure; anything else is.
 */
static int
agent_target_alive(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    int const widget = op->ints[UC_WIDGET];
    char caption[TORIRS_CHROME_INPUT_MAX];

    if( op->ints[UC_KIND] == AGENT_TARGET_MENU )
        return 1;
    if( widget < 0 || widget >= chrome->widget_count || chrome->widgets[widget].kind == TORIRS_CHROME_W_FREE )
        return 0;
    if( chrome->widgets[widget].serial == op->ints[UC_SERIAL] )
        return 1;
    agent_widget_caption(&chrome->widgets[widget], caption, sizeof(caption));
    if( chrome->widgets[widget].kind != op->ints[UC_WKIND] || strcmp(caption, op->path) != 0 )
        return 0;
    op->ints[UC_SERIAL] = chrome->widgets[widget].serial;
    return 1;
}

/** After the last character: the rest of `ui_set` (the check, and Return). */
static void
agent_set_append_commit(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    const struct ToriRSChromeWidget* w = &chrome->widgets[op->ints[UC_WIDGET]];

    Agent_ScriptFrames(op, 2);
    Agent_ScriptCall(op, UC_CALL_TYPED);
    /* Return commits a one-line field. In a text area it is a new line, so
     * there the text is left as typed. */
    if( op->ints[UC_ENTER] && w->kind != TORIRS_CHROME_W_TEXTAREA )
    {
        Agent_ScriptKeyDown(op, TORIRSK_RETURN, 0);
        Agent_ScriptFrames(op, 1);
        Agent_ScriptKeyUp(op, TORIRSK_RETURN, 0);
    }
}

/**
 * The script's CALLs, for ui_click and ui_set alike.
 *
 *   PREPARE  bring the target on screen (scrolling to it), then append the
 *            click: mods, move, a frame, VERIFY, press, a frame, release
 *   VERIFY   the pointer is on the target as the chrome sees it
 *   FOCUSED  (ui_set) the click gave the field the keyboard; append the
 *            clearing, the typing, TYPED and Return
 *   TYPED    (ui_set) the field holds what was typed
 */
static int
ui_target_check(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    int value)
{
    struct ToriRSChrome* chrome = &ctx->app->dbg_ui;
    struct AgentTarget target;
    char what[240];
    char why[320];
    int x = 0;
    int y = 0;

    agent_target_load(op, &target);
    if( !agent_target_alive(ctx, op) )
    {
        Agent_ReplyError(ctx, "the panel rebuilt its rows while this was on its way, so widget %d is no longer the "
                              "one that was meant: read ui again",
            op->ints[UC_WIDGET]);
        return 0;
    }
    agent_target_describe(ctx, &target, what, sizeof(what));

    if( value == UC_CALL_PREPARE )
    {
        int const button = op->ints[UC_BUTTON];
        unsigned const mods = (unsigned)op->ints[UC_MODS];

        /* A Build may be owed (this op just moved a scroll, a panel just
         * opened): the boxes to aim at are the ones it will resolve, so wait
         * for it -- a second scroll worked out from the old boxes would
         * overshoot. */
        if( op->ints[UC_TRIES] < AGENT_UI_SCROLL_TRIES
            && (chrome->dirty || (op->ints[UC_BUILD] && chrome->build_serial == op->ints[UC_BUILD] - 1)) )
        {
            op->ints[UC_TRIES]++;
            ctx->app->need_redraw = 1;
            return 2;
        }
        if( !agent_target_point(ctx, &target, &x, &y) )
        {
            if( op->ints[UC_TRIES] < AGENT_UI_SCROLL_TRIES && agent_target_scroll_to(ctx, &target) )
            {
                op->ints[UC_TRIES]++;
                op->ints[UC_BUILD] = chrome->build_serial + 1;
                return 2;
            }
            if( target.kind == AGENT_TARGET_OPTION )
            {
                const struct ToriRSChromeWidget* w = &chrome->widgets[target.widget];
                if( chrome->dropdown_open != target.widget )
                    Agent_ReplyError(ctx, "%s cannot be clicked: that list is not open (click its dropdown first)",
                        what);
                else
                    Agent_ReplyError(ctx,
                        "%s is outside the list's window (options %d..%d of %d show): turn the wheel over the list "
                        "to bring it in, then click it",
                        what, w->scroll, w->scroll + agent_dropdown_rows(w) - 1, w->option_count);
            }
            else if( target.kind == AGENT_TARGET_MENU )
                Agent_ReplyError(ctx, "%s cannot be clicked: the menu is not open any more", what);
            else
            {
                const struct ToriRSChromeWidget* w = &chrome->widgets[target.widget];
                Agent_ReplyError(ctx, "%s is not on screen (%s) and scrolling its panel does not bring it: make the "
                                      "panel taller, or maximise it",
                    what, agent_widget_hidden_why(chrome, w));
            }
            return 0;
        }
        op->ints[UC_X] = x;
        op->ints[UC_Y] = y;
        Agent_ScriptModsDown(ctx, op, mods);
        Agent_ScriptMove(op, x, y);
        Agent_ScriptFrames(op, 1);
        Agent_ScriptCall(op, UC_CALL_VERIFY);
        Agent_ScriptDown(op, button, x, y);
        Agent_ScriptFrames(op, 1);
        Agent_ScriptUp(op, button, x, y);
        if( op->ints[UC_DOUBLE] )
        {
            Agent_ScriptFrames(op, 1);
            Agent_ScriptDown(op, button, x, y);
            Agent_ScriptFrames(op, 1);
            Agent_ScriptUp(op, button, x, y);
        }
        Agent_ScriptModsUp(ctx, op, mods);
        if( op->text[0] || op->ints[UC_TYPED] )
        {
            /* ui_set: the click was for the keyboard. */
            Agent_ScriptFrames(op, 2);
            Agent_ScriptCall(op, UC_CALL_FOCUSED);
        }
        return 1;
    }

    if( value == UC_CALL_VERIFY )
    {
        if( agent_target_under_pointer(ctx, &target, op->ints[UC_X], op->ints[UC_Y], why, sizeof(why)) )
        {
            op->ints[UC_WAITS] = 0;
            return 1;
        }
        if( agent_check_may_wait(op) )
            return 2;
        Agent_ReplyError(ctx, "%s was not clicked: at its point (%d, %d) %s", what, op->ints[UC_X], op->ints[UC_Y],
            why);
        return 0;
    }

    if( value == UC_CALL_FOCUSED )
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[target.widget];
        int const old_length = (int)strlen(w->text);
        unsigned bad = 0;

        if( chrome->focus != target.widget )
        {
            if( agent_check_may_wait(op) )
                return 2;
            Agent_ReplyError(ctx, "%s did not take the keyboard when it was clicked%s", what,
                w->disabled ? ": it is disabled" : "");
            return 0;
        }
        op->ints[UC_WAITS] = 0;
        /* The caret to the end of what is there (a click there does the
         * same), then Backspace held until it is gone: the first press and
         * its auto-repeats, eight a frame. */
        (void)ToriRSChrome_FocusWidget(chrome, target.widget, old_length);
        if( old_length > 0 )
        {
            Agent_ScriptKeyDown(op, TORIRSK_BACKSPACE, 0);
            for( int i = 1; i < old_length; i++ )
            {
                if( i % 8 == 0 )
                    Agent_ScriptFrames(op, 1);
                Agent_ScriptKeyRepeat(op, TORIRSK_BACKSPACE);
            }
            Agent_ScriptFrames(op, 1);
            Agent_ScriptKeyUp(op, TORIRSK_BACKSPACE, 0);
            Agent_ScriptFrames(op, 1);
        }
        if( op->text[0] && !agent_script_text(op, op->text, 8, &bad) )
        {
            Agent_ReplyError(ctx, "\"text\" holds U+%04X, which a field here cannot take: fields hold the characters "
                                  "U+0020..U+00FF",
                bad);
            return 0;
        }
        agent_set_append_commit(ctx, op);
        return 1;
    }

    assert(value == UC_CALL_TYPED);
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[target.widget];
        if( strcmp(w->text, op->text) == 0 )
        {
            op->ints[UC_WAITS] = 0;
            return 1;
        }
        if( agent_check_may_wait(op) )
            return 2;
        Agent_ReplyError(ctx, "%s holds \"%s\" after typing \"%s\": the field refused or cut part of it, and it was "
                              "not committed",
            what, w->text, op->text);
        return 0;
    }
}

/** The reply of a ui_click or ui_set that went through: what was pressed,
 *  and what the chrome says of it now. */
static void
ui_target_finish(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    struct AgentJsonWriter* out = Agent_ReplyBegin(ctx);
    struct AgentTarget target;
    static const char* const k_kinds[] = { "", "widget", "tab", "row", "option", "menu" };

    agent_target_load(op, &target);
    AgentJsonW_KStr(out, "clicked", k_kinds[target.kind]);
    if( target.kind != AGENT_TARGET_MENU )
        AgentJsonW_KInt(out, "widget", target.widget);
    if( target.kind != AGENT_TARGET_WIDGET )
        AgentJsonW_KInt(out, "index", target.index);
    AgentJsonW_KInt(out, "x", op->ints[UC_X]);
    AgentJsonW_KInt(out, "y", op->ints[UC_Y]);
    /* What it did, as far as the chrome's own state says. */
    if( target.kind != AGENT_TARGET_MENU && agent_target_alive(ctx, op) )
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[target.widget];
        const struct ToriRSChromePanel* p = &chrome->panels[w->panel];

        if( w->kind == TORIRS_CHROME_W_CHECKBOX || w->kind == TORIRS_CHROME_W_RADIO
            || w->kind == TORIRS_CHROME_W_ICONBUTTON || w->kind == TORIRS_CHROME_W_TREE
            || w->kind == TORIRS_CHROME_W_LISTROW )
            AgentJsonW_KBool(out, "checked", w->checked != 0);
        if( w->kind == TORIRS_CHROME_W_TABSTRIP )
            AgentJsonW_KInt(out, "active_tab", p->active_tab);
        if( w->kind == TORIRS_CHROME_W_VLIST )
            AgentJsonW_KInt(out, "value", w->ivalue);
        if( w->kind == TORIRS_CHROME_W_DROPDOWN && !w->menu_mode )
            AgentJsonW_KInt(out, "value", w->selected);
        if( w->kind == TORIRS_CHROME_W_TEXTINPUT || w->kind == TORIRS_CHROME_W_TEXTAREA
            || w->kind == TORIRS_CHROME_W_COLORPICK )
            AgentJsonW_KStr(out, "text", w->text);
        AgentJsonW_KBool(out, "panel_open", p->visible != 0);
    }
    AgentJsonW_KInt(out, "focus", chrome->focus);
    if( chrome->dropdown_open >= 0 )
        AgentJsonW_KInt(out, "dropdown", chrome->dropdown_open);
    if( ToriRSChrome_ModalPanel(chrome) >= 0 )
        AgentJsonW_KStr(out, "dialog", agent_panel_title(ctx, ToriRSChrome_ModalPanel(chrome)));
    AgentJsonW_KInt(out, "frame", ctx->frame);
    Agent_ReplyEnd(ctx);
}

/**
 * The target a request names: "widget" (with "tab" or "row" for a part of
 * it), "option" (of the open dropdown), "menu" (a row of the right-click
 * menu), or "label" (with "panel"). 1 with `out` set, 0 with the error
 * written.
 */
static int
agent_request_target(
    struct AgentCtx* ctx,
    int inputs_only,
    struct AgentTarget* out)
{
    struct ToriRSChrome const* chrome = &ctx->app->dbg_ui;
    const char* label = NULL;
    const char* panel = NULL;
    int widget = -1;
    int index = -1;
    int const has_widget = Agent_ArgInt(ctx, "widget", &widget);
    int has_label;
    int has_tab = 0;
    int has_row = 0;
    int has_option = 0;
    int has_menu = 0;

    if( has_widget < 0 )
        return 0;
    has_label = Agent_ArgString(ctx, "label", &label);
    if( has_label < 0 || Agent_ArgString(ctx, "panel", &panel) < 0 )
        return 0;
    memset(out, 0, sizeof(*out));
    if( !inputs_only )
    {
        has_tab = Agent_ArgInt(ctx, "tab", &index);
        if( has_tab < 0 )
            return 0;
        has_row = has_tab ? 0 : Agent_ArgInt(ctx, "row", &index);
        if( has_row < 0 )
            return 0;
        has_option = has_tab || has_row ? 0 : Agent_ArgInt(ctx, "option", &index);
        if( has_option < 0 )
            return 0;
        has_menu = has_tab || has_row || has_option ? 0 : Agent_ArgInt(ctx, "menu", &index);
        if( has_menu < 0 )
            return 0;
    }

    if( has_menu )
    {
        const struct UIMinimenu* menu = agent_minimenu(ctx);
        if( !menu || !menu->visible || index < 0 || index >= menu->option_count )
        {
            Agent_ReplyError(ctx, "there is no menu row %d: %s", index,
                menu && menu->visible ? "ui's minimenu lists its entries" : "no right-click menu is open");
            return 0;
        }
        out->kind = AGENT_TARGET_MENU;
        out->widget = -1;
        out->index = index;
        return 1;
    }
    if( has_option )
    {
        if( chrome->dropdown_open < 0 || index < 0 || index >= chrome->widgets[chrome->dropdown_open].option_count )
        {
            Agent_ReplyError(ctx, "there is no option %d: %s", index,
                chrome->dropdown_open >= 0 ? "ui's dropdown lists the open list's rows"
                                           : "no dropdown list is open (click a dropdown first)");
            return 0;
        }
        out->kind = AGENT_TARGET_OPTION;
        out->widget = chrome->dropdown_open;
        out->index = index;
        return 1;
    }
    if( has_widget == has_label )
    {
        Agent_ReplyError(ctx, "name what to press by one of \"widget\": <id> and \"label\": \"<its caption>\"%s",
            inputs_only ? "" : " (or \"option\" of the open list, or \"menu\" of the right-click menu)");
        return 0;
    }
    if( has_label )
    {
        if( !agent_resolve_label(ctx, label, panel, inputs_only, out) )
            return 0;
    }
    else
    {
        if( widget < 0 || widget >= chrome->widget_count || chrome->widgets[widget].kind == TORIRS_CHROME_W_FREE
            || chrome->widgets[widget].kind == TORIRS_CHROME_W_SEPARATOR )
        {
            Agent_ReplyError(ctx, "there is no widget %d: ui lists the ids", widget);
            return 0;
        }
        out->kind = AGENT_TARGET_WIDGET;
        out->widget = widget;
    }

    /* A part of the widget, by number. */
    if( out->kind == AGENT_TARGET_WIDGET && (has_tab || has_row) )
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[out->widget];
        if( has_tab && (w->kind != TORIRS_CHROME_W_TABSTRIP || index < 0 || index >= w->option_count) )
        {
            Agent_ReplyError(ctx, "widget %d has no tab %d", out->widget, index);
            return 0;
        }
        if( has_row && (w->kind != TORIRS_CHROME_W_VLIST || index < 0 || index >= w->imax) )
        {
            Agent_ReplyError(ctx, "widget %d has no row %d%s", out->widget, index,
                w->kind == TORIRS_CHROME_W_VLIST ? "" : ": it is not a list");
            return 0;
        }
        out->kind = has_tab ? AGENT_TARGET_TAB : AGENT_TARGET_ROW;
        out->index = index;
    }
    if( out->kind == AGENT_TARGET_WIDGET )
    {
        const struct ToriRSChromeWidget* w = &chrome->widgets[out->widget];
        if( w->kind == TORIRS_CHROME_W_LABEL )
        {
            Agent_ReplyError(ctx, "widget %d is a label: it is read, not pressed", out->widget);
            return 0;
        }
        if( w->kind == TORIRS_CHROME_W_TABSTRIP )
        {
            Agent_ReplyError(ctx, "widget %d is a tab strip: say which tab (\"tab\": <index>, or its title as "
                                  "\"label\")",
                out->widget);
            return 0;
        }
        if( w->kind == TORIRS_CHROME_W_VLIST )
        {
            Agent_ReplyError(ctx, "widget %d is a list: say which row (\"row\": <index>)", out->widget);
            return 0;
        }
        if( inputs_only && w->kind != TORIRS_CHROME_W_TEXTINPUT && w->kind != TORIRS_CHROME_W_TEXTAREA
            && w->kind != TORIRS_CHROME_W_COLORPICK && w->kind != TORIRS_CHROME_W_SLIDER )
        {
            Agent_ReplyError(ctx, "widget %d is a %s, which holds no text: ui_set is for textinput, textarea, "
                                  "colorpick and slider",
                out->widget, agent_widget_kind_name(w));
            return 0;
        }
        if( w->disabled )
        {
            Agent_ReplyError(ctx, "widget %d is disabled: the panel does not offer it in its present state",
                out->widget);
            return 0;
        }
    }
    return 1;
}

static void
agent_target_store(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    const struct AgentTarget* target)
{
    op->ints[UC_KIND] = target->kind;
    op->ints[UC_WIDGET] = target->widget;
    op->ints[UC_INDEX] = target->index;
    if( target->kind != AGENT_TARGET_MENU )
    {
        const struct ToriRSChromeWidget* w = &ctx->app->dbg_ui.widgets[target->widget];
        op->ints[UC_SERIAL] = w->serial;
        op->ints[UC_WKIND] = w->kind;
        agent_widget_caption(w, op->path, sizeof(op->path));
    }
}

static enum AgentVerbResult
verb_ui_click(struct AgentCtx* ctx)
{
    struct AgentTarget target;
    struct AgentOp* op;
    unsigned mods = 0;
    int button = TORIRSM_LEFT;
    int twice = 0;

    if( Agent_ArgButton(ctx, &button) < 0 || Agent_ArgMods(ctx, &mods) < 0 || Agent_ArgBool(ctx, "double", &twice) < 0 )
        return AGENT_VERB_DONE;
    if( !agent_request_target(ctx, 0, &target) )
        return AGENT_VERB_DONE;

    op = Agent_ScriptArm(ctx, ui_target_check, ui_target_finish);
    agent_target_store(ctx, op, &target);
    op->ints[UC_BUTTON] = button;
    op->ints[UC_MODS] = (int)mods;
    op->ints[UC_DOUBLE] = twice;
    Agent_ScriptCall(op, UC_CALL_PREPARE);
    return AGENT_VERB_PENDING;
}

/* ui_set on a slider: `text` is a whole number in the slider's range, snapped
 * to its step. Done at once, not through the click-and-type script: there is
 * nothing to type into. */
static enum AgentVerbResult
agent_set_slider(
    struct AgentCtx* ctx,
    int widget,
    const char* text)
{
    struct ToriRSChrome* chrome = &ctx->app->dbg_ui;
    const struct ToriRSChromeWidget* w = &chrome->widgets[widget];
    struct AgentJsonWriter* out;
    char* end = NULL;
    long value;

    assert(text);
    value = strtol(text, &end, 10);
    if( end == text || *end != '\0' )
    {
        Agent_ReplyError(ctx, "widget %d is a slider: \"text\" must be a whole number (%d..%d), not \"%s\"", widget,
            w->imin, w->imax, text);
        return AGENT_VERB_DONE;
    }
    if( value < w->imin || value > w->imax )
    {
        Agent_ReplyError(ctx, "widget %d is a slider of %d..%d: %ld is outside it", widget, w->imin, w->imax, value);
        return AGENT_VERB_DONE;
    }
    if( w->disabled )
    {
        Agent_ReplyError(ctx, "widget %d is disabled: the panel does not offer it in its present state", widget);
        return AGENT_VERB_DONE;
    }
    if( w->ivalue != (int)value )
    {
        ToriRSChrome_SliderSet(chrome, widget, (int)value);
        chrome->activated = widget;
    }
    out = Agent_ReplyBegin(ctx);
    AgentJsonW_Key(out, "widget");
    AgentJsonW_Int(out, widget);
    AgentJsonW_Key(out, "value");
    AgentJsonW_Int(out, w->ivalue);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

static enum AgentVerbResult
verb_ui_set(struct AgentCtx* ctx)
{
    struct AgentTarget target;
    struct AgentOp* op;
    const char* text = NULL;
    unsigned bad = 0;
    int enter = 1;

    if( !Agent_NeedString(ctx, "text", &text) || Agent_ArgBool(ctx, "enter", &enter) < 0 )
        return AGENT_VERB_DONE;
    if( strlen(text) >= TORIRS_CHROME_INPUT_MAX )
    {
        Agent_ReplyError(ctx, "\"text\" is %d bytes and a field holds %d", (int)strlen(text),
            TORIRS_CHROME_INPUT_MAX - 1);
        return AGENT_VERB_DONE;
    }
    /* Refused before anything is clicked, not half way through the field.
     * The chrome's fields take printable ASCII and nothing else: the App
     * routes them the typed characters 32..126 (app_chrome_route_keys). */
    for( const char* c = text; *c && !bad; c++ )
        if( (unsigned char)*c > 126 )
            bad = (unsigned char)*c;
    if( bad || !agent_script_text(NULL, text, 8, &bad) )
    {
        Agent_ReplyError(ctx,
            "\"text\" holds a character an editor field cannot take (U+%04X or beyond): they hold U+0020..U+007E", bad);
        return AGENT_VERB_DONE;
    }
    if( !agent_request_target(ctx, 1, &target) )
        return AGENT_VERB_DONE;
    /* A slider takes a number: set as its keys set it (Left / Right / Home /
     * End on a focused slider), so the panel hears it through the same
     * `activated` latch a person's key or drag fires. Before this, a script
     * could only drag the knob and read where it landed. */
    if( ctx->app->dbg_ui.widgets[target.widget].kind == TORIRS_CHROME_W_SLIDER )
        return agent_set_slider(ctx, target.widget, text);
    /* Return commits a one-line field, so a newline can only be text in a
     * text area. */
    if( ctx->app->dbg_ui.widgets[target.widget].kind != TORIRS_CHROME_W_TEXTAREA
        && (strchr(text, '\n') || strchr(text, '\t')) )
    {
        Agent_ReplyError(ctx, "widget %d is a one-line field: \"text\" cannot hold a newline or a tab", target.widget);
        return AGENT_VERB_DONE;
    }

    op = Agent_ScriptArm(ctx, ui_target_check, ui_target_finish);
    agent_target_store(ctx, op, &target);
    op->ints[UC_BUTTON] = TORIRSM_LEFT;
    op->ints[UC_ENTER] = enter;
    /* Marks the op a ui_set even when the text is empty (clear the field). */
    op->ints[UC_TYPED] = 1;
    snprintf(op->text, sizeof(op->text), "%s", text);
    Agent_ScriptCall(op, UC_CALL_PREPARE);
    return AGENT_VERB_PENDING;
}

/* ---- the table ---------------------------------------------------------------------------- */

const struct AgentVerb g_agent_verbs_core[] = {
    { "hello", "", 0, verb_hello,
        "What this is: the API version, editor or game, the frame, the surface every x and y is in, the verbs." },
    { "help", "", 0, verb_help, "Every verb with the arguments it takes." },
    { "wait", "frames until timeout_ms", 0, verb_wait,
        "Reply after `frames` frames, or when the client is `until` \"idle\" (nothing loading, building or laying "
        "out) or \"ready\" (booted); ok:false after `timeout_ms` (default 10000)." },
    { "mouse", "x y action button mods", AGENT_VERB_INPUT, verb_mouse,
        "Pointer at (x, y): action move | down | up | click (default) | double; button left | right | middle; mods "
        "held for it." },
    { "drag", "from to steps button mods", AGENT_VERB_INPUT, verb_drag,
        "Press at from:[x,y], move to to:[x,y] over `steps` frames (default 12), release." },
    { "wheel", "x y notches mods", AGENT_VERB_INPUT, verb_wheel,
        "Turn the wheel over (x, y): positive notches scroll up, one a frame." },
    { "key", "chord action hold", AGENT_VERB_INPUT, verb_key,
        "A key chord (\"ctrl+s\", \"f8\", \"escape\"): action tap (default, down for `hold` frames) | down | up." },
    { "type", "text", AGENT_VERB_INPUT, verb_type,
        "Type text into whatever has the keyboard, a character a frame; a newline is Return." },
    { "shot", "path rect", 0, verb_shot,
        "Write the presented frame to `path` (.png or .bmp), optionally cut to rect:[x,y,w,h]." },
    { "ui", "panel visible_only text", 0, verb_ui,
        "The chrome as data: panels and their widgets with ids, labels, values and boxes; the open dropdown and "
        "right-click menu." },
    { "ui_click", "widget label panel tab row option menu button mods double", AGENT_VERB_INPUT, verb_ui_click,
        "Click a widget by id or caption (scrolled into view first), or a tab / list row of it, an option of the "
        "open dropdown, a row of the right-click menu." },
    { "ui_set", "widget label panel text enter", AGENT_VERB_INPUT, verb_ui_set,
        "Focus a text field, replace what it holds with `text`, and commit with Return (enter:false leaves it)." },
    { "log", "since limit source", 0, verb_log,
        "Lines of the editor's Output console (source console), its build and live log (live) or the chat box "
        "(chat) since a cursor." },
    { "quit", "discard", 0, verb_quit,
        "End the client as closing its window does; refused while documents are unsaved unless discard:true." },
    { NULL, NULL, 0, NULL, NULL },
};

#else /* __EMSCRIPTEN__ */

/* Not built on the web: agent_server.c refuses the flag there, and nothing
 * else reaches a verb. */
const struct AgentVerb g_agent_verbs_core[] = {
    { NULL, NULL, 0, NULL, NULL },
};

#endif
