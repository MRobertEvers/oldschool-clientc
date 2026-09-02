#ifndef SRC_PLATFORM_PLATFORM_ANDROID_H
#define SRC_PLATFORM_PLATFORM_ANDROID_H

/*
 * The seam between the Android app's Java side and the client's frame loop.
 *
 * Three files make up this lane and they are deliberately kept apart:
 *
 *   platform_android_jni.c   the JNI surface. Owns the render thread, receives
 *                            the Surface and the MotionEvents, and knows what a
 *                            jobject is. Draws nothing.
 *   platform_android.c       the PlatformWindow interface (platform_window.h) over
 *                            ANativeWindow. Owns the ARGB canvas, the
 *                            letterbox and the blit. Knows no Java.
 *   platform_android_gl.c    the EGL context, behind the platform_gl_context.h
 *                            seam the GLES2 renderer calls.
 *
 * This header is the only thing the first two share, and everything on it
 * crosses a THREAD boundary: the JNI half runs on Android's main (UI) thread
 * and the frame loop runs on the thread the JNI half started. Every function
 * here is safe to call from the UI thread while a frame is in flight; the
 * implementations take one mutex and hold it only long enough to move a value.
 *
 * Why a second thread at all, when Android would happily call back into a
 * Choreographer callback: main.c's loop is `while( frame_loop_step() )`, a
 * blocking loop that owns the stack it runs on, and the desktop lanes are the
 * ones that must keep working. The web lane already had to invert this (it
 * hands the loop to requestAnimationFrame and never returns), and doing that
 * twice would put two different loop shapes in one file. A thread lets Android
 * run the loop the same way every native lane does.
 */

#include <stdint.h>

struct ANativeWindow;

/* ---- UI thread -> platform ------------------------------------------------
 *
 * The Surface's whole life. `Set` is called for both the first surfaceCreated
 * and every surfaceChanged after it, because the two are the same fact to a
 * renderer: here is a window, this big. A destroyed surface is NULL, and the
 * frame loop must survive it -- Android takes the surface away whenever the
 * activity stops, and gives a different one back on resume.
 */
void
PlatformAndroid_SetWindow(struct ANativeWindow* window, int width, int height);

/**
 * Block until there is a surface, or until the app is being torn down.
 *
 * PlatformWindow_Init needs a size before it can allocate the canvas, and on
 * Android nothing knows one until the SurfaceView has been laid out. So the
 * frame thread waits here exactly once, at boot, rather than every lane above
 * learning that a window can be absent.
 *
 * @return 1 when a window arrived, 0 when the app quit while waiting.
 */
int
PlatformAndroid_AwaitWindow(void);

/** The current surface, or NULL while the activity is stopped. Borrowed: valid
 *  only until the next SetWindow, which is why the caller must be the frame
 *  thread and must not keep it across a present. */
struct ANativeWindow*
PlatformAndroid_Window(void);

/** The surface's size in real device pixels. Zero when there is no surface. */
void
PlatformAndroid_WindowSize(int* out_width, int* out_height);

/** Display density, as drawable pixels per 160dpi point, rounded and clamped
 *  to 1..4. What PlatformWindow_PixelDensity answers, and what the chrome picks
 *  its baked font size from. */
void
PlatformAndroid_SetDensity(int density);

/**
 * How many SURFACE rows the soft keyboard covers at the bottom; 0 = away.
 *
 * Reported by ClientActivity's inset listener on the UI thread. Latest-value
 * state rather than a queued event -- an inset is a level, and coalescing is
 * what is wanted: only the value at the drain matters, exactly like the
 * surface size. The drain (PlatformWindow_PollCommands) maps it through the
 * letterbox into canvas rows and pushes TORIRS_CMD_KEYBOARD_INSET when the
 * canvas answer changes, for the reason touch coordinates are mapped at the
 * drain and not at the post: the letterbox is the frame thread's and moves
 * under the UI thread.
 */
void
PlatformAndroid_SetKeyboardInset(int bottom_px);

/** The latest reported keyboard coverage, in surface rows. */
int
PlatformAndroid_KeyboardInset(void);

/**
 * What the device reports about its power and its network link.
 *
 * `battery_percent` is 0..100, `battery_charging` nonzero while it is on
 * mains, and `network_kind` is one of TORIRS_CMD_NETWORK_*. Reported by
 * ClientActivity from a battery broadcast and a ConnectivityManager callback,
 * both on the UI thread, so it is latest-value state like the keyboard inset:
 * the drain pushes TORIRS_CMD_DEVICE_STATUS when any of the three changes, and
 * the CS2 host answers MOBILE_BATTERYLEVEL and friends from it.
 */
void
PlatformAndroid_SetDeviceStatus(
    int battery_percent,
    int battery_charging,
    int network_kind);

/** The latest reported device status. Every out parameter is required. */
void
PlatformAndroid_DeviceStatus(
    int* battery_percent,
    int* battery_charging,
    int* network_kind);

/**
 * Ask the frame loop to stop, from onDestroy.
 *
 * A request, not a kill: the loop finishes the frame it is in, App_Shutdown
 * runs, and the thread exits. Android gives an activity a moment to go away,
 * and the alternative -- letting the process die with the cache mid-write --
 * is what corrupts a save.
 */
void
PlatformAndroid_RequestQuit(void);

/** Has a quit been requested? Read by the frame loop through
 *  PlatformWindow_QuitRequested. */
int
PlatformAndroid_QuitRequested(void);

/**
 * Clear the quit latch and drop any stale input, so a NEW run can start.
 *
 * This exists because the process OUTLIVES the client. Android destroys and
 * recreates an activity for reasons that have nothing to do with the app
 * wanting to stop -- and the shared state in this file is process-global, not
 * per-activity. Without this, the quit latch set by the first onDestroy is
 * still set when the next onCreate starts the frame loop, and main() returns
 * before drawing a single frame: a client that runs once and then, for the
 * life of the process, boots to a black screen.
 *
 * Called by nativeStart, which is the one place that knows a new run is
 * beginning. The queued input goes with it for the same reason -- a finger that
 * went down in the previous run must not arrive as a click in this one.
 */
void
PlatformAndroid_ResetForStart(void);

/**
 * The client cannot boot, for a reason the person holding the phone can fix.
 *
 * DOES NOT RETURN: it ends the frame thread, and only the frame thread.
 *
 * This exists because `exit()` is the wrong verb on this platform. A desktop
 * client that refuses to boot prints why and exits, and the person who typed
 * the command reads the line. Here exit() takes the whole PROCESS down -- the
 * activity vanishes to the launcher, which is indistinguishable from a crash,
 * and the one sentence explaining it is in logcat where nobody holding the
 * phone will ever see it.
 *
 * So the message goes to the boot menu instead (`ClientActivity.bootFailed`,
 * which shows it and cancels the countdown so the same profile is not booted
 * again on a loop), and the process is left alive for the next run.
 *
 * @param message one sentence, already logged by the caller, addressed to a
 *                user and not to a developer -- it is shown on screen.
 */
void
PlatformAndroid_BootFailed(char const* message);

/* ---- input ---------------------------------------------------------------
 *
 * Queued rather than translated on the spot: a MotionEvent arrives on the UI
 * thread, and the gesture policy that turns it into clicks (input/torirs_touch.c)
 * mutates state the frame thread owns. So the UI thread only ever appends a
 * record, and PlatformWindow_PollCommands drains the queue on the frame thread
 * where every other input source is already handled.
 *
 * Coordinates are in the SURFACE's pixels; the drain maps them into canvas
 * space, because the letterbox that decides that mapping is the platform's and
 * can change between the event and the drain.
 */
enum PlatformAndroid_TouchAction
{
    PLATFORM_ANDROID_TOUCH_DOWN = 0,
    PLATFORM_ANDROID_TOUCH_MOVE,
    PLATFORM_ANDROID_TOUCH_UP
};

/** One finger, in surface pixels. `pointer_id` is Android's, and only has to be
 *  stable from DOWN to UP. Silently dropped when the queue is full -- a lost
 *  MOVE is a coarser drag, and blocking the UI thread to keep one would be a
 *  frozen app. */
void
PlatformAndroid_PostTouch(int action, int32_t pointer_id, int x, int y);

/**
 * One key, from a hardware key or the soft keyboard.
 *
 * `android_keycode` is android.view.KeyEvent's own KEYCODE_*, passed through
 * unmapped, and `unicode` is the character it produced (0 when it produced
 * none). The translation to this tree's two key spaces happens in
 * platform_android.c, for the reason input/torirs_keymap.h gives: the route is
 * keycode -> VK -> OSRS internal code, and the VK table is the one that gets
 * diffed against the reference client. Mapping on the Java side would put a
 * second, unreviewable copy of that table in a language nothing else here can
 * check.
 *
 * KeyEvent's constants are restated in the .c rather than included from
 * anywhere, the same way platform_win32gdi.c restates the WM_TOUCH structures:
 * they are stable public API integers, and there is no C header for them.
 */
void
PlatformAndroid_PostKey(int android_keycode, int down, int unicode);

/* ---- the other two files' entry points -----------------------------------
 *
 * Declared here rather than in headers of their own because each is a single
 * function that platform_android.c calls and nothing else in the tree does.
 */

/**
 * Swap the EGL backbuffer. @see platform_android_gl.c.
 *
 * PlatformWindow_PresentGL's whole body. Presenting is the platform's job on
 * every lane, so it is not part of the platform_gl_context.h seam, which
 * covers only making a context.
 */
void
PlatformAndroidGL_SwapBuffers(void);

/**
 * Raise or dismiss the soft keyboard. @see platform_android_jni.c.
 *
 * What PlatformWindow_SetTextInput means on a device whose only keyboard is
 * drawn. It has to go through JNI (InputMethodManager is Java-only), so the
 * implementation lives with the rest of the JNI, and this is the one call
 * platform_android.c makes into it.
 */
void
PlatformAndroidJni_SetSoftKeyboard(int on);

#endif
