#ifndef SRC_APP_APP_REPLAY_CONTROL_H
#define SRC_APP_APP_REPLAY_CONTROL_H

/*
 * The replay viewer's control frames (net/torirs_control.h,
 * TORIRS_CONTROL_REPLAY_*), executed in the App layer (app_replay_control.c).
 * The shared packet executor (game/rs_gameproto_exec.c) hands every replay
 * type to App_ReplayControlExec; REPLAY_WORLD never reaches it
 * (task_gameproto_exec.c awaits that one).
 */

struct App;
struct TorirsControlFrame;

/** A replay control frame is only ever written by the RSProx replay transport
 *  (ToriRS_Network_IsReplay): on any other connection this says so and
 *  aborts. Returns when it is expected. */
void
App_ReplayControlExpected(
    struct App const* app,
    struct TorirsControlFrame const* frame);

/** REPLAY_COORD, _DIALOG, _WINDOW or _CHAT: checked as above, then executed. */
void
App_ReplayControlExec(
    struct App* app,
    struct TorirsControlFrame const* frame);

#endif
