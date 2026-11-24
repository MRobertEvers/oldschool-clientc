/*
 * The device lock (app/app_device_lock.h): fixed mode at rs289's 765x503 for
 * a device below TORIRS_DEVICE_LOW_SCORE_X10, decided by the server.
 */
#include "app/app_internal.h"
#include "app/app_device_lock.h"

#include "cs2vm2/cs2vm2_host.h"
#include "game/rs_cs2_host.h"
#include "log/torirs_log.h"
#include "net/isaac.h"
#include "net/net.h"
#include "net/rev/gameproto_revisions.h"
#include "net/torirs_control.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* A game stream that can carry a client control frame: our login (the
 * ToriRS subVersion, sent on every opcode_smart2 revision -- login_osrs.c)
 * on a real connection, not the replay transport's recording. */
static int
device_lock_online(struct App const* app)
{
    struct ToriRS_Network const* net = app->net;

    return net && net->state == TORIRS_NET_GAME && net->rev && net->rev->opcode_smart2 &&
           !net->rev->opcode_plaintext && net->random_out && !ToriRS_Network_IsReplay(net);
}

static void
device_lock_send(
    struct App* app,
    int type,
    uint8_t const* payload,
    int len)
{
    uint8_t frame[1 + TORIRS_CONTROL_HEADER_BYTES + TORIRS_CONTROL_CLIENT_PAYLOAD_MAX];
    int const n = torirs_control_client_frame(
        (uint8_t)isaac_next(app->net->random_out), type, payload, len, frame);

    ToriRS_Network_SendRaw(app->net, frame, n);
}

static void
device_lock_say(
    struct App* app,
    char const* text)
{
    RS_CS2Host_ChatAdd(&app->host, 0, "", NULL, text);
}

/* Fixed, at 765x503, and the host told to hold it. Written into the option
 * store like a pick in Settings, so the panel shows it and the next save
 * keeps it. */
static void
device_lock_enforce(struct App* app)
{
    app->device_lock.locked = true;
    app->host.window_mode_locked_fixed = true;
    if( app->host.window_mode != CS2VM_WINDOW_MODE_FIXED )
    {
        app->host.window_mode = CS2VM_WINDOW_MODE_FIXED;
        app->host.window_mode_dirty = true;
    }
    app->host.default_window_mode = CS2VM_WINDOW_MODE_FIXED;
    if( app->host.client_layout_mode != 0 )
    {
        app->host.client_layout_mode = 0;
        app->host.client_layout_dirty = true;
    }
    RS_CS2Host_SetOption(&app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MAX_PIXEL_WIDTH,
                         TORIRS_DEVICE_LOCK_WIDTH);
    RS_CS2Host_SetOption(&app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MAX_PIXEL_HEIGHT,
                         TORIRS_DEVICE_LOCK_HEIGHT);
}

void
App_DeviceLockBoot(struct App* app)
{
    char const* text = torirs_env_device_score();

    assert(app);
    app->device_lock.score_x10 = -1;
    if( !text )
        return;
    app->device_lock.score_x10 = (int)strtol(text, NULL, 10);
    if( app->device_lock.score_x10 < 0 )
    {
        TORIRS_ERR("device: TORIRS_DEVICE_SCORE=%s is not a score\n", text);
        app->device_lock.score_x10 = -1;
        return;
    }
    if( !torirs_device_locked(app->device_lock.score_x10) )
        return;
    TORIRS_REPORT("device: score %d.%d is below %d.%d: fixed mode at %dx%d until the server "
                  "says otherwise\n",
                  app->device_lock.score_x10 / 10, app->device_lock.score_x10 % 10,
                  TORIRS_DEVICE_LOW_SCORE_X10 / 10, TORIRS_DEVICE_LOW_SCORE_X10 % 10,
                  TORIRS_DEVICE_LOCK_WIDTH, TORIRS_DEVICE_LOCK_HEIGHT);
    device_lock_enforce(app);
}

void
App_DeviceLockTick(struct App* app)
{
    assert(app);
    if( app->host.window_mode_refused )
    {
        app->host.window_mode_refused = false;
        device_lock_say(app, "This device is too slow for resizable mode: it stays in fixed "
                             "mode.");
    }
    if( app->device_lock.score_x10 < 0 || !device_lock_online(app) ||
        app->device_lock.reported_stream == app->net->game_stream_serial )
        return;
    {
        uint8_t payload[TORIRS_CONTROL_DEVICE_REPORT_BYTES];

        torirs_control_put_u32(payload, (uint32_t)app->device_lock.score_x10);
        device_lock_send(app, TORIRS_CONTROL_DEVICE_REPORT, payload, (int)sizeof(payload));
        app->device_lock.reported_stream = app->net->game_stream_serial;
    }
}

void
App_DeviceLockPolicy(
    struct App* app,
    struct TorirsControlFrame const* frame)
{
    int locked;
    int width;
    int height;

    assert(app);
    assert(frame);
    if( !torirs_control_device_policy_decode(frame, &locked, &width, &height) )
    {
        TORIRS_ERR("control: DEVICE_POLICY of %d bytes does not decode -- dropped\n",
                   frame->length);
        return;
    }
    TORIRS_LOG("device: the server says %s\n", locked ? "locked" : "free");
    if( locked )
    {
        device_lock_enforce(app);
        return;
    }
    app->device_lock.locked = false;
    app->host.window_mode_locked_fixed = false;
}

void
App_DeviceLockVerdict(
    struct App* app,
    struct TorirsControlFrame const* frame)
{
    int option;
    int value;
    int allowed;

    assert(app);
    assert(frame);
    if( !torirs_control_setting_verdict_decode(frame, &option, &value, &allowed) )
    {
        TORIRS_ERR("control: SETTING_VERDICT of %d bytes does not decode -- dropped\n",
                   frame->length);
        return;
    }
    /* A refusal is the server's to say: it sends the chat line with it. */
    if( allowed )
        RS_CS2Host_SetOption(&app->host, RS_CS2_OPTION_DEVICE, option, value);
}

int
App_DeviceLockSettingAllowed(
    struct App* app,
    int option,
    int value)
{
    assert(app);
    if( !app->device_lock.locked )
        return 1;
    if( option != RS_CS2_DEVICEOPTION_MAX_PIXEL_WIDTH &&
        option != RS_CS2_DEVICEOPTION_MAX_PIXEL_HEIGHT )
        return 1;
    if( !device_lock_online(app) )
    {
        device_lock_say(app, "This device is too slow for a larger screen: it stays at "
                             "765x503.");
        return 0;
    }
    {
        uint8_t payload[TORIRS_CONTROL_SETTING_REQUEST_BYTES];

        torirs_control_put_u32(payload, (uint32_t)option);
        torirs_control_put_u32(payload + 4, (uint32_t)value);
        device_lock_send(app, TORIRS_CONTROL_SETTING_REQUEST, payload, (int)sizeof(payload));
    }
    return 0;
}
