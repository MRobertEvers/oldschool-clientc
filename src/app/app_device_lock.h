#ifndef SRC_APP_APP_DEVICE_LOCK_H
#define SRC_APP_APP_DEVICE_LOCK_H

/*
 * The device lock: a device too slow for the game stays in fixed mode with
 * its frame rendered at rs289's 765x503 (net/torirs_control.h,
 * TORIRS_DEVICE_LOW_SCORE_X10 and the DEVICE_* / SETTING_* frames).
 *
 * The web page's device check measures a CPU score and passes it in
 * TORIRS_DEVICE_SCORE (tenths). The client:
 *   - locks provisionally at boot when the score is below the line, so the
 *     title screen already runs fixed at 765x503 (App_DeviceLockBoot);
 *   - reports the score once per game stream after login (DEVICE_REPORT);
 *   - takes the SERVER's DEVICE_POLICY as the answer: locked or not;
 *   - while locked, refuses a script's switch to resizable in the CS2 host
 *     (window_mode_locked_fixed), and asks the server before it changes the
 *     render size (SETTING_REQUEST), applying it only on an allowed
 *     SETTING_VERDICT. The server refuses both anyway (torirs_server_world.c
 *     world_device_*): the client keeping to the policy is what spares a slow
 *     device the relayout.
 */

#include <stdbool.h>

struct App;
struct TorirsControlFrame;

struct AppDeviceLock
{
    /** The page's score in tenths; -1 when it gave none (no check ran). */
    int score_x10;
    /** Locked now: provisionally from the score, then as the server says. */
    bool locked;
    /** The net's game_stream_serial the report went out on (0: none yet). */
    int reported_stream;
};

/** At boot, after preferences load: the page's score (TORIRS_DEVICE_SCORE). */
void
App_DeviceLockBoot(struct App* app);

/** Once a frame: the report owed to a new game stream, and the line the
 *  chatbox owes for a refused window mode. */
void
App_DeviceLockTick(struct App* app);

/** DEVICE_POLICY from the server. */
void
App_DeviceLockPolicy(
    struct App* app,
    struct TorirsControlFrame const* frame);

/** SETTING_VERDICT from the server: an allowed change is applied here. */
void
App_DeviceLockVerdict(
    struct App* app,
    struct TorirsControlFrame const* frame);

/**
 * May the caller set device option `option` to `value` now? 1 when the lock
 * does not cover it. 0 when it does: the server is asked (online) or the
 * change is refused (offline), and the chatbox says which.
 */
int
App_DeviceLockSettingAllowed(
    struct App* app,
    int option,
    int value);

#endif
