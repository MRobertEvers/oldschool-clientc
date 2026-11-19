/*
 * The replay viewer's control frames (net/torirs_control.h, TORIRS_CONTROL_REPLAY_*):
 * the executors moved out of the shared packet executor (game/rs_gameproto_exec.c),
 * which calls App_ReplayControlExec for every replay type and nothing else.
 */
#include "app/app_internal.h"
#include "app/app_replay_control.h"

#include "net/net.h"
#include "net/torirs_control.h"
#include "replay/replay_chase.h"
#include "replay/replay_world.h"
#include "game/rs_chat.h"
#include "game/rs_cs2_host.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* [proc,meslayer_close_299](int clear_text, int rebuild_chat, int resume): the
 * chatbox prompt's close (OSRS-Content scripts/meslayer_close_299.cs2). */
#define EXEC_SCRIPT_MESLAYER_CLOSE 299

_Noreturn static void
exec_control_replay_malformed(struct TorirsControlFrame const* frame)
{
    TORIRS_ERR("control: a %s frame of %d bytes does not decode\n", torirs_control_type_name(frame->type),
               frame->length);
    abort();
}

/*
 * REPLAY_COORD (net/torirs_control.h): a replay seek's chase tick. Its world
 * packets are withheld, so the world under the client is not where the
 * recording is. A script reading `coord` reads this instead (app_tick.c) until
 * the keyframe puts the world back (replay_world_apply.c). The clock is
 * charged the cycles of the tick the chase did not run.
 */
static void
exec_control_replay_coord(
    struct App* app,
    struct TorirsControlFrame const* frame)
{
    struct TorirsControlReplayCoord c;

    /* Only the replay transport, in this process, writes it: one that does
     * not decode is a stream out of step (abort, not assert: the shipping
     * lane compiles assert() away). */
    if( !torirs_control_replay_coord_decode(frame, &c) )
        exec_control_replay_malformed(frame);
    /* A chase's first tick: the world it withholds is replaced whole by the
     * keyframe, so the players and npcs left standing from where the seek
     * started stop now rather than animate through the chase. */
    if( !ReplayChase_HasCoord(&app->replay_chase) && app->world )
        ReplayWorld_ClearEntities(app);
    ReplayChase_StateCoord(&app->replay_chase, c.coord);
    app->host.local_coord = c.coord;
    /* The clock: c.cycle is the cycle a chase of 30 cycles a tick executes
     * this tick in, were its pipeline never busy. That chase runs every
     * cycle this one ran since the last tick (a tick held up runs late there
     * too) and the rest to c.cycle besides; those are charged. The cycles
     * that did not tick clientclock here (a busy pipeline) did not there. */
    {
        int64_t const charge = ReplayChase_Charge(
            &app->replay_chase,
            c.cycle,
            c.first,
            FrameClockSerial_LogicCycle(&app->frame_clock.serial));

        app->host.client_clock += (int)charge;
        {
            static int trace = -1;

            /* TORIRS_REPLAY_CHASE_TRACE=1 (replay_scrub.c has the other half). */
            if( trace < 0 )
                trace = getenv("TORIRS_REPLAY_CHASE_TRACE") != NULL;
            if( trace )
                TORIRS_REPORT(
                    "replay: chase coord executed: clientclock %d (charged %lld), "
                    "logic cycle %llu\n",
                    app->host.client_clock,
                    (long long)charge,
                    (unsigned long long)FrameClockSerial_LogicCycle(&app->frame_clock.serial));
        }
    }
    /* What the local player's appearance would have published
     * (app_world_apply.c): the chat's name and CHAT_PLAYERNAME. */
    if( c.name[0] )
    {
        snprintf(app->chat.username, sizeof(app->chat.username), "%s", c.name);
        snprintf(app->host.local_player_name, sizeof(app->host.local_player_name), "%s", c.name);
    }
}

/*
 * REPLAY_CHAT: a public chat line of a PLAYER_INFO a replay seek's chase
 * withheld -- what player_apply_op's SAY / CHAT arms put in the chatbox
 * (task_exec_entity_info.c), less the overhead text of an entity the chase
 * has not got.
 */
static void
exec_control_replay_chat(
    struct App* app,
    struct TorirsControlFrame const* frame)
{
    struct TorirsControlReplayChat c;
    char const* name;

    if( !torirs_control_replay_chat_decode(frame, &c) )
        exec_control_replay_malformed(frame);
    name = c.name[0] ? c.name : NULL;
    if( c.say )
    {
        char* text = ToriMem_Alloc(ToriMem_Net(), (size_t)c.length + 1);

        assert(text);
        memcpy(text, c.data, (size_t)c.length);
        text[c.length] = '\0';
        RS_CS2Host_ChatAdd(&app->host, RS_CHAT_TYPE_PUBLIC, name ? name : "Player", name, text);
        App_NotifyChatMessage(app, RS_CHAT_TYPE_PUBLIC, name ? name : "Player", text);
        ToriMem_Free(ToriMem_Net(), text);
    }
    else
    {
        char* text;

        assert(app->net);
        assert(app->net->rev->chat_unpack);
        text = app->net->rev->chat_unpack(c.data, c.length);
        if( text )
        {
            char crowned[RS_CHAT_SENDER_LEN];

            RS_Chat_CrownedName(c.crown, name ? name : "Player", crowned, sizeof(crowned));
            RS_CS2Host_ChatAdd(&app->host, RS_CHAT_TYPE_PUBLIC, crowned, name, text);
            App_NotifyChatMessage(app, RS_CHAT_TYPE_PUBLIC, name ? name : "Player", text);
            ToriMem_Free(ToriMem_Net(), text);
        }
    }
}

/*
 * REPLAY_DIALOG: the recorder's client answered its chatbox prompt here. Its
 * own script closed the prompt as it sent the answer -- every one, from
 * meslayer_mode14_select (754, the GE search) to meslayer_enter (681, an
 * amount or a name), ends in ~meslayer_close_299(1, 1, 1) -- and no server
 * packet says so, so a replay closes it here or never. The third argument is
 * 0: the recorder's answer is already in the recording, and the scripts that
 * pass 1 have cleared the mode first, so theirs re-sends nothing either. Held
 * behind the tick's scripts like a RUNCLIENTSCRIPT, so the one that opened the
 * prompt runs first.
 */
static void
exec_control_replay_dialog(
    struct App* app,
    struct TorirsControlFrame const* frame)
{
    static struct PktRunClientScript close;
    int name = 0;

    if( !torirs_control_replay_dialog_decode(frame, &name) )
        exec_control_replay_malformed(frame);
    if( name != PKTOUT_NAME_RESUME_P_OBJDIALOG && name != PKTOUT_NAME_RESUME_P_COUNTDIALOG &&
        name != PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG && name != PKTOUT_NAME_RESUME_P_NAMEDIALOG &&
        name != PKTOUT_NAME_RESUME_P_STRINGDIALOG )
        exec_control_replay_malformed(frame);
    TORIRS_LOG("exec: REPLAY_DIALOG: the recorded client answered its prompt (pkt-out %d); closing it\n",
               name);
    memset(&close, 0, sizeof(close));
    close.script_id = EXEC_SCRIPT_MESLAYER_CLOSE;
    close.argc = 3;
    close.intv[0] = 1;
    close.intv[1] = 1;
    close.intv[2] = 0;
    App_RunClientScript(app, &close);
}

/*
 * REPLAY_WINDOW: the recorder's client was in this window class from here on.
 *
 * The class is the client's own: no server packet carries it, and a replay
 * would otherwise run in the watcher's saved one (rs_prefs.c), which lays a
 * resizable recorder's toplevel out as a fixed one or the other way round.
 * Set the way SETWINDOWMODE sets it, the App draining the canvas and window
 * change; the default too, which App_SyncNativeWindowMode restates from. Not
 * a script's choice (default_window_mode_from_script stays), so the watcher's
 * preferences file keeps theirs.
 */
static void
exec_control_replay_window(
    struct App* app,
    struct TorirsControlFrame const* frame)
{
    int mode = 0;

    assert(app);
    if( !torirs_control_replay_window_decode(frame, &mode) )
        exec_control_replay_malformed(frame);
    TORIRS_LOG("exec: REPLAY_WINDOW: the recorded client is %s from here\n",
               mode == CS2VM_WINDOW_MODE_FIXED ? "fixed" : "resizable");
    app->host.default_window_mode = mode;
    if( app->host.window_mode == mode )
        return;
    app->host.window_mode = mode;
    app->host.window_mode_dirty = true;
}

/*
 * A control frame (net/torirs_control.h), in its place among the packets.
 *
 * The replay's types are written by the RSProx replay transport alone, into
 * the stream it enciphers in this process. On any other transport one is a
 * stream out of step -- a server cannot have sent it -- and the client stops
 * here rather than read on. REPLAY_WORLD never reaches this: Task_GameProtoExec
 * awaits it (task_gameproto_exec.c).
 */
void
App_ReplayControlExpected(
    struct App const* app,
    struct TorirsControlFrame const* frame)
{
    assert(frame);
    if( app && app->net && ToriRS_Network_IsReplay(app->net) )
        return;
    TORIRS_ERR("control: a %s frame (%d bytes) on a connection that is not an RSProx replay: "
               "only the replay transport writes it\n",
               torirs_control_type_name(frame->type), frame->length);
    abort();
}

/*
 * A replay control frame (TORIRS_CONTROL_REPLAY_* other than REPLAY_WORLD), in
 * its place among the packets: the shared executor's exec_control hands every
 * replay type here.
 */
void
App_ReplayControlExec(
    struct App* app,
    struct TorirsControlFrame const* frame)
{
    App_ReplayControlExpected(app, frame);
    assert(frame->type != TORIRS_CONTROL_REPLAY_WORLD);
    if( frame->type == TORIRS_CONTROL_REPLAY_COORD )
        exec_control_replay_coord(app, frame);
    else if( frame->type == TORIRS_CONTROL_REPLAY_DIALOG )
        exec_control_replay_dialog(app, frame);
    else if( frame->type == TORIRS_CONTROL_REPLAY_WINDOW )
        exec_control_replay_window(app, frame);
    else
        exec_control_replay_chat(app, frame);
}
